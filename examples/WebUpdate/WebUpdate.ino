/*
 * LiteOTA Web Update Example
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Demonstrates the built-in /update web page.
 * Combines:
 *   - Server-based OTA (auto, every 24h)
 *   - Browser upload (/update)
 *   - Status endpoint (/ota/status)
 *
 * Open in browser:  http://<device-ip>/update
 * Login:            admin / admin
 *
 * Requirements:
 *   - #define LITEOTA_USE_WEB
 *   - #define LITEOTA_USE_ROLLBACK  (optional)
 *   - #define LITEOTA_USE_TLS       (optional)
 */

#define LITEOTA_USE_WEB
// #define LITEOTA_USE_ROLLBACK
// #define LITEOTA_USE_TLS

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LiteOTA.h>

#define WIFI_SSID       "your-ssid"
#define WIFI_PASSWORD   "your-password"
#define CURRENT_VERSION "1.0.0"

LiteOTA ota(CURRENT_VERSION, "http://your-server.com/ota/project.json");

ESP8266WebServer server(80);

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== LiteOTA Web Update ==="));

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    Serial.printf_P(PSTR("Open: http://%s/update\n"),
                    WiFi.localIP().toString().c_str());

    // User routes
    server.on("/", []() {
        server.send(200, "text/plain", "Hello from LiteOTA Web Update");
    });

    server.begin();

    // ⚡ LiteOTA web endpoints with basic auth
    ota.attachWebServer(&server, "admin", "admin");
    // Optional: mount at /ota-prefix instead of /
    // ota.setWebPrefix("/ota");
    // ota.setWebAutoReboot(true);

    ota.setCheckInterval(24UL * 3600UL);
    ota.setMinFreeHeap(8000);

    ota.onProgress([](size_t r, size_t t, uint8_t p) {
        Serial.printf_P(PSTR("[OTA] %u/%u (%u%%)\n"), r, t, p);
    });

    ota.begin();
}

void loop() {
    server.handleClient();
    ota.tick();
    yield();
}