#include "crypto_ops.h"
#include "globals.h"
#include "storage.h"
#include "tamper.h"

#include "nvs_flash.h"
#include "psa/crypto.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


extern "C" void app_main()
{
    currentState = STATE_INIT;

    // ------------------------------------------------
    // Initialize NVS
    // ------------------------------------------------

    esp_err_t nvs_ret = nvs_flash_init();

    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(nvs_ret);

    // ------------------------------------------------
    // Initialize PSA Crypto
    // ------------------------------------------------

    psa_status_t psa_ret = psa_crypto_init();

    if (psa_ret != PSA_SUCCESS) {
        currentState = STATE_ERROR;
        return;
    }

    // ------------------------------------------------
    // Initialize persistent key state
    // ------------------------------------------------

    if (!keystore_init()) {
        currentState = STATE_ERROR;
        return;
    }

    // ------------------------------------------------
    // Initialize tamper hardware
    // ------------------------------------------------

    if (!initTamper()) {
        currentState = STATE_ERROR;
        return;
    }

    // ------------------------------------------------
    // Power-up self tests
    // ------------------------------------------------

    currentState = STATE_SELFTEST;

    runSelfTests();

    if (currentState == STATE_ERROR) {
        return;
    }

    // ------------------------------------------------
    // Main loop
    // ------------------------------------------------

    while (true) {
        checkTamper();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}