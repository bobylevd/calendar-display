#include "calendar.h"
#include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

static CalendarData calData = {};

static time_t parseISO8601(const char* str) {
    int y, m, d, H = 0, M = 0, S = 0;
    if (sscanf(str, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &H, &M, &S) < 3) return 0;

    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = H;
    t.tm_min = M;
    t.tm_sec = S;
    t.tm_isdst = -1;
    return mktime(&t);
}

void calendarInit() {
    calData.count = 0;
}

bool calendarUpdate(const String& url) {
    if (url.isEmpty()) return false;

    BearSSL::WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000);
    http.begin(client, url);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;

    if (doc.containsKey("error")) {
        Serial.printf("[calendar] API error: %s\n", doc["error"].as<const char*>());
        return false;
    }

    JsonArray events = doc["events"].as<JsonArray>();
    calData.count = 0;

    for (JsonObject ev : events) {
        if (calData.count >= MAX_EVENTS) break;
        CalendarEvent& e = calData.events[calData.count];
        e.title = ev["title"].as<String>();
        e.allDay = ev["allDay"] | false;
        e.start = parseISO8601(ev["start"] | "");
        e.end = parseISO8601(ev["end"] | "");
        calData.count++;
    }

    Serial.printf("[calendar] %d events fetched\n", calData.count);
    return true;
}

const CalendarData& getCalendarData() {
    return calData;
}
