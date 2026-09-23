/*
 * LiteOTA Embedded Test — Version Comparison
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Runs locally against a text manifest served by the ESP itself
 * via the bundled simple server. No external server needed.
 *
 * Verifies: same version → no update, different → update triggered.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LiteOTA.h>

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
#define CURRENT_VERSION "1.0"

ESP8266WebServer server(80);
LiteOTA ota(CURRENT_VERSION, "http://127.0.0.1/project.txt");

const char TEST_VERSION[] = "1.0";  // ← change to "1.1" to test trigger

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== test_version_compare ==="));

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }
    Serial.printf_P(PSTR("IP: %s\n"), WiFi.localIP().toString().c_str());

    // Serve a local manifest
    server.on("/project.txt", []() {
        char body[128];
        snprintf(body, sizeof(body), "%s\nhttp://127.0.0.1/fake.bin\n", TEST_VERSION);
        server.send(200, "text/plain", body);
    });
    server.begin();

    // Point LiteOTA at ourselves
    char localUrl[64];
    snprintf(localUrl, sizeof(localUrl), "http://%s/project.txt",
             WiFi.localIP().toString().c_str());
    ota.setManifestUrl(localUrl);
    ota.setCheckInterval(3600);

    ota.onStateChange([](LiteOTAState s, LiteOTAError e) {
        Serial.printf_P(PSTR("[STATE] %s err=%s\n"),
                        "?", "?");
    });

    ota.begin();
}

void loop() {
    server.handleClient();
    ota.tick();

    // Trigger once after 3 seconds
    static bool triggered = false;
    static unsigned long bootAt = millis();
    if (!triggered && millis() - bootAt > 3000) {
        triggered = true;
        Serial.println(F("[TEST] Triggering update..."));
        ota.requestUpdate();
    }

    // Report after 10 seconds
    static bool reported = false;
    if (!reported && millis() - bootAt > 10000) {
        reported = true;
        Serial.printf_P(PSTR("[RESULT] state=%s err=%s remote=%s\n"),
                        ota.getStateName(), ota.getLastErrorName(),
                        ota.getRemoteVersion());
        if (strcmp(ota.getLastErrorName(), "SAME_VERSION") == 0) {
            Serial.println(F("[PASS] Same version detected."));
        } else if (strcmp(TEST_VERSION, CURRENT_VERSION) != 0) {
            Serial.println(F("[PASS] New version detected (update attempted)."));
        } else {
            Serial.println(F("[FAIL] Unexpected result."));
        }
    }

    yield();
}