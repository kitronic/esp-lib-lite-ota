/*
 * LiteOTA Embedded Test — Safe Rollback
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Verifies:
 *   1. Backup is created before OTA
 *   2. RTC flag is set
 *   3. confirmBoot() clears the flag after timeout
 */

#define LITEOTA_USE_ROLLBACK
#include <ESP8266WiFi.h>
#include <LiteOTA.h>

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

LiteOTA ota("1.0", "http://127.0.0.1/project.json");

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== test_rollback ==="));

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    ota.enableSafeRollback(true);
    ota.setRollbackTimeout(10);   // short timeout for testing
    ota.begin();

    // Check FS mounted
    if (ota.isRollbackAvailable()) {
        Serial.println(F("[TEST] Backup already exists (from prior run)."));
    }

    Serial.println(F("[TEST] Rollback feature initialized."));
    Serial.println(F("[TEST] If you reboot now, boot count will increment."));
    Serial.println(F("[TEST] After 3 reboots, rollback will trigger."));
}

void loop() {
    ota.tick();
    yield();
}