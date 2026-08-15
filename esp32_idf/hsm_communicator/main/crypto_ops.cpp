#include "crypto_ops.h"

#include "storage.h"
#include "tamper.h"

#include "esp_log.h"
#include "psa/crypto.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

static const char *TAG = "CRYPTO";

static constexpr psa_key_id_t HSM_KEY_ID = PSA_KEY_ID_USER_MIN;

psa_key_id_t hsm_key_id = HSM_KEY_ID;
bool keyExists = false;


// =====================================================================
// Helpers
// =====================================================================

static void logPsaError(const char *operation, psa_status_t status)
{
    ESP_LOGE(
        TAG,
        "%s failed: PSA status = %ld",
        operation,
        static_cast<long>(status)
    );
}


static std::string bytesToHex(
    const unsigned char *data,
    size_t len
)
{
    static constexpr char hexChars[] = "0123456789abcdef";

    std::string result;
    result.reserve(len * 2);

    for (size_t i = 0; i < len; ++i) {
        result += hexChars[(data[i] >> 4) & 0x0F];
        result += hexChars[data[i] & 0x0F];
    }

    return result;
}


// =====================================================================
// SELF-TESTS
// =====================================================================

bool selfTestSHA256()
{
    const char *msg = "abc";

    unsigned char hash[32];
    size_t hashLen = 0;

    psa_status_t status = psa_hash_compute(
        PSA_ALG_SHA_256,
        reinterpret_cast<const uint8_t *>(msg),
        strlen(msg),
        hash,
        sizeof(hash),
        &hashLen
    );

    if (status != PSA_SUCCESS) {
        logPsaError("SHA-256", status);
        return false;
    }

    static constexpr unsigned char expected[32] = {
        0xba, 0x78, 0x16, 0xbf,
        0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde,
        0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3,
        0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61,
        0xf2, 0x00, 0x15, 0xad
    };

    return hashLen == sizeof(expected) &&
           memcmp(hash, expected, sizeof(expected)) == 0;
}


bool selfTestECDSA()
{
    /*
     * Generate a temporary P-256 key, sign a SHA-256 digest,
     * verify the signature, then destroy the temporary key.
     */

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_type(
        &attributes,
        PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1)
    );

    psa_set_key_bits(&attributes, 256);

    psa_set_key_usage_flags(
        &attributes,
        PSA_KEY_USAGE_SIGN_HASH |
        PSA_KEY_USAGE_VERIFY_HASH
    );

    psa_set_key_algorithm(
        &attributes,
        PSA_ALG_ECDSA(PSA_ALG_SHA_256)
    );

    /*
     * Leave the lifetime as volatile.
     * This key exists only for the self-test.
     */

    psa_key_id_t testKey = PSA_KEY_ID_NULL;

    psa_status_t status = psa_generate_key(
        &attributes,
        &testKey
    );

    psa_reset_key_attributes(&attributes);

    if (status != PSA_SUCCESS) {
        logPsaError("ECDSA self-test key generation", status);
        return false;
    }

    const char *msg = "hsm_selftest_vector";

    unsigned char hash[32];
    size_t hashLen = 0;

    status = psa_hash_compute(
        PSA_ALG_SHA_256,
        reinterpret_cast<const uint8_t *>(msg),
        strlen(msg),
        hash,
        sizeof(hash),
        &hashLen
    );

    if (status != PSA_SUCCESS) {
        logPsaError("ECDSA self-test hashing", status);
        psa_destroy_key(testKey);
        return false;
    }

    /*
     * P-256 ECDSA signatures are 64 bytes in PSA format:
     *
     *     r || s
     *
     * rather than the DER-encoded representation used by some
     * older Mbed TLS APIs.
     */

    unsigned char signature[PSA_SIGNATURE_MAX_SIZE];
    size_t signatureLen = 0;

    status = psa_sign_hash(
        testKey,
        PSA_ALG_ECDSA(PSA_ALG_SHA_256),
        hash,
        hashLen,
        signature,
        sizeof(signature),
        &signatureLen
    );

    if (status != PSA_SUCCESS) {
        logPsaError("ECDSA self-test signing", status);
        psa_destroy_key(testKey);
        return false;
    }

    status = psa_verify_hash(
        testKey,
        PSA_ALG_ECDSA(PSA_ALG_SHA_256),
        hash,
        hashLen,
        signature,
        signatureLen
    );

    psa_destroy_key(testKey);

    if (status != PSA_SUCCESS) {
        logPsaError("ECDSA self-test verification", status);
        return false;
    }

    return true;
}


