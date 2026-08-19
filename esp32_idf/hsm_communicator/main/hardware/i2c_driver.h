#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_err.h"

constexpr gpio_num_t I2C_SCL = GPIO_NUM_13;
constexpr gpio_num_t I2C_SDA = GPIO_NUM_14;

extern i2c_master_bus_handle_t hwI2cBus;

void resetI2CBus(gpio_num_t sda_pin, gpio_num_t scl_pin);

bool initHardware();
void checkI2CLines();
#endif
