/*
 * LiteOTA Embedded Test — Abort Mid-Download
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Starts an update, then aborts after 3 seconds.
 * Verifies that services resume and state returns to IDLE.
 */

#include <ESP8266WiFi.h>
#include <LiteOTA.h>

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

LiteOTA ota("1.0", "http://192.168.1.100:8080/project.json");

bool beforeFired = false;
bool afterFired  = false;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== test_abort ==="));

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    ota.onBeforeRequest([]() {
        beforeFired = true;
        Serial.println(F("[BEFORE] Services paused."));
    });
    ota.onAfterRequest([]() {
        afterFired = true;
        Serial.println(F("[AFTER] Services restored."));
    });

    ota.begin();
    ota.requestUpdate();

    unsigned long start = millis();
    while (millis() - start < 3000UL) {
        ota.tick();
        delay(5);
        yield();
    }

    Serial.println(F("[TEST] Calling abort()..."));
    ota.abort();

    // Give it a moment
    start = millis();
    while (millis() - start < 500UL) {
        ota.tick();
        delay(5);
        yield();
    }

    Serial.printf_P(PSTR("State: %s\n"), ota.getStateName());

    if (strcmp(ota.getStateName(), "IDLE") == 0 && afterFired) {
        Serial.println(F("[PASS] Abort cleaned up and resumed services."));
    } else {
        Serial.println(F("[FAIL] Abort did not clean up."));
    }

    Serial.println(F("\n=== DONE ==="));
}

void loop() { yield(); }