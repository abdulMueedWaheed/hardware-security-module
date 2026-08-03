
#include "storage.h"
#include <cstdio>


// =====================================================================
// Timing and Logging stuff
// =====================================================================

unsigned long getCurrentUnixTime() {
  if (!timeSynced) 
    return 0; // no sync yet, caller should treat 0 as "unknown"
  
  unsigned long elapsedSec = (millis() - millisAtSync) / 1000;
  return hostTimeAtSync + elapsedSec;
}


void logEvent(const char *eventType) {
  logCounter++;

  char entry[LOG_ENTRY_MAXLEN];
  unsigned long ts = getCurrentUnixTime();
  if (ts > 0) {
    snprintf(entry, sizeof(entry), "#%lu [%lu] %s", logCounter, ts, eventType);
  } 
  
  else {
    snprintf(entry, sizeof(entry), "#%lu [unsynced] %s", logCounter, eventType);
  }

  int slot = logCounter % LOG_MAX_ENTRIES;
  char key[12];
  snprintf(key, sizeof(key), "log_%d", slot);
  prefs.putString(key, entry);
  prefs.putULong("log_count", logCounter);
}


void printLog() {
  unsigned long total = prefs.getULong("log_count", 0);
  
  if (total == 0) {
    Serial.println("LOG_EMPTY");
    return;
  }

  unsigned long start = (total > LOG_MAX_ENTRIES) ? (total - LOG_MAX_ENTRIES + 1) : 1;

  Serial.println("LOG_BEGIN");
  
  for (unsigned long i = start; i <= total; i++) {
    int slot = i % LOG_MAX_ENTRIES;
    char key[12];
    snprintf(key, sizeof(key), "log_%d", slot);
    String entry = prefs.getString(key, "");
    if (entry.length() > 0) {
      Serial.println(entry);
    }
  }
  
  Serial.println("LOG_END");
}


void zeroizeKeys() {
  prefs.remove("privkey");
  prefs.remove("pubkey");
  keyExists = false;

  logEvent("KEY_ZEROIZED");
}

// =====================================================================
// Key storage abstraction (NVS-backed implementation)
// =====================================================================

bool keystore_save_keypair(const unsigned char *privKey, size_t privLen,
                            const unsigned char *pubKey, size_t pubLen) {
  size_t w1 = prefs.putBytes("privkey", privKey, privLen);
  size_t w2 = prefs.putBytes("pubkey", pubKey, pubLen);
  keyExists = (w1 == privLen && w2 == pubLen);
  return keyExists;
}

size_t keystore_load_private_key(unsigned char *privKeyOut, size_t maxLen) {
  return prefs.getBytes("privkey", privKeyOut, maxLen);
}

size_t keystore_load_public_key(unsigned char *pubKeyOut, size_t maxLen) {
  return prefs.getBytes("pubkey", pubKeyOut, maxLen);
}

bool keystore_key_exists() {
  return keyExists;
}








