#include "rtc.h"
#include <ctime>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

static const char *TAG = "RTC";

static constexpr gpio_num_t RTC_SCL = GPIO_NUM_13;
static constexpr gpio_num_t RTC_SDA = GPIO_NUM_14;

static constexpr uint8_t DS3231_ADDR = 0x68;

static i2c_master_bus_handle_t rtcBus = nullptr;
static i2c_master_dev_handle_t rtcDevice = nullptr;


// The RTC stores data in the form of binary encoded decimal
// so we need to convert between that
// and actual decimal values

static uint8_t bcdToDec(uint8_t value) {
    return ((value >> 4) * 10) + (value & 0x0F);
}

static uint8_t decToBcd(uint8_t value) {
    return static_cast<uint8_t>(
        ((value / 10) << 4) | (value % 10)
    );
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


bool initRTC() {

    //Clear any stuck slave state on the bus
    resetI2CBus(GPIO_NUM_13, GPIO_NUM_14);

    i2c_master_bus_config_t busConfig = {};

    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    busConfig.i2c_port = I2C_NUM_0;
    busConfig.scl_io_num = static_cast<gpio_num_t>(RTC_SCL);
    busConfig.sda_io_num = static_cast<gpio_num_t>(RTC_SDA);
    busConfig.glitch_ignore_cnt = 7;
    busConfig.flags.enable_internal_pullup = false; // Using HW-084's built-in 4.7k resistors

    esp_err_t err = i2c_new_master_bus(&busConfig, &rtcBus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(err));
        return false;
    }

    i2c_device_config_t deviceConfig = {};
    deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    deviceConfig.device_address = DS3231_ADDR; // Must be 0x68
    deviceConfig.scl_speed_hz = 100000;         // 100kHz
    deviceConfig.scl_wait_us = 100000;
    deviceConfig.flags = {};

    err = i2c_master_bus_add_device(rtcBus, &deviceConfig, &rtcDevice);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add DS3231: %s", esp_err_to_name(err));
        i2c_del_master_bus(rtcBus); // Cleanup bus on failure
        rtcBus = nullptr;
        return false;
    }

    err = i2c_master_probe(rtcBus, DS3231_ADDR, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 not detected: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(rtcDevice); // Cleanup device & bus
        i2c_del_master_bus(rtcBus);
        rtcDevice = nullptr;
        rtcBus = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "DS3231 detected at 0x68");
    return true;
}


bool rtcPresent() {
    return rtcDevice != nullptr;
}

bool rtcReadTime(RtcDateTime& time) {
    if (rtcDevice == nullptr) return false;

    uint8_t reg = 0x00;
    uint8_t data[7] = {};

    // 1. Send register pointer address (0x00)
    esp_err_t err = i2c_master_transmit(rtcDevice, &reg, 1, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC register select failed: %s", esp_err_to_name(err));
        return false;
    }

    // Small delay to let DS3231 internal memory pointer settle
    esp_rom_delay_us(100);

    // 2. Read 7 time registers
    err = i2c_master_receive(rtcDevice, data, sizeof(data), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC data read failed: %s", esp_err_to_name(err));
        return false;
    }

    // Decode BCD values
    time.second = bcdToDec(data[0] & 0x7F);
    time.minute = bcdToDec(data[1] & 0x7F);
    time.hour   = bcdToDec(data[2] & 0x3F);
    time.day    = bcdToDec(data[4] & 0x3F);
    time.month  = bcdToDec(data[5] & 0x1F);
    time.year   = 2000 + bcdToDec(data[6]);

    return true;
}

bool rtcSetTime(const RtcDateTime& time)
{
    if (time.year < 2000 || time.year > 2099 ||
        time.month < 1 || time.month > 12 ||
        time.day < 1 || time.day > 31 ||
        time.hour > 23 ||
        time.minute > 59 ||
        time.second > 59) {
        return false;
    }

    uint8_t data[8] = {};

    data[0] = 0x00; // start register
    data[1] = decToBcd(time.second);
    data[2] = decToBcd(time.minute);
    data[3] = decToBcd(time.hour);
    data[4] = decToBcd(1); // day-of-week
    data[5] = decToBcd(time.day);
    data[6] = decToBcd(time.month);
    data[7] = decToBcd(
        static_cast<uint8_t>(time.year % 100)
    );

    esp_err_t err = i2c_master_transmit(
        rtcDevice,
        data,
        sizeof(data),
        1000
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "RTC write failed: %s",
            esp_err_to_name(err)
        );
        return false;
    }

    return true;
}

static bool isLeapYear(uint16_t year) {
    return (year % 4 == 0 &&
            year % 100 != 0) ||
           (year % 400 == 0);
}

