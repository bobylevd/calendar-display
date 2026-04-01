#pragma once

#include "calendar.h"

void displayInit();
void displayCheckFonts();
void displaySetBrightness(int percent);
void displayUpdate(const CalendarData& data);
void displayFlashEvent(int eventIndex, const CalendarEvent& ev);
void displayShowConnecting();
void displayShowSetup();
