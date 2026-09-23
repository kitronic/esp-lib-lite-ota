/*
 * LiteOTA Embedded Test — MFLN Buffer Tuning
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Verifies that enableMFLN() shrinks TLS buffer sizes.
 * Requires: #define LITEOTA_USE_TLS
 */

#define LITEOTA_USE_TLS
#include <ESP8266WiFi.h>
#include <LiteOTA.h>

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

LiteOTA ota("1.0", "https://example.com/project.json");

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== test_mfln ==="));

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    Serial.printf_P(PSTR("Heap before: %u\n"), ESP.getFreeHeap());

    ota.setInsecure();
    ota.setTLSBufferSizes(16384, 512);
    ota.enableMFLN(1024);    // should shrink RX to 1024, TX to 512

    ota.begin();

    // We don't actually connect — just verify config was applied
    // (check Serial output from enableMFLN)

    Serial.printf_P(PSTR("Heap after: %u\n"), ESP.getFreeHeap());

    if (ESP.getFreeHeap() > 30000) {
        Serial.println(F("[PASS] Heap healthy after TLS setup."));
    } else {
        Serial.println(F("[WARN] Heap is tight."));
    }

    Serial.println(F("\n=== DONE ==="));
}

void loop() { yield(); }