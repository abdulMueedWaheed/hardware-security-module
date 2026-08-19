#ifndef TAMPER_H
#define TAMPER_H

#include <cstdint>
constexpr int CALIBRATION_SAMPLES = 100;
extern int ldrBaseline;
extern int TAMPER_THRESHOLD;
extern const int LDR_SAMPLES;
extern uint32_t lastTamperCheck;
extern const uint32_t TAMPER_CHECK_INTERVAL_MS;
extern const uint32_t AUTH_TIMEOUT_MS;



bool initTamper();
int readLDRAveraged();
void checkTamper();

#endif