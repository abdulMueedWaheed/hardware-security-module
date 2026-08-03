#include "crypto_ops.h"

#include "storage.h"
#include "tamper.h"
#include <cstring>

// =====================================================================
// SELF-TESTS (FIPS 140-2 Area 9 - power-up tests)
// =====================================================================
bool selfTestSHA256() {
  const char *msg = "abc";

  unsigned char hash[32];

  mbedtls_sha256(
    (const unsigned char *)msg,
    strlen(msg),
    hash,
    0);

  const unsigned char expected[32] = {
    0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
    0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
    0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
    0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
  };

  return memcmp(hash, expected, 32) == 0;
}

bool selfTestECDSA() {
  // Sign/verify round-trip test using a throwaway key (not the stored HSM key).
  mbedtls_ecdsa_context ecdsa;
  mbedtls_ecdsa_init(&ecdsa);

  int ret = mbedtls_ecdsa_genkey(&ecdsa, MBEDTLS_ECP_DP_SECP256R1,
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
  if (ret != 0) { mbedtls_ecdsa_free(&ecdsa); return false; }

  unsigned char hash[32];
  const char *msg = "hsm_selftest_vector";
  mbedtls_sha256((const unsigned char *)msg, strlen(msg), hash, 0);

  unsigned char sig[MBEDTLS_ECDSA_MAX_LEN];
  size_t sigLen;
  ret = mbedtls_ecdsa_write_signature(&ecdsa, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                                       sig, sizeof(sig), &sigLen,
                                       mbedtls_ctr_drbg_random, &ctr_drbg);
  if (ret == 0) {
    ret = mbedtls_ecdsa_read_signature(&ecdsa, hash, sizeof(hash), sig, sigLen);
  }
  mbedtls_ecdsa_free(&ecdsa);
  return ret == 0;
}

void runSelfTests() {
  Serial.println("SELFTEST_RUNNING");

  bool shaOk   = selfTestSHA256();
  bool ecdsaOk = selfTestECDSA();

  if (shaOk && ecdsaOk) {
    Serial.println("SELFTEST_PASS");
    currentState = STATE_IDLE;
  } 
  
  else {
    if (!shaOk)   Serial.println("SELFTEST_FAIL_SHA256");
    if (!ecdsaOk) Serial.println("SELFTEST_FAIL_ECDSA");
    Serial.println("SELFTEST_FAIL");
    currentState = STATE_ERROR;   // Module halts - will not execute commands
  }
}


// =====================================================================
// KEY MANAGEMENT (FIPS 140-2 Area 7)
// =====================================================================
void genKey() {
  mbedtls_ecdsa_context ecdsa;
  mbedtls_ecdsa_init(&ecdsa);

  int ret = mbedtls_ecdsa_genkey(&ecdsa, MBEDTLS_ECP_DP_SECP256R1,
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
  if (ret != 0) {
    Serial.println("ERR_KEYGEN_FAILED");
    logEvent("ERR_KEYGEN_FAILED");
    mbedtls_ecdsa_free(&ecdsa);
    return;
  }

  // Pairwise consistency test - required conditional test before a newly
  // generated key pair is accepted/stored.
  unsigned char testHash[32];
  const char *testMsg = "pairwise_consistency_check";
  mbedtls_sha256((const unsigned char *)testMsg, strlen(testMsg), testHash, 0);

  unsigned char testSig[MBEDTLS_ECDSA_MAX_LEN];
  size_t testSigLen;
  ret = mbedtls_ecdsa_write_signature(&ecdsa, MBEDTLS_MD_SHA256, testHash, sizeof(testHash),
                                       testSig, sizeof(testSig), &testSigLen,
                                       mbedtls_ctr_drbg_random, &ctr_drbg);
  if (ret == 0) {
    ret = mbedtls_ecdsa_read_signature(&ecdsa, testHash, sizeof(testHash), testSig, testSigLen);
  }
  if (ret != 0) {
    Serial.println("ERR_PAIRWISE_CONSISTENCY_FAILED");
    mbedtls_ecdsa_free(&ecdsa);

    logEvent("ERR_PAIRWISE_CONSISTENCY_FAILED");
    return;
  }

  unsigned char priv_buf[32];
  mbedtls_mpi_write_binary(&ecdsa.MBEDTLS_PRIVATE(d), priv_buf, sizeof(priv_buf));

  unsigned char pub_buf[65];
  size_t pub_len;
  mbedtls_ecp_point_write_binary(&ecdsa.MBEDTLS_PRIVATE(grp), &ecdsa.MBEDTLS_PRIVATE(Q),
                                  MBEDTLS_ECP_PF_UNCOMPRESSED, &pub_len,
                                  pub_buf, sizeof(pub_buf));

  prefs.putBytes("privkey", priv_buf, sizeof(priv_buf));
  prefs.putBytes("pubkey", pub_buf, pub_len);
  keyExists = true;

  mbedtls_ecdsa_free(&ecdsa);
  memset(priv_buf, 0, sizeof(priv_buf)); // clear private key from RAM

  Serial.println("OK_KEY_GENERATED");
  logEvent("OK_KEY_GENERATED");
}

void getPubKey() {
  if (!keyExists) {
    Serial.println("ERR_NO_KEY");
    logEvent("ERR_NO_KEY");
    return;
  }
  unsigned char pub_buf[65];
  size_t len = prefs.getBytes("pubkey", pub_buf, sizeof(pub_buf));
  if (len == 0) {
    Serial.println("ERR_NO_KEY");
    logEvent("ERR_NO_KEY");
    return;
  }
  String hexStr = "";
  for (size_t i = 0; i < len; i++) {
    if (pub_buf[i] < 0x10) hexStr += "0";
    hexStr += String(pub_buf[i], HEX);
  }
  Serial.println(hexStr);
  logEvent("PUB_KEY_RETURNED");
}


int hexStringToBytes(String hex, unsigned char *out, size_t maxLen) {
  size_t len = hex.length() / 2;
  if (len > maxLen) return -1;
  for (size_t i = 0; i < len; i++) {
    String byteStr = hex.substring(i * 2, i * 2 + 2);
    out[i] = (unsigned char) strtol(byteStr.c_str(), NULL, 16);
  }
  return len;
}


void signData(String hexData) {
  unsigned char outData[256];
  int dataLen = hexStringToBytes(hexData, outData, sizeof(outData));
  if (dataLen < 0) {
    Serial.println("ERR_BAD_INPUT");
    return;
  }

  unsigned char hash[32];
  mbedtls_sha256(outData, dataLen, hash, 0);

  unsigned char priv_buf[32];
  size_t privLen = prefs.getBytes("privkey", priv_buf, sizeof(priv_buf));
  if (privLen == 0) {
    Serial.println("ERR_NO_KEY");
    return;
  }

  mbedtls_ecdsa_context ecdsa;
  mbedtls_ecdsa_init(&ecdsa);
  mbedtls_ecp_group_load(&ecdsa.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1);
  mbedtls_mpi_read_binary(&ecdsa.MBEDTLS_PRIVATE(d), priv_buf, privLen);

  unsigned char sig[MBEDTLS_ECDSA_MAX_LEN];
  size_t sigLen;
  int ret = mbedtls_ecdsa_write_signature(&ecdsa, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                                           sig, sizeof(sig), &sigLen,
                                           mbedtls_ctr_drbg_random, &ctr_drbg);

  mbedtls_ecdsa_free(&ecdsa);
  memset(priv_buf, 0, sizeof(priv_buf)); // clear private key from RAM

  if (ret != 0) {
    Serial.println("ERR_SIGN_FAILED");
    logEvent("ERR_SIGN_FAILED");
    return;
  }

  String hexSig = "";
  for (size_t i = 0; i < sigLen; i++) {
    if (sig[i] < 0x10) hexSig += "0";
    hexSig += String(sig[i], HEX);
  }
  Serial.println(hexSig);
  logEvent("KEY_SIGNED_SUCCESS");
}















