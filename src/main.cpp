#include <Arduino.h>
#include "display.h"
#include "calendar.h"
#include "config.h"
#include "ota.h"

static unsigned long lastCalendarUpdate = 0;
static unsigned long lastDisplayUpdate = 0;
static unsigned long lastAlertCheck = 0;
static const unsigned long CALENDAR_INTERVAL = 60000;
static const unsigned long DISPLAY_INTERVAL = 1000;
static const unsigned long ALERT_INTERVAL = 1000;

// Track how many flashes each event has received (by start time)
static time_t flashedEventTime = 0;
static int flashCount = 0;
static time_t lastFlashedEvent = 0; // prevent re-flashing same event

static bool waitForNTP() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_year >= 124;
}

// Find which displayed slot (0-2) an event occupies, and return the event
static int findEventSlot(const CalendarData& data, time_t start, const CalendarEvent** outEv = nullptr) {
    time_t now;
    time(&now);
    int slot = 0;
    int maxSlots = getShowClock() ? 3 : 4;
    for (int i = 0; i < data.count && slot < maxSlots; i++) {
        const CalendarEvent& ev = data.events[i];
        if (!ev.allDay && ev.end < now) continue;
        if (ev.start == start) {
            if (outEv) *outEv = &ev;
            return slot;
        }
        slot++;
    }
    return -1;
}

static void checkAlerts(const CalendarData& data) {
    if (!isFlashAlertEnabled()) return;

    time_t now;
    time(&now);

    // If we're already flashing an event, continue the countdown
    if (flashedEventTime != 0) {
        int diff = flashedEventTime - now;
        if (diff < -10 || flashCount >= getFlashCount()) {
            // Done flashing — remember so we don't re-trigger
            lastFlashedEvent = flashedEventTime;
            flashedEventTime = 0;
            flashCount = 0;
            return;
        }
        const CalendarEvent* evPtr = nullptr;
        int slot = findEventSlot(data, flashedEventTime, &evPtr);
        if (slot >= 0 && evPtr) {
            displayFlashEvent(slot, *evPtr);
            flashCount++;
        }
        return;
    }

    // Look for events starting within 10 seconds
    for (int i = 0; i < data.count; i++) {
        const CalendarEvent& ev = data.events[i];
        if (ev.allDay) continue;
        if (ev.start == lastFlashedEvent) continue; // already flashed this one

        int diff = ev.start - now;
        if (diff >= -5 && diff <= 10) {
            Serial.printf("[alert] Event in %ds: %s\n", diff, ev.title.c_str());
            flashedEventTime = ev.start;
            flashCount = 0;

            const CalendarEvent* evPtr = nullptr;
            int slot = findEventSlot(data, ev.start, &evPtr);
            if (slot >= 0 && evPtr) {
                displayFlashEvent(slot, *evPtr);
                flashCount++;
            }
            return;
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n[main] starting");

    displayInit();
    Serial.println("[main] display ready");

    displayShowConnecting();

    configInit();
    Serial.println("[main] WiFi connected");

    displayCheckFonts();
    displaySetBrightness(getBrightness());

    otaInit();
    Serial.println("[main] OTA ready");

    calendarInit();

    if (!isConfigured()) {
        Serial.println("[main] no Apps Script URL configured");
        displayShowSetup();
        return;
    }

    setenv("TZ", getTimezone(), 1);
    tzset();
    configTzTime(getTimezone(), "pool.ntp.org");
    Serial.println("[main] waiting for NTP...");
    unsigned long ntpStart = millis();
    while (!waitForNTP()) {
        otaHandle();
        delay(100);
        if (millis() - ntpStart > 30000) {
            Serial.println("[main] NTP timeout - continuing without sync");
            break;
        }
    }
    Serial.println("[main] NTP done");

    if (!calendarUpdate(getScriptUrl())) {
        displayShowFetchError(true);
    }
    lastCalendarUpdate = millis();
    displayUpdate(getCalendarData());
}

void loop() {
    otaHandle();

    if (!isConfigured()) return;

    unsigned long now = millis();

    if (now - lastCalendarUpdate >= CALENDAR_INTERVAL) {
        lastCalendarUpdate = now;
        if (!calendarUpdate(getScriptUrl())) {
            Serial.println("[main] calendar fetch failed");
            displayShowFetchError(true);
        } else {
            displayShowFetchError(false);
        }
    }

    if (now - lastAlertCheck >= ALERT_INTERVAL) {
        lastAlertCheck = now;
        checkAlerts(getCalendarData());
    }

    if (now - lastDisplayUpdate >= DISPLAY_INTERVAL) {
        lastDisplayUpdate = now;
        displayUpdate(getCalendarData());
    }
}
