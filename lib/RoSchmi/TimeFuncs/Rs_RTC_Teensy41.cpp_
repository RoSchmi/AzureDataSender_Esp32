// The code was adapted from this source:
//
// -https://github.com/manitou48/teensy4/blob/master/rtc.ino

// 
/*
    The MIT License (MIT)
    Author: RoSchmi
    Copyright (C) 2021 RoSchmi
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#ifdef __IMXRT1062__
#include "Rs_RTC_Teensy41.h"

#define SNVS_DEFAULT_PGD_VALUE (0x41736166U)
#define SNVS_LPSR_PGD_MASK                       (0x8U)
#define SNVS_LPSRTCMR      (IMXRT_SNVS.offset050)
#define SNVS_LPSRTCLR      (IMXRT_SNVS.offset054)


bool Rs_RTC_Teensy41::begin()
{

  CCM_CCGR2 |= CCM_CCGR2_IOMUXC_SNVS(CCM_CCGR_ON);
  SNVS_LPGPR = SNVS_DEFAULT_PGD_VALUE;
  SNVS_LPSR = SNVS_LPSR_PGD_MASK;
  // ? calibration
  // ? tamper pins

  SNVS_LPCR |= 1;             // start RTC
  while (!(SNVS_LPCR & 1));
  return true;
}

void Rs_RTC_Teensy41::adjust(const DateTime &dt)
{
    uint32_t secs = dt.secondstime();
    SNVS_LPCR &= ~1;   // stop RTC
  while (SNVS_LPCR & 1);
  SNVS_LPSRTCMR = (uint32_t)(secs >> 17U);
  SNVS_LPSRTCLR = (uint32_t)(secs << 15U);
  SNVS_LPCR |= 1;             // start RTC
  while (!(SNVS_LPCR & 1));
}


DateTime Rs_RTC_Teensy41::now()
{    
  uint32_t seconds = 0;
  uint32_t tmp = 0;

  /* Do consecutive reads until value is correct */
  do
  {
    seconds = tmp;
    tmp = (SNVS_LPSRTCMR << 17U) | (SNVS_LPSRTCLR >> 15U);
  } while (tmp != seconds);
  return seconds;
}
#endif