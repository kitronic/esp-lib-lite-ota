/*
 * LiteOTA Basic Example
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Runs a single OTA check on boot.
 * Works with both JSON and plain-text manifests.
 */

#include <ESP8266WiFi.h>
#include <LiteOTA.h>

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

// Bump this on every release
#define CURRENT_VERSION "1.0"

// Point to your manifest (JSON or TXT — auto-detected)
LiteOTA ota(CURRENT_VERSION, "http://your-server.com/liteota/project.json");

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n\n=== LiteOTA Basic ==="));

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print(F("Connecting WiFi"));
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
        yield();
    }
    Serial.printf_P(PSTR("\nConnected. IP: %s\n"),
                    WiFi.localIP().toString().c_str());
    Serial.printf_P(PSTR("Free heap before OTA: %u\n"), ESP.getFreeHeap());

    ota.onProgress([](size_t r, size_t t, uint8_t p) {
        Serial.printf_P(PSTR("[OTA] %u/%u (%u%%)\n"), r, t, p);
    });

    ota.begin();
    ota.requestUpdate();

    // Non-blocking loop — wait here until done or failed
    unsigned long start = millis();
    while (ota.isUpdating() && millis() - start < 60000UL) {
        ota.tick();
        delay(5);
        yield();
    }

    Serial.printf_P(PSTR("Final: state=%s err=%s\n"),
                    ota.getStateName(), ota.getLastErrorName());
    Serial.printf_P(PSTR("Free heap after OTA: %u\n"), ESP.getFreeHeap());
}

void loop() {
    yield();
}