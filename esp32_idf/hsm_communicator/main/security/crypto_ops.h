#ifndef CRYPTO_OPPS_H
#define CRYPTO_OPPS_H

#include "psa/crypto_types.h"
#include <string>

bool runSelfTests();

bool generateKey();
bool hasKey();
bool zeroize();
bool getPubKey(std::string& publicKeyHex);

bool signData(const std::string& hexData, std::string& signatureHex);

bool initCrypto();

#endif
