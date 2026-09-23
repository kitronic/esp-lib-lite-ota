/*
 * LiteOTA HTTPS Example
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * IMPORTANT: Enable TLS in your sketch BEFORE including LiteOTA.h:
 *     #define LITEOTA_USE_TLS
 *     #include <LiteOTA.h>
 *
 * TLS costs ~25-35KB RAM during requests. Do NOT combine with
 * heavy MQTT + Web traffic on ESP8266 unless you know the heap
 * can handle it. For best results, use TLS only for the manifest
 * and plain HTTP for the firmware binary (mixed-mode).
 */

#define LITEOTA_USE_TLS    // ← لازم قبل الـ include
#include <ESP8266WiFi.h>
#include <LiteOTA.h>

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
#define CURRENT_VERSION "1.0"

LiteOTA ota(CURRENT_VERSION, "https://kitronic.tech/ota/project.json");

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== LiteOTA HTTPS ==="));

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    Serial.printf_P(PSTR("Free heap before setup: %u\n"), ESP.getFreeHeap());

    // ─── اختر واحد من هدول ───

    // (أ) بدون التحقق من الشهادة — أبسط، أخفّ رام
    ota.setInsecure();

    // (ب) مع CA cert — أكثر أماناً
    // static const char CA_CERT[] PROGMEM = R"EOF(
    // -----BEGIN CERTIFICATE-----
    // MIID... (شهادة السيرفر)
    // -----END CERTIFICATE-----
    // )EOF";
    // ota.setCACert(CA_CERT);

    // إعدادات
    ota.setCheckInterval(24 * 3600);
    ota.setMinFreeHeap(25000);   // TLS يحتاج رام أكثر
    ota.setChunkSize(512);       // أصغر شوي مع TLS

    ota.onProgress([](size_t r, size_t t, uint8_t p) {
        Serial.printf_P(PSTR("[OTA] %u/%u (%u%%)\n"), r, t, p);
    });

    ota.begin();

    // فحص يدوي عند الإقلاع
    ota.requestUpdate();
}

void loop() {
    ota.tick();
    yield();
}