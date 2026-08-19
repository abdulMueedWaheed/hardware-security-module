#ifndef RTC_H
#define RTC_H

#include "driver/i2c_types.h"
#include <cstdint>

struct RtcDateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

bool initRTC(i2c_master_bus_handle_t busHandle);

bool rtcReadTime(RtcDateTime& time);
bool rtcSetTime(const RtcDateTime& time);

uint32_t rtcGetUnixTime();
bool rtcSetUnixTime(uint32_t unixTime);

bool rtcIsRunning();
uint32_t convertTimeToUnix(const RtcDateTime& time);

// testing functions
bool rtcDumpRegisters();
bool rtcTestWrite();
bool rtcTestRead(i2c_master_bus_handle_t busHandle);
bool rtcRawRead();
bool rtcRawWrite();
void probeRTC(i2c_master_bus_handle_t hwI2cBus);
bool rtcRegisterWriteTest();
bool rtcRepeatedStartRead();
bool rtcExplicitRepeatedStartRead();
bool rtcRawWriteDummyTest(i2c_master_bus_handle_t hwI2cBus);
// bool rtcRawReadOneByte();

#endif