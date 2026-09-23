/*
 * LiteOTA Manual Trigger Example
 *
 * Kitronic
 * info@kitronic.tech
 * www.kitronic.tech
 * https://github.com/kitronic/esp-lib-lite-ota
 *
 * Adds a /ota HTTP endpoint to an existing web server.
 * Hit it from a browser to trigger an update on demand.
 *
 * Also includes an MQTT trigger on topic "cmd/ota".
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <LiteOTA.h>

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
#define MQTT_HOST     "broker.local"
#define CURRENT_VERSION "1.0"

ESP8266WebServer server(80);
WiFiClient       mqttNet;
PubSubClient     mqtt(mqttNet);

LiteOTA ota(CURRENT_VERSION, "http://your-server.com/liteota/project.json");

volatile bool manualTrigger = false;
unsigned long lastMqttTry = 0;

void mqttCallback(char* topic, byte* payload, unsigned int len) {
    if (strcmp(topic, "cmd/ota") != 0) return;
    if (len < 6) return;
    if (strncmp((char*)payload, "update", 6) == 0) {
        Serial.println(F("[MQTT] Manual OTA requested"));
        manualTrigger = true;
    }
}

void ensureMqtt() {
    if (mqtt.connected()) return;
    if (millis() - lastMqttTry < 5000) return;
    lastMqttTry = millis();

    Serial.println(F("[MQTT] Reconnecting..."));
    if (mqtt.connect("LiteOTA-Demo")) {
        mqtt.subscribe("cmd/ota");
        Serial.println(F("[MQTT] Subscribed to cmd/ota"));
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== LiteOTA ManualTrigger ==="));

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    Serial.printf_P(PSTR("IP: %s\n"), WiFi.localIP().toString().c_str());

    mqtt.setServer(MQTT_HOST, 1883);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(128);

    server.on("/ota", []() {
        server.send(200, "text/plain", "OTA triggered. Check serial.");
        manualTrigger = true;
    });

    server.on("/heap", []() {
        char buf[32];
        snprintf(buf, sizeof(buf), "%u", ESP.getFreeHeap());
        server.send(200, "text/plain", buf);
    });

    server.begin();
    Serial.println(F("HTTP server started."));
}

void loop() {
    server.handleClient();
    ensureMqtt();
    mqtt.loop();

    if (manualTrigger) {
        manualTrigger = false;
        Serial.println(F("[Manual] Running OTA now..."));
        ota.checkAndUpdate();
    }

    yield();
}