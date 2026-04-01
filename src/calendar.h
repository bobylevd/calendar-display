#pragma once

#include <Arduino.h>
#include <time.h>

#define MAX_EVENTS 10

struct CalendarEvent {
    String title;
    time_t start;
    time_t end;
    bool allDay;
};

struct CalendarData {
    CalendarEvent events[MAX_EVENTS];
    int count;
};

void calendarInit();
bool calendarUpdate(const String& url);
const CalendarData& getCalendarData();
