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
 *   PING                 -> PONG_HSM_READY
 *   STATUS               -> STATE:<x> / KEY_PRESENT:<YES|NO>
 *   LDRVAL                -> raw analog reading (use this to calibrate TAMPER_THRESHOLD)
 *   GENKEY                -> requires button press -> OK_KEY_GENERATED / ERR_*
 *   GETPUBKEY              -> hex-encoded uncompressed public key / ERR_NO_KEY
 *   SIGN:<hexdata>         -> requires button press -> hex-encoded DER signature
 *   ZEROIZE                -> requires button press -> OK_ZEROIZED
 */

#include <Preferences.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha256.h>

// ---------------- Pin assignments ----------------
#define BUTTON_PIN 10
#define LDR_PIN    11
#define LED_RED    16
#define LED_YELLOW 15

// ---------------- Tamper detection ----------------
// IMPORTANT: run LDRVAL a few times under normal ("closed") conditions and
// under "tampered/opened" conditions to pick real numbers for your enclosure.
int ldrBaseline = 0;
int TAMPER_THRESHOLD = 1500;   // placeholder - calibrate against your setup
const unsigned long TAMPER_CHECK_INTERVAL_MS = 250;
unsigned long lastTamperCheck = 0;
const int LDR_SAMPLES = 5;

// ---------------- Authorization ----------------
const unsigned long AUTH_TIMEOUT_MS = 8000;

// ---------------- State machine ----------------
enum HsmState {
  STATE_INIT,
  STATE_SELFTEST,
  STATE_IDLE,
  STATE_AWAITING_AUTH,
  STATE_PROCESSING,
  STATE_TAMPER_LOCKED,
  STATE_ERROR
};
volatile HsmState currentState = STATE_INIT;

// ---------------- Crypto / storage handles ----------------
Preferences prefs;
mbedtls_entropy_context entropy;
mbedtls_ctr_drbg_context ctr_drbg;
bool keyExists = false;

// ---------------- LED blink bookkeeping ----------------
unsigned long lastBlink = 0;
bool blinkOn = false;

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
// SELF-TESTS (FIPS 140-2 Area 9 - power-up tests)
// =====================================================================
bool selfTestSHA256() {
  // Known-answer test: SHA-256("abc")
  const char *msg = "abc";
  unsigned char hash[32];
  mbedtls_sha256((const unsigned char *)msg, strlen(msg), hash, 0);

  const unsigned char expected[32] = {
    0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
    0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
    0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
    0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
  };
  return memcmp(hash, expected, 32) == 0;
}

