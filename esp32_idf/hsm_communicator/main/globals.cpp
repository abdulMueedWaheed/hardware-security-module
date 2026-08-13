

#include "globals.h"
#include <cstdint>

volatile HsmState currentState = STATE_INIT;

nvs_handle_t prefs = 0;

int ldrBaseline = 0;
int TAMPER_THRESHOLD = 4000;

const uint32_t TAMPER_CHECK_INTERVAL_MS = 250;
const uint32_t AUTH_TIMEOUT_MS = 8000;
const int LDR_SAMPLES = 5;

uint32_t lastTamperCheck = 0;

uint32_t lastBlink = 0;
bool blinkOn = false;

uint32_t hostTimeAtSync = 0;
uint32_t millisAtSync = 0;
bool timeSynced = false;

uint32_t logCounter = 0;