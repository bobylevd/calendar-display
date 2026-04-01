#include "config.h"
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static const char* KEY_URL = "scriptUrl";
static const char* KEY_FLASH = "flashAlert";
static const char* KEY_TZ = "tz";
static const char* KEY_BRT = "brightness";
static const char* KEY_FCNT = "flashCount";
static const char* KEY_THEME = "theme";
static const char* KEY_CLOCK = "showClock";
static String cachedUrl;
static bool flashAlert = true;
static String tzString = "CET-1CEST,M3.5.0/2,M10.5.0/3";
static int brightness = 100;
static int flashCountCfg = 5;
static int themeCfg = 0;
static bool showClockCfg = true;

static void loadConfig() {
    File f = LittleFS.open("/config.json", "r");
    if (!f) return;
    JsonDocument doc;
    deserializeJson(doc, f);
    f.close();
    cachedUrl = doc[KEY_URL] | "";
    flashAlert = doc[KEY_FLASH] | true;
    if (doc[KEY_TZ].is<const char*>()) tzString = doc[KEY_TZ].as<String>();
    brightness = doc[KEY_BRT] | 100;
    flashCountCfg = doc[KEY_FCNT] | 5;
    themeCfg = doc[KEY_THEME] | 0;
    showClockCfg = doc[KEY_CLOCK] | true;
}

void saveConfig() {
    JsonDocument doc;
    doc[KEY_URL] = cachedUrl;
    doc[KEY_FLASH] = flashAlert;
    doc[KEY_TZ] = tzString;
    doc[KEY_BRT] = brightness;
    doc[KEY_FCNT] = flashCountCfg;
    doc[KEY_THEME] = themeCfg;
    doc[KEY_CLOCK] = showClockCfg;
    File f = LittleFS.open("/config.json", "w");
    serializeJson(doc, f);
    f.close();
}

void configInit() {
    LittleFS.begin();

    loadConfig();

    WiFiManager wm;

    WiFiManagerParameter instructions(
        "<p><b>Setup instructions:</b></p>"
        "<ol>"
        "<li>Go to <a href='https://script.google.com' target='_blank'>script.google.com</a></li>"
        "<li>Create a new project, paste the calendar.gs script</li>"
        "<li>Change <code>SECRET_KEY</code> to a random string</li>"
        "<li>Deploy &rarr; New deployment &rarr; Web app</li>"
        "<li>Set 'Execute as' to <b>Me</b>, 'Access' to <b>Anyone</b></li>"
        "<li>Paste the URL below with <code>?key=YOUR_SECRET</code> appended</li>"
        "</ol>"
        "<p><small>Example: https://script.google.com/.../exec?key=mySecret123</small></p>"
    );
    wm.addParameter(&instructions);
    WiFiManagerParameter urlParam("script_url", "Apps Script URL", cachedUrl.c_str(), 256);
    wm.addParameter(&urlParam);

    WiFiManagerParameter flashParam(
        "flash_alert",
        "Flash screen on event start",
        flashAlert ? "on" : "",
        5,
        "type='checkbox' style='width:auto'"
    );
    wm.addParameter(&flashParam);

    WiFiManagerParameter tzParam("tz", "Timezone (POSIX, e.g. CET-1CEST,M3.5.0/2,M10.5.0/3)", tzString.c_str(), 64);
    wm.addParameter(&tzParam);

    wm.setConnectTimeout(30);
    wm.setConfigPortalTimeout(180);

    if (!wm.autoConnect("CalendarDisplay")) {
        Serial.println("[config] WiFi failed - restarting");
        ESP.restart();
    }

    bool changed = false;

    String newUrl = String(urlParam.getValue());
    if (newUrl.length() > 0 && newUrl != cachedUrl) {
        cachedUrl = newUrl;
        changed = true;
    }

    bool newFlash = String(flashParam.getValue()).length() > 0;
    if (newFlash != flashAlert) {
        flashAlert = newFlash;
        changed = true;
    }

    String newTz = String(tzParam.getValue());
    if (newTz.length() > 0 && newTz != tzString) {
        tzString = newTz;
        changed = true;
    }

    if (changed) saveConfig();
}

const String& getScriptUrl() {
    return cachedUrl;
}

void setScriptUrl(const String& url) {
    cachedUrl = url;
}

bool isConfigured() {
    return cachedUrl.length() > 0;
}

bool isFlashAlertEnabled() {
    return flashAlert;
}

void setFlashAlert(bool enabled) {
    flashAlert = enabled;
}

const char* getTimezone() {
    return tzString.c_str();
}

void setTimezone(const String& tz) {
    tzString = tz;
}

int getBrightness() {
    return brightness;
}

void setBrightness(int percent) {
    brightness = constrain(percent, 5, 100);
}

int getFlashCount() {
    return flashCountCfg;
}

void setFlashCount(int count) {
    flashCountCfg = constrain(count, 1, 20);
}

int getTheme() {
    return themeCfg;
}

void setTheme(int theme) {
    themeCfg = constrain(theme, 0, 1);
}

bool getShowClock() {
    return showClockCfg;
}

void setShowClock(bool show) {
    showClockCfg = show;
}
