# CLAUDE.md - CalendarDisplay ESP Firmware

## Project Overview

Custom firmware for the **GeekMagic SmallTV-Ultra** device. It replaces the stock firmware with a Google Calendar display that shows the current time (via NTP) and up to 3 upcoming events with color-coded urgency dots on a 240x240 TFT screen.

The device polls a Google Apps Script endpoint every 60 seconds over HTTPS, parses the JSON response, and renders events on a ST7789 IPS LCD.

## Architecture

```
Google Calendar API
       |
Google Apps Script (apps-script/calendar.gs)
  - Deployed as a web app ("Execute as Me", "Anyone" access)
  - Protected by a shared SECRET_KEY query parameter
  - Returns JSON: { events: [...], count, nextUpdate }
  - Events sorted: timed first (by start), then all-day
  - Max 10 events returned, ISO 8601 timestamps
       |  HTTPS GET (follows redirects)
       v
ESP8266 (ESP-12E) firmware
  - WiFiManager captive portal for initial WiFi + config
  - Fetches calendar JSON every 60s
  - Parses with ArduinoJson
  - NTP time sync (pool.ntp.org)
  - POSIX timezone via setenv("TZ", ...) + tzset()
       |
ST7789 240x240 IPS LCD (SPI)
  - Current time (HH:MM) in large bitmap Font 7
  - Up to 3 events with title + time range
  - Color dots: cyan (in-progress), red (<15min), yellow (<1hr), white (later), green (all-day)
  - 2 themes: Dark, Light
  - Optional clock display (3 events with clock, 4 without)
  - Configurable flash alert before events start
```

## Hardware

**Target device:** GeekMagic SmallTV-Ultra

| Component | Details |
|-----------|---------|
| MCU | ESP8266 ESP-12E, 4MB flash |
| Display | ST7789 IPS LCD, 240x240, SPI |
| Flash layout | 4m1m (3MB firmware + 1MB LittleFS) |
| Flash mode | **dio** (required - quad modes fail on this board) |

### Pin Mapping (ESP8266)

| Function | GPIO | Notes |
|----------|------|-------|
| SPI MOSI | 13 | |
| SPI SCLK | 14 | |
| TFT CS | -1 | Not connected (directly tied) |
| TFT DC | 0 | |
| TFT RST | 2 | |
| TFT Backlight | 5 | **Active-LOW PWM** |

### Backlight Control

GPIO5 is active-low. Usable PWM range 0-250:
- `analogWrite(5, 0)` = full brightness
- `analogWrite(5, 250)` = minimum usable brightness (~5%)
- Formula: `int pwm = 250 - (percent * 250 / 100)`
- **Values above ~250 make the display appear off**

## Build System

**PlatformIO** with one environment:

### `esp8266`
```ini
platform = espressif8266
board = esp12e
board_build.flash_mode = dio
board_build.filesystem = littlefs
board_build.ldscript = eagle.flash.4m1m.ld
build_type = release
```
Key flags: `-Os`, `-DPIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY_LOW_FLASH`, `-DWM_NODEBUG`, `-DTFT_RGB_ORDER=1`

### Build Flags
- `USER_SETUP_LOADED=1` - tells TFT_eSPI to use build flags instead of User_Setup.h
- `SMOOTH_FONT=1` - enables VLW font support in TFT_eSPI
- `LOAD_FONT2`, `LOAD_FONT4`, `LOAD_FONT7` - bitmap font sizes included
- `SPI_FREQUENCY=40000000` - 40MHz SPI clock

### Libraries
- `bodmer/TFT_eSPI@^2.5.43` - display driver
- `bblanchon/ArduinoJson@^7.0.0` - JSON parsing
- `tzapu/WiFiManager@^2.0.17` - captive portal for WiFi config

### Build Commands
```bash
pio run -e esp8266              # build full firmware (~566KB)
pio run -e esp8266 -t upload    # flash via USB
pio run -e bootstrap            # build OTA bootstrap (~334KB)
```

## File-by-File Guide

### `src/main.cpp`
Entry point. Orchestrates setup and the main loop.

- **`setup()`**: init display -> show "Connecting" -> configInit (WiFi + LittleFS) -> check fonts -> set brightness -> init OTA -> init calendar -> set timezone -> NTP sync (30s timeout) -> first calendar fetch -> first display render
- **`loop()`**: handles OTA, then on timers: calendar fetch (60s), alert check (1s), display update (1s)
- **`checkAlerts()`**: if flash alerts enabled, looks for events starting within [-5s, +10s] window. Triggers configurable number of flashes (default 5) per event via `displayFlashEvent()`. Tracks `lastFlashedEvent` to avoid re-triggering same event.
- **`findEventSlot()`**: maps an event's start time to its displayed slot index (0-2 or 0-3 without clock), skipping past non-all-day events. Optionally returns a pointer to the matched event.
- **`waitForNTP()`**: returns true once `tm_year >= 124` (year 2024+), meaning NTP has synced.

