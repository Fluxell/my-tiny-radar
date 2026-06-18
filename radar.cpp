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

static TFT_eSPI tft = TFT_eSPI();
static PNG       png;

// Stitched 512×512 RGB565 image — must live in PSRAM
static uint16_t* s_stitch  = nullptr;
// Temporary buffer for a single compressed tile download
static uint8_t*  s_tileBuf = nullptr;
static const size_t TILE_BUF_SIZE = 81920; // 80 KB — more than enough for a 256-px PNG tile

// Current tile's pixel offset within the stitch buffer (set before each decode)
static int s_tileOffX = 0;
static int s_tileOffY = 0;

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

// Sub-tile pixel position (0 – TILE_SIZE_PX-1) for a coordinate at zoom z
static int lon2subPixel(double lon, int z) {
    double tx = (lon + 180.0) / 360.0 * (1 << z);
    return (int)((tx - floor(tx)) * TILE_SIZE_PX);
}

static int lat2subPixel(double lat, int z) {
    double rad = lat * DEG_TO_RAD;
    double ty  = (1.0 - log(tan(rad) + 1.0 / cos(rad)) / M_PI) / 2.0 * (1 << z);
    return (int)((ty - floor(ty)) * TILE_SIZE_PX);
}

// ─── PNG draw callback (called per row by PNGdec) ────────────────────────────

static void pngRowCallback(PNGDRAW* pDraw) {
    uint16_t line[TILE_SIZE_PX];
    // PNG_RGB565_LITTLE_ENDIAN produces native uint16_t values TFT_eSPI expects
    png.getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0x0000 /* black bg */);

    int destRow = s_tileOffY + pDraw->y;
    if (destRow >= STITCH_SIZE) return;

    memcpy(&s_stitch[destRow * STITCH_SIZE + s_tileOffX],
           line,
           TILE_SIZE_PX * sizeof(uint16_t));
}

// ─── HTTP helpers ─────────────────────────────────────────────────────────────

// Fills s_tileBuf with the response body; returns byte count or 0 on error.
static int httpGetBinary(const String& url) {
    WiFiClientSecure client;
    client.setInsecure(); // skip cert validation — acceptable for radar tiles
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(HTTP_TIMEOUT_MS);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[radar] HTTP %d  %s\n", code, url.c_str());
        http.end();
        return 0;
    }

    WiFiClient* stream    = http.getStreamPtr();
    int         total     = 0;
    int         contentLen = http.getSize(); // may be -1 for chunked
    unsigned long deadline = millis() + HTTP_TIMEOUT_MS;

    while ((http.connected() || stream->available()) && millis() < deadline) {
        int avail = stream->available();
        if (avail > 0) {
            int toRead = min(avail, (int)(TILE_BUF_SIZE - total));
            if (toRead <= 0) break;
            total   += stream->readBytes(s_tileBuf + total, toRead);
            deadline = millis() + 5000; // reset on progress
        }
        if (contentLen > 0 && total >= contentLen) break;
        delay(1);
    }

    http.end();
    return total;
}

// ─── RainViewer API ───────────────────────────────────────────────────────────

struct RadarInfo {
    String host;
    String path; // e.g. "/v2/radar/1702320600"
};

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
    if (past.size() == 0) {
        Serial.println("[radar] no past frames in API response");
        return false;
    }

    out.host = doc["host"].as<String>();
    out.path = past[past.size() - 1]["path"].as<String>();
    Serial.printf("[radar] latest frame: %s%s\n", out.host.c_str(), out.path.c_str());
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
    Serial.printf("[radar] tile %s\n", url.c_str());

    int len = httpGetBinary(url);
    if (len == 0) return false;

    s_tileOffX = offX;
    s_tileOffY = offY;

    int rc = png.openRAM(s_tileBuf, len, pngRowCallback);
    if (rc != PNG_SUCCESS) {
        Serial.printf("[radar] PNG open failed: %d\n", rc);
        return false;
    }

    rc = png.decode(nullptr, 0);
    png.close();

    if (rc != PNG_SUCCESS) {
        Serial.printf("[radar] PNG decode failed: %d\n", rc);
        return false;
    }
    return true;
}

// ─── WiFi ─────────────────────────────────────────────────────────────────────

