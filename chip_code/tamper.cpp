#include "tamper.h"

#include "storage.h"

// =====================================================================
// TAMPER DETECTION (FIPS 140-2 Area 5 - physical security)
// =====================================================================
// ---------------- Tamper detection smoothing ----------------

int readLDRAveraged() {
  long sum = 0;
  for (int i = 0; i < LDR_SAMPLES; i++) {
    sum += analogRead(LDR_PIN);
    delay(2);
  }
  return sum / LDR_SAMPLES;
}


void checkTamper() {
  if (currentState == STATE_TAMPER_LOCKED) return;
  if (millis() - lastTamperCheck < TAMPER_CHECK_INTERVAL_MS) return;
  lastTamperCheck = millis();

  int reading = readLDRAveraged();
  int current_value = abs(reading - ldrBaseline);
  if (current_value > TAMPER_THRESHOLD) {
    zeroizeKeys();
    currentState = STATE_TAMPER_LOCKED;
    Serial.print("TAMPER_DETECTED_KEYS_ZEROIZED, current_value: ");
    Serial.println(current_value);
  }
}


// =====================================================================
// AUTHORIZATION GATE (FIPS 140-2 Area 3 - roles/authentication)
// =====================================================================
// Blocks until the button is pressed (fresh press) or times out.
// Returns true if authorized, false on timeout.

bool requireAuthorization() {
  currentState = STATE_AWAITING_AUTH;
  Serial.println("AWAITING_AUTH_PRESS_BUTTON");

  unsigned long start = millis();

  // If the button happens to already be held down, wait for release first
  // so a stale press doesn't auto-authorize the next command.
  while (digitalRead(BUTTON_PIN) == HIGH && millis() - start < AUTH_TIMEOUT_MS) {
    delay(20);
  }

  start = millis();
  while (millis() - start < AUTH_TIMEOUT_MS) {
    if (digitalRead(BUTTON_PIN) == HIGH) {
      delay(30); // debounce
      if (digitalRead(BUTTON_PIN) == HIGH) {
        return true;
      }
    }
    checkTamper();  // stay responsive to tamper even while waiting
    delay(10);
  }
  return false; // timed out
}


















