/*
 * LiteOTA Embedded Test — Callback Order
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Verifies that onBeforeRequest / onAfterRequest fire correctly
 * and that _servicesPaused prevents double-firing.
 *
 * Uses a local fake manifest server inside the ESP.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LiteOTA.h>

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

ESP8266WebServer server(80);
LiteOTA ota("1.0", "http://127.0.0.1/project.txt");

int beforeCount = 0;
int afterCount  = 0;
bool mqttRunning = true;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== test_callbacks ==="));

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    // Local fake manifest — same version, so no actual update happens
    server.on("/project.txt", []() {
        server.send(200, "text/plain", "1.0\nhttp://127.0.0.1/fake.bin\n");
    });
    server.begin();

    char url[64];
    snprintf(url, sizeof(url), "http://%s/project.txt",
             WiFi.localIP().toString().c_str());
    ota.setManifestUrl(url);

    ota.onBeforeRequest([]() {
        beforeCount++;
        mqttRunning = false;
        Serial.printf_P(PSTR("[BEFORE] count=%d mqtt=off\n"), beforeCount);
    });
    ota.onAfterRequest([]() {
        afterCount++;
        mqttRunning = true;
        Serial.printf_P(PSTR("[AFTER] count=%d mqtt=on\n"), afterCount);
    });
    ota.onStateChange([](LiteOTAState s, LiteOTAError e) {
        Serial.printf_P(PSTR("[STATE] %d err=%d\n"), (int)s, (int)e);
    });

    ota.begin();
    ota.requestUpdate();
}

void loop() {
    server.handleClient();
    ota.tick();

    static bool done = false;
    static unsigned long start = millis();
    if (!done && !ota.isUpdating() && millis() - start > 2000UL) {
        done = true;
        Serial.printf_P(PSTR("\n[RESULT] before=%d after=%d\n"),
                        beforeCount, afterCount);
        if (beforeCount == afterCount && beforeCount >= 1) {
            Serial.println(F("[PASS] Callbacks matched."));
        } else {
            Serial.println(F("[FAIL] Callback mismatch."));
        }
        if (mqttRunning) {
            Serial.println(F("[PASS] Services restored."));
        } else {
            Serial.println(F("[FAIL] Services still paused."));
        }
    }

    yield();
}