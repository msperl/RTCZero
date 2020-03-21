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

#include <time.h>

#include "RTCZero.h"

#define EPOCH_TIME_OFF      946684800  // This is 1st January 2000, 00:00:00 in epoch time
#define EPOCH_TIME_YEAR_OFF 100        // years since 1900

// Default date & time after reset
#define DEFAULT_YEAR    2000    // 2000..2063
#define DEFAULT_MONTH   1       // 1..12
#define DEFAULT_DAY     1       // 1..31
#define DEFAULT_HOUR    0       // 1..23
#define DEFAULT_MINUTE  0       // 0..59
#define DEFAULT_SECOND  0       // 0..59

voidFuncPtr RTC_callBack = NULL;
uint8_t rtc_mode = 2;

RTCZero::RTCZero()
{
  _configured = false;
}

void RTCZero::begin(bool resetTime, uint8_t mode, bool clearOnMatch, Prescaler prescale, uint8_t gclk_prescale)
{
  uint16_t tmp_reg = 0;
  rtc_mode = mode;
  bool validTime = false;
  RTC_MODE0_COUNT_Type mode0_oldCount;
  RTC_MODE1_COUNT_Type mode1_oldCount;
  RTC_MODE2_CLOCK_Type mode2_oldTime;
  
  PM->APBAMASK.reg |= PM_APBAMASK_RTC; // turn on digital interface clock
  config32kOSC();


   if (rtc_mode==0) {
    // If the RTC is in 32-bit counter mode and the reset was
    // not due to POR or BOD, preserve the clock time
    // POR causes a reset anyway, BOD behaviour is?
    if ((!resetTime) && (PM->RCAUSE.reg & (PM_RCAUSE_SYST | PM_RCAUSE_WDT | PM_RCAUSE_EXT))) {
      if (RTC->MODE0.CTRL.reg & RTC_MODE0_CTRL_MODE_COUNT32) {
        validTime = true;
        mode0_oldCount.reg = RTC->MODE0.COUNT.reg;
      }
    }
  } 
  else if (rtc_mode==1) {
    // If the RTC is in 16-bit counter mode and the reset was
    // not due to POR or BOD, preserve the clock time
    // POR causes a reset anyway, BOD behaviour is?
    if ((!resetTime) && (PM->RCAUSE.reg & (PM_RCAUSE_SYST | PM_RCAUSE_WDT | PM_RCAUSE_EXT))) {
      if (RTC->MODE1.CTRL.reg & RTC_MODE1_CTRL_MODE_COUNT16) {
        validTime = true;
        mode1_oldCount.reg = RTC->MODE1.COUNT.reg;
      }
    }
  } 
  else {
    // If the RTC is in clock mode and the reset was
    // not due to POR or BOD, preserve the clock time
    // POR causes a reset anyway, BOD behaviour is?
    if ((!resetTime) && (PM->RCAUSE.reg & (PM_RCAUSE_SYST | PM_RCAUSE_WDT | PM_RCAUSE_EXT))) {
      if (RTC->MODE2.CTRL.reg & RTC_MODE2_CTRL_MODE_CLOCK) {
        validTime = true;
        mode2_oldTime.reg = RTC->MODE2.CLOCK.reg;
      }
    }
    // as we are in RTC mode: use gclk prescale divider of 32 regardless of what is given as method argument
    gclk_prescale = 4;
  }

  configureClock(gclk_prescale);

  RTCdisable();

  RTCreset();

  if (rtc_mode==0) {
    if (prescale==None) prescale = MODE0_DIV1024;   // set prescaler default to 1024
    tmp_reg |= RTC_MODE0_CTRL_MODE_COUNT32;         // set 32-bit counter operating mode
    tmp_reg |= prescale;                            // set prescaler
    
    if (clearOnMatch==true) tmp_reg |= RTC_MODE0_CTRL_MATCHCLR;     // enable clear on match
    else tmp_reg &= ~RTC_MODE0_CTRL_MATCHCLR;                       // disable clear on match

    RTC->MODE0.READREQ.reg &= ~RTC_READREQ_RCONT;   // disable continuously mode

    RTC->MODE0.CTRL.reg = tmp_reg;
  }
  else if (rtc_mode==1) {
    if (prescale==None) prescale = MODE1_DIV1024;   // set prescaler default to 1024
    tmp_reg |= RTC_MODE1_CTRL_MODE_COUNT16;         // set 16-bit counter operating mode
    tmp_reg |= prescale;                            // set prescaler

    RTC->MODE1.READREQ.reg &= ~RTC_READREQ_RCONT;   // disable continuously mode

    RTC->MODE1.CTRL.reg = tmp_reg;
  }
  else {
    if (prescale==None) prescale = MODE2_DIV1024;   // set prescaler default to 1024
    tmp_reg |= RTC_MODE2_CTRL_MODE_CLOCK;           // set clock operating mode
    tmp_reg |= prescale;                            // set prescaler
    tmp_reg &= ~RTC_MODE2_CTRL_MATCHCLR;            // disable clear on match
    
    //According to the datasheet RTC_MODE2_CTRL_CLKREP = 0 for 24h
    tmp_reg &= ~RTC_MODE2_CTRL_CLKREP; // 24h time representation

    RTC->MODE2.READREQ.reg &= ~RTC_READREQ_RCONT; // disable continuously mode

    RTC->MODE2.CTRL.reg = tmp_reg;
  }

  while (RTCisSyncing());

  NVIC_EnableIRQ(RTC_IRQn); // enable RTC interrupt 
  NVIC_SetPriority(RTC_IRQn, 0x00);

  //Mode 0 and Mode 1 compare interrupts don't have a "disabled" mask option, so don't enable interrupts until alarm is enabled for those modes
  if (rtc_mode==2){
    RTC->MODE2.INTENSET.reg |= RTC_MODE2_INTENSET_ALARM0; // enable alarm interrupt
    RTC->MODE2.Mode2Alarm[0].MASK.bit.SEL = MATCH_OFF; // default alarm match is off (disabled)
  }

  while (RTCisSyncing());

  RTCenable();
  RTCresetRemove();

  // If desired and valid, restore the time or count value, else use first valid time value/start count from 0
  if (rtc_mode==0) {
    if ((!resetTime) && (validTime) && (mode0_oldCount.reg != 0L)) {
      RTC->MODE0.COUNT.reg = mode0_oldCount.reg;
    }
    else {
      RTC->MODE0.COUNT.reg = RTC_MODE0_COUNT_RESETVALUE;
    }
  }
  else if (rtc_mode==1) {
    if ((!resetTime) && (validTime) && (mode1_oldCount.reg != 0L)) {
      RTC->MODE1.COUNT.reg = mode1_oldCount.reg;
    }
    else {
      RTC->MODE1.COUNT.reg = RTC_MODE1_COUNT_RESETVALUE;
    }
  }
  else {
    if ((!resetTime) && (validTime) && (mode2_oldTime.reg != 0L)) {
      RTC->MODE2.CLOCK.reg = mode2_oldTime.reg;
    }
    else {
      RTC->MODE2.CLOCK.reg = RTC_MODE2_CLOCK_YEAR(DEFAULT_YEAR - 2000) | RTC_MODE2_CLOCK_MONTH(DEFAULT_MONTH) 
          | RTC_MODE2_CLOCK_DAY(DEFAULT_DAY) | RTC_MODE2_CLOCK_HOUR(DEFAULT_HOUR) 
          | RTC_MODE2_CLOCK_MINUTE(DEFAULT_MINUTE) | RTC_MODE2_CLOCK_SECOND(DEFAULT_SECOND);
    }
  }

  while (RTCisSyncing());

  _configured = true;
}

