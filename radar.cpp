#include "radar.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PNGdec.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <time.h>

// ─── Module-level objects ─────────────────────────────────────────────────────

extern TFT_eSPI tft;
static PNG png;

// Download buffer for one compressed PNG tile (~64 KB is enough for 256-px tiles)
static const size_t TILE_BUF_SIZE = 65536;
static uint8_t* s_tileBuf = nullptr;

// Crop region in stitch-space coordinates (set before each tile decode)
struct RenderCtx {
    int tileOffX, tileOffY;   // tile's top-left in stitch space
    int cropX,    cropY;      // 240×240 crop origin in stitch space
    int gridOriginTileX;      // tile X of top-left grid tile (for geo projection)
    int gridOriginTileY;      // tile Y of top-left grid tile
};
static RenderCtx s_ctx;

// ─── Display helpers ──────────────────────────────────────────────────────────

static void drawCenterDot() {
    tft.fillCircle(DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2, 3,
                   tft.color565(210, 210, 210));
}

// Small cloud glyph shown in the bottom-right quadrant while tiles are
// downloading. Not explicitly erased: the bottom-right map tile is the last
// one decoded each pass, so its incoming pixel rows overwrite the glyph
// naturally right as loading finishes.
static void drawLoadingIcon() {
    const int cx = 188, cy = 190;
    uint16_t c = tft.color565(180, 180, 180);
    tft.fillCircle(cx - 5, cy + 1, 4, c);
    tft.fillCircle(cx + 4, cy + 1, 5, c);
    tft.fillCircle(cx,     cy - 3, 5, c);
    tft.fillRect(cx - 9, cy - 1, 18, 6, c);
}

static void drawRadarTime(uint32_t radarUnixTime, const char* tzPosix, int tileZoom) {
    setenv("TZ", tzPosix, 1);
    tzset();
    time_t t = (time_t)radarUnixTime;
    struct tm tmLocal;
    localtime_r(&t, &tmLocal);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d (%d)", tmLocal.tm_hour, tmLocal.tm_min, tileZoom);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(tft.color565(200, 200, 200), TFT_BLACK);
    tft.drawString(buf, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT - 18, 2);
}

// ─── Mercator tile math ───────────────────────────────────────────────────────

static int lon2tileX(double lon, int z) {
    return (int)floor((lon + 180.0) / 360.0 * (1 << z));
}

static int lat2tileY(double lat, int z) {
    double rad = lat * DEG_TO_RAD;
    return (int)floor((1.0 - log(tan(rad) + 1.0 / cos(rad)) / M_PI) / 2.0 * (1 << z));
}

static int lon2subPixel(double lon, int z) {
    double tx = (lon + 180.0) / 360.0 * (1 << z);
    return (int)((tx - floor(tx)) * TILE_SIZE_PX);
}

static int lat2subPixel(double lat, int z) {
    double rad = lat * DEG_TO_RAD;
    double ty  = (1.0 - log(tan(rad) + 1.0 / cos(rad)) / M_PI) / 2.0 * (1 << z);
    return (int)((ty - floor(ty)) * TILE_SIZE_PX);
}

// ─── PNG draw callbacks ───────────────────────────────────────────────────────
// Each callback is called once per row during decode. Only pixels that fall
// inside the 240×240 crop region are written to the display.

static int mapRowCallback(PNGDRAW* pDraw) {
    int stitchRow  = s_ctx.tileOffY + pDraw->y;
    int displayRow = stitchRow - s_ctx.cropY;
    if (displayRow < 0 || displayRow >= DISPLAY_HEIGHT) return 1;

    int overlapStart = max(s_ctx.tileOffX,                s_ctx.cropX);
    int overlapEnd   = min(s_ctx.tileOffX + TILE_SIZE_PX, s_ctx.cropX + DISPLAY_WIDTH);
    if (overlapStart >= overlapEnd) return 1;

    int tileCol    = overlapStart - s_ctx.tileOffX;
    int displayCol = overlapStart - s_ctx.cropX;
    int width      = overlapEnd - overlapStart;

    uint16_t line[TILE_SIZE_PX];
    png.getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0x0000);
    tft.pushImage(displayCol, displayRow, width, 1, &line[tileCol]);
    return 1;
}

