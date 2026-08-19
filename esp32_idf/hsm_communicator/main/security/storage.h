

#ifndef STORAGE_H
#define STORAGE_H

#include "nvs.h"
#include <cstdint>

#define LOG_MAX_ENTRIES 20
#define LOG_ENTRY_MAXLEN 40

extern nvs_handle_t prefs;

bool zeroizeKeys();

void logEvent(const char *eventType);
void printLog();

bool keystore_init();

#endif