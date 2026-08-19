#include "FreeRTOSConfig.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "portmacro.h"


#include "interface.h"
#include "state.h"

#include "../driver/rtc.h"
#include "../driver/oled.h"

#include "../hardware/i2c_driver.h"

#include "../security/authorization.h"
#include "../security/storage.h"
#include "../security/tamper.h"
#include "../security/crypto_ops.h"


static const char * TAG = "INTERFACE";


void handleCommand(const std::string& cmd) {
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

        generateKey();

        if (currentState != STATE_TAMPER_LOCKED) {
            currentState = STATE_IDLE;
        }
    }

    // ------------------------------------------------
    // GETPUBKEY
    // ------------------------------------------------

    else if (cmd == "GETPUBKEY") {
        std::string publicKey;

        if (!getPubKey(publicKey)) {
            printf("ERR_NO_KEY\n");
            return;
        }

        printf("PUBKEY:%s\n", publicKey.c_str());
    }

    // ------------------------------------------------
    // SIGN:<hexdata>
    // ------------------------------------------------

    else if (cmd.rfind("SIGN:", 0) == 0) {
        if (!hasKey()) {
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

        std::string signature;

        if (!signData(cmd.substr(5), signature)) {
            printf("ERR_SIGN_FAILED\n");

            if (currentState != STATE_TAMPER_LOCKED) {
                currentState = STATE_IDLE;
            }

            return;
        }

        printf("SIGNATURE:%s\n", signature.c_str());

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

        zeroize();

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
        if(!rtcRawWriteDummyTest(hwI2cBus)) {
            ESP_LOGE(TAG, "Failed to write to dummy address!");
        }
    }

    else if (cmd == "RS_READ") {
        if(!rtcRepeatedStartRead()) {
            ESP_LOGE(TAG, "Failed to write!");
        }
    }

    else if (cmd == "PROBE") {
        probeRTC(hwI2cBus);
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

void printStatus()
{
    printf(
        "STATE:%s\n",
        stateToString(currentState)
    );

    printf(
        "KEY_PRESENT:%s\n",
        hasKey() ? "YES" : "NO"
    );
}

void commandTask(void *arg) {
    (void)arg;

    char buffer[512];

    while (true) {

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