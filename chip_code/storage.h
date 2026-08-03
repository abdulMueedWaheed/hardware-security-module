#pragma once

#include "globals.h"

void zeroizeKeys();

unsigned long getCurrentUnixTime();

void logEvent(const char *eventType);

void printLog();

bool keystore_init();

bool keystore_save_keypair(const unsigned char *privKey, size_t privLen,
                            const unsigned char *pubKey, size_t pubLen);

size_t keystore_load_private_key(unsigned char *privKeyOut, size_t maxLen);
size_t keystore_load_public_key(unsigned char *pubKeyOut, size_t maxLen);

bool keystore_key_exists();



