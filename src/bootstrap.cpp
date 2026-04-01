// Minimal OTA bootstrap firmware for GeekMagic SmallTV-Ultra
// Purpose: fits within the stock OTA partition (~520KB) and provides
// a web-based firmware upload endpoint so the full firmware can be flashed.
//
// Build: pio run -e bootstrap
// Flash via stock OTA: upload .pio/build/bootstrap/firmware.bin

#ifdef BOOTSTRAP

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

static ESP8266WebServer server(8080);

static const char PAGE[] PROGMEM = R"rawliteral(
<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
body{background:#222;color:#eee;font-family:sans-serif;max-width:500px;margin:0 auto;padding:16px;text-align:center}
h2{color:#fff}
p{color:#aaa}
.btn{background:#5865F2;color:#fff;border:none;padding:10px 20px;border-radius:4px;cursor:pointer;margin:8px}
</style></head><body>
<h2>CalendarDisplay Bootstrap</h2>
<p>Upload the full firmware below.</p>
<form method='POST' action='/update' enctype='multipart/form-data'>
<input type='file' name='firmware' accept='.bin'><br><br>
<input type='submit' value='Upload Firmware' class='btn'>
</form>
<p style='margin-top:40px;font-size:12px;color:#666'>
IP: <script>document.write(location.hostname)</script> | Port: 8080
</p>
</body></html>
)rawliteral";

void setup() {
    Serial.begin(115200);
    Serial.println("\n[bootstrap] starting");

    WiFiManager wm;
    wm.setConnectTimeout(30);
    wm.setConfigPortalTimeout(180);

    if (!wm.autoConnect("CalendarDisplay")) {
        Serial.println("[bootstrap] WiFi failed - restarting");
        ESP.restart();
    }

    Serial.printf("[bootstrap] connected, IP: %s\n", WiFi.localIP().toString().c_str());

    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", PAGE);
    });

    server.on("/update", HTTP_POST, []() {
        bool ok = !Update.hasError();
        if (ok) {
            server.send(200, "text/html",
                "<html><body style='background:#222;color:#eee;font-family:sans-serif;text-align:center;padding-top:80px'>"
                "<h2>Firmware updated!</h2><p>Rebooting...</p>"
                "<script>setTimeout(function r(){fetch('/').then(()=>location.href='/').catch(()=>setTimeout(r,2000))},5000)</script>"
                "</body></html>");
        } else {
            server.send(200, "text/html",
                "<html><body style='background:#222;color:#eee;font-family:sans-serif;text-align:center;padding-top:80px'>"
                "<h2 style='color:#d33'>Update failed!</h2><p><a href='/' style='color:#88f'>Try again</a></p>"
                "</body></html>");
        }
        delay(500);
        if (ok) ESP.restart();
    }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            uint32_t maxSize = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            Update.begin(maxSize);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            Update.write(upload.buf, upload.currentSize);
        } else if (upload.status == UPLOAD_FILE_END) {
            Update.end(true);
        }
    });

    server.begin();
    Serial.println("[bootstrap] OTA ready on port 8080");
}

void loop() {
    server.handleClient();
}

#endif
