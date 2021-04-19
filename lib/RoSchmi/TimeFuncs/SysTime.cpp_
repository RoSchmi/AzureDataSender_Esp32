#include <Arduino.h>
#include "SysTime.h"

static bool rtcIsStarted = false;

static Rs_RTC_Teensy41 rtc;

SysTime::SysTime()
{ 
    if (!rtcIsStarted)
    {
        rtcIsStarted = true;
        rtc.begin();
    }
}

SysTime::~SysTime(){};

void SysTime::setTime(DateTime dateTime)
{
    begin(dateTime);
}

void SysTime::begin(DateTime dateTime)
{   
    if (!rtcIsStarted)
    {
        rtcIsStarted = true;
        rtc.begin();
    }
    rtc.adjust(dateTime);   
}
DateTime SysTime::getTime()
{    
    if (!rtcIsStarted)
    {
        rtcIsStarted = true;
        rtc.begin();
    }
    return rtc.now();
}
