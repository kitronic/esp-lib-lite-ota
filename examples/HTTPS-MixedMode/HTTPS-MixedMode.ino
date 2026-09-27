/*
 * LiteOTA HTTPS Mixed-Mode Example
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Strategy:
 *   1. MFLN shrinks TLS buffers from 18KB -> ~2KB
 *   2. onBeforeRequest() disconnects MQTT during TLS
 *   3. onAfterRequest() reconnects MQTT
 *   4. Manifest via HTTPS, firmware via HTTP (mixed mode)
 *   5. Heap guard tuned for TLS
 */

#define LITEOTA_USE_TLS
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <LiteOTA.h>

#define WIFI_SSID       "your-ssid"
#define WIFI_PASSWORD   "your-password"
#define MQTT_HOST       "broker.local"
#define CURRENT_VERSION "1.0"

// manifest HTTPS (small), firmware HTTP (large) -> mixed mode
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

    // ⚡ TLS tuning
    ota.setInsecure();                 // skip cert check (lighter)
    ota.enableMFLN(1024);              // 🔑 shrink TLS RX to 1 KB
    ota.setTLSBufferSizes(1024, 512);  // 🔑 BearSSL buffers
    ota.setMinFreeHeap(20000);         // TLS needs ~20 KB with MFLN

    // ⚡ Cooperative handoff
    ota.onBeforeRequest([]() {
        Serial.println(F("[OTA] Freeing MQTT..."));
        if (mqtt.connected()) {
            mqttWasConnected = true;
            mqtt.disconnect();
            delay(100);
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
    ota.requestUpdate();
}

void loop() {
    server.handleClient();
    if (mqtt.connected()) mqtt.loop();

    ota.tick();   // ⚡ non-blocking

    yield();
}