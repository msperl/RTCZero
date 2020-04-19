/*
  RTC library for Arduino Zero.
  Copyright (c) 2015 Arduino LLC. All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

//Edited by A. McMahon 2/20/2020, Added Mode 0 and Mode 1

#ifndef RTC_ZERO_H
#define RTC_ZERO_H

#include "Arduino.h"

typedef void(*voidFuncPtr)(void);

class RTCZero {
public:

  enum Alarm_Match: uint8_t // Should we have this enum or just use the identifiers from /component/rtc.h ?
  {
    MATCH_OFF          = RTC_MODE2_MASK_SEL_OFF_Val,          // Never
    MATCH_SS           = RTC_MODE2_MASK_SEL_SS_Val,           // Every Minute
    MATCH_MMSS         = RTC_MODE2_MASK_SEL_MMSS_Val,         // Every Hour
    MATCH_HHMMSS       = RTC_MODE2_MASK_SEL_HHMMSS_Val,       // Every Day
    MATCH_DHHMMSS      = RTC_MODE2_MASK_SEL_DDHHMMSS_Val,     // Every Month
    MATCH_MMDDHHMMSS   = RTC_MODE2_MASK_SEL_MMDDHHMMSS_Val,   // Every Year
    MATCH_YYMMDDHHMMSS = RTC_MODE2_MASK_SEL_YYMMDDHHMMSS_Val  // Once, on a specific date and a specific time
  };

  enum Int_Source: uint8_t  // Helper to make interrupts simpler 
  {
    INT_COMP0 = 0,
    INT_COMP1 = 1,
    INT_ALARM0 = 2,
    INT_OVERFLOW = 3,
    INT_UNKNOWN = 4
  };

  RTCZero();
  void begin(bool resetTime = false, uint8_t rtc_mode = 2, bool clearOnMatch = false, uint32_t prescale = 32768);

  void enableAlarm(Alarm_Match match);
  void disableAlarm();

  void enableCounter(unsigned long comp0);
  void enableCounter(unsigned int comp0, unsigned int comp1);
  void disableCounter();

  void enableOverflow();
  void disableOverflow();

  void attachInterrupt(voidFuncPtr callback);
  void detachInterrupt();
  
  void standbyMode();
  
  /* Get Functions */

  uint8_t getIntSource();

  uint32_t getCount();

  uint32_t get_Prescaler() { return effective_prescaler; }

  uint32_t getCompare();
  uint16_t getCompare(uint8_t c);

  uint8_t getSeconds();
  uint8_t getMinutes();
  uint8_t getHours();
  
  uint8_t getDay();
  uint8_t getMonth();
  uint8_t getYear();
  
  uint8_t getAlarmSeconds();
  uint8_t getAlarmMinutes();
  uint8_t getAlarmHours();

  uint8_t getAlarmDay();
  uint8_t getAlarmMonth();
  uint8_t getAlarmYear();

  /* Set Functions */

  void setCount(uint32_t count);
  void setCount(uint16_t count);

  void setPeriod(uint16_t period);

  void setSeconds(uint8_t seconds);
  void setMinutes(uint8_t minutes);
  void setHours(uint8_t hours);
  void setTime(uint8_t hours, uint8_t minutes, uint8_t seconds);

  void setDay(uint8_t day);
  void setMonth(uint8_t month);
  void setYear(uint8_t year);
  void setDate(uint8_t day, uint8_t month, uint8_t year);

  void setAlarmSeconds(uint8_t seconds);
  void setAlarmMinutes(uint8_t minutes);
  void setAlarmHours(uint8_t hours);
  void setAlarmTime(uint8_t hours, uint8_t minutes, uint8_t seconds);

  void setAlarmDay(uint8_t day);
  void setAlarmMonth(uint8_t month);
  void setAlarmYear(uint8_t year);
  void setAlarmDate(uint8_t day, uint8_t month, uint8_t year);

  /* Epoch Functions */

  uint32_t getEpoch();
  uint32_t getY2kEpoch();
  void setEpoch(uint32_t ts);
  void setY2kEpoch(uint32_t ts);
  void setAlarmEpoch(uint32_t ts);

  bool isConfigured() {
    return _configured;
  }

private:
  bool _configured;
  uint32_t effective_prescaler;

  void config32kOSC(void);
  void computeDivider(uint32_t prescale, uint32_t *rtc_prescale, uint32_t *gclk_prescale);
  void configureClock(uint32_t gclk_div);
  void RTCreadRequest();
  bool RTCisSyncing(void);
  void RTCdisable();
  void RTCenable();
  void RTCreset();
  void RTCresetRemove();
};

#endif // RTC_ZERO_H
