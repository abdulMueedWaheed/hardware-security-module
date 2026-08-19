#ifndef CRYPTO_OPPS_H
#define CRYPTO_OPPS_H

#include "psa/crypto_types.h"
#include <string>

extern bool keyExists;
extern psa_key_id_t hsm_key_id;

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
