#pragma once

#include "globals.h"

bool selfTestSHA256();
bool selfTestECDSA();

void runSelfTests();

void genKey();

void getPubKey();

int hexStringToBytes(String hex,
  unsigned char *out,
  size_t maxLen);

void signData(String hexData);