static bool connectWiFi(const AppConfig& cfg) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSSID, cfg.wifiPass);
    Serial.print("[radar] WiFi connecting");
    for (int i = 0; i < 40; i++) {           // 20-second timeout
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
    // Initialise display
    pinMode(LCD_BL_PIN, OUTPUT);
    digitalWrite(LCD_BL_PIN, HIGH);
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    showStatus("Connecting...");

    // Allocate PSRAM buffers
    if (!s_stitch) {
        s_stitch = (uint16_t*)ps_malloc((size_t)STITCH_SIZE * STITCH_SIZE * sizeof(uint16_t));
    }
    if (!s_tileBuf) {
        s_tileBuf = (uint8_t*)ps_malloc(TILE_BUF_SIZE);
    }
    if (!s_stitch || !s_tileBuf) {
        showStatus("PSRAM error");
        Serial.println("[radar] PSRAM allocation failed");
        return;
    }

    // WiFi
    if (!connectWiFi(cfg)) {
        showStatus("WiFi failed");
        return;
    }
    showStatus("Fetching radar...");

    // Get latest radar frame path
    RadarInfo radarInfo;
    if (!fetchRadarInfo(radarInfo)) {
        showStatus("API error");
        WiFi.disconnect(true);
        return;
    }

    // Tile grid origin: place our location near centre of the 2×2 grid
    int tileX   = lon2tileX(cfg.longitude, TILE_ZOOM);
    int tileY   = lat2tileY(cfg.latitude,  TILE_ZOOM);
    int subX    = lon2subPixel(cfg.longitude, TILE_ZOOM);
    int subY    = lat2subPixel(cfg.latitude,  TILE_ZOOM);

    // If our location pixel is in the right/lower half of its tile we can use that
    // tile as the top-left of the 2×2 grid; otherwise shift one tile left/up.
    int gridOriginX = (subX >= TILE_SIZE_PX / 2) ? tileX     : tileX - 1;
    int gridOriginY = (subY >= TILE_SIZE_PX / 2) ? tileY     : tileY - 1;

    // Pixel position of our location within the stitched 512×512 buffer
    int locPixX = (tileX - gridOriginX) * TILE_SIZE_PX + subX;
    int locPixY = (tileY - gridOriginY) * TILE_SIZE_PX + subY;

    // Clear stitch buffer (black = no data)
    memset(s_stitch, 0, (size_t)STITCH_SIZE * STITCH_SIZE * sizeof(uint16_t));

    // Fetch 2×2 tiles
    int tilesOK = 0;
    for (int row = 0; row < TILE_GRID; row++) {
        for (int col = 0; col < TILE_GRID; col++) {
            String url = buildTileURL(radarInfo,
                                      TILE_ZOOM,
                                      gridOriginX + col,
                                      gridOriginY + row);
            if (fetchAndDecodeTile(url, col * TILE_SIZE_PX, row * TILE_SIZE_PX)) {
                tilesOK++;
            }
        }
    }

    if (tilesOK == 0) {
        showStatus("No tile data");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Crop a 240×240 region centred on our location
    int cropX = constrain(locPixX - DISPLAY_WIDTH  / 2, 0, STITCH_SIZE - DISPLAY_WIDTH);
    int cropY = constrain(locPixY - DISPLAY_HEIGHT / 2, 0, STITCH_SIZE - DISPLAY_HEIGHT);

    Serial.printf("[radar] crop origin (%d,%d)  loc pixel (%d,%d)\n",
                  cropX, cropY, locPixX, locPixY);

    // Copy crop region into a contiguous buffer and push to display in one call
    uint16_t* crop = (uint16_t*)ps_malloc((size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
    if (crop) {
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            memcpy(&crop[y * DISPLAY_WIDTH],
                   &s_stitch[(cropY + y) * STITCH_SIZE + cropX],
                   DISPLAY_WIDTH * sizeof(uint16_t));
        }
        tft.pushImage(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, crop);
        free(crop);
    } else {
        // Fallback: row-by-row (slower, no extra allocation)
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            tft.pushImage(0, y, DISPLAY_WIDTH, 1,
                          &s_stitch[(cropY + y) * STITCH_SIZE + cropX]);
        }
    }

    Serial.println("[radar] display updated");
}