void runSelfTests()
{
    ESP_LOGI(TAG, "SELFTEST_RUNNING");

    bool shaOk = selfTestSHA256();
    bool ecdsaOk = selfTestECDSA();

    if (shaOk && ecdsaOk) {
        ESP_LOGI(TAG, "SELFTEST_PASS");
        currentState = STATE_IDLE;
    }
    else {
        if (!shaOk) {
            ESP_LOGE(TAG, "SELFTEST_FAIL_SHA256");
        }

        if (!ecdsaOk) {
            ESP_LOGE(TAG, "SELFTEST_FAIL_ECDSA");
        }

        ESP_LOGE(TAG, "SELFTEST_FAIL");

        currentState = STATE_ERROR;
    }
}


// =====================================================================
// KEY MANAGEMENT
// =====================================================================

void genKey()
{
    /*
     * We use a persistent PSA key.
     *
     * The private key is therefore managed by PSA rather than being
     * manually copied into a raw 32-byte buffer by our application.
     */

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_usage_flags(
        &attributes,
        PSA_KEY_USAGE_SIGN_HASH |
        PSA_KEY_USAGE_VERIFY_HASH
    );

    psa_set_key_type(
        &attributes,
        PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1)
    );

    psa_set_key_bits(
        &attributes,
        256
    );

    psa_set_key_algorithm(
        &attributes,
        PSA_ALG_ECDSA(PSA_ALG_SHA_256)
    );

    /*
     * The key is persistent because psa_set_key_id() sets a persistent
     * lifetime.
     */

    psa_status_t status = psa_generate_key(
        &attributes,
        &hsm_key_id
    );

    psa_reset_key_attributes(&attributes);

    if (status != PSA_SUCCESS) {
        logPsaError("KEYGEN", status);
        logEvent("ERR_KEYGEN_FAILED");
        return;
    }

    /*
     * Pairwise consistency test.
     */

    const char *testMsg = "pairwise_consistency_check";

    unsigned char testHash[32];
    size_t testHashLen = 0;

    status = psa_hash_compute(
        PSA_ALG_SHA_256,
        reinterpret_cast<const uint8_t *>(testMsg),
        strlen(testMsg),
        testHash,
        sizeof(testHash),
        &testHashLen
    );

    if (status != PSA_SUCCESS) {
        logPsaError("PAIRWISE_HASH", status);
        psa_destroy_key(hsm_key_id);
        hsm_key_id = HSM_KEY_ID;
        logEvent("ERR_PAIRWISE_CONSISTENCY_FAILED");
        return;
    }

    unsigned char testSig[PSA_SIGNATURE_MAX_SIZE];
    size_t testSigLen = 0;

    status = psa_sign_hash(
        hsm_key_id,
        PSA_ALG_ECDSA(PSA_ALG_SHA_256),
        testHash,
        testHashLen,
        testSig,
        sizeof(testSig),
        &testSigLen
    );

    if (status == PSA_SUCCESS) {
        status = psa_verify_hash(
            hsm_key_id,
            PSA_ALG_ECDSA(PSA_ALG_SHA_256),
            testHash,
            testHashLen,
            testSig,
            testSigLen
        );
    }

    if (status != PSA_SUCCESS) {
        logPsaError(
            "PAIRWISE_CONSISTENCY",
            status
        );

        psa_destroy_key(hsm_key_id);

        hsm_key_id = HSM_KEY_ID;

        keyExists = false;

        logEvent("ERR_PAIRWISE_CONSISTENCY_FAILED");
        return;
    }

    keyExists = true;

    ESP_LOGI(TAG, "OK_KEY_GENERATED");
    logEvent("OK_KEY_GENERATED");
}


