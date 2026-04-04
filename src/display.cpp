#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <time.h>
#include <LittleFS.h>

static TFT_eSPI tft = TFT_eSPI();

static const int SCREEN_W = 240;
static const int MAX_DISPLAY_EVENTS = 3;
static const int EVENT_HEIGHT = 56;

// Active theme colors
static uint16_t colBg;
static uint16_t colText;
static uint16_t colSubtext;
static uint16_t colSeparator;
static uint16_t colDotInProgress;
static uint16_t colDotSoon;
static uint16_t colDotHour;
static uint16_t colDotLater;
static uint16_t colDotAllDay;
static uint16_t colFlashBg;
static uint16_t colFlashText;

static int lastMinute = -1;
static int lastTheme = -1;
static bool lastClock = true;
static uint32_t lastDataHash = 0xFFFFFFFF;
static bool smoothFonts = false;
static bool firstBoot = true;
static bool redrawEvents = false;
static bool fetchError = false;

static void applyTheme(int theme) {
    uint16_t gray = tft.color565(0x88, 0x88, 0x88);
    uint16_t darkGray = tft.color565(0x55, 0x55, 0x55);
    uint16_t lightGray = tft.color565(0xCC, 0xCC, 0xCC);
    uint16_t offWhite = tft.color565(0xF0, 0xF0, 0xF0);
    uint16_t darkBg = tft.color565(0x20, 0x20, 0x20);

    switch (theme) {
        default:
        case 0: // Dark
            colBg = TFT_BLACK; colText = TFT_WHITE; colSubtext = gray;
            colSeparator = darkGray;
            colDotInProgress = TFT_CYAN; colDotSoon = TFT_RED;
            colDotHour = TFT_YELLOW; colDotLater = TFT_WHITE;
            colDotAllDay = TFT_GREEN;
            colFlashBg = TFT_WHITE; colFlashText = TFT_BLACK;
            break;
        case 1: // Light
            colBg = offWhite; colText = TFT_BLACK; colSubtext = darkGray;
            colSeparator = lightGray;
            colDotInProgress = TFT_CYAN; colDotSoon = TFT_RED;
            colDotHour = tft.color565(0xD0, 0xA0, 0x00); // darker yellow
            colDotLater = gray;
            colDotAllDay = tft.color565(0x00, 0xA0, 0x00); // darker green
            colFlashBg = TFT_BLACK; colFlashText = TFT_WHITE;
            break;
    }
    lastTheme = theme;
}

static uint16_t eventDotColor(const CalendarEvent& ev, time_t now);
static void drawFetchBadge();

static uint32_t hashCalendarData(const CalendarData& data, time_t now) {
    uint32_t h = data.count * 31;
    for (int i = 0; i < data.count; i++) {
        const CalendarEvent& ev = data.events[i];
        h ^= (uint32_t)ev.start * 7 + (uint32_t)ev.end * 13 + ev.allDay;
        for (unsigned int j = 0; j < ev.title.length(); j++) h = h * 31 + ev.title[j];
        // Include dot color so display updates at urgency thresholds
        h ^= (uint32_t)eventDotColor(ev, now) << (i * 4);
        // Include whether event is past (so display updates when events expire)
        if (!ev.allDay && ev.end < now) h ^= (1 << (i + 16));
    }
    return h;
}

static String truncateSmooth(const String& text, int maxWidth) {
    if (tft.textWidth(text) <= maxWidth) return text;
    String truncated = text;
    int dotsWidth = tft.textWidth("...");
    while (truncated.length() > 0 && tft.textWidth(truncated) + dotsWidth > maxWidth) {
        truncated.remove(truncated.length() - 1);
    }
    return truncated + "...";
}

static String truncateBitmap(const String& text, int maxWidth, uint8_t font) {
    if (tft.textWidth(text, font) <= maxWidth) return text;
    String truncated = text;
    int dotsWidth = tft.textWidth("...", font);
    while (truncated.length() > 0 && tft.textWidth(truncated, font) + dotsWidth > maxWidth) {
        truncated.remove(truncated.length() - 1);
    }
    return truncated + "...";
}

static void formatTimeRange(time_t start, time_t end, char* buf, size_t len) {
    struct tm s, e;
    localtime_r(&start, &s);
    localtime_r(&end, &e);
    snprintf(buf, len, "%d:%02d - %d:%02d", s.tm_hour, s.tm_min, e.tm_hour, e.tm_min);
}

