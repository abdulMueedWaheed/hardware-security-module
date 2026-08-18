#include "globals.h"

#include "crypto_ops.h"
#include "storage.h"
#include "tamper.h"
#include "rtc.h"
#include "driver/i2c_master.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "psa/crypto.h"

// #include "driver/usb_serial_jtag.h"
// #include "esp_vfs_usb_serial_jtag.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static const char *TAG = "MAIN";


static void checkI2CLines()
{
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

// =====================================================================
// Helpers
// =====================================================================

static uint32_t millis_now()
{
    return static_cast<uint32_t>(
        esp_timer_get_time() / 1000ULL
    );
}


static const char *stateToString(HsmState state)
{
    switch (state) {
        case STATE_IDLE:
            return "IDLE";

        case STATE_AWAITING_AUTH:
            return "AWAITING_AUTH";

        case STATE_PROCESSING:
            return "PROCESSING";

        case STATE_TAMPER_LOCKED:
            return "TAMPER_LOCKED";

        case STATE_ERROR:
            return "ERROR";

        case STATE_INIT:
            return "INIT";

        case STATE_SELFTEST:
            return "SELFTEST";

        default:
            return "UNKNOWN";
    }
}


// =====================================================================
// Forward declarations
// =====================================================================

// void handleCommand(const std::string& cmd);
// void updateLEDs();
// void printStatus();


// =====================================================================
// GPIO initialization
// =====================================================================

static bool initGPIO()
{
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


// =====================================================================
// SETUP / INITIALIZATION
// =====================================================================

static bool initializeHSM()
{
    currentState = STATE_INIT;

    // ------------------------------------------------
    // NVS
    // ------------------------------------------------

    esp_err_t nvs_ret = nvs_flash_init();

    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_LOGW(
            TAG,
            "NVS requires reinitialization; erasing NVS"
        );

        nvs_ret = nvs_flash_erase();

        if (nvs_ret != ESP_OK) {
            ESP_LOGE(
                TAG,
                "NVS erase failed: %s",
                esp_err_to_name(nvs_ret)
            );

            return false;
        }

        nvs_ret = nvs_flash_init();
    }

    if (nvs_ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "NVS initialization failed: %s",
            esp_err_to_name(nvs_ret)
        );

        return false;
    }

    // ------------------------------------------------
    // PSA Crypto
    // ------------------------------------------------

    psa_status_t psaStatus = psa_crypto_init();

    if (psaStatus != PSA_SUCCESS) {
        ESP_LOGE(
            TAG,
            "PSA initialization failed: %ld",
            static_cast<long>(psaStatus)
        );

        return false;
    }

    // ------------------------------------------------
    // GPIO
    // ------------------------------------------------

    if (!initGPIO()) {
        return false;
    }

    // ------------------------------------------------
    // Persistent key state + event log
    // ------------------------------------------------

    if (!keystore_init()) {
        return false;
    }

    // ------------------------------------------------
    // Tamper hardware
    // ------------------------------------------------

    if (!initTamper()) {
        return false;
    }

    /*
     * Establish the ambient-light baseline.
     */

    ldrBaseline = readLDRAveraged();

    if (ldrBaseline < 0) {
        ESP_LOGE(
            TAG,
            "Failed to establish LDR baseline"
        );

        return false;
    }

    ESP_LOGI(
        TAG,
        "LDR baseline: %d",
        ldrBaseline
    );

    // ------------------------------------------------
    // Power-up self-tests
    // ------------------------------------------------

    currentState = STATE_SELFTEST;

    runSelfTests();

    if (currentState == STATE_ERROR) {
        return false;
    }

    ESP_LOGI(
        TAG,
        "HSM initialization complete"
    );

    return true;
}


// =====================================================================
// COMMAND HANDLING
// =====================================================================