bool selfTestECDSA() {
  // Sign/verify round-trip test using a throwaway key (not the stored HSM key).
  mbedtls_ecdsa_context ecdsa;
  mbedtls_ecdsa_init(&ecdsa);

  int ret = mbedtls_ecdsa_genkey(&ecdsa, MBEDTLS_ECP_DP_SECP256R1,
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
  if (ret != 0) { mbedtls_ecdsa_free(&ecdsa); return false; }

  unsigned char hash[32];
  const char *msg = "hsm_selftest_vector";
  mbedtls_sha256((const unsigned char *)msg, strlen(msg), hash, 0);

  unsigned char sig[MBEDTLS_ECDSA_MAX_LEN];
  size_t sigLen;
  ret = mbedtls_ecdsa_write_signature(&ecdsa, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                                       sig, sizeof(sig), &sigLen,
                                       mbedtls_ctr_drbg_random, &ctr_drbg);
  if (ret == 0) {
    ret = mbedtls_ecdsa_read_signature(&ecdsa, hash, sizeof(hash), sig, sigLen);
  }
  mbedtls_ecdsa_free(&ecdsa);
  return ret == 0;
}

void runSelfTests() {
  Serial.println("SELFTEST_RUNNING");

  bool shaOk   = selfTestSHA256();
  bool ecdsaOk = selfTestECDSA();

  if (shaOk && ecdsaOk) {
    Serial.println("SELFTEST_PASS");
    currentState = STATE_IDLE;
  } else {
    if (!shaOk)   Serial.println("SELFTEST_FAIL_SHA256");
    if (!ecdsaOk) Serial.println("SELFTEST_FAIL_ECDSA");
    Serial.println("SELFTEST_FAIL");
    currentState = STATE_ERROR;   // Module halts - will not execute commands
  }
}

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
  if (abs(reading - ldrBaseline) > TAMPER_THRESHOLD) {
    zeroizeKeys();
    currentState = STATE_TAMPER_LOCKED;
    Serial.println("TAMPER_DETECTED_KEYS_ZEROIZED");
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
  else if (cmd == "LDRVAL") {
    Serial.println(analogRead(LDR_PIN));
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
// KEY MANAGEMENT (FIPS 140-2 Area 7)
// =====================================================================
void genKey() {
  mbedtls_ecdsa_context ecdsa;
  mbedtls_ecdsa_init(&ecdsa);

  int ret = mbedtls_ecdsa_genkey(&ecdsa, MBEDTLS_ECP_DP_SECP256R1,
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
  if (ret != 0) {
    Serial.println("ERR_KEYGEN_FAILED");
    mbedtls_ecdsa_free(&ecdsa);
    return;
  }

  // Pairwise consistency test - required conditional test before a newly
  // generated key pair is accepted/stored.
  unsigned char testHash[32];
  const char *testMsg = "pairwise_consistency_check";
  mbedtls_sha256((const unsigned char *)testMsg, strlen(testMsg), testHash, 0);

  unsigned char testSig[MBEDTLS_ECDSA_MAX_LEN];
  size_t testSigLen;
  ret = mbedtls_ecdsa_write_signature(&ecdsa, MBEDTLS_MD_SHA256, testHash, sizeof(testHash),
                                       testSig, sizeof(testSig), &testSigLen,
                                       mbedtls_ctr_drbg_random, &ctr_drbg);
  if (ret == 0) {
    ret = mbedtls_ecdsa_read_signature(&ecdsa, testHash, sizeof(testHash), testSig, testSigLen);
  }
  if (ret != 0) {
    Serial.println("ERR_PAIRWISE_CONSISTENCY_FAILED");
    mbedtls_ecdsa_free(&ecdsa);
    return;
  }

  unsigned char priv_buf[32];
  mbedtls_mpi_write_binary(&ecdsa.MBEDTLS_PRIVATE(d), priv_buf, sizeof(priv_buf));

  unsigned char pub_buf[65];
  size_t pub_len;
  mbedtls_ecp_point_write_binary(&ecdsa.MBEDTLS_PRIVATE(grp), &ecdsa.MBEDTLS_PRIVATE(Q),
                                  MBEDTLS_ECP_PF_UNCOMPRESSED, &pub_len,
                                  pub_buf, sizeof(pub_buf));

  prefs.putBytes("privkey", priv_buf, sizeof(priv_buf));
  prefs.putBytes("pubkey", pub_buf, pub_len);
  keyExists = true;

  mbedtls_ecdsa_free(&ecdsa);
  memset(priv_buf, 0, sizeof(priv_buf)); // clear private key from RAM

  Serial.println("OK_KEY_GENERATED");
}

void getPubKey() {
  if (!keyExists) {
    Serial.println("ERR_NO_KEY");
    return;
  }
  unsigned char pub_buf[65];
  size_t len = prefs.getBytes("pubkey", pub_buf, sizeof(pub_buf));
  if (len == 0) {
    Serial.println("ERR_NO_KEY");
    return;
  }
  String hexStr = "";
  for (size_t i = 0; i < len; i++) {
    if (pub_buf[i] < 0x10) hexStr += "0";
    hexStr += String(pub_buf[i], HEX);
  }
  Serial.println(hexStr);
}

int hexStringToBytes(String hex, unsigned char *out, size_t maxLen) {
  size_t len = hex.length() / 2;
  if (len > maxLen) return -1;
  for (size_t i = 0; i < len; i++) {
    String byteStr = hex.substring(i * 2, i * 2 + 2);
    out[i] = (unsigned char) strtol(byteStr.c_str(), NULL, 16);
  }
  return len;
}

void signData(String hexData) {
  unsigned char outData[256];
  int dataLen = hexStringToBytes(hexData, outData, sizeof(outData));
  if (dataLen < 0) {
    Serial.println("ERR_BAD_INPUT");
    return;
  }

  unsigned char hash[32];
  mbedtls_sha256(outData, dataLen, hash, 0);

  unsigned char priv_buf[32];
  size_t privLen = prefs.getBytes("privkey", priv_buf, sizeof(priv_buf));
  if (privLen == 0) {
    Serial.println("ERR_NO_KEY");
    return;
  }

  mbedtls_ecdsa_context ecdsa;
  mbedtls_ecdsa_init(&ecdsa);
  mbedtls_ecp_group_load(&ecdsa.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1);
  mbedtls_mpi_read_binary(&ecdsa.MBEDTLS_PRIVATE(d), priv_buf, privLen);

  unsigned char sig[MBEDTLS_ECDSA_MAX_LEN];
  size_t sigLen;
  int ret = mbedtls_ecdsa_write_signature(&ecdsa, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                                           sig, sizeof(sig), &sigLen,
                                           mbedtls_ctr_drbg_random, &ctr_drbg);

  mbedtls_ecdsa_free(&ecdsa);
  memset(priv_buf, 0, sizeof(priv_buf)); // clear private key from RAM

  if (ret != 0) {
    Serial.println("ERR_SIGN_FAILED");
    return;
  }

  String hexSig = "";
  for (size_t i = 0; i < sigLen; i++) {
    if (sig[i] < 0x10) hexSig += "0";
    hexSig += String(sig[i], HEX);
  }
  Serial.println(hexSig);
}

void zeroizeKeys() {
  prefs.remove("privkey");
  prefs.remove("pubkey");
  keyExists = false;
}
