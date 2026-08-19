idf_component_register(
    SRCS 
         "main.cpp"
         
         "app/interface.cpp"
         "app/state.cpp"
         
         "driver/led.cpp"
         "driver/oled.cpp"
         "driver/rtc.cpp"
         
         "hardware/gpio.cpp"
         "hardware/i2c_driver.cpp"
         
         "security/hsm.cpp"
         "security/crypto_ops.cpp"
         "security/storage.cpp"
         "security/tamper.cpp"

    PRIV_REQUIRES 
         spi_flash

    REQUIRES
         nvs_flash
         mbedtls
         esp_adc
         esp_driver_gpio
         esp_timer
         esp_driver_i2c
     
    INCLUDE_DIRS "")
