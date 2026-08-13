idf_component_register(
    SRCS 
         "main.cpp"
         "crypto_ops.cpp"
         "storage.cpp"
         "tamper.cpp"
         "globals.cpp"

    PRIV_REQUIRES 
         spi_flash

    REQUIRES
         nvs_flash
         mbedtls
         esp_adc
         esp_driver_gpio
         esp_timer
     
    INCLUDE_DIRS "")
