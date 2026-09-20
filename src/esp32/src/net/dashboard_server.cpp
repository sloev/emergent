#include "net/dashboard_server.h"

#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "net/wifi_ap.h"

namespace {
constexpr uint32_t kBroadcastIntervalMs = 200;  // 5 Hz

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
uint32_t last_broadcast_ms = 0;

void build_channels_json(Body& body, Physiology& phys, JsonDocument& doc) {
    JsonArray actuators = doc["actuators"].to<JsonArray>();
    for (size_t i = 0; i < body.actuator_count(); i++) {
        Actuator& a = body.actuator_at(i);
        JsonObject o = actuators.add<JsonObject>();
        o["name"] = a.name();
        o["min"] = a.spec().range_min;
        o["max"] = a.spec().range_max;
        o["value"] = a.value();
    }

    JsonArray sensors = doc["sensors"].to<JsonArray>();
    for (size_t i = 0; i < body.sensor_count(); i++) {
        Sensor& s = body.sensor_at(i);
        JsonObject o = sensors.add<JsonObject>();
        o["name"] = s.name();
        o["value"] = s.last_value();
    }

    JsonArray physiology = doc["physiology"].to<JsonArray>();
    for (size_t i = 0; i < kPhysVarCount; i++) {
        PhysVar v = static_cast<PhysVar>(i);
        JsonObject o = physiology.add<JsonObject>();
        o["name"] = Physiology::var_name(v);
        o["value"] = phys.value(v);
        o["drive"] = phys.drive(v);
    }
    doc["fuzz_scale"] = phys.fuzz_scale();
}
}  // namespace

namespace dashboard {

void begin(Body& body, Physiology& phys) {
    if (!LittleFS.begin(true)) {
        Serial.println("[dashboard] LittleFS mount failed");
    }

    ws.onEvent([&body](AsyncWebSocket* server_, AsyncWebSocketClient* client, AwsEventType type,
                        void* arg, uint8_t* data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            Serial.printf("[ws] client #%u connected\n", client->id());
        } else if (type == WS_EVT_DISCONNECT) {
            Serial.printf("[ws] client #%u disconnected\n", client->id());
        } else if (type == WS_EVT_DATA) {
            auto* info = static_cast<AwsFrameInfo*>(arg);
            if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) return;

            JsonDocument doc;
            if (deserializeJson(doc, data, len)) return;

            const char* name = doc["actuator"] | static_cast<const char*>(nullptr);
            if (!name) return;
            float value = doc["value"] | 0.0f;

            Actuator* act = body.actuator(name);
            if (act) act->write(value);
        }
    });
    server.addHandler(&ws);

    server.on("/api/channels", HTTP_GET, [&body, &phys](AsyncWebServerRequest* request) {
        JsonDocument doc;
        build_channels_json(body, phys, doc);
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on(
        "/api/config/wifi", HTTP_POST, [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                request->send(400, "application/json", R"({"error":"bad json"})");
                return;
            }
            const char* ssid = doc["ssid"] | "";
            const char* password = doc["password"] | "";
            if (strlen(ssid) == 0) {
                request->send(400, "application/json", R"({"error":"ssid required"})");
                return;
            }
            wifi_ap::save_credentials(ssid, password);
            request->send(200, "application/json", R"({"ok":true,"restarting":true})");
            delay(200);
            ESP.restart();
        });

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server.begin();
    Serial.println("[dashboard] server started on port 80");
}

void loop_tick(Body& body, Physiology& phys) {
    ws.cleanupClients();

    uint32_t now = millis();
    if (now - last_broadcast_ms < kBroadcastIntervalMs) return;
    last_broadcast_ms = now;

    if (ws.count() == 0) return;

    JsonDocument doc;
    build_channels_json(body, phys, doc);
    String out;
    serializeJson(doc, out);
    ws.textAll(out);
}

}  // namespace dashboard
