/*
 * LiteOTA Safe Rollback Example
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Demonstrates automatic rollback on boot failure.
 *
 * How it works:
 *   1. Before OTA, current firmware is backed up to LittleFS
 *   2. RTC memory is marked "update pending"
 *   3. New firmware boots. If it survives 30s, it confirms boot.
 *   4. If it crashes before 30s (3 times), old firmware is restored.
 *
 * Requirements:
 *   - LittleFS partition with ~500KB free
 *   - Enable with #define LITEOTA_USE_ROLLBACK
 */

#define LITEOTA_USE_ROLLBACK
#include <ESP8266WiFi.h>
#include <LiteOTA.h>

#define WIFI_SSID       "your-ssid"
#define WIFI_PASSWORD   "your-password"
#define CURRENT_VERSION "1.0"

LiteOTA ota(CURRENT_VERSION, "http://kitronic.tech/ota/project.json");

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== LiteOTA Safe Rollback ==="));

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    Serial.printf_P(PSTR("IP: %s\n"), WiFi.localIP().toString().c_str());
    Serial.printf_P(PSTR("Heap: %u\n"), ESP.getFreeHeap());

    // ─── Rollback configuration ───
    ota.enableSafeRollback(true);
    ota.setRollbackTimeout(30);              // confirm boot after 30s
    ota.setRollbackBackupPath("/liteota/backup.bin");

    // Optional: check if a backup exists from a previous run
    if (ota.isRollbackAvailable()) {
        Serial.println(F("[APP] Rollback backup is available."));
    }

    // Callbacks
    ota.onProgress([](size_t r, size_t t, uint8_t p) {
        Serial.printf_P(PSTR("[OTA] %u/%u (%u%%)\n"), r, t, p);
    });
    ota.onStateChange([](LiteOTAState s, LiteOTAError e) {
        Serial.printf_P(PSTR("[OTA] state=%d err=%d\n"), (int)s, (int)e);
    });

    ota.begin();
    ota.requestUpdate();
}

void loop() {
    ota.tick();

    // ⚠️ If you deliberately want to test rollback:
    // Comment out ota.tick() before the timeout expires,
    // or trigger ESP.restart() manually within 30s.
    // The library will detect the boot loop after 3 attempts
    // and restore the previous firmware.

    yield();
}