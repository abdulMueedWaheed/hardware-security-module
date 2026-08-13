

#ifndef STORAGE_H
#define STORAGE_H

#include "globals.h"

void zeroizeKeys();

uint32_t getCurrentUnixTime();

void logEvent(const char *eventType);
void printLog();

bool keystore_init();

#endif