#include <cstdint>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "soc/gpio_num.h"

#include "../app/state.h"
#include "../hardware/gpio.h"

uint32_t lastBlink = 0;
bool blinkOn = false;
HsmState previousState = STATE_IDLE;

static constexpr int HIGH = 0;
static constexpr int LOW  = 1;

static uint32_t millis_now() {

    return static_cast<uint32_t>(
        esp_timer_get_time() / 1000ULL
    );
}

void updateLEDs()
{
    uint32_t now = millis_now();

    // Reset blink state whenever the HSM changes state.
    if (currentState != previousState) {
        blinkOn = false;
        lastBlink = now;
        previousState = currentState;
    }

    switch (currentState) {

        case STATE_IDLE:
            gpio_set_level(
                static_cast<gpio_num_t>(LED_RED),
                LOW
            );

            gpio_set_level(
                static_cast<gpio_num_t>(LED_YELLOW),
                HIGH
            );
            break;

        case STATE_AWAITING_AUTH:

            if (now - lastBlink >= 300) {
                blinkOn = !blinkOn;

                gpio_set_level(
                    static_cast<gpio_num_t>(LED_YELLOW),
                    blinkOn ? LOW : HIGH
                );

                gpio_set_level(
                    static_cast<gpio_num_t>(LED_RED),
                    blinkOn ? HIGH : LOW 
                );

                lastBlink = now;
            }
            break;

        case STATE_PROCESSING:
            gpio_set_level(
                static_cast<gpio_num_t>(LED_RED),
                LOW
            );

            if (now - lastBlink >= 300) {
                blinkOn = !blinkOn;

                gpio_set_level(
                    static_cast<gpio_num_t>(LED_YELLOW),
                    blinkOn ? HIGH : LOW
                );

                lastBlink = now;
            }
            break;

        case STATE_TAMPER_LOCKED:
            gpio_set_level(
                static_cast<gpio_num_t>(LED_YELLOW),
                0
            );

            if (now - lastBlink >= 150) {
                blinkOn = !blinkOn;

                gpio_set_level(
                    static_cast<gpio_num_t>(LED_RED),
                    blinkOn ? HIGH : LOW
                );

                lastBlink = now;
            }
            break;

        case STATE_ERROR:
            gpio_set_level(
                static_cast<gpio_num_t>(LED_YELLOW),
                LOW
            );

            gpio_set_level(
                static_cast<gpio_num_t>(LED_RED),
                LOW
            );
            break;

        default:
            gpio_set_level(
                static_cast<gpio_num_t>(LED_RED),
                LOW
            );

            gpio_set_level(
                static_cast<gpio_num_t>(LED_YELLOW),
                LOW
            );
            break;
    }
}

