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

    if (!initStorage()) {
        ESP_LOGE(
            TAG, 
            "Storage(NVS) Subsystem Failed to initialize!",
        );
        return false;
    }

    if(!initCrypto()) {
        ESP_LOGE(
            TAG, 
            "Crypto Subsystem Failed to initialize!",
        );

        return false;
    }


    if (!initGPIO()) {
        ESP_LOGE(
            TAG, 
            "GPIO Subsystem Failed to initialize!",
        );
        return false;
    }

    if (!initTamper()) {
        ESP_LOGE(
            TAG, 
            "Tamper Subsystem Failed to initialize!",
        );
        return false;
    }

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