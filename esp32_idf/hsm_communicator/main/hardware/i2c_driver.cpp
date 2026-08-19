// hardware.cpp

#include "i2c_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"

i2c_master_bus_handle_t hwI2cBus = nullptr;

static const char * TAG = "I2C_DRIVER";

bool initHardware()
{
    // 1. Pass explicit GPIO enum types to bus reset
    resetI2CBus(static_cast<gpio_num_t>(I2C_SDA), static_cast<gpio_num_t>(I2C_SCL));

    i2c_master_bus_config_t config = {};
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.i2c_port = I2C_NUM_0;
    config.scl_io_num = static_cast<gpio_num_t>(I2C_SCL); // GPIO 5
    config.sda_io_num = static_cast<gpio_num_t>(I2C_SDA); // GPIO 4
    config.glitch_ignore_cnt = 7;
    
    // Enable internal pull-ups so the lines never float
    config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&config, &hwI2cBus);
    if (err != ESP_OK) {
        ESP_LOGE("HW", "Failed to initialize I2C bus: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

void resetI2CBus(gpio_num_t sda_pin, gpio_num_t scl_pin) {
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD; // Open-drain mode
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pin_bit_mask = (1ULL << sda_pin) | (1ULL << scl_pin);
    gpio_config(&io_conf);

    // Drive SCL low and high 9 times to flush stuck slave bits
    for (int i = 0; i < 9; i++) {
        gpio_set_level(scl_pin, 0);
        esp_rom_delay_us(10);
        gpio_set_level(scl_pin, 1);
        esp_rom_delay_us(10);
    }

    // Send I2C STOP condition (SDA goes LOW to HIGH while SCL is HIGH)
    gpio_set_level(sda_pin, 0);
    esp_rom_delay_us(10);
    gpio_set_level(scl_pin, 1);
    esp_rom_delay_us(10);
    gpio_set_level(sda_pin, 1);
    esp_rom_delay_us(10);
}

void checkI2CLines() {
    gpio_config_t config = {};

    config.pin_bit_mask =
        (1ULL << GPIO_NUM_13) |
        (1ULL << GPIO_NUM_14);

    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    ESP_ERROR_CHECK(gpio_config(&config));

    ESP_LOGI(
        TAG,
        "SDA=%d SCL=%d",
        gpio_get_level(GPIO_NUM_13),
        gpio_get_level(GPIO_NUM_14)
    );
}