// =====================================================================
// PUBLIC KEY
// =====================================================================

void getPubKey()
{
    if (!keyExists) {
        ESP_LOGE(TAG, "ERR_NO_KEY");
        logEvent("ERR_NO_KEY");
        return;
    }

    /*
     * Export only the public key.
     *
     * For a P-256 ECC public key PSA uses the uncompressed X9.63
     * representation:
     *
     *     0x04 || X || Y
     *
     * = 65 bytes total.
     */

    unsigned char pubKey[65];
    size_t pubKeyLen = 0;

    psa_status_t status = psa_export_public_key(
        hsm_key_id,
        pubKey,
        sizeof(pubKey),
        &pubKeyLen
    );

    if (status != PSA_SUCCESS) {
        logPsaError("PUBLIC_KEY_EXPORT", status);
        logEvent("ERR_NO_KEY");
        return;
    }

    std::string hexStr = bytesToHex(
        pubKey,
        pubKeyLen
    );

    ESP_LOGI(
        TAG,
        "%s",
        hexStr.c_str()
    );

    logEvent("PUB_KEY_RETURNED");
}


// =====================================================================
// HEX → BYTES
// =====================================================================

int hexStringToBytes(
    const std::string& hex,
    unsigned char *out,
    size_t maxLen
)
{
    if (hex.length() % 2 != 0) {
        return -1;
    }

    size_t len = hex.length() / 2;

    if (len > maxLen) {
        return -1;
    }

    for (size_t i = 0; i < len; ++i) {
        char byteStr[3] = {
            hex[i * 2],
            hex[i * 2 + 1],
            '\0'
        };

        char *end = nullptr;

        long value = strtol(
            byteStr,
            &end,
            16
        );

        if (end == byteStr || *end != '\0' || value < 0 || value > 255) {
            return -1;
        }

        out[i] = static_cast<unsigned char>(value);
    }

    return static_cast<int>(len);
}


// =====================================================================
// SIGN DATA
// =====================================================================

void signData(const std::string& hexData)
{
    unsigned char data[256];

    int dataLen = hexStringToBytes(
        hexData,
        data,
        sizeof(data)
    );

    if (dataLen < 0) {
        ESP_LOGE(TAG, "ERR_BAD_INPUT");
        return;
    }

    if (!keyExists) {
        ESP_LOGE(TAG, "ERR_NO_KEY");
        logEvent("ERR_NO_KEY");
        return;
    }

    unsigned char hash[32];
    size_t hashLen = 0;

    psa_status_t status = psa_hash_compute(
        PSA_ALG_SHA_256,
        data,
        dataLen,
        hash,
        sizeof(hash),
        &hashLen
    );

    if (status != PSA_SUCCESS) {
        logPsaError("HASH", status);
        logEvent("ERR_SIGN_FAILED");
        return;
    }

    unsigned char signature[PSA_SIGNATURE_MAX_SIZE];
    size_t signatureLen = 0;

    status = psa_sign_hash(
        hsm_key_id,
        PSA_ALG_ECDSA(PSA_ALG_SHA_256),
        hash,
        hashLen,
        signature,
        sizeof(signature),
        &signatureLen
    );

    if (status != PSA_SUCCESS) {
        logPsaError("SIGN", status);
        logEvent("ERR_SIGN_FAILED");
        return;
    }

    std::string hexSig = bytesToHex(
        signature,
        signatureLen
    );

    ESP_LOGI(
        TAG,
        "%s",
        hexSig.c_str()
    );

    logEvent("KEY_SIGNED_SUCCESS");
}
