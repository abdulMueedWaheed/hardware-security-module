#ifndef TAMPER_H
#define TAMPER_H

#include "globals.h"

constexpr int CALIBRATION_SAMPLES = 100;

bool initTamper();

int readLDRAveraged();

static bool calibrateLDR();

void checkTamper();

bool requireAuthorization();

#endif