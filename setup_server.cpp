#include "setup_server.h"
#include "config.h"
#include "storage.h"
#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

static WebServer server(80);

// ─── HTML pages stored in flash ───────────────────────────────────────────────

static const char SETUP_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TinyRadar Setup</title>
<style>
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    background: #0f172a; color: #e2e8f0;
    min-height: 100vh; display: flex; align-items: center; justify-content: center;
    padding: 16px;
  }
  .card {
    background: #1e293b; border-radius: 12px; padding: 28px;
    width: 100%; max-width: 420px; box-shadow: 0 4px 24px rgba(0,0,0,0.4);
  }
  h1 { font-size: 1.4rem; color: #38bdf8; margin-bottom: 4px; }
  .sub { font-size: 0.8rem; color: #64748b; margin-bottom: 24px; }
  section { margin-bottom: 20px; }
  h2 { font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.08em;
       color: #94a3b8; margin-bottom: 10px; }
  label { display: block; font-size: 0.85rem; color: #cbd5e1; margin-bottom: 4px; }
  input, select {
    width: 100%; padding: 9px 12px; background: #0f172a; border: 1px solid #334155;
    border-radius: 6px; color: #e2e8f0; font-size: 0.9rem; outline: none;
    transition: border-color 0.15s;
  }
  input:focus, select:focus { border-color: #38bdf8; }
  .field { margin-bottom: 12px; }
  .hint { font-size: 0.75rem; color: #64748b; margin-top: 3px; }
  .zip-row { display: flex; gap: 8px; align-items: flex-end; }
  .zip-row input { flex: 1; }
  .zip-btn {
    padding: 9px 14px; background: #1e40af; border: none; border-radius: 6px;
    color: #fff; cursor: pointer; font-size: 0.85rem; white-space: nowrap;
    transition: background 0.15s;
  }
  .zip-btn:hover { background: #2563eb; }
  #zipMsg { font-size: 0.78rem; margin-top: 4px; min-height: 18px; }
  .ok { color: #4ade80; }
  .err { color: #f87171; }
  .coords { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
  .submit {
    width: 100%; padding: 11px; background: #0284c7; border: none; border-radius: 8px;
    color: #fff; font-size: 1rem; font-weight: 600; cursor: pointer;
    margin-top: 8px; transition: background 0.15s;
  }
  .submit:hover { background: #0369a1; }
  .submit:disabled { background: #334155; cursor: not-allowed; }
  hr { border: none; border-top: 1px solid #1e3a5f; margin: 20px 0; }
</style>
</head>
<body>
<div class="card">
  <h1>&#x1F4E1; TinyRadar</h1>
  <p class="sub">Configure your device once — it will remember these settings.</p>

  <section>
    <h2>Home WiFi</h2>
    <div class="field">
      <label for="ssid">Network name (SSID)</label>
      <input type="text" id="ssid" autocomplete="off" placeholder="Your home WiFi">
    </div>
    <div class="field">
      <label for="pass">Password</label>
      <input type="password" id="pass" autocomplete="current-password" placeholder="WiFi password">
    </div>
  </section>

  <hr>

  <section>
    <h2>Location</h2>
    <div class="field">
      <label>US ZIP code</label>
      <div class="zip-row">
        <input type="text" id="zip" inputmode="numeric" maxlength="5" placeholder="e.g. 90210">
        <button class="zip-btn" onclick="lookupZip()">Look up</button>
      </div>
      <div id="zipMsg"></div>
    </div>
    <div class="coords">
      <div class="field">
        <label for="lat">Latitude</label>
        <input type="number" id="lat" step="0.0001" placeholder="34.0195">
      </div>
      <div class="field">
        <label for="lon">Longitude</label>
        <input type="number" id="lon" step="0.0001" placeholder="-118.4912">
      </div>
    </div>
    <p class="hint">Enter coordinates directly, or use the ZIP lookup above (requires internet on your phone/computer).</p>
  </section>

  <hr>

  <section>
    <h2>Refresh Rate</h2>
    <div class="field">
      <label for="refresh">Update radar every</label>
      <select id="refresh">
        <option value="5">5 minutes</option>
        <option value="10">10 minutes</option>
        <option value="15" selected>15 minutes</option>
        <option value="30">30 minutes</option>
        <option value="60">60 minutes</option>
      </select>
    </div>
  </section>

  <hr>

  <section>
    <h2>Time Zone</h2>
    <div class="field">
      <label for="tz">Time zone</label>
      <select id="tz">
        <optgroup label="North America">
          <option value="NST3:30NDT,M3.2.0/0:01,M11.1.0/0:01">Newfoundland (UTC−3:30/−2:30)</option>
          <option value="AST4ADT,M3.2.0,M11.1.0">Atlantic (UTC−4/−3)</option>
          <option value="EST5EDT,M3.2.0,M11.1.0">Eastern (UTC−5/−4)</option>
          <option value="CST6CDT,M3.2.0,M11.1.0" selected>Central (UTC−6/−5)</option>
          <option value="MST7MDT,M3.2.0,M11.1.0">Mountain (UTC−7/−6)</option>
          <option value="MST7">Mountain – Arizona (UTC−7, no DST)</option>
          <option value="PST8PDT,M3.2.0,M11.1.0">Pacific (UTC−8/−7)</option>
          <option value="AKST9AKDT,M3.2.0,M11.1.0">Alaska (UTC−9/−8)</option>
          <option value="HST10">Hawaii (UTC−10, no DST)</option>
        </optgroup>
        <optgroup label="South America">
          <option value="COT5">Colombia (UTC−5)</option>
          <option value="PET5">Peru (UTC−5)</option>
          <option value="VET4:30">Venezuela (UTC−4:30)</option>
          <option value="BOT4">Bolivia (UTC−4)</option>
          <option value="CLT4CLST,M10.2.6/24,M3.2.6/24">Chile (UTC−4/−3)</option>
          <option value="ART3">Argentina (UTC−3)</option>
          <option value="BRT3BRST,M10.3.0/0,M2.3.0/0">Brazil/Sao Paulo (UTC−3/−2)</option>
        </optgroup>
        <optgroup label="Europe &amp; Africa">
          <option value="UTC0">UTC (UTC+0)</option>
          <option value="GMT0BST,M3.5.0/1,M10.5.0">London (UTC+0/+1)</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Central Europe (UTC+1/+2)</option>
          <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Eastern Europe (UTC+2/+3)</option>
          <option value="MSK-3">Moscow (UTC+3)</option>
          <option value="EAT-3">East Africa (UTC+3)</option>
          <option value="SAST-2">South Africa (UTC+2)</option>
        </optgroup>
        <optgroup label="Asia &amp; Middle East">
          <option value="GST-4">Gulf / UAE (UTC+4)</option>
          <option value="AFT-4:30">Afghanistan (UTC+4:30)</option>
          <option value="PKT-5">Pakistan (UTC+5)</option>
          <option value="IST-5:30">India (UTC+5:30)</option>
          <option value="NPT-5:45">Nepal (UTC+5:45)</option>
          <option value="BDT-6">Bangladesh (UTC+6)</option>
          <option value="MMT-6:30">Myanmar (UTC+6:30)</option>
          <option value="ICT-7">SE Asia / Bangkok (UTC+7)</option>
          <option value="CST-8">China / Singapore (UTC+8)</option>
          <option value="JST-9">Japan / Korea (UTC+9)</option>
        </optgroup>
        <optgroup label="Australia &amp; Pacific">
          <option value="ACST-9:30">Australia/Darwin (UTC+9:30, no DST)</option>
          <option value="ACST-9:30ACDT,M10.1.0,M4.1.0/3">Australia/Adelaide (UTC+9:30/+10:30)</option>
          <option value="AEST-10">Australia/Brisbane (UTC+10, no DST)</option>
          <option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Australia/Sydney (UTC+10/+11)</option>
          <option value="NZST-12NZDT,M9.5.0,M4.1.0/3">New Zealand (UTC+12/+13)</option>
        </optgroup>
      </select>
      <p class="hint">DST transitions are handled automatically.</p>
    </div>
  </section>

  <button class="submit" onclick="save()">Save &amp; Start Radar</button>
</div>

<script>
function lookupZip() {
  const zip = document.getElementById('zip').value.trim();
  const msg = document.getElementById('zipMsg');
  if (!/^\d{5}$/.test(zip)) { msg.className='err'; msg.textContent='Enter a 5-digit ZIP code.'; return; }
  msg.className=''; msg.textContent='Looking up…';
  fetch('https://api.zippopotam.us/us/' + zip)
    .then(r => { if (!r.ok) throw new Error('not found'); return r.json(); })
    .then(d => {
      const p = d.places[0];
      document.getElementById('lat').value = parseFloat(p.latitude).toFixed(4);
      document.getElementById('lon').value = parseFloat(p.longitude).toFixed(4);
      msg.className = 'ok';
      msg.textContent = '✓ ' + p['place name'] + ', ' + p['state abbreviation'];
    })
    .catch(() => { msg.className='err'; msg.textContent='ZIP not found — enter coordinates manually.'; });
}

function save() {
  const ssid = document.getElementById('ssid').value.trim();
  const lat  = parseFloat(document.getElementById('lat').value);
  const lon  = parseFloat(document.getElementById('lon').value);
  if (!ssid)           { alert('Please enter your WiFi network name.'); return; }
  if (isNaN(lat) || isNaN(lon)) { alert('Please enter a valid location.'); return; }
  if (lat < -90 || lat > 90)   { alert('Latitude must be between -90 and 90.'); return; }
  if (lon < -180 || lon > 180) { alert('Longitude must be between -180 and 180.'); return; }

  const f = document.createElement('form');
  f.method = 'POST'; f.action = '/save';
  const data = {
    ssid, pass: document.getElementById('pass').value,
    lat: lat.toFixed(6), lon: lon.toFixed(6),
    refresh: document.getElementById('refresh').value,
    tz: document.getElementById('tz').value
  };
  for (const [k, v] of Object.entries(data)) {
    const i = document.createElement('input');
    i.type = 'hidden'; i.name = k; i.value = v;
    f.appendChild(i);
  }
  document.body.appendChild(f);
  f.submit();
}

async function loadSaved() {
  try {
    const r = await fetch('/config');
    if (!r.ok) return;
    const d = await r.json();
    if (d.ssid)               document.getElementById('ssid').value    = d.ssid;
    if (d.pass)               document.getElementById('pass').value    = d.pass;
    if (d.lat  != null)       document.getElementById('lat').value     = parseFloat(d.lat).toFixed(4);
    if (d.lon  != null)       document.getElementById('lon').value     = parseFloat(d.lon).toFixed(4);
    if (d.refresh)            document.getElementById('refresh').value = d.refresh;
    if (d.tz)                 document.getElementById('tz').value      = d.tz;
  } catch(e) {}
}
document.addEventListener('DOMContentLoaded', loadSaved);
</script>
</body>
</html>
)HTML";

static const char SAVED_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TinyRadar — Saved</title>
<style>
  body {
    font-family: -apple-system, sans-serif; background: #0f172a; color: #e2e8f0;
    min-height: 100vh; display: flex; align-items: center; justify-content: center;
    text-align: center; padding: 24px;
  }
  h1 { color: #4ade80; font-size: 1.5rem; margin-bottom: 12px; }
  p  { color: #94a3b8; }
</style>
</head>
<body>
  <div>
    <h1>&#x2713; Saved!</h1>
    <p>TinyRadar is restarting.<br>It will connect to your WiFi and display the radar image.</p>
  </div>
</body>
</html>
)HTML";

// ─── Setup screen ─────────────────────────────────────────────────────────────

static void showSetupScreen() {
    pinMode(LCD_BL_PIN, OUTPUT);
    digitalWrite(LCD_BL_PIN, HIGH);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    const uint16_t wifiColor = tft.color565(56, 189, 248);  // #38bdf8
    const int cx   = DISPLAY_WIDTH / 2;
    const int dotY = 130;

    // Three arcs clockwise from 305°→55° (sweeps through 0°/top = upward-facing)
    tft.drawArc(cx, dotY, 75, 69, 305, 55, wifiColor, TFT_BLACK);
    tft.drawArc(cx, dotY, 52, 46, 305, 55, wifiColor, TFT_BLACK);
    tft.drawArc(cx, dotY, 29, 23, 305, 55, wifiColor, TFT_BLACK);

    // Dot at arc origin
    tft.fillCircle(cx, dotY, 8, wifiColor);

    // Labels
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("TinyRadar", cx, 190, 2);
    tft.setTextColor(tft.color565(148, 163, 184), TFT_BLACK);
    tft.drawString("Connect to: " AP_SSID, cx, 210, 1);
}

// ─── Route handlers ───────────────────────────────────────────────────────────

// Build a JSON-safe quoted string (handles " and \ in WiFi passwords / TZ strings).
static String jsonStr(const char* s) {
    String out = "\"";
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') out += '\\';
        out += *s;
    }
    out += "\"";
    return out;
}

// Returns the saved config as JSON so the setup page can pre-populate its form.
// Omits the password if there is none; includes it when present since this
// endpoint is only reachable over the device's own local AP (no internet path).
static void handleConfig() {
    AppConfig saved = {};
    if (!loadConfig(saved)) {
        server.send(204, "text/plain", "");
        return;
    }
    String json = "{";
    json += "\"ssid\":"    + jsonStr(saved.wifiSSID)   + ",";
    json += "\"pass\":"    + jsonStr(saved.wifiPass)   + ",";
    json += "\"lat\":"     + String(saved.latitude, 6) + ",";
    json += "\"lon\":"     + String(saved.longitude, 6)+ ",";
    json += "\"refresh\":" + String(saved.refreshMinutes) + ",";
    json += "\"tz\":"      + jsonStr(saved.tzPosix);
    json += "}";
    server.send(200, "application/json", json);
}

static void handleRoot() {
    server.send_P(200, "text/html", SETUP_HTML);
}

static void handleSave() {
    if (!server.hasArg("ssid") || !server.hasArg("lat") || !server.hasArg("lon")) {
        server.send(400, "text/plain", "Missing fields");
        return;
    }

    AppConfig cfg = {};
    server.arg("ssid").toCharArray(cfg.wifiSSID, sizeof(cfg.wifiSSID));
    server.arg("pass").toCharArray(cfg.wifiPass, sizeof(cfg.wifiPass));
    cfg.latitude       = server.arg("lat").toFloat();
    cfg.longitude      = server.arg("lon").toFloat();
    cfg.refreshMinutes = constrain(server.arg("refresh").toInt(), REFRESH_MIN, REFRESH_MAX);
    server.arg("tz").toCharArray(cfg.tzPosix, sizeof(cfg.tzPosix));

    saveConfig(cfg);
    server.send_P(200, "text/html", SAVED_HTML);

    delay(1500);
    ESP.restart();
}

static void handleNotFound() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

// ─── Public entry point ───────────────────────────────────────────────────────

void startSetupServer() {
    // WiFi must claim its DMA channels before TFT_eSPI initialises SPI2
    Serial.println("[setup] calling softAP"); Serial.flush();
    WiFi.persistent(false);
    bool apOK;
    if (strlen(AP_PASSWORD) > 0) {
        apOK = WiFi.softAP(AP_SSID, AP_PASSWORD);
    } else {
        apOK = WiFi.softAP(AP_SSID);
    }
    Serial.printf("[setup] softAP returned: %d\n", apOK ? 1 : 0); Serial.flush();
    delay(500);
    Serial.printf("[setup] AP IP: %s\n", WiFi.softAPIP().toString().c_str()); Serial.flush();

    // Display init after WiFi is up
    Serial.println("[setup] showSetupScreen"); Serial.flush();
    showSetupScreen();

    server.on("/",       HTTP_GET,  handleRoot);
    server.on("/config", HTTP_GET,  handleConfig);
    server.on("/save",   HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

    Serial.println("[setup] entering loop"); Serial.flush();
    while (true) {
        server.handleClient();
        delay(2);
    }
}
