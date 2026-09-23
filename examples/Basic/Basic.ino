/*
 * LiteOTA Basic Example
 *
 * Kitronic
 * info@kitronic.tech
 * www.kitronic.tech
 * https://github.com/kitronic/esp-lib-lite-ota
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
    Serial.printf_P(PSTR("\nConnected. IP: %s\n"), WiFi.localIP().toString().c_str());
    Serial.printf_P(PSTR("Free heap before OTA: %u\n"), ESP.getFreeHeap());

    ota.checkAndUpdate();

    Serial.printf_P(PSTR("Free heap after OTA: %u\n"), ESP.getFreeHeap());
}

void loop() {
    yield();
}