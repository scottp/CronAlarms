/*
  CronAlarms.cpp - Arduino cron alarms
  Copyright (c) 2019 Martin Laclaustra

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 3 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  This is a wrapper of ccronexpr
  Copyright 2015, alex at staticlibs.net
  Licensed under the Apache License, Version 2.0
  https://github.com/staticlibs/ccronexpr

  API and implementation are inspired in TimeAlarms
  Copyright 2008-2011 Michael Margolis, maintainer:Paul Stoffregen
  GNU Lesser General Public License, Version 2.1 or later
  https://github.com/PaulStoffregen/TimeAlarms
 */

#include "CronAlarms.h"

extern "C" {
#include "ccronexpr/ccronexpr.h"
}

//**************************************************************
//* Cron Event Class Constructor

CronEventClass::CronEventClass()
{
  memset(&expr, 0, sizeof(expr));
  onTickHandler = nullptr;  // prevent a callback until this pointer is explicitly set
  nextTrigger = 0;
  isEnabled = isOneShot = false;
}


//**************************************************************
//* Cron Event Class Methods

void CronEventClass::updateNextTrigger(bool forced)
{
  if (isEnabled) {
    time_t timenow = time(nullptr);
    if (onTickHandler && ((nextTrigger <= timenow) || forced)) {
      // update alarm if next trigger is not yet in the future
      nextTrigger = cron_next(&expr, timenow);
    }
  }
}

//**************************************************************
//* Cron Class Public Methods

CronClass::CronClass()
{
  isServicing = false;
  for(uint8_t id = 0; id < dtNBR_ALARMS; id++) {
    free(id);   // ensure all Alarms are cleared and available for allocation
  }
}

void CronClass::globalUpdateNextTrigger()
{
  for (uint8_t eachCronId = 0; eachCronId < dtNBR_ALARMS; eachCronId++) {
    if (Alarm[eachCronId].isEnabled) {
      Alarm[eachCronId].updateNextTrigger(true);
    }
  }
}

void CronClass::globalenable()
{
  globalUpdateNextTrigger();
  globalEnabled = true;
}

void CronClass::globaldisable()
{
  globalEnabled = false;
}

void CronClass::enable(CronID_t ID)
{
  if (isAllocated(ID)) {
    Alarm[ID].isEnabled = true;
    Alarm[ID].updateNextTrigger();
  }
}

void CronClass::disable(CronID_t ID)
{
  if (isAllocated(ID)) {
    Alarm[ID].isEnabled = false;
  }
}

void CronClass::free(CronID_t ID)
{
  if (isAllocated(ID)) {
    memset(&(Alarm[ID].expr), 0, sizeof(Alarm[ID].expr));
    Alarm[ID].onTickHandler = nullptr;
    Alarm[ID].nextTrigger = 0;
    Alarm[ID].isEnabled = false;
    Alarm[ID].isOneShot = false;
  }
}

// returns the number of allocated timers
uint8_t CronClass::count() const
{
  uint8_t c = 0;
  for(uint8_t id = 0; id < dtNBR_ALARMS; id++) {
    if (isAllocated(id)) c++;
  }
  return c;
}

// returns true if this id is allocated
bool CronClass::isAllocated(CronID_t ID) const
{
  return (ID < dtNBR_ALARMS && Alarm[ID].onTickHandler);
}

// returns the currently triggered alarm id
// returns dtINVALID_ALARM_ID if not invoked from within an alarm handler
CronID_t CronClass::getTriggeredCronId() const
{
  if (isServicing) {
    return servicedCronId;  // new private data member used instead of local loop variable i in serviceAlarms();
  } else {
    return dtINVALID_ALARM_ID; // valid ids only available when servicing a callback
  }
}

// following functions are not Alarm ID specific.
void CronClass::delay(unsigned long ms)
{
  unsigned long start = millis();
  do {
    serviceAlarms();
    yield();
  } while (millis() - start  <= ms);
}

//returns isServicing
bool CronClass::getIsServicing() const
{
  return isServicing;
}

//***********************************************************
//* Private Methods

void CronClass::serviceAlarms()
{
  if (globalEnabled && !isServicing) {
    isServicing = true;
    for (servicedCronId = 0; servicedCronId < dtNBR_ALARMS; servicedCronId++) {
      if (Alarm[servicedCronId].isEnabled && (time(nullptr) >= Alarm[servicedCronId].nextTrigger)) {
        CronEvent_function TickHandler = Alarm[servicedCronId].onTickHandler;
        if (Alarm[servicedCronId].isOneShot) {
          free(servicedCronId);  // free the ID if mode is OnShot
        } else {
          Alarm[servicedCronId].updateNextTrigger();
        }
        if (TickHandler) {
          TickHandler(servicedCronId);
        }
      }
    }
    isServicing = false;
  }
}

// returns the absolute time of the next scheduled alarm, or 0 if none
time_t CronClass::getNextTrigger() const
{
  time_t nextTrigger = 0;

  for (uint8_t id = 0; id < dtNBR_ALARMS; id++) {
    if (isAllocated(id)) {
      if (nextTrigger == 0) {
        nextTrigger = Alarm[id].nextTrigger;
      }
      else if (Alarm[id].nextTrigger <  nextTrigger) {
        nextTrigger = Alarm[id].nextTrigger;
      }
    }
  }
  return nextTrigger;
}

time_t CronClass::getNextTrigger(CronID_t ID) const
{
  if (isAllocated(ID)) {
    return Alarm[ID].nextTrigger;
  } else {
    return 0;
  }
}

char* CronClass::futureSeconds(uint32_t seconds) {
  struct tm timeinfo;
  time_t timecalc = time(nullptr) + seconds;
  localtime_r(&timecalc, &timeinfo);
  strftime(cronstring_buf, sizeof(cronstring_buf), "%S %M %H %d %m *", &timeinfo);
  return cronstring_buf;
}

CronID_t CronClass::create(char * cronstring, OnTick_t onTickHandler_C, bool isOneShot)
{
  OnTick_t localHandler = onTickHandler_C;
  return create(cronstring, [=](CronID_t id) {localHandler();}, isOneShot);
}

CronID_t CronClass::create(uint32_t seconds, OnTick_t onTickHandler_C, bool isOneShot)
{
  OnTick_t localHandler = onTickHandler_C;
  return create(futureSeconds(seconds), [=](CronID_t id) {localHandler();}, isOneShot);
}

CronID_t CronClass::create(uint32_t seconds, CronEvent_function onTickHandler, bool isOneShot)
{
  return create(futureSeconds(seconds), onTickHandler, isOneShot);
}

// attempt to create a cron alarm and return CronID if successful
CronID_t CronClass::create(char * cronstring, CronEvent_function onTickHandler, bool isOneShot)
{
  for (uint8_t id = 0; id < dtNBR_ALARMS; id++) {
    if (!isAllocated(id)) {
      // here if there is an Alarm id that is not allocated
      const char* err = NULL;
      memset(&(Alarm[id].expr), 0, sizeof(Alarm[id].expr));
      cron_parse_expr(cronstring, &(Alarm[id].expr), &err);
      if (err) {
        memset(&(Alarm[id].expr), 0, sizeof(Alarm[id].expr));
        return dtINVALID_ALARM_ID;
      }
      Alarm[id].onTickHandler = onTickHandler;
      Alarm[id].isOneShot = isOneShot;
      enable(id);
      return id;  // alarm created ok
    }
  }
  return dtINVALID_ALARM_ID; // no IDs available or time is invalid
}

// make one instance for the user to use
CronClass Cron = CronClass() ;
