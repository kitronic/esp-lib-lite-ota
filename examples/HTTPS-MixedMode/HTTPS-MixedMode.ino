/*
 * LiteOTA Mixed-Mode Example — الحل الكامل
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Strategy:
 *   1. MFLN shrinks TLS buffers from 18KB → ~2KB
 *   2. onBeforeRequest() disconnects MQTT during TLS
 *   3. onAfterRequest() reconnects MQTT
 *   4. Manifest via HTTPS, firmware via HTTP (mixed mode)
 *   5. Heap guard raises to 25KB only when TLS is active
 */

#define LITEOTA_USE_TLS
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <LiteOTA.h>

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
#define MQTT_HOST     "broker.local"
#define CURRENT_VERSION "1.0"

// manifest HTTPS (صغير), firmware HTTP (كبير) → mixed mode
LiteOTA ota(CURRENT_VERSION, "https://kitronic.tech/ota/project.json");

ESP8266WebServer server(80);
WiFiClient       mqttNet;
PubSubClient     mqtt(mqttNet);
bool mqttWasConnected = false;

void setup() {
    Serial.begin(115200);
    delay(200);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    mqtt.setServer(MQTT_HOST, 1883);
    mqtt.setBufferSize(128);
    mqtt.connect("LiteOTA-Mixed");

    server.on("/", []() { server.send(200, "text/plain", "OK"); });
    server.begin();

    // ⚡⚡⚡ الإعدادات السحرية ⚡⚡⚡
    ota.setInsecure();                 // لا نتحقق من الشهادة (أخف)
    ota.enableMFLN(1024);              // 🔑 يصغّر بافر TLS من 16KB → 1KB
    ota.setTLSBufferSizes(1024, 512);  // 🔑 بافرات BearSSL صغيرة
    ota.setMinFreeHeap(20000);         // TLS يحتاج ~20KB بس مع MFLN

    // ⚡ Cooperative handoff
    ota.onBeforeRequest([]() {
        Serial.println(F("[OTA] Freeing MQTT..."));
        if (mqtt.connected()) {
            mqttWasConnected = true;
            mqtt.disconnect();
            delay(100);  // خلّي TCP يتنفس
        }
    });

    ota.onAfterRequest([]() {
        Serial.println(F("[OTA] Restoring MQTT..."));
        if (mqttWasConnected) {
            mqtt.connect("LiteOTA-Mixed");
            mqttWasConnected = false;
        }
    });

    ota.begin();

    // فحص عند الإقلاع
    ota.requestUpdate();
}

void loop() {
    server.handleClient();
    if (mqtt.connected()) mqtt.loop();

    ota.tick();   // ⚡ non-blocking

    yield();
}