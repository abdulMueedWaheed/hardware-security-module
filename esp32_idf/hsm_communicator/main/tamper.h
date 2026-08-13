#ifndef TAMPER_H
#define TAMPER_H

#include "globals.h"

bool initTamper();

int readLDRAveraged();

void checkTamper();

bool requireAuthorization();

#endif