#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <mbedtls/ecdsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha256.h>

// ----------------------------------------------------
// Pin assignments
// ----------------------------------------------------

#define BUTTON_PIN   10
#define LDR_PIN      4
#define LED_RED      16
#define LED_YELLOW   15

// ----------------------------------------------------
// Constants
// ----------------------------------------------------

extern const unsigned long TAMPER_CHECK_INTERVAL_MS;
extern const unsigned long AUTH_TIMEOUT_MS;

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

extern Preferences prefs;

extern mbedtls_entropy_context entropy;
extern mbedtls_ctr_drbg_context ctr_drbg;

extern bool keyExists;

extern int ldrBaseline;
extern int TAMPER_THRESHOLD;

extern unsigned long lastTamperCheck;

extern unsigned long lastBlink;
extern bool blinkOn;

extern unsigned long hostTimeAtSync;
extern unsigned long millisAtSync;
extern bool timeSynced;

extern unsigned long logCounter;

// ----------------------------------------------------
// Functions implemented in hsm_firmware.ino
// ----------------------------------------------------

void handleCommand(String cmd);
void updateLEDs();
void printStatus();