void handleCommand(const std::string& cmd)
{
    if (cmd.empty()) {
        return;
    }

    /*
     * Once tamper has locked the module, only STATUS and GETLOG are
     * useful for inspection. Cryptographic commands are rejected.
     */

    if (currentState == STATE_TAMPER_LOCKED) {

        if (cmd != "STATUS" && cmd != "GETLOG") {
            printf("ERR_LOCKED_TAMPER_DETECTED\n");
            return;
        }
    }

    if (currentState == STATE_ERROR) {
        printf("ERR_SELFTEST_FAILED_MODULE_HALTED\n");
        return;
    }

    // ------------------------------------------------
    // PING
    // ------------------------------------------------

    if (cmd == "PING") {
        printf("PONG_HSM_READY\n");
    }

    // ------------------------------------------------
    // STATUS
    // ------------------------------------------------

    else if (cmd == "STATUS") {
        printStatus();
    }

    // ------------------------------------------------
    // GETLOG
    // ------------------------------------------------

    else if (cmd == "GETLOG") {
        printLog();
    }

    // ------------------------------------------------
    // GENKEY
    // ------------------------------------------------

    else if (cmd == "GENKEY") {

        if (!requireAuthorization()) {
            printf("ERR_AUTH_TIMEOUT\n");

            if (currentState != STATE_TAMPER_LOCKED) {
                currentState = STATE_IDLE;
            }

            return;
        }

        currentState = STATE_PROCESSING;

        genKey();

        if (currentState != STATE_TAMPER_LOCKED) {
            currentState = STATE_IDLE;
        }
    }

    // ------------------------------------------------
    // GETPUBKEY
    // ------------------------------------------------

    else if (cmd == "GETPUBKEY") {
        getPubKey();
    }

    // ------------------------------------------------
    // SIGN:<hexdata>
    // ------------------------------------------------

    else if (
        cmd.rfind("SIGN:", 0) == 0
    ) {
        if (!keyExists) {
            printf("ERR_NO_KEY\n");
            return;
        }

        if (!requireAuthorization()) {
            printf("ERR_AUTH_TIMEOUT\n");

            if (currentState != STATE_TAMPER_LOCKED) {
                currentState = STATE_IDLE;
            }

            return;
        }

        currentState = STATE_PROCESSING;

        signData(
            cmd.substr(5)
        );

        if (currentState != STATE_TAMPER_LOCKED) {
            currentState = STATE_IDLE;
        }
    }

    // ------------------------------------------------
    // ZEROIZE
    // ------------------------------------------------

    else if (cmd == "ZEROIZE") {

        if (!requireAuthorization()) {
            printf("ERR_AUTH_TIMEOUT\n");

            if (currentState != STATE_TAMPER_LOCKED) {
                currentState = STATE_IDLE;
            }

            return;
        }

        currentState = STATE_PROCESSING;

        zeroizeKeys();

        if (currentState != STATE_TAMPER_LOCKED) {
            printf("OK_ZEROIZED\n");
            currentState = STATE_IDLE;
        }
    }

    // ------------------------------------------------
    // SETTIME:<unix timestamp>
    // ------------------------------------------------

    else if (cmd.rfind("SETTIME:", 0) == 0)
    {
        const char *value = cmd.c_str() + 8;
        char *end = nullptr;
        unsigned long timestamp = strtoul(value, &end, 10);

        if (end == value || *end != '\0') {
            printf("ERR_BAD_TIME\n");
            return;
        }

        if (!rtcSetUnixTime(static_cast<uint32_t>(timestamp))) {
            printf("ERR_RTC_WRITE\n"); // Changed from ERR_BAD_TIME for better debugging
            return;
        }

        printf("OK_TIME_SYNCED\n");
    }

    else if (cmd == "GETTIME") {
        RtcDateTime time;

        if (!rtcReadTime(time)) {
            printf("ERR_RTC_READ\n");
            return;
        }

        printf(
            "TIME:%04u-%02u-%02u %02u:%02u:%02u\n",
            time.year, time.month, time.day,
            time.hour, time.minute, time.second
        );

        // Convert the already-read time struct directly
        printf("UNIX:%lu\n", static_cast<unsigned long>(convertTimeToUnix(time)));
    }

    else if (cmd == "RTCREGS") {
        rtcDumpRegisters();
    }

    else if (cmd == "RAWREAD") {
        if(!rtcRawRead()) {
            ESP_LOGE(TAG, "Failed to read!");
        }
    }

    // else if (cmd == "RSR_READ") {
    //     if(!rtcExplicitRepeatedStartRead())
    //         ESP_LOGE(TAG, "FAILED repeated read write");
    // }

    // else if (cmd == "RAWREADONE") {
    //     if(!rtcRawReadOneByte()) {
    //         ESP_LOGE(TAG, "Failed to read one byte!");
    //     }
    // }

    else if (cmd == "RAWWRITE") {
        if(!rtcRawWrite()) {
            ESP_LOGE(TAG, "Failed to write!");
        }
    }

    else if (cmd == "DUMMY_WRITE") {
        if(!rtcRawWriteDummyTest()) {
            ESP_LOGE(TAG, "Failed to write to dummy address!");
        }
    }

    else if (cmd == "RS_READ") {
        if(!rtcRepeatedStartRead()) {
            ESP_LOGE(TAG, "Failed to write!");
        }
    }

    else if (cmd == "PROBE") {
        probeRTC();
    }

    else if (cmd == "CHECK_I2C") {
        checkI2CLines();
    }

    // ------------------------------------------------
    // LDRVAL
    // ------------------------------------------------

    else if (cmd == "LDRVAL") {

        int reading = readLDRAveraged();

        if (reading < 0) {
            printf("ERR_LDR_READ\n");
        }
        else {
            printf("%d\n", reading);
        }
    }

    // ------------------------------------------------
    // Unknown command
    // ------------------------------------------------

    else {
        printf("ERR_UNKNOWN_COMMAND\n");
    }
}


