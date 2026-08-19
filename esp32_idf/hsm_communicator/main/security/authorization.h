#ifndef AUTHORIZATION_H
#define AUTHORIZATION_H

#include <cstdint>

extern const uint32_t AUTH_TIMEOUT_MS;

bool requireAuthorization();
bool isAuthorized();

#endif