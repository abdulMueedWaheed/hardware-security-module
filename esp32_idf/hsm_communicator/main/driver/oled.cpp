#include "oled.h"

#include <iostream>
#include <format>
#include <sstream>

#include "../app/state.h"
#include "../security/authorization.h"
#include "../security/tamper.h"
#include "../security/crypto_ops.h"

static const char *TAG = "OLED";

static i2c_master_dev_handle_t oled_dev = nullptr;
static uint8_t oled_buffer[1024] = {0}; // 128x64 pixels / 8 bits = 1024 bytes

// Helper to send a single command byte
static esp_err_t oled_write_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd}; // 0x00 = Command control byte
    return i2c_master_transmit(oled_dev, buf, sizeof(buf), 1000);
}

// Clear internal frame buffer
void oledClear() {
    memset(oled_buffer, 0, sizeof(oled_buffer));
}

// Push local frame buffer to physical SSD1306 display
void oledUpdate() {
    if (!oled_dev) return;

    // Reset column and page pointers to (0,0)
    oled_write_cmd(0x21); oled_write_cmd(0); oled_write_cmd(127);
    oled_write_cmd(0x22); oled_write_cmd(0); oled_write_cmd(7);

    // Write 1024 bytes of graphics RAM
    uint8_t tx_buf[1025];
    tx_buf[0] = 0x40; // 0x40 = Data stream control byte
    memcpy(&tx_buf[1], oled_buffer, 1024);

    i2c_master_transmit(oled_dev, tx_buf, sizeof(tx_buf), 1000);
}

// Print text at line (0-7) and column (0-120)
void oledWriteString(uint8_t line, uint8_t col, const char *str) {
    if (line > 7 || col >= OLED_WIDTH) return;

    while (*str) {
        char c = *str++;
        if (c < 32 || c > 90) c = ' '; // Cap to supported ASCII set

        uint16_t font_idx = c - 32;
        uint16_t buf_idx = (line * OLED_WIDTH) + col;

        for (int i = 0; i < 5; i++) {
            if (col + i < OLED_WIDTH) {
                oled_buffer[buf_idx + i] = font5x7[font_idx][i];
            }
        }

        col += 6; // 5 pixels width + 1 pixel spacing
    }
}

// Initialize SSD1306 using existing master bus handle
bool initOLED(i2c_master_bus_handle_t bus_handle) {
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = OLED_ADDR;
    dev_cfg.scl_speed_hz = 400000; // SSD1306 supports Fast Mode (400kHz)

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &oled_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach OLED to I2C bus: %s", esp_err_to_name(err));
        return false;
    }

    // SSD1306 Power-On Initialization Sequence
    const uint8_t init_cmds[] = {
        0xAE,       // Display OFF
        0xD5, 0x80, // Set Display Clock Divide Ratio
        0xA8, 0x3F, // Set Multiplex Ratio (1 to 64)
        0xD3, 0x00, // Set Display Offset
        0x40,       // Set Start Line at 0
        0x8D, 0x14, // Enable Charge Pump
        0x20, 0x00, // Memory Addressing Mode (Horizontal)
        0xA1,       // Segment Re-map (Flips horizontally)
        0xC8,       // COM Output Scan Direction (Flips vertically)
        0xDA, 0x12, // Set COM Pins Hardware Configuration
        0x81, 0xCF, // Set Contrast Control
        0xD9, 0xF1, // Set Pre-charge Period
        0xDB, 0x40, // Set VCOMH Deselect Level
        0xA4,       // Entire Display ON (Follow RAM)
        0xA6,       // Set Normal Display (Not Inverted)
        0xAF        // Display ON
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        oled_write_cmd(init_cmds[i]);
    }

    oledClear();
    oledUpdate();

    ESP_LOGI(TAG, "SSD1306 OLED initialized successfully at 0x3C");
    return true;
}

void oledDisplayStatus() {

    oledWriteString(0, 0, "=== ESP32 HSM ===");

    std::stringstream ss;
    ss << "STATE: " << stateToString(currentState);
    oledWriteString(2, 0, ss.str().c_str());

    oledWriteString(
        3,
        0,
        isAuthorized() ? "AUTH: YES" : "AUTH: NO"
    );

    oledWriteString(
        5,
        0,
        hasKey() ? "KEY: PRESENT" : "KEY: NONE"
    );

    char ldrText[20];
    snprintf(
        ldrText,
        sizeof(ldrText),
        "LDR: %d",
        readLDRAveraged()
    );

    oledWriteString(7, 0, ldrText);
}