### `src/display.h` / `src/display.cpp`
All TFT rendering logic using TFT_eSPI.

- **`displayInit()`**: calls `tft.init()`, `vendorInit()` for ST7789-specific register setup, sets GPIO5 backlight to full on, shows "Booting..." splash.
- **`vendorInit()`**: sends raw ST7789 register commands (sleep out, porch control, gate control, VCOM, LCM, VDV/VRH, frame rate, display inversion on, gamma curves, display on). **This is required** - without it the display shows garbage or nothing on the SmallTV-Ultra hardware.
- **`displayUpdate()`**: smart redraw strategy using content hashing. Time only redraws on minute change (overdraw with opaque background). Events only redraw when `hashCalendarData()` detects actual content changes (titles, times, dot colors, expired status). Bitmap fonts use opaque overdraw (no fillRect clear needed — text draws over old content, then remaining pixels are padded to full width). Smooth fonts require per-slot fillRect clear since VLW rendering is transparent. Theme changes trigger a full screen clear and redraw.
- **`hashCalendarData()`**: computes a hash of event titles, times, dot colors, and expired status. Dot colors are included so events redraw when crossing 15min/1hr urgency thresholds.
- **`applyTheme()`**: sets all color variables from a theme preset (0=Dark, 1=Light). Called at init and on theme change.
- **`displayCheckFonts()`**: checks if `/Roboto30.vlw` exists on LittleFS. If yes, sets `smoothFonts = true`. **Must be called after `LittleFS.begin()`** (which happens in `configInit()`).
- **`displaySetBrightness(int percent)`**: converts 0-100% to active-low PWM value.
- **`displayFlashEvent(int slot, ev)`**: draws inverted event (theme flash colors) for 200ms then clears, sets `redrawEvents` flag to restore content.
- **`truncateSmooth()` / `truncateBitmap()`**: truncates text to fit pixel width, appending "..." - two versions because smooth fonts use `tft.textWidth(text)` while bitmap fonts need `tft.textWidth(text, font)`.
- **`eventDotColor()`**: cyan if started (in progress), red if <=15min, yellow if <=1hr, white/gray if later, green if all-day. Colors come from theme.
- **Smooth fonts**: loaded per-draw with `tft.loadFont("Roboto30", LittleFS)`. Uses Roboto30 for titles, Roboto16 for time/subtitle. **The `LittleFS` parameter is required** in `loadFont()` on ESP8266. Smooth fonts require per-slot fillRect clear on redraw (visible flicker).
- **Bitmap fonts**: built-in Font 4 (titles), Font 2 (subtitles), Font 7 (clock). Overdraw with opaque bg — no flicker on redraw. Remaining pixels after text are padded with bg-colored fillRect.
- **Layout**: clock mode (default): time 0-58, separator at 60, events at y=68, 56px spacing, 3 events. No-clock mode: events start at y=8, 4 events.

### `src/calendar.h` / `src/calendar.cpp`
HTTP fetch and JSON parsing.

