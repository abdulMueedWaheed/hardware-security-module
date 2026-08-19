#include <cstdint>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "soc/gpio_num.h"

#include "../app/state.h"
#include "../hardware/gpio.h"

uint32_t lastBlink = 0;
bool blinkOn = false;

static uint32_t millis_now()
{
    return static_cast<uint32_t>(
        esp_timer_get_time() / 1000ULL
    );
}

void updateLEDs()
{
    uint32_t now = millis_now();

    switch (currentState) {

        case STATE_IDLE:

            gpio_set_level(
                static_cast<gpio_num_t>(LED_RED),
                0
            );

            gpio_set_level(
                static_cast<gpio_num_t>(LED_YELLOW),
                0
            );

            break;


        case STATE_AWAITING_AUTH:

            gpio_set_level(
                static_cast<gpio_num_t>(LED_RED),
                0
            );

            if (now - lastBlink > 300) {

                blinkOn = !blinkOn;

                gpio_set_level(
                    static_cast<gpio_num_t>(LED_YELLOW),
                    blinkOn
                );

                lastBlink = now;
            }

            break;


        case STATE_PROCESSING:

            gpio_set_level(
                static_cast<gpio_num_t>(LED_YELLOW),
                1
            );

            gpio_set_level(
                static_cast<gpio_num_t>(LED_RED),
                0
            );

            break;


        case STATE_TAMPER_LOCKED:

            gpio_set_level(
                static_cast<gpio_num_t>(LED_YELLOW),
                0
            );

            if (now - lastBlink > 150) {

                blinkOn = !blinkOn;

                gpio_set_level(
                    static_cast<gpio_num_t>(LED_RED),
                    blinkOn
                );

                lastBlink = now;
            }

            break;


        case STATE_ERROR:

            gpio_set_level(
                static_cast<gpio_num_t>(LED_YELLOW),
                0
            );

            gpio_set_level(
                static_cast<gpio_num_t>(LED_RED),
                1
            );

            break;


        default:
            break;
    }
}