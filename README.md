# CalendarDisplay

Custom firmware for GeekMagic SmallTV-Ultra that shows Google Calendar events on a 240x240 TFT display.

## What it does

- Displays current time (NTP synced, can be hidden for more events)
- Shows up to 3 upcoming events (4 without clock) with color-coded urgency dots
  - Cyan: already started (in progress)
  - Red: starts within 15 minutes
  - Yellow: starts within 1 hour
  - White: starts later
  - Green: all-day event
- Flashes screen when events are about to start (configurable count)
- Polls your calendar every 60 seconds
- 2 theme presets: Dark, Light
- Web-based settings page (theme, brightness, timezone, clock toggle, flash alerts)
- OTA firmware and font updates over WiFi
- Optional smooth anti-aliased fonts (Roboto)

## Hardware

- GeekMagic SmallTV-Ultra (ESP8266 + ST7789 240x240)

## Setup

### 1. Deploy the Google Apps Script

This script reads your Google Calendar and serves events as JSON. The device fetches this URL periodically.

1. Go to [script.google.com](https://script.google.com)
2. Click **New project**
3. Delete the default code, paste the contents of `apps-script/calendar.gs`
4. **Change `SECRET_KEY`** to a random string (generate one at [randomkeygen.com](https://randomkeygen.com))
5. Click **Deploy** > **New deployment**
6. Set type to **Web app**
7. Set "Execute as" to **Me**
8. Set "Who has access" to **Anyone**
9. Click **Deploy**, authorize when prompted
10. Copy the **Web app URL** and append your key: `https://script.google.com/macros/s/.../exec?key=YOUR_SECRET`

Alternatively, deploy via CLI:

```
npm install -g @google/clasp
clasp login
cd apps-script
clasp create --title CalendarDisplay --type standalone
clasp push
clasp deploy
```

**Important:** Your Google account timezone must match the device timezone, otherwise event times will be wrong.

### 2. Flash the firmware

Install [PlatformIO](https://platformio.org/install/cli).

**Option A: USB** (if you have serial access):
```
pio run -e esp8266 -t upload
```

**Option B: OTA from stock firmware** (no USB needed):

The full firmware (~566KB) is too large for the stock OTA partition (~520KB). Use the bootstrap firmware as a stepping stone:

1. Build the bootstrap: `pio run -e bootstrap`
2. Connect the device to WiFi using the stock firmware
3. Find its IP (check your router's DHCP leases or serial output)
4. Open `http://<device-ip>/update` and upload `.pio/build/bootstrap/firmware.bin` (~334KB)
5. The device reboots into bootstrap mode — connect to the **CalendarDisplay** WiFi AP and configure WiFi
6. Open `http://<device-ip>:8080` and upload the full firmware: `.pio/build/esp8266/firmware.bin`
7. The device reboots with the full firmware

After the first flash, subsequent updates go through the device manager at port 8080 with no size limit.

### 3. Configure the device

On first boot the display shows setup instructions:

1. Connect your phone/laptop to the **CalendarDisplay** WiFi network
2. A captive portal opens automatically (or browse to **192.168.4.1**)
3. Select your WiFi network and enter the password
4. Paste the Apps Script URL from step 1
5. Click Save — the device reboots and connects

## Device Manager

After setup, open `http://<device-ip>:8080` in a browser for:

- **Theme** selector (Dark, Light, High Contrast Dark/Light — applies immediately)
- **Brightness** slider (real-time, persisted)
- **Show clock** toggle (on: clock + 3 events, off: 4 events, no clock)
- **Settings**: Apps Script URL, timezone dropdown, flash alert toggle + count
- **WiFi**: view SSID/IP, reset WiFi credentials
- **Firmware update**: upload `.bin` file
- **File manager**: upload/delete LittleFS files (fonts, config)

### Smooth Fonts (optional)

Upload these VLW files via the file manager for anti-aliased text:
- `Roboto30.vlw` — event titles
- `Roboto16.vlw` — event times

Without them, the display uses built-in bitmap fonts.

## Development

### Project structure

```
├── platformio.ini          # Build configuration
├── src/
│   ├── main.cpp            # Entry point, setup/loop
│   ├── display.h/cpp       # TFT rendering
│   ├── calendar.h/cpp      # HTTP fetch + JSON parsing
│   ├── config.h/cpp        # WiFi + config portal
│   └── ota.h/cpp           # Web server + OTA + settings
├── data/                   # VLW font files for LittleFS
├── tools/
│   ├── ttf2vlw.py          # TTF to VLW font converter
│   └── fonts/Roboto.ttf    # Source font
└── apps-script/
    └── calendar.gs         # Google Apps Script source
```

## Architecture

```
Google Calendar
    |
Google Apps Script (deployed as web app)
    |  (simple HTTP JSON endpoint)
    v
ESP8266 (polls every 60s)
    |
TFT 240x240 display
    -> Current time (via NTP)
    -> Next 3 events with times
    -> Color-coded urgency dots
```

## Security

The Apps Script is deployed with "Anyone" access because the ESP8266 cannot perform Google OAuth authentication. A shared secret key mitigates this:

- The script rejects requests without the correct `?key=` parameter
- The URL + key acts as a bearer token — **treat it like a password**
- HTTPS protects the key in transit
- The key is stored in plaintext on the device's flash (LittleFS)
- The script is **read-only** — it can only read your calendar, not modify it

**Recommendations:**
- Use a strong random key (32+ characters)
- Don't share your deployment URL
- If compromised, change the key in the script and redeploy
- Periodically rotate the key

## Disclaimer

This is unofficial third-party firmware. Flashing it **replaces the stock firmware** and voids any warranty. I am not responsible for bricked devices, data loss, or any other damage. Flash at your own risk. If something goes wrong, USB serial flashing can usually recover the device.

## License

MIT
