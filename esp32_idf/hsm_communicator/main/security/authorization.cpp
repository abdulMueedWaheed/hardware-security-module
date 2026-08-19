#include "FreeRTOSConfig.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "portmacro.h"

#include "../app/state.h"
#include "authorization.h"
#include "tamper.h"
#include "../hardware/gpio.h"

static const char * TAG = "AUTHORIZATION";

bool requireAuthorization()
{
    currentState = STATE_AWAITING_AUTH;

    ESP_LOGI(
        TAG,
        "AWAITING_AUTH_PRESS_BUTTON"
    );

    uint32_t startMs = static_cast<uint32_t>(
        esp_timer_get_time() / 1000ULL
    );

    /*
     * If the button is already held down, wait for release first.
     * This prevents a stale press from automatically authorizing
     * the next command.
     */

    while (gpio_get_level(
               static_cast<gpio_num_t>(BUTTON_PIN)
           ) == 1 &&
           static_cast<uint32_t>(
               esp_timer_get_time() / 1000ULL
           ) - startMs < AUTH_TIMEOUT_MS) {

        checkTamper();

        if (currentState == STATE_TAMPER_LOCKED) {
            return false;
        }

        vTaskDelay(
            pdMS_TO_TICKS(20)
        );
    }

    startMs = static_cast<uint32_t>(
        esp_timer_get_time() / 1000ULL
    );

    while (
        static_cast<uint32_t>(
            esp_timer_get_time() / 1000ULL
        ) - startMs < AUTH_TIMEOUT_MS
    ) {

        if (gpio_get_level(
                static_cast<gpio_num_t>(BUTTON_PIN)
            ) == 1) {

            vTaskDelay(
                pdMS_TO_TICKS(30)
            );

            if (gpio_get_level(
                    static_cast<gpio_num_t>(BUTTON_PIN)
                ) == 1) {

                return true;
            }
        }

        /*
         * Stay responsive to tamper while waiting for authorization.
         */

        checkTamper();

        if (currentState == STATE_TAMPER_LOCKED) {
            return false;
        }

        vTaskDelay(
            pdMS_TO_TICKS(10)
        );
    }

    return false;
}