static uint16_t eventDotColor(const CalendarEvent& ev, time_t now) {
    if (ev.allDay) return colDotAllDay;
    int diff = ev.start - now;
    if (diff < 0) return colDotInProgress;
    if (diff <= 900) return colDotSoon;
    if (diff <= 3600) return colDotHour;
    return colDotLater;
}

static void vendorInit() {
    tft.writecommand(0x11); delay(120);
    tft.writecommand(0xB2); tft.writedata(0x1F); tft.writedata(0x1F);
    tft.writedata(0x00); tft.writedata(0x33); tft.writedata(0x33);
    tft.writecommand(0xB7); tft.writedata(0x00);
    tft.writecommand(0xBB); tft.writedata(0x36);
    tft.writecommand(0xC0); tft.writedata(0x2C);
    tft.writecommand(0xC2); tft.writedata(0x01);
    tft.writecommand(0xC3); tft.writedata(0x13);
    tft.writecommand(0xC4); tft.writedata(0x20);
    tft.writecommand(0xC6); tft.writedata(0x13);
    tft.writecommand(0xD6); tft.writedata(0xA1);
    tft.writecommand(0xD0); tft.writedata(0xA4); tft.writedata(0xA1);
    tft.writecommand(0xD6); tft.writedata(0xA1);
    tft.writecommand(0xE0);
    uint8_t pgamma[] = {0xF0,0x08,0x0E,0x09,0x08,0x04,0x2F,0x33,0x45,0x36,0x13,0x12,0x2A,0x2D};
    for (uint8_t b : pgamma) tft.writedata(b);
    tft.writecommand(0xE1);
    uint8_t ngamma[] = {0xF0,0x0E,0x12,0x0C,0x0A,0x15,0x2E,0x32,0x44,0x39,0x17,0x18,0x2B,0x2F};
    for (uint8_t b : ngamma) tft.writedata(b);
    tft.writecommand(0xE4); tft.writedata(0x1D); tft.writedata(0x00); tft.writedata(0x00);
    tft.writecommand(0x21);
    tft.writecommand(0x29);
}