// Radar tiles are RGBA (iPixelType == 6). Only pixels with alpha >= 16 are
// drawn, letting the map layer show through where there is no precipitation.
static int radarRowCallback(PNGDRAW* pDraw) {
    int stitchRow  = s_ctx.tileOffY + pDraw->y;
    int displayRow = stitchRow - s_ctx.cropY;
    if (displayRow < 0 || displayRow >= DISPLAY_HEIGHT) return 1;

    int overlapStart = max(s_ctx.tileOffX,                s_ctx.cropX);
    int overlapEnd   = min(s_ctx.tileOffX + TILE_SIZE_PX, s_ctx.cropX + DISPLAY_WIDTH);
    if (overlapStart >= overlapEnd) return 1;

    int tileCol    = overlapStart - s_ctx.tileOffX;
    int displayCol = overlapStart - s_ctx.cropX;
    int width      = overlapEnd - overlapStart;

    if (pDraw->iPixelType != 6) {
        // Non-RGBA tile (unexpected): fall back to opaque draw
        uint16_t line[TILE_SIZE_PX];
        png.getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0x0000);
        tft.pushImage(displayCol, displayRow, width, 1, &line[tileCol]);
        return 1;
    }

    const uint8_t* rgba = pDraw->pPixels + tileCol * 4;
    uint16_t span[TILE_SIZE_PX];
    int spanStart = -1;

    for (int i = 0; i < width; i++, rgba += 4) {
        if (rgba[3] >= 16) {
            uint16_t c = ((uint16_t)(rgba[0] & 0xF8) << 8) |
                         ((uint16_t)(rgba[1] & 0xFC) << 3) |
                         (rgba[2] >> 3);
            span[i] = (c >> 8) | (c << 8);  // little-endian for TFT_eSPI
            if (spanStart < 0) spanStart = i;
        } else {
            if (spanStart >= 0) {
                tft.pushImage(displayCol + spanStart, displayRow,
                              i - spanStart, 1, span + spanStart);
                spanStart = -1;
            }
        }
    }
    if (spanStart >= 0) {
        tft.pushImage(displayCol + spanStart, displayRow,
                      width - spanStart, 1, span + spanStart);
    }
    return 1;
}

// ─── HTTP helpers ─────────────────────────────────────────────────────────────

static int httpGetBinary(const String& url) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(HTTP_TIMEOUT_MS);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[radar] HTTP %d  %s\n", code, url.c_str());
        http.end();
        return 0;
    }

    WiFiClient*   stream     = http.getStreamPtr();
    int           total      = 0;
    int           contentLen = http.getSize();
    unsigned long deadline   = millis() + HTTP_TIMEOUT_MS;

    while ((http.connected() || stream->available()) && millis() < deadline) {
        int avail = stream->available();
        if (avail > 0) {
            int toRead = min(avail, (int)(TILE_BUF_SIZE - total));
            if (toRead <= 0) break;
            total   += stream->readBytes(s_tileBuf + total, toRead);
            deadline = millis() + 5000;
        }
        if (contentLen > 0 && total >= contentLen) break;
        delay(1);
    }

    http.end();
    return total;
}

// ─── RainViewer API ───────────────────────────────────────────────────────────

struct RadarInfo { String host, path; uint32_t time; };

static bool fetchRadarInfo(RadarInfo& out) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, RV_API_URL);
    http.setTimeout(HTTP_TIMEOUT_MS);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[radar] API HTTP %d\n", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[radar] JSON parse failed");
        return false;
    }

    JsonArray past = doc["radar"]["past"];
    if (past.size() == 0) { Serial.println("[radar] no past frames"); return false; }

    out.host = doc["host"].as<String>();
    out.path = past[past.size() - 1]["path"].as<String>();
    out.time = past[past.size() - 1]["time"].as<uint32_t>();
    Serial.printf("[radar] host=%s\n[radar] path=%s\n[radar] frame=%s%s\n",
                  out.host.c_str(), out.path.c_str(), out.host.c_str(), out.path.c_str());
    return true;
}

static String buildTileURL(const RadarInfo& info, int z, int x, int y) {
    return info.host + info.path
        + "/" + String(TILE_SIZE_PX)
        + "/" + String(z)
        + "/" + String(x)
        + "/" + String(y)
        + "/" + String(RV_TILE_COLOR)
        + "/" + RV_TILE_OPTIONS + ".png";
}

static String buildMapTileURL(int z, int x, int y) {
    return String("https://tiles.stadiamaps.com/tiles/")
        + STADIA_STYLE
        + "/" + String(z)
        + "/" + String(x)
        + "/" + String(y)
        + ".png?api_key=" + STADIA_API_KEY;
}

// ─── Tile fetch + decode ──────────────────────────────────────────────────────

static bool fetchAndDecodeTile(const String& url, int offX, int offY,
                               PNG_DRAW_CALLBACK* cb) {
    Serial.printf("[radar] tile x=%d y=%d  url=%s\n", offX / TILE_SIZE_PX, offY / TILE_SIZE_PX, url.c_str());

    int len = httpGetBinary(url);
    if (len == 0) return false;

    s_ctx.tileOffX = offX;
    s_ctx.tileOffY = offY;

    int rc = png.openRAM(s_tileBuf, len, cb);
    if (rc != PNG_SUCCESS) { Serial.printf("[radar] PNG open failed: %d\n", rc); return false; }

    rc = png.decode(nullptr, 0);
    png.close();

    if (rc != PNG_SUCCESS) { Serial.printf("[radar] PNG decode failed: %d\n", rc); return false; }
    return true;
}

