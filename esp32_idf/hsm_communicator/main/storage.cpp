
#include "rtc.h"
#include "storage.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "psa/crypto.h"

#include <cstdio>
#include <cstdint>

static const char *TAG = "STORAGE";


// =====================================================================
// Timing
// =====================================================================

uint32_t getCurrentUnixTime() {
    return rtcGetUnixTime();
}


// =====================================================================
// Event logging
// =====================================================================

void logEvent(const char *eventType) {
    logCounter++;

    char entry[LOG_ENTRY_MAXLEN];

    uint32_t ts = getCurrentUnixTime();

    if (ts > 0) {
        snprintf(
            entry,
            sizeof(entry),
            "#%lu [%lu] %s",
            static_cast<unsigned long>(logCounter),
            static_cast<unsigned long>(ts),
            eventType
        );
    }
    else {
        snprintf(
            entry,
            sizeof(entry),
            "#%lu [unsynced] %s",
            static_cast<unsigned long>(logCounter),
            eventType
        );
    }

    int slot = logCounter % LOG_MAX_ENTRIES;

    char key[12];

    snprintf(
        key,
        sizeof(key),
        "log_%d",
        slot
    );

    esp_err_t err = nvs_set_str(
        prefs,
        key,
        entry
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to store log entry: %s",
            esp_err_to_name(err)
        );
        return;
    }

    err = nvs_set_u32(
        prefs,
        "log_count",
        logCounter
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to store log counter: %s",
            esp_err_to_name(err)
        );
        return;
    }

    /*
     * NVS changes are not persistent until committed.
     */

    err = nvs_commit(prefs);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to commit log: %s",
            esp_err_to_name(err)
        );
    }
}


// =====================================================================
// Print log
// =====================================================================

void printLog()
{
    uint32_t total = 0;

    esp_err_t err = nvs_get_u32(
        prefs,
        "log_count",
        &total
    );

    if (err == ESP_ERR_NVS_NOT_FOUND || total == 0) {
        ESP_LOGI(TAG, "LOG_EMPTY");
        return;
    }

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read log count: %s",
            esp_err_to_name(err)
        );
        return;
    }

    uint32_t start =
        (total > LOG_MAX_ENTRIES)
            ? total - LOG_MAX_ENTRIES + 1
            : 1;

    ESP_LOGI(TAG, "LOG_BEGIN");

    for (uint32_t i = start; i <= total; ++i) {

        int slot = i % LOG_MAX_ENTRIES;

        char key[12];

        snprintf(
            key,
            sizeof(key),
            "log_%d",
            slot
        );

        char entry[LOG_ENTRY_MAXLEN];
        size_t length = sizeof(entry);

        err = nvs_get_str(
            prefs,
            key,
            entry,
            &length
        );

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "%s", entry);
        }
    }

    ESP_LOGI(TAG, "LOG_END");
}


// =====================================================================
// Key management
// =====================================================================

bool zeroizeKeys()
{
    /*
     * Destroying a persistent PSA key removes it from persistent
     * storage as well as invalidating its key identifier.
     */

    if (!keyExists) {
        ESP_LOGI(TAG, "No persistent key to zeroize");
        return true;
    }

    psa_status_t status = psa_destroy_key(
        hsm_key_id
    );

    if (status == PSA_SUCCESS ||
        status == PSA_ERROR_DOES_NOT_EXIST) {
        keyExists = false;

        ESP_LOGI(
            TAG,
            "KEY_ZEROIZED"
        );

        logEvent("KEY_ZEROIZED");
        return true;
    }
    else {
        ESP_LOGE(
            TAG,
            "Failed to zeroize key: PSA status %ld",
            static_cast<long>(status)
        );

        logEvent("ERR_KEY_ZEROIZE_FAILED");
        return false;
    }
}


// =====================================================================
// Key-store initialization
// =====================================================================

bool keystore_init()
{
    /*
     * The ECDSA key itself is managed by PSA.
     *
     * We only need to determine whether our persistent key currently
     * exists.
     */

    esp_err_t err = nvs_open(
        "hsm",
        NVS_READWRITE,
        &prefs
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to open HSM NVS namespace: %s",
            esp_err_to_name(err)
        );
        return false;
    }

    psa_key_attributes_t attributes =
        PSA_KEY_ATTRIBUTES_INIT;

    psa_status_t status = psa_get_key_attributes(
        hsm_key_id,
        &attributes
    );

    if (status == PSA_SUCCESS) {
        keyExists = true;
        return true;
    }

    if (status == PSA_ERROR_DOES_NOT_EXIST ||
        status == PSA_ERROR_INVALID_HANDLE) {

        keyExists = false;

        ESP_LOGI(
            TAG,
            "No persistent HSM key found; key generation required"
        );

        return true;
    }

    ESP_LOGE(
        TAG,
        "Failed to inspect persistent key: PSA status %ld",
        static_cast<long>(status)
    );

    keyExists = false;
    return false;
}

