#include <Arduino.h>
#include <Time.h>
#include "DateTime.h"
#include "Rs_RTC_Teensy41.h"

#ifndef _SYSTIME_H_
#define _SYSTIME_H_

class SysTime
{
public:
    SysTime();
    ~SysTime();
    
    DateTime getTime();
    void setTime(DateTime);
    void begin(DateTime);
};
#endif 