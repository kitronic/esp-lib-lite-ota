/*
 * LiteOTA Scheduled Example
 *
 * Kitronic
 * info@kitronic.tech
 * www.kitronic.tech
 * https://github.com/kitronic/esp-lib-lite-ota
 *
 * Checks for updates once per day at a specific hour.
 * Uses NTP for time. Non-blocking (millis-based).
 */

#include <ESP8266WiFi.h>
#include <LiteOTA.h>
#include <time.h>

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
#define CURRENT_VERSION "1.0"

#define CHECK_HOUR   3
#define CHECK_MINUTE 0

LiteOTA ota(CURRENT_VERSION, "http://your-server.com/liteota/project.json");

unsigned long lastWiFiCheck = 0;
int lastCheckedDay = -1;

void ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    if (millis() - lastWiFiCheck < 10000) return;
    lastWiFiCheck = millis();

    Serial.println(F("[WiFi] Reconnecting..."));
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== LiteOTA Scheduled ==="));

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    // Syria = UTC+3 → 3 * 3600 seconds offset
    configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println(F("Waiting NTP..."));
    time_t now = time(nullptr);
    while (now < 24 * 3600) {
        delay(500);
        Serial.print('.');
        now = time(nullptr);
        yield();
    }
    Serial.println(F("\nTime synced."));
}

void loop() {
    ensureWiFi();

    if (WiFi.status() == WL_CONNECTED) {
        time_t now = time(nullptr);
        struct tm* ti = localtime(&now);

        bool onTargetTime = (ti->tm_hour == CHECK_HOUR && ti->tm_min == CHECK_MINUTE);
        bool notDoneToday = (ti->tm_mday != lastCheckedDay);

        if (onTargetTime && notDoneToday) {
            lastCheckedDay = ti->tm_mday;
            Serial.println(F("[Scheduler] Daily OTA check triggered"));
            ota.checkAndUpdate();
        }
    }

    yield();
}