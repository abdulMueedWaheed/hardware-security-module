#ifndef LED_H
#define LED_H

#include <stdint.h>

extern uint32_t lastBlink;
extern bool blinkOn;

void updateLEDs();

#endif