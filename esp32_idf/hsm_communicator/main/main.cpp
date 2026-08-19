


#include "app/state.h"
#include "app/interface.h"

#include "driver/led.h"
#include "driver/oled.h"
#include "driver/rtc.h"

#include "hardware/i2c_driver.h"

#include "security/hsm.h"
#include "security/tamper.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "psa/crypto.h"




static const char *TAG = "MAIN"; 


extern "C" void app_main()
{
    if (!initHSM()) {

        currentState = STATE_ERROR;

        ESP_LOGE(
            TAG,
            "HSM initialization failed"
        );
    }

    if(!initHardware()) {
        ESP_LOGE(TAG, "Hardware bus Initialization Failed!");
        currentState = STATE_ERROR;
        return;
    }

    ESP_LOGI("HW",
         "I2C bus initialized: SDA=%d SCL=%d",
         I2C_SDA,
         I2C_SCL);

    if (!initRTC(hwI2cBus)) {
        ESP_LOGE(TAG, "RTC Initialization Failed!");
        currentState = STATE_ERROR;
        return;
    }

    ESP_LOGI(TAG, "RTC inited!");

    if (initOLED(hwI2cBus)) {
        oledClear();
        oledDisplayStatus();
        oledUpdate();
    }
    else {
        ESP_LOGE(TAG, "OLED Initialization Failed!");
        currentState = STATE_ERROR;
        return;
    }

    xTaskCreate(
        commandTask,
        "command_task",
        8192,
        nullptr,
        4,
        nullptr
    );

    vTaskDelay(
        pdMS_TO_TICKS(10)
    );
    int i = 0;
    while (true) {

        checkTamper();

        updateLEDs();

        if (i == 0) {
            oledClear();
            oledDisplayStatus();
            oledUpdate();
        }
        else {
            i %= 10;
        }

        vTaskDelay(
            pdMS_TO_TICKS(10)
        );
    }
}