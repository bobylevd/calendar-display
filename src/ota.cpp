#include "ota.h"
#include "display.h"
#include "config.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
static ESP8266WebServer otaServer(8080);

static const char OTA_PAGE[] PROGMEM = R"rawliteral(
<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
body{background:#222;color:#eee;font-family:sans-serif;max-width:500px;margin:0 auto;padding:16px}
h2{text-align:center;color:#fff}
h3{color:#aaa;border-bottom:1px solid #444;padding-bottom:6px;margin-top:24px}
input[type=text],input[type=range]{width:100%;box-sizing:border-box}
input[type=text]{background:#333;color:#fff;border:1px solid #555;padding:8px;border-radius:4px;margin:4px 0}
label{display:block;margin-top:10px;color:#ccc;font-size:14px}
.btn{background:#5865F2;color:#fff;border:none;padding:10px 20px;border-radius:4px;cursor:pointer;margin:8px 4px}
.btn-red{background:#d33}
.small{font-size:12px;color:#888}
hr{border:none;border-top:1px solid #444;margin:20px 0}
#fl a{color:#88f}
</style></head><body>
<h2>CalendarDisplay</h2>

<h3>Brightness</h3>
<input type='range' id='brt' min='5' max='100'>
<span id='bv'></span>%

<h3>Theme</h3>
<select id='theme' style='width:100%;background:#333;color:#fff;border:1px solid #555;padding:8px;border-radius:4px'>
<option value='0'>Dark</option>
<option value='1'>Light</option>
</select>

<h3>Settings</h3>
<label>Apps Script URL (with ?key=)</label>
<input type='text' id='url' placeholder='https://script.google.com/.../exec?key=...'>
<label>Timezone</label>
<select id='tz' style='width:100%;background:#333;color:#fff;border:1px solid #555;padding:8px;border-radius:4px;margin:4px 0'>
<option value='GMT0'>London (GMT)</option>
<option value='CET-1CEST,M3.5.0/2,M10.5.0/3'>Berlin / Paris / Rome (CET)</option>
<option value='EET-2EEST,M3.5.0/3,M10.5.0/4'>Helsinki / Athens (EET)</option>
<option value='MSK-3'>Moscow (MSK)</option>
<option value='IST-5:30'>Mumbai (IST)</option>
<option value='CST-8'>Shanghai / Singapore (CST)</option>
<option value='JST-9'>Tokyo (JST)</option>
<option value='KST-9'>Seoul (KST)</option>
<option value='AEST-10AEDT,M10.1.0,M4.1.0/3'>Sydney (AEST)</option>
<option value='NZST-12NZDT,M9.5.0,M4.1.0/3'>Auckland (NZST)</option>
<option value='EST5EDT,M3.2.0,M11.1.0'>New York (EST)</option>
<option value='CST6CDT,M3.2.0,M11.1.0'>Chicago (CST)</option>
<option value='MST7MDT,M3.2.0,M11.1.0'>Denver (MST)</option>
<option value='PST8PDT,M3.2.0,M11.1.0'>Los Angeles (PST)</option>
<option value='<-03>3'>Sao Paulo (BRT)</option>
</select>
<label><input type='checkbox' id='clock'> Show clock (3 events)</label>
<label><input type='checkbox' id='flash'> Flash screen on event start</label>
<label>Flash count: <span id='fv'></span></label>
<input type='range' id='fcnt' min='1' max='20'>
<br><button class='btn' onclick='saveSettings()'>Save Settings</button> <span id='ss' style='color:#5b5;font-size:14px'></span>
<button class='btn btn-red' onclick="if(confirm('Reboot?'))fetch('/reboot')">Reboot</button>

<h3>WiFi</h3>
<div id='wifi'>Connected</div>
<button class='btn btn-red' onclick="if(confirm('Reset WiFi and reboot?'))fetch('/resetwifi')">Reset WiFi</button>

<h3>Firmware Update</h3>
<form method='POST' action='/update' enctype='multipart/form-data'>
<input type='file' name='firmware' accept='.bin'><br>
<input type='submit' value='Upload Firmware' class='btn'>
</form>

<h3>File Upload (LittleFS)</h3>
<form method='POST' action='/upload' enctype='multipart/form-data'>
<input type='file' name='file' multiple><br>
<input type='submit' value='Upload Files' class='btn'>
</form>

<h3>Files</h3>
<div id='fl'>Loading...</div>

<script>
var bs=document.getElementById('brt'),bv=document.getElementById('bv');
var fc=document.getElementById('fcnt'),fv=document.getElementById('fv');
fetch('/api/config').then(r=>r.json()).then(c=>{
  bs.value=c.brightness;bv.textContent=c.brightness;
  document.getElementById('url').value=c.url||'';
  document.getElementById('tz').value=c.tz||'';
  document.getElementById('clock').checked=c.showClock!==false;
  document.getElementById('flash').checked=c.flash;
  fc.value=c.flashCount||5;fv.textContent=c.flashCount||5;
  document.getElementById('theme').value=c.theme||0;
  document.getElementById('wifi').innerHTML='SSID: '+c.ssid+'<br>IP: '+c.ip;
});
bs.oninput=function(){bv.textContent=bs.value;fetch('/brightness?v='+bs.value+'&save=0')};
bs.onchange=function(){fetch('/brightness?v='+bs.value+'&save=1')};
fc.oninput=function(){fv.textContent=fc.value};
function saveSettings(){
  var d={url:document.getElementById('url').value,
    tz:document.getElementById('tz').value,
    showClock:document.getElementById('clock').checked,
    flash:document.getElementById('flash').checked,
    flashCount:parseInt(fc.value),
    theme:parseInt(document.getElementById('theme').value)};
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(d)}).then(function(r){
    var s=document.getElementById('ss');
    if(r.ok){s.textContent='Saved!';s.style.color='#5b5'}else{s.textContent='Error!';s.style.color='#d33'}
    setTimeout(function(){s.textContent=''},2000)});
}
fetch('/files').then(r=>r.text()).then(t=>document.getElementById('fl').innerHTML=t);
</script>
</body></html>
)rawliteral";

static File uploadFile;

void otaInit() {
    LittleFS.begin();

    otaServer.on("/", HTTP_GET, []() {
        otaServer.send_P(200, "text/html", OTA_PAGE);
    });

    // Config API - GET
    otaServer.on("/api/config", HTTP_GET, []() {
        JsonDocument doc;
        doc["url"] = getScriptUrl();
        doc["tz"] = getTimezone();
        doc["showClock"] = getShowClock();
        doc["flash"] = isFlashAlertEnabled();
        doc["flashCount"] = getFlashCount();
        doc["brightness"] = getBrightness();
        doc["theme"] = getTheme();
        doc["ssid"] = WiFi.SSID();
        doc["ip"] = WiFi.localIP().toString();
        String json;
        serializeJson(doc, json);
        otaServer.send(200, "application/json", json);
    });

    // Config API - POST
    otaServer.on("/api/config", HTTP_POST, []() {
        JsonDocument doc;
        if (deserializeJson(doc, otaServer.arg("plain"))) {
            otaServer.send(400, "text/plain", "Invalid JSON");
            return;
        }

        if (doc["url"].is<const char*>()) setScriptUrl(doc["url"].as<String>());
        if (doc["tz"].is<const char*>()) setTimezone(doc["tz"].as<String>());
        if (doc["showClock"].is<bool>()) setShowClock(doc["showClock"]);
        if (doc["flash"].is<bool>()) setFlashAlert(doc["flash"]);
        if (doc["flashCount"].is<int>()) setFlashCount(doc["flashCount"]);
        if (doc["theme"].is<int>()) setTheme(doc["theme"]);
        saveConfig();

        setenv("TZ", getTimezone(), 1);
        tzset();

        otaServer.send(200, "text/plain", "Saved!");
    });

    // Brightness API
    otaServer.on("/brightness", HTTP_GET, []() {
        if (otaServer.hasArg("v")) {
            int val = otaServer.arg("v").toInt();
            setBrightness(val);
            displaySetBrightness(getBrightness());
            if (otaServer.arg("save") == "1") saveConfig();
        }
        otaServer.send(200, "text/plain", String(getBrightness()));
    });

    // Reboot
    otaServer.on("/reboot", HTTP_GET, []() {
        otaServer.send(200, "text/plain", "Rebooting...");
        delay(500);
        ESP.restart();
    });

    // Reset WiFi
    otaServer.on("/resetwifi", HTTP_GET, []() {
        otaServer.send(200, "text/plain", "WiFi reset. Rebooting...");
        delay(500);
        WiFi.disconnect(true);
        ESP.restart();
    });

    // Firmware OTA
    otaServer.on("/update", HTTP_POST, []() {
        bool ok = !Update.hasError();
        if (ok) {
            otaServer.send(200, "text/html",
                "<html><body style='background:#222;color:#eee;font-family:sans-serif;text-align:center;padding-top:80px'>"
                "<h2>Firmware updated!</h2><p>Rebooting... will redirect when ready.</p>"
                "<script>setTimeout(function r(){fetch('/').then(()=>location.href='/').catch(()=>setTimeout(r,2000))},5000)</script>"
                "</body></html>");
        } else {
            otaServer.send(200, "text/html",
                "<html><body style='background:#222;color:#eee;font-family:sans-serif;text-align:center;padding-top:80px'>"
                "<h2 style='color:#d33'>Update failed!</h2><p><a href='/' style='color:#88f'>Back to settings</a></p>"
                "</body></html>");
        }
        delay(500);
        if (ok) ESP.restart();
    }, []() {
        HTTPUpload& upload = otaServer.upload();
        if (upload.status == UPLOAD_FILE_START) {
            uint32_t maxSize = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            Update.begin(maxSize);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            Update.write(upload.buf, upload.currentSize);
        } else if (upload.status == UPLOAD_FILE_END) {
            Update.end(true);
        }
    });

    // File upload to LittleFS
    otaServer.on("/upload", HTTP_POST, []() {
        otaServer.sendHeader("Location", "/");
        otaServer.send(303);
    }, []() {
        HTTPUpload& upload = otaServer.upload();
        if (upload.status == UPLOAD_FILE_START) {
            String path = "/" + upload.filename;
            uploadFile = LittleFS.open(path, "w");
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
        } else if (upload.status == UPLOAD_FILE_END) {
            if (uploadFile) uploadFile.close();
        }
    });

    // List files
    otaServer.on("/files", HTTP_GET, []() {
        String html;
        Dir dir = LittleFS.openDir("/");
        while (dir.next()) {
            html += dir.fileName() + " (" + String(dir.fileSize()) + "b)";
            html += " <a href='/delete?f=/" + dir.fileName() + "'>del</a><br>";
        }
        if (html.isEmpty()) html = "No files";
        otaServer.send(200, "text/html", html);
    });

    // Delete file
    otaServer.on("/delete", HTTP_GET, []() {
        String path = otaServer.arg("f");
        if (path.length() > 0 && LittleFS.exists(path)) {
            LittleFS.remove(path);
            otaServer.send(200, "text/plain", "Deleted " + path);
        } else {
            otaServer.send(404, "text/plain", "Not found");
        }
    });

    otaServer.begin();
    Serial.printf("OTA ready on port 8080\n");
}

void otaHandle() {
    otaServer.handleClient();
}
