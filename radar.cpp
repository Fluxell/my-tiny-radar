#include "radar.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PNGdec.h>
#include <TFT_eSPI.h>
#include <math.h>

// ─── Module-level objects ─────────────────────────────────────────────────────

extern TFT_eSPI tft;
static PNG png;

// Download buffer for one compressed PNG tile (~64 KB is enough for 256-px tiles)
static const size_t TILE_BUF_SIZE = 65536;
static uint8_t* s_tileBuf = nullptr;

// Crop region in stitch-space coordinates (set before each tile decode)
struct RenderCtx {
    int tileOffX, tileOffY; // tile's top-left in stitch space
    int cropX,    cropY;    // 240×240 crop origin in stitch space
};
static RenderCtx s_ctx;

// ─── Display helpers ──────────────────────────────────────────────────────────

static void showStatus(const char* msg) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(msg, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2, 2);
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

// ─── PNG draw callback ────────────────────────────────────────────────────────
// Called once per row during decode. Writes only the pixels that fall inside
// the 240×240 crop region directly to the display — no stitch buffer needed.

static int pngRowCallback(PNGDRAW* pDraw) {
    uint16_t line[TILE_SIZE_PX];
    png.getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0x0000);

    int stitchRow  = s_ctx.tileOffY + pDraw->y;
    int displayRow = stitchRow - s_ctx.cropY;
    if (displayRow < 0 || displayRow >= DISPLAY_HEIGHT) return 1;

    // Column overlap between this tile and the crop region
    int overlapStart = max(s_ctx.tileOffX,                  s_ctx.cropX);
    int overlapEnd   = min(s_ctx.tileOffX + TILE_SIZE_PX,   s_ctx.cropX + DISPLAY_WIDTH);
    if (overlapStart >= overlapEnd) return 1;

    int tileCol    = overlapStart - s_ctx.tileOffX;
    int displayCol = overlapStart - s_ctx.cropX;
    int width      = overlapEnd - overlapStart;

    tft.pushImage(displayCol, displayRow, width, 1, &line[tileCol]);
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

struct RadarInfo { String host, path; };

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
    Serial.printf("[radar] frame: %s%s\n", out.host.c_str(), out.path.c_str());
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

// ─── Tile fetch + decode ──────────────────────────────────────────────────────

static bool fetchAndDecodeTile(const String& url, int offX, int offY) {
    Serial.printf("[radar] tile x=%d y=%d\n", offX / TILE_SIZE_PX, offY / TILE_SIZE_PX);

    int len = httpGetBinary(url);
    if (len == 0) return false;

    s_ctx.tileOffX = offX;
    s_ctx.tileOffY = offY;

    int rc = png.openRAM(s_tileBuf, len, pngRowCallback);
    if (rc != PNG_SUCCESS) { Serial.printf("[radar] PNG open failed: %d\n", rc); return false; }

    rc = png.decode(nullptr, 0);
    png.close();

    if (rc != PNG_SUCCESS) { Serial.printf("[radar] PNG decode failed: %d\n", rc); return false; }
    return true;
}

// ─── WiFi ─────────────────────────────────────────────────────────────────────

static bool connectWiFi(const AppConfig& cfg) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSSID, cfg.wifiPass);
    Serial.print("[radar] WiFi connecting");
    for (int i = 0; i < 40; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf(" OK  IP %s\n", WiFi.localIP().toString().c_str());
            return true;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println(" FAILED");
    return false;
}

// ─── Public entry point ───────────────────────────────────────────────────────

void fetchAndDisplayRadar(const AppConfig& cfg) {
    pinMode(LCD_BL_PIN, OUTPUT);
    digitalWrite(LCD_BL_PIN, HIGH);
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    showStatus("Connecting...");

    // Single tile download buffer from regular heap
    if (!s_tileBuf) {
        s_tileBuf = (uint8_t*)malloc(TILE_BUF_SIZE);
    }
    if (!s_tileBuf) {
        showStatus("No memory");
        Serial.println("[radar] tile buffer alloc failed");
        return;
    }

    if (!connectWiFi(cfg)) { showStatus("WiFi failed"); return; }
    showStatus("Fetching radar...");

    RadarInfo radarInfo;
    if (!fetchRadarInfo(radarInfo)) {
        showStatus("API error");
        WiFi.disconnect(true);
        return;
    }

    // Tile grid + crop geometry
    int tileX  = lon2tileX(cfg.longitude, TILE_ZOOM);
    int tileY  = lat2tileY(cfg.latitude,  TILE_ZOOM);
    int subX   = lon2subPixel(cfg.longitude, TILE_ZOOM);
    int subY   = lat2subPixel(cfg.latitude,  TILE_ZOOM);

    int gridOriginX = (subX >= TILE_SIZE_PX / 2) ? tileX : tileX - 1;
    int gridOriginY = (subY >= TILE_SIZE_PX / 2) ? tileY : tileY - 1;

    int locPixX = (tileX - gridOriginX) * TILE_SIZE_PX + subX;
    int locPixY = (tileY - gridOriginY) * TILE_SIZE_PX + subY;

    s_ctx.cropX = constrain(locPixX - DISPLAY_WIDTH  / 2, 0, STITCH_SIZE - DISPLAY_WIDTH);
    s_ctx.cropY = constrain(locPixY - DISPLAY_HEIGHT / 2, 0, STITCH_SIZE - DISPLAY_HEIGHT);

    Serial.printf("[radar] crop (%d,%d)  loc (%d,%d)\n",
                  s_ctx.cropX, s_ctx.cropY, locPixX, locPixY);

    // Black background — tiles write over it as they decode
    tft.fillScreen(TFT_BLACK);

    int tilesOK = 0;
    for (int row = 0; row < TILE_GRID; row++) {
        for (int col = 0; col < TILE_GRID; col++) {
            String url = buildTileURL(radarInfo,
                                      TILE_ZOOM,
                                      gridOriginX + col,
                                      gridOriginY + row);
            if (fetchAndDecodeTile(url, col * TILE_SIZE_PX, row * TILE_SIZE_PX)) tilesOK++;
        }
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    if (tilesOK == 0) showStatus("No tile data");
    else Serial.println("[radar] display updated");
}