static uint32_t daysBeforeMonth(uint16_t year, uint8_t month)
{
    static constexpr uint16_t daysPerMonth[] = {
        0, 31, 59, 90, 120, 151,
        181, 212, 243, 273, 304, 334
    };

    uint32_t elapsedDays = daysPerMonth[month - 1];

    if (month > 2 && isLeapYear(year)) {
        ++elapsedDays;
    }

    return elapsedDays;
}


uint32_t rtcGetUnixTime() {
    RtcDateTime time;

    if (!rtcReadTime(time)) {
        return 0;
    }

    // Safety check for uninitialized or zeroed RTC memory
    if (time.year < 1970 || time.month < 1 || time.month > 12 || time.day < 1 || time.day > 31) {
        return 0;
    }

    uint32_t days = 0;

    for (uint16_t year = 1970; year < time.year; ++year) {
        days += isLeapYear(year) ? 366 : 365;
    }

    days += daysBeforeMonth(time.year, time.month);
    days += time.day - 1;

    return days * 86400UL
         + static_cast<uint32_t>(time.hour) * 3600UL
         + static_cast<uint32_t>(time.minute) * 60UL
         + time.second;
}


bool rtcSetUnixTime(uint32_t unixTime) {
    time_t rawTime = static_cast<time_t>(unixTime);
    struct tm tm_info;

    // Convert epoch directly to UTC calendar components
    if (gmtime_r(&rawTime, &tm_info) == nullptr) {
        return false;
    }

    // DS3231 validity check (2000 to 2099)
    int fullYear = tm_info.tm_year + 1900;
    if (fullYear < 2000 || fullYear > 2099) {
        return false;
    }

    RtcDateTime time;
    time.year   = static_cast<uint16_t>(fullYear);
    time.month  = static_cast<uint8_t>(tm_info.tm_mon + 1); // struct tm uses 0-11
    time.day    = static_cast<uint8_t>(tm_info.tm_mday);    // 1-31
    time.hour   = static_cast<uint8_t>(tm_info.tm_hour);    // 0-23
    time.minute = static_cast<uint8_t>(tm_info.tm_min);     // 0-59
    time.second = static_cast<uint8_t>(tm_info.tm_sec);     // 0-59

    // Return the actual boolean success of the I2C transfer
    return rtcSetTime(time);
}

bool rtcDumpRegisters()
{
    uint8_t reg = 0x00;
    uint8_t data[7] = {};

    esp_err_t err = i2c_master_transmit_receive(
        rtcDevice,
        &reg,
        1,
        data,
        sizeof(data),
        1000
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "RTC register read failed: %s",
            esp_err_to_name(err)
        );
        return false;
    }

    ESP_LOGI(
        TAG,
        "RTC regs: %02X %02X %02X %02X %02X %02X %02X",
        data[0],
        data[1],
        data[2],
        data[3],
        data[4],
        data[5],
        data[6]
    );

    return true;
}

bool rtcTestWrite()
{
    uint8_t data[] = {
        0x00, // start register
        0x56, // seconds = 56
        0x34, // minutes = 34
        0x12, // hours = 12
        0x01, // day of week
        0x01, // date
        0x01, // month
        0x26  // year = 2026
    };

    esp_err_t err = i2c_master_transmit(
        rtcDevice,
        data,
        sizeof(data),
        1000
    );

    ESP_LOGI(
        TAG,
        "RTC test write result: %s",
        esp_err_to_name(err)
    );

    return err == ESP_OK;
}

bool rtcTestRead()
{
    // Guard against uninitialized or corrupted handles
    if (rtcBus == nullptr || rtcDevice == nullptr) {
        ESP_LOGE(TAG, "Cannot test RTC: handles are NULL (bus: %p, dev: %p)", rtcBus, rtcDevice);
        return false;
    }

    // Print handle addresses to detect buffer corruption early
    ESP_LOGI(TAG, "Probing bus at handle %p...", rtcBus);
    esp_err_t probe = i2c_master_probe(rtcBus, DS3231_ADDR, 1000);

    ESP_LOGI(TAG, "Probe result: %s", esp_err_to_name(probe));
    if (probe != ESP_OK) {
        return false;
    }

    uint8_t reg = 0x00;
    uint8_t value = 0;

    esp_err_t err = i2c_master_transmit_receive(
        rtcDevice,
        &reg,
        1,
        &value,
        1,
        1000
    );

    ESP_LOGI(TAG, "RTC single-byte read: %s", esp_err_to_name(err));

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTC register 0x00 = 0x%02X", value);
    }

    return err == ESP_OK;
}