void RTC_Handler(void)
{
  if (RTC_callBack != NULL) {
    RTC_callBack();
  }

  // must clear flag at end
  if (rtc_mode==0) RTC->MODE0.INTFLAG.reg |= (RTC_MODE0_INTFLAG_CMP0 | RTC_MODE0_INTFLAG_OVF);
  else if (rtc_mode==1) RTC->MODE1.INTFLAG.reg |= (RTC_MODE1_INTFLAG_CMP0 | RTC_MODE1_INTFLAG_CMP1 | RTC_MODE0_INTFLAG_OVF);
  else RTC->MODE2.INTFLAG.reg |= (RTC_MODE2_INTFLAG_ALARM0 | RTC_MODE0_INTFLAG_OVF);
}

void RTCZero::enableAlarm(Alarm_Match match)
{
  if (_configured && rtc_mode==2) {
    RTC->MODE2.Mode2Alarm[0].MASK.bit.SEL = match;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::disableAlarm()
{
  if (_configured && rtc_mode==2) {
    RTC->MODE2.Mode2Alarm[0].MASK.bit.SEL = 0x00;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::enableCounter(unsigned int comp0, unsigned int comp1)
{
  if (_configured && rtc_mode==1) {
    RTC->MODE1.COMP[0].reg = comp0;
    RTC->MODE1.COMP[1].reg = comp1;
    RTC->MODE1.INTENSET.reg |= (RTC_MODE1_INTENSET_CMP0 | RTC_MODE1_INTENSET_CMP1);
    while (RTCisSyncing());
  }
}

void RTCZero::enableCounter(unsigned long comp0)
{
  if (_configured) {
    if (rtc_mode==0) {
      RTC->MODE0.COMP[0].reg = comp0;
      RTC->MODE0.INTENSET.reg |= RTC_MODE0_INTENSET_CMP0;
    }
    else if (rtc_mode==1) {
      RTC->MODE1.COMP[0].reg = (unsigned int) comp0;
      RTC->MODE1.INTENSET.reg |= RTC_MODE1_INTENSET_CMP0;
    }
    while (RTCisSyncing());
  }
}

void RTCZero::disableCounter()
{
  if (_configured) {
    if (rtc_mode==0) RTC->MODE0.INTENCLR.reg |= RTC_MODE0_INTENCLR_CMP0;
    else if (rtc_mode==1) RTC->MODE1.INTENCLR.reg |= (RTC_MODE1_INTENCLR_CMP0 | RTC_MODE1_INTENCLR_CMP1);
    while (RTCisSyncing());
  }
}

void RTCZero::enableOverflow()
{
  if (_configured) {
    if (rtc_mode==0) RTC->MODE0.INTENSET.reg |= RTC_MODE0_INTENSET_OVF;
    else if (rtc_mode==1) RTC->MODE1.INTENSET.reg |= RTC_MODE1_INTENSET_OVF;
    else RTC->MODE2.INTENSET.reg |= RTC_MODE2_INTENSET_OVF;   // mode 2
    while (RTCisSyncing());
  }
}

void RTCZero::disableOverflow()
{
  if (_configured) {
    if (rtc_mode==0) RTC->MODE0.INTENCLR.reg |= RTC_MODE0_INTENCLR_OVF;
    else if (rtc_mode==1) RTC->MODE1.INTENCLR.reg |= RTC_MODE1_INTENCLR_OVF;
    else RTC->MODE2.INTENCLR.reg |= RTC_MODE2_INTENCLR_OVF;   // mode 2
    while (RTCisSyncing());
  }
}

void RTCZero::attachInterrupt(voidFuncPtr callback)
{
  RTC_callBack = callback;
}

void RTCZero::detachInterrupt()
{
  RTC_callBack = NULL;
}

void RTCZero::standbyMode()
{
  // Entering standby mode when connected
  // via the native USB port causes issues.
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
  __DSB();
  __WFI();
}

/*
 * Get Functions
 */

uint8_t RTCZero::getIntSource()
{
  RTCreadRequest();
  if (rtc_mode==0) {
    if (RTC->MODE0.INTFLAG.bit.CMP0) return INT_COMP0;
    else if (RTC->MODE0.INTFLAG.bit.OVF) return INT_OVERFLOW;
  }
  else if (rtc_mode==1) {
    if (RTC->MODE1.INTFLAG.bit.CMP0) return INT_COMP0;
    else if (RTC->MODE1.INTFLAG.bit.CMP1) return INT_COMP1;
    else if (RTC->MODE1.INTFLAG.bit.OVF) return INT_OVERFLOW;
  }
  else { // mode 2
    if (RTC->MODE2.INTFLAG.bit.ALARM0) return INT_ALARM0;
    else if (RTC->MODE2.INTFLAG.bit.OVF) return INT_OVERFLOW;
  }
  return INT_UNKNOWN;
}

uint32_t RTCZero::getCount()
{
  RTCreadRequest();
  if (rtc_mode==0) return RTC->MODE0.COUNT.reg;
  else if (rtc_mode==1) return RTC->MODE1.COUNT.reg;
  return 0;
}

uint32_t RTCZero::getCompare()
{
  RTCreadRequest();
  if (rtc_mode==0) return RTC->MODE0.COMP[0].reg;
  else return 0;
}

uint16_t RTCZero::getCompare(uint8_t c)
{
  RTCreadRequest();
  if (rtc_mode==1) {
    if (c==0) return RTC->MODE1.COMP[0].reg;
    if (c==1) return RTC->MODE1.COMP[1].reg;
  }
  return 0;
}


uint8_t RTCZero::getSeconds()
{
  RTCreadRequest();
  return RTC->MODE2.CLOCK.bit.SECOND;
}

uint8_t RTCZero::getMinutes()
{
  RTCreadRequest();
  return RTC->MODE2.CLOCK.bit.MINUTE;
}

uint8_t RTCZero::getHours()
{
  RTCreadRequest();
  return RTC->MODE2.CLOCK.bit.HOUR;
}

uint8_t RTCZero::getDay()
{
  RTCreadRequest();
  return RTC->MODE2.CLOCK.bit.DAY;
}

uint8_t RTCZero::getMonth()
{
  RTCreadRequest();
  return RTC->MODE2.CLOCK.bit.MONTH;
}

uint8_t RTCZero::getYear()
{
  RTCreadRequest();
  return RTC->MODE2.CLOCK.bit.YEAR;
}

uint8_t RTCZero::getAlarmSeconds()
{
  return RTC->MODE2.Mode2Alarm[0].ALARM.bit.SECOND;
}

uint8_t RTCZero::getAlarmMinutes()
{
  return RTC->MODE2.Mode2Alarm[0].ALARM.bit.MINUTE;
}

uint8_t RTCZero::getAlarmHours()
{
  return RTC->MODE2.Mode2Alarm[0].ALARM.bit.HOUR;
}

uint8_t RTCZero::getAlarmDay()
{
  return RTC->MODE2.Mode2Alarm[0].ALARM.bit.DAY;
}

uint8_t RTCZero::getAlarmMonth()
{
  return RTC->MODE2.Mode2Alarm[0].ALARM.bit.MONTH;
}

uint8_t RTCZero::getAlarmYear()
{
  return RTC->MODE2.Mode2Alarm[0].ALARM.bit.YEAR;
}

/*
 * Set Functions
 */

void RTCZero::setCount(uint32_t count)
{
  if (_configured && rtc_mode==0) {
    RTC->MODE0.COUNT.reg = count;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setCount(uint16_t count)
{
  if (_configured && rtc_mode==1) {
    RTC->MODE1.COUNT.reg = count;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setPeriod(uint16_t period)
{
  if (_configured && rtc_mode==1) {
    RTC->MODE1.PER.reg = period;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setSeconds(uint8_t seconds)
{
  if (_configured) {
    RTC->MODE2.CLOCK.bit.SECOND = seconds;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setMinutes(uint8_t minutes)
{
  if (_configured) {
    RTC->MODE2.CLOCK.bit.MINUTE = minutes;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setHours(uint8_t hours)
{
  if (_configured) {
    RTC->MODE2.CLOCK.bit.HOUR = hours;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setTime(uint8_t hours, uint8_t minutes, uint8_t seconds)
{
  if (_configured) {
    setSeconds(seconds);
    setMinutes(minutes);
    setHours(hours);
  }
}

void RTCZero::setDay(uint8_t day)
{
  if (_configured) {
    RTC->MODE2.CLOCK.bit.DAY = day;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setMonth(uint8_t month)
{
  if (_configured) {
    RTC->MODE2.CLOCK.bit.MONTH = month;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setYear(uint8_t year)
{
  if (_configured) {
    RTC->MODE2.CLOCK.bit.YEAR = year;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setDate(uint8_t day, uint8_t month, uint8_t year)
{
  if (_configured) {
    setDay(day);
    setMonth(month);
    setYear(year);
  }
}

void RTCZero::setAlarmSeconds(uint8_t seconds)
{
  if (_configured) {
    RTC->MODE2.Mode2Alarm[0].ALARM.bit.SECOND = seconds;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setAlarmMinutes(uint8_t minutes)
{
  if (_configured) {
    RTC->MODE2.Mode2Alarm[0].ALARM.bit.MINUTE = minutes;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setAlarmHours(uint8_t hours)
{
  if (_configured) {
    RTC->MODE2.Mode2Alarm[0].ALARM.bit.HOUR = hours;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setAlarmTime(uint8_t hours, uint8_t minutes, uint8_t seconds)
{
  if (_configured) {
    setAlarmSeconds(seconds);
    setAlarmMinutes(minutes);
    setAlarmHours(hours);
  }
}

void RTCZero::setAlarmDay(uint8_t day)
{
  if (_configured) {
    RTC->MODE2.Mode2Alarm[0].ALARM.bit.DAY = day;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setAlarmMonth(uint8_t month)
{
  if (_configured) {
    RTC->MODE2.Mode2Alarm[0].ALARM.bit.MONTH = month;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setAlarmYear(uint8_t year)
{
  if (_configured) {
    RTC->MODE2.Mode2Alarm[0].ALARM.bit.YEAR = year;
    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setAlarmDate(uint8_t day, uint8_t month, uint8_t year)
{
  if (_configured) {
    setAlarmDay(day);
    setAlarmMonth(month);
    setAlarmYear(year);
  }
}

uint32_t RTCZero::getEpoch()
{
  RTCreadRequest();
  RTC_MODE2_CLOCK_Type clockTime;
  clockTime.reg = RTC->MODE2.CLOCK.reg;

  struct tm tm;

  tm.tm_isdst = -1;
  tm.tm_yday = 0;
  tm.tm_wday = 0;
  tm.tm_year = clockTime.bit.YEAR + EPOCH_TIME_YEAR_OFF;
  tm.tm_mon = clockTime.bit.MONTH - 1;
  tm.tm_mday = clockTime.bit.DAY;
  tm.tm_hour = clockTime.bit.HOUR;
  tm.tm_min = clockTime.bit.MINUTE;
  tm.tm_sec = clockTime.bit.SECOND;

  return mktime(&tm);
}

uint32_t RTCZero::getY2kEpoch()
{
  return (getEpoch() - EPOCH_TIME_OFF);
}

void RTCZero::setAlarmEpoch(uint32_t ts)
{
  if (_configured) {
    if (ts < EPOCH_TIME_OFF) {
      ts = EPOCH_TIME_OFF;
    }

    time_t t = ts;
    struct tm* tmp = gmtime(&t);

    setAlarmDate(tmp->tm_mday, tmp->tm_mon + 1, tmp->tm_year - EPOCH_TIME_YEAR_OFF);
    setAlarmTime(tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
  }
}

void RTCZero::setEpoch(uint32_t ts)
{
  if (_configured) {
    if (ts < EPOCH_TIME_OFF) {
      ts = EPOCH_TIME_OFF;
    }

    time_t t = ts;
    struct tm* tmp = gmtime(&t);

    RTC_MODE2_CLOCK_Type clockTime;

    clockTime.bit.YEAR = tmp->tm_year - EPOCH_TIME_YEAR_OFF;
    clockTime.bit.MONTH = tmp->tm_mon + 1;
    clockTime.bit.DAY = tmp->tm_mday;
    clockTime.bit.HOUR = tmp->tm_hour;
    clockTime.bit.MINUTE = tmp->tm_min;
    clockTime.bit.SECOND = tmp->tm_sec;

    RTC->MODE2.CLOCK.reg = clockTime.reg;

    while (RTCisSyncing())
      ;
  }
}

void RTCZero::setY2kEpoch(uint32_t ts)
{
  if (_configured) {
    setEpoch(ts + EPOCH_TIME_OFF);
  }
}

/* Attach peripheral clock to 32k oscillator */
void RTCZero::configureClock(uint8_t gclk_div) {
  GCLK->GENDIV.reg = GCLK_GENDIV_ID(2)|GCLK_GENDIV_DIV(gclk_div);
  while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY)
    ;
#ifdef CRYSTALLESS
  GCLK->GENCTRL.reg = (GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_OSCULP32K | GCLK_GENCTRL_ID(2) | GCLK_GENCTRL_DIVSEL );
#else
  GCLK->GENCTRL.reg = (GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_XOSC32K | GCLK_GENCTRL_ID(2) | GCLK_GENCTRL_DIVSEL );
#endif
  while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY)
    ;
  GCLK->CLKCTRL.reg = (uint32_t)((GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK2 | (RTC_GCLK_ID << GCLK_CLKCTRL_ID_Pos)));
  while (GCLK->STATUS.bit.SYNCBUSY)
    ;
}

/*
 * Private Utility Functions
 */

/* Configure the 32768Hz Oscillator */
void RTCZero::config32kOSC() 
{
#ifndef CRYSTALLESS
  SYSCTRL->XOSC32K.reg = SYSCTRL_XOSC32K_ONDEMAND |
                         SYSCTRL_XOSC32K_RUNSTDBY |
                         SYSCTRL_XOSC32K_EN32K |
                         SYSCTRL_XOSC32K_XTALEN |
                         SYSCTRL_XOSC32K_STARTUP(6) |
                         SYSCTRL_XOSC32K_ENABLE;
#endif
}

/* Synchronise the CLOCK register for reading*/
inline void RTCZero::RTCreadRequest() {
  if (_configured) {
    if (rtc_mode==0) RTC->MODE0.READREQ.reg = RTC_READREQ_RREQ;         // set read request bit, Mode 0
    else if (rtc_mode==1) RTC->MODE1.READREQ.reg = RTC_READREQ_RREQ;    // set read request bit, Mode 1
    else RTC->MODE2.READREQ.reg = RTC_READREQ_RREQ;                     // set read request bit, Mode 2
    while (RTCisSyncing())
      ;
  }
}

/* Wait for sync in write operations */
inline bool RTCZero::RTCisSyncing()
{
  if (rtc_mode==0) return (RTC->MODE0.STATUS.bit.SYNCBUSY);             // return sync busy bit, Mode 0
  else if (rtc_mode==1) return (RTC->MODE1.STATUS.bit.SYNCBUSY);        // return sync busy bit, Mode 1
  else return (RTC->MODE2.STATUS.bit.SYNCBUSY);                         // return sync busy bit, Mode 2
}

void RTCZero::RTCdisable()
{
  if (rtc_mode==0) RTC->MODE0.CTRL.reg &= ~RTC_MODE0_CTRL_ENABLE;       // disable RTC, Mode 0
  else if (rtc_mode==1) RTC->MODE1.CTRL.reg &= ~RTC_MODE1_CTRL_ENABLE;  // disable RTC, Mode 1
  else RTC->MODE2.CTRL.reg &= ~RTC_MODE2_CTRL_ENABLE;                   // disable RTC, Mode 2
  while (RTCisSyncing())
    ;
}

void RTCZero::RTCenable()
{
  if (rtc_mode==0) RTC->MODE0.CTRL.reg |= RTC_MODE0_CTRL_ENABLE;        // enable RTC, Mode 0
  else if (rtc_mode==1) RTC->MODE1.CTRL.reg |= RTC_MODE1_CTRL_ENABLE;   // enable RTC, Mode 1
  else RTC->MODE2.CTRL.reg |= RTC_MODE2_CTRL_ENABLE;                    // enable RTC, Mode 2
  while (RTCisSyncing())
    ;
}

void RTCZero::RTCreset()
{
  if (rtc_mode==0) RTC->MODE0.CTRL.reg |= RTC_MODE0_CTRL_SWRST;         // software reset, Mode 0
  else if (rtc_mode==1) RTC->MODE1.CTRL.reg |= RTC_MODE1_CTRL_SWRST;    // software reset, Mode 1
  else RTC->MODE2.CTRL.reg |= RTC_MODE2_CTRL_SWRST;                     // software reset, Mode 2
  while (RTCisSyncing())
    ;
}

void RTCZero::RTCresetRemove()
{
  if (rtc_mode==0) RTC->MODE0.CTRL.reg &= ~RTC_MODE0_CTRL_SWRST;        // software reset remove, Mode 0
  if (rtc_mode==1) RTC->MODE1.CTRL.reg &= ~RTC_MODE1_CTRL_SWRST;        // software reset remove, Mode 1
  else RTC->MODE2.CTRL.reg &= ~RTC_MODE2_CTRL_SWRST;                    // software reset remove, Mode 2
  while (RTCisSyncing())
    ;
}
