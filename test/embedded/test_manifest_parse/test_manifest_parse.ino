/*
 * LiteOTA Embedded Test — Manifest Parsing (JSON + TXT)
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Prerequisites:
 *   1. Start a local HTTP server:
 *        cd test/server && python3 -m http.server 8080
 *   2. Update SERVER_HOST below with your PC's IP
 *   3. Upload sketch, open Serial Monitor @ 115200
 */

#include <ESP8266WiFi.h>
#include <LiteOTA.h>

#define WIFI_SSID    "your-ssid"
#define WIFI_PASSWORD "your-password"
#define SERVER_HOST  "192.168.1.100"   // ← your PC's IP
#define SERVER_PORT  8080

#define CURRENT_VERSION "1.0"

LiteOTA ota(CURRENT_VERSION, "http://" SERVER_HOST ":8080/project.json");

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== test_manifest_parse ==="));

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }
    Serial.printf_P(PSTR("IP: %s\n"), WiFi.localIP().toString().c_str());

    // Hook callbacks
    ota.onProgress([](size_t r, size_t t, uint8_t p) {
        Serial.printf_P(PSTR("[PROGRESS] %u/%u (%u%%)\n"), r, t, p);
    });
    ota.onStateChange([](LiteOTAState s, LiteOTAError e) {
        Serial.printf_P(PSTR("[STATE] %d err=%d\n"), (int)s, (int)e);
    });
    ota.onBeforeRequest([]() { Serial.println(F("[HOOK] before")); });
    ota.onAfterRequest([]()  { Serial.println(F("[HOOK] after"));  });

    ota.begin();

    // Test 1: JSON manifest
    Serial.println(F("\n--- Test 1: JSON manifest ---"));
    ota.requestUpdate();
    unsigned long start = millis();
    while (ota.isUpdating() && millis() - start < 15000UL) {
        ota.tick();
        delay(5);
        yield();
    }
    Serial.printf_P(PSTR("Final state: %s | error: %s\n"),
                    ota.getStateName(), ota.getLastErrorName());
    Serial.printf_P(PSTR("Remote: %s | FW URL: %s | Type: %s\n"),
                    ota.getRemoteVersion(), ota.getFirmwareUrl(),
                    ota.getManifestType());

    delay(2000);

    // Test 2: TXT manifest
    Serial.println(F("\n--- Test 2: TXT manifest ---"));
    ota.setManifestUrl("http://" SERVER_HOST ":8080/project.txt");
    ota.reset();
    ota.requestUpdate();
    start = millis();
    while (ota.isUpdating() && millis() - start < 15000UL) {
        ota.tick();
        delay(5);
        yield();
    }
    Serial.printf_P(PSTR("Final state: %s | error: %s\n"),
                    ota.getStateName(), ota.getLastErrorName());
    Serial.printf_P(PSTR("Remote: %s | FW URL: %s | Type: %s\n"),
                    ota.getRemoteVersion(), ota.getFirmwareUrl(),
                    ota.getManifestType());

    Serial.println(F("\n=== DONE ==="));
}

void loop() {
    // لا تنفذ تحديث تلقائي — اختبار فقط
    yield();
}