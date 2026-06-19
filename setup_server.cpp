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
    refresh: document.getElementById('refresh').value
  };
  for (const [k, v] of Object.entries(data)) {
    const i = document.createElement('input');
    i.type = 'hidden'; i.name = k; i.value = v;
    f.appendChild(i);
  }
  document.body.appendChild(f);
  f.submit();
}
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

    server.on("/",     HTTP_GET,  handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

    Serial.println("[setup] entering loop"); Serial.flush();
    while (true) {
        server.handleClient();
        delay(2);
    }
}