// =====================================================================
// STATUS
// =====================================================================

void printStatus()
{
    printf(
        "STATE:%s\n",
        stateToString(currentState)
    );

    printf(
        "KEY_PRESENT:%s\n",
        keyExists ? "YES" : "NO"
    );
}


// =====================================================================
// LED STATUS
// =====================================================================

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


// =====================================================================
// COMMAND TASK
// =====================================================================

static void commandTask(void *arg)
{
    (void)arg;

    char buffer[512];

    while (true) {

        /*
         * stdin is the ESP-IDF console connected to the same console
         * that idf.py monitor is displaying.
         *
         * fgets() blocks this task only; the main HSM task continues
         * running tamper detection and LED updates.
         */

        if (fgets(buffer, sizeof(buffer), stdin) == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        std::string cmd(buffer);

        /*
         * Remove CR/LF.
         */

        while (!cmd.empty() &&
               (cmd.back() == '\n' ||
                cmd.back() == '\r')) {

            cmd.pop_back();
        }

        /*
         * Trim leading whitespace.
         */

        const size_t first = cmd.find_first_not_of(" \t");

        if (first == std::string::npos) {
            continue;
        }

        /*
         * Trim trailing whitespace.
         */

        const size_t last = cmd.find_last_not_of(" \t");

        cmd = cmd.substr(
            first,
            last - first + 1
        );

        handleCommand(cmd);
    }
}


// =====================================================================
// APPLICATION ENTRY POINT
// =====================================================================

extern "C" void app_main()
{
    if (!initializeHSM()) {

        currentState = STATE_ERROR;

        ESP_LOGE(
            TAG,
            "HSM initialization failed"
        );
    }

    if (!initRTC()) {
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

    while (true) {

        checkTamper();

        updateLEDs();
        // checkI2CLines();

        /*
         * Give IDLE0 and other FreeRTOS tasks CPU time.
         */

        vTaskDelay(
            pdMS_TO_TICKS(10)
        );

    //     for (int i = 0; i < 50; ++i) {
    //         int reading = readLDRAveraged();

    //         ESP_LOGI(
    //             TAG,
    //             "STARTUP_LDR[%d] = %d",
    //             i,
    //             reading
    //         );

    //         vTaskDelay(pdMS_TO_TICKS(100));
    //     }
    }
}