// ─── WiFi ─────────────────────────────────────────────────────────────────────

static bool connectWiFi(const AppConfig& cfg) {
    if (WiFi.status() == WL_CONNECTED) {
        return true; // already connected — e.g. a re-render during the button linger window
    }
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.begin(cfg.wifiSSID, cfg.wifiPass);
    Serial.printf("[radar] connecting to SSID: '%s'\n", cfg.wifiSSID);
    Serial.print("[radar] WiFi connecting");
    for (int i = 0; i < 40; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf(" OK  IP %s\n", WiFi.localIP().toString().c_str());
            delay(1000);  // let routing/DNS settle before first HTTPS request
            return true;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println(" FAILED");
    return false;
}

// ─── Public entry point ───────────────────────────────────────────────────────

void disconnectWifi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

bool fetchAndDisplayRadar(const AppConfig& cfg, bool keepWifiConnected) {
    // Single tile download buffer from regular heap
    if (!s_tileBuf) {
        s_tileBuf = (uint8_t*)malloc(TILE_BUF_SIZE);
    }

    // WiFi before TFT — DMA constraint (same as setup_server's softAP path).
    // The existing radar image stays on screen during WiFi connect + API fetch.
    if (!connectWiFi(cfg)) {
        Serial.println("[radar] WiFi connect failed — leaving last frame on screen");
        return false;  // caller will enter setup mode (draws its own screen)
    }

    if (!s_tileBuf) {
        Serial.println("[radar] tile buffer alloc failed — leaving last frame on screen");
        return true;
    }

    RadarInfo radarInfo;
    if (!fetchRadarInfo(radarInfo)) {
        Serial.println("[radar] API error — leaving last frame on screen");
        if (!keepWifiConnected) WiFi.disconnect(true);
        return true;
    }

    // Tile grid + crop geometry
    int tileX  = lon2tileX(cfg.longitude, cfg.tileZoom);
    int tileY  = lat2tileY(cfg.latitude,  cfg.tileZoom);
    int subX   = lon2subPixel(cfg.longitude, cfg.tileZoom);
    int subY   = lat2subPixel(cfg.latitude,  cfg.tileZoom);

    int gridOriginX = (subX >= TILE_SIZE_PX / 2) ? tileX : tileX - 1;
    int gridOriginY = (subY >= TILE_SIZE_PX / 2) ? tileY : tileY - 1;

    int locPixX = (tileX - gridOriginX) * TILE_SIZE_PX + subX;
    int locPixY = (tileY - gridOriginY) * TILE_SIZE_PX + subY;

    s_ctx.cropX           = constrain(locPixX - DISPLAY_WIDTH  / 2, 0, STITCH_SIZE - DISPLAY_WIDTH);
    s_ctx.cropY           = constrain(locPixY - DISPLAY_HEIGHT / 2, 0, STITCH_SIZE - DISPLAY_HEIGHT);
    s_ctx.gridOriginTileX = gridOriginX;
    s_ctx.gridOriginTileY = gridOriginY;

    Serial.printf("[radar] crop (%d,%d)  loc (%d,%d)\n",
                  s_ctx.cropX, s_ctx.cropY, locPixX, locPixY);

    // Init display here — right before tiles start streaming — so the existing
    // image stays visible through all the network work above. Tiles overwrite
    // the display row-by-row with no blank clear in between.
    pinMode(LCD_BL_PIN, OUTPUT);
    digitalWrite(LCD_BL_PIN, HIGH);
    tft.init();
    tft.setRotation(0);
    drawLoadingIcon();

    // Pass 1: base map
    int mapOK = 0;
    for (int row = 0; row < TILE_GRID; row++) {
        for (int col = 0; col < TILE_GRID; col++) {
            String url = buildMapTileURL(cfg.tileZoom, gridOriginX + col, gridOriginY + row);
            if (fetchAndDecodeTile(url, col * TILE_SIZE_PX, row * TILE_SIZE_PX,
                                   mapRowCallback)) mapOK++;
        }
    }

    // Pass 2: radar overlay (alpha-composited over the map)
    int radarOK = 0;
    for (int row = 0; row < TILE_GRID; row++) {
        for (int col = 0; col < TILE_GRID; col++) {
            String url = buildTileURL(radarInfo, cfg.tileZoom,
                                      gridOriginX + col, gridOriginY + row);
            if (fetchAndDecodeTile(url, col * TILE_SIZE_PX, row * TILE_SIZE_PX,
                                   radarRowCallback)) radarOK++;
        }
    }

    if (!keepWifiConnected) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    if (mapOK == 0 && radarOK == 0) {
        Serial.println("[radar] no tile data — leaving last frame on screen");
    } else {
        drawCenterDot();
        drawRadarTime(radarInfo.time, cfg.tzPosix, cfg.tileZoom);
        Serial.printf("[radar] display updated (map=%d radar=%d)\n", mapOK, radarOK);
    }
    return true;
}
