/*
 * LiteOTA Embedded Test — Heap Guard
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Verifies that requestUpdate() is refused when free heap
 * is below the configured threshold.
 */

#include <ESP8266WiFi.h>
#include <LiteOTA.h>

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

LiteOTA ota("1.0", "http://127.0.0.1/project.json");

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== test_heap_guard ==="));

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    Serial.printf_P(PSTR("Free heap: %u\n"), ESP.getFreeHeap());

    // Set an absurdly high threshold — update should be refused
    ota.setMinFreeHeap(500000);
    ota.begin();
    ota.requestUpdate();

    unsigned long start = millis();
    while (millis() - start < 3000UL) {
        ota.tick();
        delay(10);
        yield();
    }

    Serial.printf_P(PSTR("State: %s | Error: %s\n"),
                    ota.getStateName(), ota.getLastErrorName());

    if (strcmp(ota.getLastErrorName(), "LOW_HEAP") == 0) {
        Serial.println(F("[PASS] Heap guard refused update."));
    } else {
        Serial.println(F("[FAIL] Heap guard did not trigger."));
    }

    Serial.println(F("\n=== DONE ==="));
}

void loop() { yield(); }