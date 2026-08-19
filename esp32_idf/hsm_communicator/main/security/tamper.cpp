#include "tamper.h"

#include "../app/state.h"
#include "../driver/led.h"
#include "../hardware/gpio.h"
#include "../hardware/gpio.h"

#include "crypto_ops.h"
#include "storage.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdlib>

static const char *TAG = "TAMPER";

static adc_oneshot_unit_handle_t adc_handle = nullptr;
static adc_unit_t ldr_adc_unit;
static adc_channel_t ldr_adc_channel;


int ldrBaseline = 0;
int TAMPER_THRESHOLD = 1500;
const int LDR_SAMPLES = 5;
uint32_t lastTamperCheck = 0;
const uint32_t TAMPER_CHECK_INTERVAL_MS = 250;
const uint32_t AUTH_TIMEOUT_MS = 8000;

bool initTamper() {
    /*
     * GPIO 4 on the ESP32-S3 is ADC1_CH3.
     *
     * We use adc_oneshot_io_to_channel() rather than hard-coding the
     * ADC channel so the relationship is established by ESP-IDF.
     */

    esp_err_t err = adc_oneshot_io_to_channel(
        LDR_PIN,
        &ldr_adc_unit,
        &ldr_adc_channel
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "LDR GPIO %d is not a valid ADC input: %s",
            LDR_PIN,
            esp_err_to_name(err)
        );

        return false;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ldr_adc_unit,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };

    err = adc_oneshot_new_unit(
        &unit_config,
        &adc_handle
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize ADC unit: %s",
            esp_err_to_name(err)
        );

        return false;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    err = adc_oneshot_config_channel(
        adc_handle,
        ldr_adc_channel,
        &channel_config
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure LDR ADC channel: %s",
            esp_err_to_name(err)
        );

        adc_oneshot_del_unit(adc_handle);
        adc_handle = nullptr;

        return false;
    }

    ESP_LOGI(
        TAG,
        "LDR initialized on GPIO %d (ADC%d channel %d)",
        LDR_PIN,
        static_cast<int>(ldr_adc_unit) + 1,
        static_cast<int>(ldr_adc_channel)
    );

    return true;
}


// =====================================================================
// TAMPER DETECTION
// =====================================================================

// ---------------- Tamper detection smoothing ----------------

int readLDRAveraged()
{
    if (adc_handle == nullptr) {
        ESP_LOGE(TAG, "ADC has not been initialized");
        return -1;
    }

    long sum = 0;

    for (int i = 0; i < LDR_SAMPLES; ++i) {

        int reading = 0;

        esp_err_t err = adc_oneshot_read(
            adc_handle,
            ldr_adc_channel,
            &reading
        );

        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "ADC read failed: %s",
                esp_err_to_name(err)
            );

            return -1;
        }

        sum += reading;

        vTaskDelay(
            pdMS_TO_TICKS(2)
        );
    }

    return static_cast<int>(
        sum / LDR_SAMPLES
    );
}

static bool calibrateLDR()
{
    long long sum = 0;

    for (int i = 0; i < CALIBRATION_SAMPLES; ++i) {
        int reading = readLDRAveraged();

        if (reading < 0) {
            return false;
        }

        sum += reading;

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ldrBaseline =
        static_cast<int>(sum / CALIBRATION_SAMPLES);

    ESP_LOGI(
        TAG,
        "LDR calibrated: baseline=%d",
        ldrBaseline
    );

    return true;
}


void checkTamper()
{
    if (currentState == STATE_TAMPER_LOCKED) {
        return;
    }

    /*
     * esp_timer_get_time() returns microseconds since boot.
     * Convert to milliseconds to preserve the semantics of the old
     * Arduino millis() implementation.
     */

    uint32_t nowMs = static_cast<uint32_t>(
        esp_timer_get_time() / 1000ULL
    );

    if (
        nowMs - lastTamperCheck <
        TAMPER_CHECK_INTERVAL_MS
    ) {
        return;
    }

    lastTamperCheck = nowMs;

    int reading = readLDRAveraged();

    if (reading < 0) {
        /*
         * ADC failure is not a normal sensor reading.
         * Treating it as tamper would be possible, but that is a
         * separate policy decision. For now, leave the state unchanged.
         */
        return;
    }

    int current_value = std::abs(
        reading - ldrBaseline
    );

    if (current_value > TAMPER_THRESHOLD) {

        if (zeroize()) {
            currentState = STATE_TAMPER_LOCKED;
    
            ESP_LOGE(
                TAG,
                "TAMPER_DETECTED_KEYS_ZEROIZED, current_value: %d",
                current_value
            );
        }
        
        else {
            currentState = STATE_ERROR;
            ESP_LOGE(
                TAG,
                "KEYS FAILED TO ZEROIZE"
            );

        }

    }
}

