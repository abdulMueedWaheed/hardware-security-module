/*
 * HSM Firmware for ESP32-S3 (N16R8)
 * -----------------------------------------
 * General-purpose Hardware Security Module demo, designed with reference
 * to FIPS 140-2 requirement areas achievable on this hardware:
 *
 *   Area 3  - Roles/Authentication : pushbutton gates GENKEY / SIGN / ZEROIZE
 *   Area 4  - Finite State Model   : explicit HsmState enum + transitions
 *   Area 5  - Physical Security    : LDR as a tamper sensor -> auto zeroize
 *   Area 7  - Key Management       : NVS storage, zeroization on tamper/cmd,
 *                                    keys cleared from RAM after use
 *   Area 9  - Self-Tests           : power-up KAT (SHA-256) + pairwise
 *                                    consistency test on every key generation
 *
 * Pin map (confirmed working wiring):
 *   BUTTON_PIN  -> GPIO10  (pull-down, HIGH = pressed)
 *   LDR_PIN     -> GPIO11  (analog, voltage divider w/ 220ohm to GND)
 *   LED_RED     -> GPIO16  (tamper / error indicator)
 *   LED_YELLOW  -> GPIO15  (auth / processing indicator)
 *
 * Serial protocol (115200 baud, line-based, ASCII):
 *   PING                   -> PONG_HSM_READY
 *   STATUS                 -> STATE:<x> / KEY_PRESENT:<YES|NO>
 *   LDRVAL                 -> raw analog reading (use this to calibrate TAMPER_THRESHOLD)
 *   GENKEY                 -> requires button press -> OK_KEY_GENERATED / ERR_*
 *   GETPUBKEY              -> hex-encoded uncompressed public key / ERR_NO_KEY
 *   SIGN:<hexdata>         -> requires button press -> hex-encoded DER signature
 *   ZEROIZE                -> requires button press -> OK_ZEROIZED
 *   LOG                    -> prints out log
 */

#include "globals.h"

#include "crypto_ops.h"
#include "storage.h"
#include "tamper.h"

// ----------------------------------------------------
// Global definitions
// ----------------------------------------------------

Preferences prefs;

mbedtls_entropy_context entropy;
mbedtls_ctr_drbg_context ctr_drbg;

volatile HsmState currentState = STATE_INIT;

bool keyExists = false;

int ldrBaseline = 0;
int TAMPER_THRESHOLD = 1700;

const unsigned long TAMPER_CHECK_INTERVAL_MS = 250;
const unsigned long AUTH_TIMEOUT_MS = 8000;
const int LDR_SAMPLES = 5;

unsigned long lastTamperCheck = 0;

unsigned long lastBlink = 0;
bool blinkOn = false;

unsigned long hostTimeAtSync = 0;
unsigned long millisAtSync = 0;
bool timeSynced = false;

unsigned long logCounter = 0;


// =====================================================================
// SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, LOW);

  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  const char *pers = "hsm_keygen";
  mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                         (const unsigned char *)pers, strlen(pers));

  prefs.begin("hsm", false);
  keyExists = prefs.isKey("privkey");

  // Establish a light-level baseline for tamper detection.
  ldrBaseline = readLDRAveraged();

  currentState = STATE_SELFTEST;
  runSelfTests();
}

// =====================================================================
// MAIN LOOP
// =====================================================================
void loop() {
  checkTamper();
  updateLEDs();

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      handleCommand(cmd);
    }
  }
}


// =====================================================================
// COMMAND HANDLING
// =====================================================================
void handleCommand(String cmd) {
  if (currentState == STATE_TAMPER_LOCKED) {
    Serial.println("ERR_LOCKED_TAMPER_DETECTED");
    return;
  }
  
  if (currentState == STATE_ERROR) {
    Serial.println("ERR_SELFTEST_FAILED_MODULE_HALTED");
    return;
  }

  if (cmd == "PING") {
    Serial.println("PONG_HSM_READY");
  }
  
  else if (cmd == "STATUS") {
    printStatus();
  }

  else if (cmd == "LOG") {
    printLog();
  }
  
  else if (cmd == "GENKEY") {
    if (!requireAuthorization()) {
      Serial.println("ERR_AUTH_TIMEOUT");
      currentState = STATE_IDLE;
      return;
    }
    currentState = STATE_PROCESSING;
    genKey();
    currentState = STATE_IDLE;
  }

  else if (cmd == "GETPUBKEY") {
    getPubKey();
  }
  
  else if (cmd.startsWith("SIGN:")) {
    if (!keyExists) {
      Serial.println("ERR_NO_KEY");
      return;
    }
    if (!requireAuthorization()) {
      Serial.println("ERR_AUTH_TIMEOUT");
      currentState = STATE_IDLE;
      return;
    }
    currentState = STATE_PROCESSING;
    signData(cmd.substring(5));
    currentState = STATE_IDLE;
  }
  
  else if (cmd == "ZEROIZE") {
    if (!requireAuthorization()) {
      Serial.println("ERR_AUTH_TIMEOUT");
      currentState = STATE_IDLE;
      return;
    }
    currentState = STATE_PROCESSING;
    zeroizeKeys();
    Serial.println("OK_ZEROIZED");
    currentState = STATE_IDLE;
  }
  
  else if (cmd.startsWith("SETTIME:")) {
    unsigned long ts = strtoul(cmd.substring(8).c_str(), NULL, 10);
    hostTimeAtSync = ts;
    millisAtSync = millis();
    timeSynced = true;
    Serial.println("OK_TIME_SYNCED");
  }

  else if (cmd == "LDRVAL") {
    Serial.println(analogRead(LDR_PIN));
  }
  
  else {
    Serial.println("ERR_UNKNOWN_COMMAND");
  }
}

void printStatus() {
  Serial.print("STATE:");
  switch (currentState) {
    case STATE_IDLE:           Serial.println("IDLE"); break;
    case STATE_AWAITING_AUTH:  Serial.println("AWAITING_AUTH"); break;
    case STATE_PROCESSING:     Serial.println("PROCESSING"); break;
    case STATE_TAMPER_LOCKED:  Serial.println("TAMPER_LOCKED"); break;
    case STATE_ERROR:          Serial.println("ERROR"); break;
    default:                   Serial.println("UNKNOWN"); break;
  }
  Serial.print("KEY_PRESENT:");
  Serial.println(keyExists ? "YES" : "NO");
}



// =====================================================================
// LED STATUS (mirrors the finite state model)
// =====================================================================

void updateLEDs() {
  unsigned long now = millis();

  switch (currentState) {
    case STATE_IDLE:
      digitalWrite(LED_RED, LOW);
      digitalWrite(LED_YELLOW, LOW);
      break;

    case STATE_AWAITING_AUTH:
      digitalWrite(LED_RED, LOW);
      if (now - lastBlink > 300) {
        blinkOn = !blinkOn;
        digitalWrite(LED_YELLOW, blinkOn);
        lastBlink = now;
      }
      break;

    case STATE_PROCESSING:
      digitalWrite(LED_YELLOW, HIGH);
      digitalWrite(LED_RED, LOW);
      break;

    case STATE_TAMPER_LOCKED:
      digitalWrite(LED_YELLOW, LOW);
      if (now - lastBlink > 150) {
        blinkOn = !blinkOn;
        digitalWrite(LED_RED, blinkOn);
        lastBlink = now;
      }
      break;

    case STATE_ERROR:
      digitalWrite(LED_YELLOW, LOW);
      digitalWrite(LED_RED, HIGH);
      break;

    default:
      break;
  }
}