uint32_t convertTimeToUnix(const RtcDateTime& time) {
    // Sanity check to prevent invalid conversions on uninitialized data
    if (time.year < 1970 || time.month < 1 || time.month > 12 || time.day < 1 || time.day > 31) {
        return 0;
    }

    struct tm tm_info = {};
    tm_info.tm_sec  = time.second;
    tm_info.tm_min  = time.minute;
    tm_info.tm_hour = time.hour;
    tm_info.tm_mday = time.day;
    tm_info.tm_mon  = time.month - 1;    // struct tm expects months 0–11
    tm_info.tm_year = time.year - 1900;  // struct tm expects years since 1900

    time_t timestamp = timegm(&tm_info); // Converts UTC tm struct to Epoch seconds
    
    return (timestamp == static_cast<time_t>(-1)) ? 0 : static_cast<uint32_t>(timestamp);
}

void probeRTC()
{
    for (int i = 0; i < 5; ++i) {
        esp_err_t err = i2c_master_probe(
            rtcBus,
            DS3231_ADDR,
            1000
        );

        ESP_LOGI(
            TAG,
            "Probe %d: %s",
            i,
            esp_err_to_name(err)
        );

        // vTaskDelay(pdMS_TO_TICKS(500));
    }
}

bool rtcRawWrite()
{
    uint8_t data[] = {
        0x00, // seconds register
        0x00  // seconds = 00
    };

    esp_err_t err = i2c_master_transmit(
        rtcDevice,
        data,
        sizeof(data),
        1000
    );

    ESP_LOGI(
        TAG,
        "Raw write: %s",
        esp_err_to_name(err)
    );

    return err == ESP_OK;
}

bool rtcRawRead()
{
    uint8_t reg = 0x00;
    uint8_t value = 0;

    esp_err_t err = i2c_master_transmit_receive(
        rtcDevice,
        &reg,
        1,
        &value,
        1,
        1000
    );

    ESP_LOGI(
        TAG,
        "RTC raw read: %s",
        esp_err_to_name(err)
    );

    if (err == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Register 0x%02X = 0x%02X",
            reg,
            value
        );
    }

    return err == ESP_OK;
}

bool rtcRegisterWriteTest()
{
    uint8_t reg = 0x00;

    esp_err_t err = i2c_master_transmit(
        rtcDevice,
        &reg,
        1,
        1000
    );

    ESP_LOGI(
        TAG,
        "RTC register write test: %s",
        esp_err_to_name(err)
    );

    return err == ESP_OK;
}

bool rtcRepeatedStartRead()
{
    uint8_t reg = 0x00;
    uint8_t value = 0;

    i2c_operation_job_t operations[5] = {};

    operations[0].command = I2C_MASTER_CMD_START;

    operations[1].command = I2C_MASTER_CMD_WRITE;
    operations[1].write.ack_check = true;
    operations[1].write.data = &reg;
    operations[1].write.total_bytes = 1;

    operations[2].command = I2C_MASTER_CMD_START;

    operations[3].command = I2C_MASTER_CMD_READ;
    operations[3].read.ack_value = I2C_NACK_VAL;
    operations[3].read.data = &value;
    operations[3].read.total_bytes = 1;

    operations[4].command = I2C_MASTER_CMD_STOP;

    esp_err_t err = i2c_master_execute_defined_operations(
        rtcDevice,
        operations,
        5,
        1000
    );

    ESP_LOGI(TAG,
             "Repeated-start read: %s",
             esp_err_to_name(err));

    if (err == ESP_OK) {
        ESP_LOGI(TAG,
                 "Register 0x%02X = 0x%02X",
                 reg,
                 value);
    }

    return err == ESP_OK;
}

bool rtcRawWriteDummyTest()
{
    if (rtcBus == nullptr) {
        ESP_LOGE(TAG, "RTC Bus not initialized!");
        return false;
    }

    // Configure a temporary dummy device on non-existent address 0x3C
    i2c_device_config_t dummyConfig = {};
    dummyConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dummyConfig.device_address = 0x3C; // Unused dummy address
    dummyConfig.scl_speed_hz = 100000;
    dummyConfig.scl_wait_us = 1000;

    i2c_master_dev_handle_t dummyDevice = nullptr;
    esp_err_t err = i2c_master_bus_add_device(rtcBus, &dummyConfig, &dummyDevice);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add dummy device: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t data[] = {
        0x00, // register
        0x00  // dummy value
    };

    // Attempt to write to 0x3C
    err = i2c_master_transmit(
        dummyDevice,
        data,
        sizeof(data),
        1000
    );

    ESP_LOGI(
        TAG,
        "Dummy write to 0x3C result: %s",
        esp_err_to_name(err)
    );

    // Clean up dummy device handle
    i2c_master_bus_rm_device(dummyDevice);

    return err == ESP_OK;
}