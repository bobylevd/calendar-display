#pragma once

#include <Arduino.h>

void configInit();
const String& getScriptUrl();
void setScriptUrl(const String& url);
bool isConfigured();
bool isFlashAlertEnabled();
void setFlashAlert(bool enabled);
const char* getTimezone();
void setTimezone(const String& tz);
int getBrightness();
void setBrightness(int percent);
int getFlashCount();
void setFlashCount(int count);
int getTheme();
void setTheme(int theme);
bool getShowClock();
void setShowClock(bool show);
void saveConfig();
