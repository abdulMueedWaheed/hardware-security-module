#ifndef STORAGE_H
#define STORAGE_H

#include "nvs.h"
#include <cstdint>

constexpr size_t LOG_MAX_ENTRIES = 20;
constexpr size_t LOG_ENTRY_MAXLEN = 40;

bool initStorage();

void logEvent(const char* eventType);
void printLog();

#endif