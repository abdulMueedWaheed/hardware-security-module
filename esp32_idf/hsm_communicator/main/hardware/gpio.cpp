#include "gpio.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char * TAG = "GPIO";

bool initGPIO() {
    gpio_config_t ledConfig = {};

    ledConfig.pin_bit_mask =
        (1ULL << LED_RED) |
        (1ULL << LED_YELLOW);

    ledConfig.mode = GPIO_MODE_OUTPUT;
    ledConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    ledConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ledConfig.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&ledConfig);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure LEDs: %s",
            esp_err_to_name(err)
        );

        return false;
    }

    gpio_set_level(static_cast<gpio_num_t>(LED_RED), 0);
    gpio_set_level(static_cast<gpio_num_t>(LED_YELLOW), 0);
    
    /*
     * Button is an input with external pulldown according to
     * your original wiring.
     */

    gpio_config_t buttonConfig = {};

    buttonConfig.pin_bit_mask = (1ULL << BUTTON_PIN);
    buttonConfig.mode = GPIO_MODE_INPUT;
    buttonConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    buttonConfig.pull_down_en = GPIO_PULLDOWN_ENABLE;
    buttonConfig.intr_type = GPIO_INTR_DISABLE;

    err = gpio_config(&buttonConfig);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure button: %s",
            esp_err_to_name(err)
        );

        return false;
    }

    gpio_set_level(
        static_cast<gpio_num_t>(LED_RED),
        0
    );

    gpio_set_level(
        static_cast<gpio_num_t>(LED_YELLOW),
        0
    );

    return true;
}