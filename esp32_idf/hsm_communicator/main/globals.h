

#ifndef GLOBALS_H
#define GLOBALS_H

#include <cstdint>
#include <string>

#include "nvs.h"
#include "psa/crypto.h"

// ----------------------------------------------------
// Pin assignments
// ----------------------------------------------------

#define BUTTON_PIN   10
#define LDR_PIN      11
#define LED_RED      18
#define LED_YELLOW   17

// ----------------------------------------------------
// Constants
// ----------------------------------------------------

extern const uint32_t TAMPER_CHECK_INTERVAL_MS;
extern const uint32_t AUTH_TIMEOUT_MS;

extern const int LDR_SAMPLES;

#define LOG_MAX_ENTRIES 20
#define LOG_ENTRY_MAXLEN 40

// ----------------------------------------------------
// State Machine
// ----------------------------------------------------

enum HsmState {
    STATE_INIT,
    STATE_SELFTEST,
    STATE_IDLE,
    STATE_AWAITING_AUTH,
    STATE_PROCESSING,
    STATE_TAMPER_LOCKED,
    STATE_ERROR
};

// ----------------------------------------------------
// Global Variables
// ----------------------------------------------------

extern volatile HsmState currentState;

extern nvs_handle_t prefs;

// PSA Crypto
extern psa_key_id_t hsm_key_id;
extern bool keyExists;

extern int ldrBaseline;
extern int TAMPER_THRESHOLD;

extern uint32_t lastTamperCheck;

extern uint32_t lastBlink;
extern bool blinkOn;

extern uint32_t logCounter;

// ----------------------------------------------------
// Functions implemented in main.cpp
// ----------------------------------------------------

void handleCommand(std::string *cmd);
void updateLEDs();
void printStatus();

#endif