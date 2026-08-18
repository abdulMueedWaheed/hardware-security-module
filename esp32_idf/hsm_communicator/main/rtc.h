#ifndef RTC_H
#define RTC_H

#include <cstdint>

struct RtcDateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

bool initRTC();

bool rtcReadTime(RtcDateTime& time);
bool rtcSetTime(const RtcDateTime& time);

uint32_t rtcGetUnixTime();
bool rtcSetUnixTime(uint32_t unixTime);

bool rtcIsRunning();
bool rtcDumpRegisters();
bool rtcTestWrite();
bool rtcTestRead();
uint32_t convertTimeToUnix(const RtcDateTime& time);


bool rtcRawRead();
bool rtcRawWrite();
void probeRTC();
bool rtcRegisterWriteTest();
bool rtcRepeatedStartRead();
bool rtcExplicitRepeatedStartRead();
bool rtcRawWriteDummyTest();
// bool rtcRawReadOneByte();

#endif