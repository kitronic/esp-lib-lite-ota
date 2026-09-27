/*
 * LiteOTA NonBlocking Example
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Shows the tick()-driven non-blocking API.
 * Web + MQTT keep running throughout the OTA download.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <LiteOTA.h>

#define WIFI_SSID       "your-ssid"
#define WIFI_PASSWORD   "your-password"
#define MQTT_HOST       "broker.local"
#define CURRENT_VERSION "1.0"

ESP8266WebServer server(80);
WiFiClient       mqttNet;
PubSubClient     mqtt(mqttNet);

LiteOTA ota(CURRENT_VERSION, "http://your-server.com/liteota/project.json");

unsigned long lastMqttTry = 0;
volatile bool manualTrigger = false;

void mqttCallback(char* topic, byte* payload, unsigned int len) {
    if (strcmp(topic, "cmd/ota") != 0) return;
    if (len < 6) return;
    if (strncmp((char*)payload, "update", 6) == 0) {
        manualTrigger = true;
    }
}

void ensureMqtt() {
    if (mqtt.connected()) return;
    if (millis() - lastMqttTry < 5000) return;
    lastMqttTry = millis();
    if (mqtt.connect("LiteOTA-NB")) {
        mqtt.subscribe("cmd/ota");
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== LiteOTA NonBlocking ==="));

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    Serial.printf_P(PSTR("IP: %s\n"), WiFi.localIP().toString().c_str());

    // MQTT
    mqtt.setServer(MQTT_HOST, 1883);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(128);

    // Web
    server.on("/", []() {
        server.send(200, "text/plain", "LiteOTA non-blocking demo");
    });
    server.on("/ota", []() {
        manualTrigger = true;
        server.send(200, "text/plain", "OTA requested");
    });
    server.on("/status", []() {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "state=%s err=%s progress=%u%% heap=%u",
                 ota.getStateName(), ota.getLastErrorName(),
                 ota.getProgressPercent(), ESP.getFreeHeap());
        server.send(200, "text/plain", buf);
    });
    server.begin();

    // LiteOTA configuration
    ota.setCheckInterval(24 * 3600);
    ota.setMinFreeHeap(8000);
    ota.setChunkSize(512);
    ota.setMaxRetries(3);

    ota.onProgress([](size_t r, size_t t, uint8_t p) {
        Serial.printf_P(PSTR("[OTA] %u/%u (%u%%)\n"), r, t, p);
    });

    ota.onStateChange([](LiteOTAState s, LiteOTAError e) {
        Serial.printf_P(PSTR("[OTA] state=%d err=%d\n"), (int)s, (int)e);
    });

    ota.begin();
}

void loop() {
    // Your normal app — never blocked
    server.handleClient();
    ensureMqtt();
    mqtt.loop();

    // Manual trigger
    if (manualTrigger) {
        manualTrigger = false;
        ota.requestUpdate();
    }

    // ⚡ LiteOTA tick — non-blocking
    ota.tick();

    yield();
}