void displayInit() {
    tft.init();
    tft.setRotation(0);
    vendorInit();

    // Backlight: GPIO5, active LOW — full on initially
    pinMode(5, OUTPUT);
    digitalWrite(5, LOW);

    applyTheme(getTheme());

    tft.fillScreen(colBg);
    tft.setTextColor(colText, colBg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Booting...", SCREEN_W / 2, SCREEN_W / 2, 4);
    Serial.println("[display] init done");
}

void displayUpdate(const CalendarData& data) {
    time_t now;
    time(&now);
    struct tm* tm = localtime(&now);

    // Check for theme or layout change
    int curTheme = getTheme();
    bool curClock = getShowClock();
    if (curTheme != lastTheme || curClock != lastClock) {
        if (curTheme != lastTheme) applyTheme(curTheme);
        lastClock = curClock;
        tft.fillScreen(colBg);
        lastMinute = -1;
        lastDataHash = 0xFFFFFFFF;
        drawFetchBadge();
    }

    uint32_t dataHash = hashCalendarData(data, now);
    bool dataChanged = dataHash != lastDataHash;
    bool minuteChanged = tm->tm_min != lastMinute;
    bool needsEventRedraw = redrawEvents;
    redrawEvents = false;

    if (!dataChanged && !minuteChanged && !needsEventRedraw) return;

    lastMinute = tm->tm_min;
    lastDataHash = dataHash;

    if (firstBoot) {
        tft.fillScreen(colBg);
        firstBoot = false;
        dataChanged = true;
        drawFetchBadge();
    }

    bool showClock = getShowClock();
    int maxEvents = showClock ? MAX_DISPLAY_EVENTS : 4;
    int eventsStartY = showClock ? 68 : 8;

    // Time — only redraw on minute change
    if (minuteChanged && showClock) {
        char timeBuf[6];
        sprintf(timeBuf, "%02d:%02d", tm->tm_hour, tm->tm_min);
        tft.setTextColor(colText, colBg);
        tft.setTextDatum(TC_DATUM);
        tft.drawString(timeBuf, SCREEN_W / 2, 2, 7);
        tft.drawFastHLine(10, 60, SCREEN_W - 20, colSeparator);
    }

    // Events — redraw on new data or after flash
    if (dataChanged || needsEventRedraw) {
        int y = eventsStartY;
        int shown = 0;
        for (int i = 0; i < data.count && shown < maxEvents; i++) {
            const CalendarEvent& ev = data.events[i];
            if (!ev.allDay && ev.end < now) continue;

            uint16_t dotColor = eventDotColor(ev, now);
            bool isNext = (shown == 0);

            if (smoothFonts) {
                // Smooth fonts are transparent — must clear slot first
                tft.fillRect(0, y - 4, SCREEN_W, EVENT_HEIGHT, colBg);

                tft.fillCircle(15, y + 25, 7, dotColor);
                if (!isNext) tft.fillCircle(15, y + 25, 4, colBg);

                tft.setTextDatum(TL_DATUM);
                tft.loadFont("Roboto30", LittleFS);
                tft.setTextColor(colText, colBg);
                String title = truncateSmooth(ev.title, SCREEN_W - 35);
                tft.drawString(title, 28, y);
                tft.unloadFont();

                tft.loadFont("Roboto16", LittleFS);
                tft.setTextColor(colSubtext, colBg);
                if (!ev.allDay) {
                    char tr[20];
                    formatTimeRange(ev.start, ev.end, tr, sizeof(tr));
                    tft.drawString(tr, 28, y + 32);
                } else {
                    tft.drawString("All day", 28, y + 32);
                }
                tft.unloadFont();
            } else {
                // Bitmap fonts have opaque bg — clear dot area, then overdraw text
                tft.fillRect(0, y - 4, 26, EVENT_HEIGHT, colBg); // dot column only
                tft.fillCircle(15, y + 25, 7, dotColor);
                if (!isNext) tft.fillCircle(15, y + 25, 4, colBg);

                // Text with opaque bg overwrites old content; pad line to full width
                tft.setTextDatum(TL_DATUM);
                tft.setTextColor(colText, colBg);
                String title = truncateBitmap(ev.title, SCREEN_W - 35, 4);
                tft.drawString(title, 28, y, 4);
                // Clear remainder of title line
                int tw = tft.textWidth(title, 4);
                if (tw + 28 < SCREEN_W) tft.fillRect(28 + tw, y, SCREEN_W - 28 - tw, 26, colBg);

                tft.setTextColor(colSubtext, colBg);
                if (!ev.allDay) {
                    char tr[20];
                    formatTimeRange(ev.start, ev.end, tr, sizeof(tr));
                    tft.drawString(tr, 28, y + 24, 2);
                    int trw = tft.textWidth(tr, 2);
                    if (trw + 28 < SCREEN_W) tft.fillRect(28 + trw, y + 24, SCREEN_W - 28 - trw, 16, colBg);
                } else {
                    tft.drawString("All day", 28, y + 24, 2);
                    int aw = tft.textWidth("All day", 2);
                    if (aw + 28 < SCREEN_W) tft.fillRect(28 + aw, y + 24, SCREEN_W - 28 - aw, 16, colBg);
                }
            }

            // Separator line between events (not after last)
            if (shown < maxEvents - 1 && i + 1 < data.count) {
                tft.drawFastHLine(28, y + EVENT_HEIGHT - 6, SCREEN_W - 38, colSeparator);
            }

            y += EVENT_HEIGHT;
            shown++;
        }

        // Clear any remaining slots below last event
        if (dataChanged && y < 240) {
            tft.fillRect(0, y - 4, SCREEN_W, 240 - y + 4, colBg);
        }

        if (shown == 0) {
            int centerY = showClock ? 150 : 120;
            tft.setTextColor(colSubtext, colBg);
            tft.setTextDatum(MC_DATUM);
            if (smoothFonts) {
                tft.loadFont("Roboto30", LittleFS);
                tft.drawString("No events", SCREEN_W / 2, centerY);
                tft.unloadFont();
            } else {
                tft.drawString("No events", SCREEN_W / 2, centerY, 4);
            }
        }
    }
}

void displayCheckFonts() {
    if (LittleFS.exists("/Roboto30.vlw")) {
        smoothFonts = true;
        Serial.println("[display] smooth fonts available");
    } else {
        Serial.println("[display] no smooth fonts, using bitmap");
    }
}

static void drawFetchBadge() {
    int cx = SCREEN_W - 12, cy = getShowClock() ? 8 : 3;
    int r = getShowClock() ? 7 : 4;
    if (fetchError) {
        tft.fillCircle(cx, cy, r, TFT_RED);
        int d = r - 2;
        tft.drawLine(cx - d, cy - d, cx + d, cy + d, TFT_WHITE);
        tft.drawLine(cx - d, cy + d, cx + d, cy - d, TFT_WHITE);
    } else {
        tft.fillRect(cx - r - 1, cy - r - 1, r * 2 + 2, r * 2 + 2, colBg);
    }
}

void displayShowFetchError(bool error) {
    fetchError = error;
    drawFetchBadge();
}

void displaySetBrightness(int percent) {
    if (percent > 100) percent = 100;
    if (percent < 5) percent = 5;
    // Usable PWM range is ~0-256 (active low: 0=brightest, ~256=off)
    // Map 5-100% to PWM 250-0
    int pwm = 250 - (percent * 250 / 100);
    analogWrite(5, pwm);
}

void displayFlashEvent(int eventIndex, const CalendarEvent& ev) {
    int eventsStartY = getShowClock() ? 68 : 8;
    int y = eventsStartY + eventIndex * EVENT_HEIGHT;
    int h = EVENT_HEIGHT;

    // Flash: draw inverted event (white bg, black text)
    tft.fillRect(0, y - 2, SCREEN_W, h, colFlashBg);

    tft.fillCircle(15, y + 25, 7, colFlashText);

    tft.setTextColor(colFlashText, colFlashBg);
    tft.setTextDatum(TL_DATUM);

    if (smoothFonts) {
        tft.loadFont("Roboto30", LittleFS);
        String title = truncateSmooth(ev.title, SCREEN_W - 35);
        tft.drawString(title, 28, y);
        tft.unloadFont();

        tft.loadFont("Roboto16", LittleFS);
        if (!ev.allDay) {
            char tr[20];
            formatTimeRange(ev.start, ev.end, tr, sizeof(tr));
            tft.drawString(tr, 28, y + 32);
        } else {
            tft.drawString("All day", 28, y + 32);
        }
        tft.unloadFont();
    } else {
        String title = truncateBitmap(ev.title, SCREEN_W - 35, 4);
        tft.drawString(title, 28, y, 4);
        if (!ev.allDay) {
            char tr[20];
            formatTimeRange(ev.start, ev.end, tr, sizeof(tr));
            tft.drawString(tr, 28, y + 24, 2);
        } else {
            tft.drawString("All day", 28, y + 24, 2);
        }
    }

    delay(200);

    // Clear and mark for redraw
    tft.fillRect(0, y - 2, SCREEN_W, h, colBg);
    redrawEvents = true;
}

void displayShowConnecting() {
    tft.fillScreen(colBg);
    tft.setTextColor(colText, colBg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Connecting", SCREEN_W / 2, SCREEN_W / 2 - 10, 4);
    tft.drawString("to WiFi...", SCREEN_W / 2, SCREEN_W / 2 + 20, 4);
}

void displayShowSetup() {
    tft.fillScreen(colBg);
    tft.setTextDatum(TC_DATUM);

    tft.setTextColor(TFT_YELLOW, colBg);
    tft.drawString("Setup Required", SCREEN_W / 2, 10, 4);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(colText, colBg);

    int y = 50;
    tft.drawString("1. Connect to WiFi:", 10, y, 2);      y += 18;
    tft.setTextColor(TFT_CYAN, colBg);
    tft.drawString("   CalendarDisplay", 10, y, 2);        y += 26;

    tft.setTextColor(colText, colBg);
    tft.drawString("2. Open browser to:", 10, y, 2);       y += 18;
    tft.setTextColor(TFT_CYAN, colBg);
    tft.drawString("   192.168.4.1", 10, y, 2);            y += 26;

    tft.setTextColor(colText, colBg);
    tft.drawString("3. Enter WiFi creds", 10, y, 2);       y += 18;
    tft.drawString("4. Paste Script URL", 10, y, 2);

    tft.setTextColor(colSubtext, colBg);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("See README for details", SCREEN_W / 2, 220, 2);
}

