#include "storage.h"

#include "../driver/rtc.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include <cstdio>
#include <cstdint>

static const char* TAG = "STORAGE";

static constexpr const char* NVS_NAMESPACE = "hsm";
static nvs_handle_t prefs = 0;
static uint32_t logCounter = 0;

// ============================================================================
// Initialization
// ============================================================================

bool initStorage()
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_LOGW(
            TAG,
            "NVS requires reinitialization; erasing NVS"
        );

        err = nvs_flash_erase();

        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "NVS erase failed: %s",
                esp_err_to_name(err)
            );
            return false;
        }

        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "NVS initialization failed: %s",
            esp_err_to_name(err)
        );
        return false;
    }

    // Now open the application's NVS namespace.
    err = nvs_open("hsm", NVS_READWRITE, &prefs);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to open HSM NVS namespace: %s",
            esp_err_to_name(err)
        );
        return false;
    }

    return true;
}

// ============================================================================
// Event logging
// ============================================================================

void logEvent(const char* eventType) {

    if (prefs == 0) {
        ESP_LOGE(TAG, "Storage not initialized");
        return;
    }

    ++logCounter;

    char entry[LOG_ENTRY_MAXLEN];

    const uint32_t ts = rtcGetUnixTime();

    if (ts > 0) {
        snprintf(
            entry,
            sizeof(entry),
            "#%lu [%lu] %s",
            static_cast<unsigned long>(logCounter),
            static_cast<unsigned long>(ts),
            eventType
        );
    } else {
        snprintf(
            entry,
            sizeof(entry),
            "#%lu [unsynced] %s",
            static_cast<unsigned long>(logCounter),
            eventType
        );
    }

    const int slot = logCounter % LOG_MAX_ENTRIES;

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

    err = nvs_commit(prefs);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to commit log: %s",
            esp_err_to_name(err)
        );
    }
}

// ============================================================================
// Print log
// ============================================================================

void printLog()
{
    if (prefs == 0) {
        ESP_LOGE(TAG, "Storage not initialized");
        return;
    }

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

    const uint32_t start =
        (total > LOG_MAX_ENTRIES)
            ? total - LOG_MAX_ENTRIES + 1
            : 1;

    ESP_LOGI(TAG, "LOG_BEGIN");

    for (uint32_t i = start; i <= total; ++i) {
        const int slot = i % LOG_MAX_ENTRIES;

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

