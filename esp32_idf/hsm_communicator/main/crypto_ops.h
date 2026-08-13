#ifndef CRYPTO_OPPS_H
#define CRYPTO_OPPS_H

#include "globals.h"
#include <string>

bool selfTestSHA256();
bool selfTestECDSA();

void runSelfTests();

void genKey();
void getPubKey();

int hexStringToBytes(
    const std::string& hex,
    unsigned char *out,
    size_t maxLen
);

void signData(const std::string& hexData);

#endif