- **`CalendarEvent`** struct: `title` (String), `start`/`end` (time_t), `allDay` (bool)
- **`CalendarData`** struct: array of 10 events, count
- **`calendarUpdate(url)`**: HTTPS GET with `setInsecure()` (no cert validation - ESP8266 cannot do proper TLS cert pinning with Google's rotating certs), follows redirects (Apps Script redirects on exec), 10s timeout. Parses JSON into CalendarData.
- **`parseISO8601()`**: parses `yyyy-MM-ddTHH:mm:ss` strings. Uses `mktime()` which respects the TZ environment variable. **Timezone must be set via `setenv("TZ", ...)` + `tzset()` before any parsing**, otherwise times will be wrong.
- Uses `BearSSL::WiFiClientSecure` for HTTPS.

### `src/config.h` / `src/config.cpp`
WiFi provisioning and persistent configuration.

- **`configInit()`**: starts LittleFS, loads config, launches WiFiManager. Creates `CalendarDisplay` AP with captive portal.
- **WiFiManager parameters**: script URL (256 char), flash alert checkbox, timezone string (64 char). Instructions HTML rendered in portal.
- **Persistence**: `/config.json` on LittleFS.
- **Config fields**: `scriptUrl`, `flashAlert` (bool, default true), `flashCount` (int 1-20, default 5), `tz` (POSIX string, default `CET-1CEST,M3.5.0/2,M10.5.0/3`), `brightness` (int 5-100, default 100), `theme` (int 0-1, default 0), `showClock` (bool, default true).
- **`setBrightness()`**: clamps to 5-100 range (never fully off).
- Portal timeout: 180s. Connect timeout: 30s. Restarts on WiFi failure.

### `src/ota.h` / `src/ota.cpp`
Web-based device management on **port 8080**.

- **Endpoints**:
  - `GET /` - management page (HTML stored in PROGMEM)
  - `GET /api/config` - returns JSON with all settings + WiFi SSID/IP
  - `POST /api/config` - saves URL, timezone, flash alert
  - `GET /brightness?v=N` - get/set brightness (0-100), saves to config
  - `GET /reboot` - reboots the device
  - `GET /resetwifi` - clears WiFi credentials and reboots
  - `POST /update` - firmware upload (multipart), reboots on success
  - `POST /upload` - file upload to LittleFS root (multipart, supports multiple files)
  - `GET /files` - list all LittleFS files with sizes and delete links
  - `GET /delete?f=/filename` - delete a file from LittleFS
- **Settings page**: brightness slider, theme dropdown (Dark/Light), show clock toggle, Apps Script URL, timezone dropdown (15 common timezones with DST rules), flash alert toggle + count slider, WiFi info, reset/reboot buttons. Theme and timezone apply immediately without reboot. Firmware update page auto-redirects back to settings after reboot. File upload redirects back to settings.
- Firmware max size: `(ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000`
- Uses `ESP8266WebServer`.

### `apps-script/calendar.gs`
Google Apps Script deployed as a web app.

- **`doGet(e)`**: validates `?key=` parameter against `SECRET_KEY`. Fetches today's events from default calendar. Returns JSON with events sorted (timed first by start time, then all-day). Max 10 events.
- **Event format**: `{ title, start, end, allDay }` with ISO 8601 timestamps in script's timezone.
- **`SECRET_KEY`**: must be changed from default `CHANGE_ME_TO_A_RANDOM_STRING`.

### `platformio.ini`
Build configuration. See Build System section above.

### `.gitignore`
Ignores `.pio/`, `.vscode/`, `.DS_Store`, `platformio_override.ini`.

## Configuration

### WiFiManager Captive Portal
On first boot (or when WiFi is unavailable), the device creates an AP named **`CalendarDisplay`**. Connect to it and a captive portal opens at `192.168.4.1` with fields for:
1. WiFi SSID + password
2. Apps Script URL (with `?key=SECRET` appended)
3. Flash alert toggle
4. Timezone (POSIX format)

### LittleFS `/config.json` Structure
```json
{
  "scriptUrl": "https://script.google.com/macros/s/.../exec?key=...",
  "flashAlert": true,
  "flashCount": 5,
  "tz": "CET-1CEST,M3.5.0/2,M10.5.0/3",
  "brightness": 100,
  "theme": 0,
  "showClock": true
}
```

### Smooth Fonts (Optional)
Upload VLW font files via the OTA file manager (`http://<device-ip>:8080`):
- `/Roboto30.vlw` - event titles (30px)
- `/Roboto16.vlw` - event times/subtitles (16px)

Without these files, the display falls back to built-in bitmap fonts (Font 4 and Font 2).

Generate fonts from TTF:
```bash
python3 tools/ttf2vlw.py tools/fonts/Roboto.ttf 30 -o data/Roboto30.vlw
python3 tools/ttf2vlw.py tools/fonts/Roboto.ttf 16 -o data/Roboto16.vlw
```

## OTA System

Web interface at `http://<device-ip>:8080`:
- **Firmware update**: upload `.bin` file, device reboots automatically
- **File manager**: upload/delete LittleFS files (fonts, config)
- **Brightness slider**: real-time adjustment, persisted to config

## Alert System

When `flashAlert` is enabled:
1. Every second, `checkAlerts()` scans events starting within [-5s, +10s] window
2. When found, the event's display slot is identified via `findEventSlot()`
3. The slot is drawn inverted (theme flash colors with visible text) for 200ms, then cleared
4. Repeats up to `flashCount` times (configurable, default 5)
5. After all flashes or 10 seconds past start time, flashing stops
6. `lastFlashedEvent` tracks the event to prevent re-triggering
7. `redrawEvents` flag restores content without full-area clear

## Timezone

- Default: `CET-1CEST,M3.5.0/2,M10.5.0/3` (Central European Time with DST)
- Set via WiFiManager portal, OTA settings page dropdown, or by editing `/config.json`
- OTA page has a dropdown with 15 common timezones (correct POSIX DST rules)
- Applied in `setup()` via `setenv("TZ", tz, 1); tzset();` **before** any `mktime()` or `localtime()` calls
- Also passed to `configTzTime()` for NTP
- `parseISO8601()` uses `mktime()` which relies on the TZ env var being set

## Known Constraints

- **ESP8266 heap**: ~50KB available. ArduinoJson, WiFiClientSecure (BearSSL), and String allocations compete for this. Keep JSON responses small.
- **HTTPS**: `setInsecure()` is used (no certificate validation). ESP8266 BearSSL cannot practically pin Google's rotating certificates.
- **Binary size budget**: the 4m1m layout gives ~3MB for firmware, but **first OTA flash from stock GeekMagic firmware is limited to ~520KB** because the stock OTA partition is small. Subsequent flashes via the custom OTA endpoint use full available space.
- **SPI frequency**: 40MHz. Higher may cause display glitches on the SmallTV-Ultra board.
- **MAX_EVENTS**: hardcoded to 10 in `calendar.h`.
- **Display refresh**: uses content hashing — only redraws events when data actually changes (titles, times, dot colors). Bitmap fonts use opaque overdraw (text draws with bg color, remaining line pixels padded) — **no flicker**. Smooth fonts (VLW) require per-slot fillRect clear before drawing — **brief flicker per slot on data change**. Time overdraws with opaque background on minute change. Flash alerts use `redrawEvents` flag to restore content.
- **Color order**: `TFT_RGB_ORDER=1` is required — without it, the ST7789 panel swaps red/blue channels.

## First Flash Procedure

The full firmware (~566KB) exceeds the stock OTA partition limit (~520KB). Use the **bootstrap firmware** (~334KB) as a stepping stone:

1. Build bootstrap: `pio run -e bootstrap`
2. Connect the device to WiFi via stock firmware's setup
3. Find its IP address
4. Navigate to `http://<device-ip>/update` and upload `.pio/build/bootstrap/firmware.bin`
5. Device reboots into bootstrap mode (WiFiManager AP + OTA on port 8080)
6. Configure WiFi, then open `http://<device-ip>:8080`
7. Upload the full firmware: `.pio/build/esp8266/firmware.bin`
8. After first flash, subsequent updates go through the device manager at port 8080

Alternatively, flash via USB: `pio run -e esp8266 -t upload`

### Bootstrap firmware (`src/bootstrap.cpp`)
Minimal firmware (~334KB) that only provides WiFiManager + OTA web server on port 8080. Built with `pio run -e bootstrap`. Uses `build_src_filter = +<bootstrap.cpp>` to exclude all other source files. Guarded by `#ifdef BOOTSTRAP`.

## Common Pitfalls

| Issue | Cause | Fix |
|-------|-------|-----|
| Display blank/garbage after flash | Missing `vendorInit()` or wrong flash_mode | Ensure `board_build.flash_mode = dio` in platformio.ini |
| Backlight off at low brightness | GPIO5 active-low, PWM values >~820 (~20%) dim too much | Keep brightness >= 5% (enforced by `setBrightness()`) |
| Smooth fonts not loading | `displayCheckFonts()` called before `LittleFS.begin()` | `configInit()` (which calls `LittleFS.begin()`) must run before `displayCheckFonts()` |
| `loadFont()` fails silently | Missing `LittleFS` parameter | Must call `tft.loadFont("FontName", LittleFS)` not `tft.loadFont("FontName")` |
| Colors wrong (red shows blue) | Missing `TFT_RGB_ORDER` flag | Add `-DTFT_RGB_ORDER=1` to esp8266 build flags |
| Wrong event times | Timezone not set before `mktime()` | `setenv("TZ",...) + tzset()` must happen before `calendarUpdate()` |
| First OTA fails | Full firmware too large for stock OTA partition | Use the bootstrap firmware (`pio run -e bootstrap`, ~334KB) as a stepping stone |
| Flash mode wrong | Using qio/qout instead of dio | This board requires `dio`. Set in platformio.ini, not just build flags |
| Calendar fetch fails | Google Apps Script redirects | `http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS)` is already set |
| Config lost after reflash | LittleFS partition erased | Upload via OTA (`/update` endpoint) to preserve LittleFS; USB flash may erase it |
