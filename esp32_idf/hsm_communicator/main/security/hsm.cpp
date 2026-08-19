#include "hsm.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "psa/crypto.h"
#include "psa/crypto_types.h"

#include "../app/state.h"
#include "../hardware/gpio.h"

#include "storage.h"
#include "tamper.h"
#include "crypto_ops.h"

static const char * TAG = "HSM";

bool initHSM() {
    currentState = STATE_INIT;

    // ------------------------------------------------
    // NVS
    // ------------------------------------------------

    esp_err_t nvs_ret = nvs_flash_init();

    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_LOGW(
            TAG,
            "NVS requires reinitialization; erasing NVS"
        );

        nvs_ret = nvs_flash_erase();

        if (nvs_ret != ESP_OK) {
            ESP_LOGE(
                TAG,
                "NVS erase failed: %s",
                esp_err_to_name(nvs_ret)
            );

            return false;
        }

        nvs_ret = nvs_flash_init();
    }

    if (nvs_ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "NVS initialization failed: %s",
            esp_err_to_name(nvs_ret)
        );

        return false;
    }

    // ------------------------------------------------
    // PSA Crypto
    // ------------------------------------------------

    psa_status_t psaStatus = psa_crypto_init();

    if (psaStatus != PSA_SUCCESS) {
        ESP_LOGE(
            TAG,
            "PSA initialization failed: %ld",
            static_cast<long>(psaStatus)
        );

        return false;
    }

    // ------------------------------------------------
    // GPIO
    // ------------------------------------------------

    if (!initGPIO()) {
        return false;
    }

    // ------------------------------------------------
    // Persistent key state + event log
    // ------------------------------------------------

    if (!keystore_init()) {
        return false;
    }

    // ------------------------------------------------
    // Tamper hardware
    // ------------------------------------------------

    if (!initTamper()) {
        return false;
    }

    /*
     * Establish the ambient-light baseline.
     */

    ldrBaseline = readLDRAveraged();

    if (ldrBaseline < 0) {
        ESP_LOGE(
            TAG,
            "Failed to establish LDR baseline"
        );

        return false;
    }

    ESP_LOGI(
        TAG,
        "LDR baseline: %d",
        ldrBaseline
    );

    // ------------------------------------------------
    // Power-up self-tests
    // ------------------------------------------------

    currentState = STATE_SELFTEST;

    runSelfTests();

    if (currentState == STATE_ERROR) {
        return false;
    }

    ESP_LOGI(
        TAG,
        "HSM initialization complete"
    );

    return true;
}