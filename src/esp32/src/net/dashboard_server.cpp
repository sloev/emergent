#include "net/dashboard_server.h"

#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "net/wifi_ap.h"

namespace {
constexpr uint32_t kBroadcastIntervalMs = 200;  // 5 Hz
constexpr size_t kContingencyTopN = 20;

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
        o["manual"] = a.manual_override_active(millis());
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

// Decodes a packed delta code back into {name, delta} pairs for display —
// delta is -1/0/+1, matching ContingencyMemory's quantization.
void add_decoded_channels(JsonArray arr, Body& body, uint32_t code, bool is_actuator) {
    size_t n = is_actuator ? body.actuator_count() : body.sensor_count();
    for (size_t i = 0; i < n; i++) {
        int8_t d = ContingencyMemory::decode_channel(code, i);
        if (d == 0) continue;  // only list channels that moved
        JsonObject o = arr.add<JsonObject>();
        o["name"] = is_actuator ? body.actuator_at(i).name() : body.sensor_at(i).name();
        o["delta"] = d;
    }
}

void build_contingency_json(Body& body, ContingencyMemory& contingency, JsonDocument& doc) {
    doc["count"] = contingency.count();
    doc["capacity"] = ContingencyMemory::capacity();

    static ContingencyEntry top[kContingencyTopN];
    size_t n = contingency.top_entries(top, kContingencyTopN);

    JsonArray entries = doc["entries"].to<JsonArray>();
    for (size_t i = 0; i < n; i++) {
        JsonObject o = entries.add<JsonObject>();
        o["strength"] = top[i].strength;
        o["age"] = top[i].age;
        o["mean_drive_delta"] = top[i].mean_drive_delta;
        add_decoded_channels(o["actuators"].to<JsonArray>(), body, top[i].action_code, true);
        add_decoded_channels(o["sensors"].to<JsonArray>(), body, top[i].sensor_code, false);
    }
}

// Full fixed-size grid (occupied or not) so the dashboard's layout is
// stable across calls — cells fill in as they're discovered rather than
// the list reordering/reshuffling.
void build_spatial_json(SpatialMemory& spatial, Physiology& phys, JsonDocument& doc) {
    doc["count"] = spatial.count();
    doc["capacity"] = SpatialMemory::capacity();

    JsonArray cells = doc["cells"].to<JsonArray>();
    for (size_t i = 0; i < SpatialMemory::capacity(); i++) {
        const SpatialCell& c = spatial.cell_at(i);
        JsonObject o = cells.add<JsonObject>();
        o["occupied"] = c.occupied;
        if (!c.occupied) continue;
        o["visit_count"] = c.visit_count;
        o["age"] = c.age;
        float score = 0.0f;
        for (size_t v = 0; v < kPhysVarCount; v++) {
            score += phys.drive(static_cast<PhysVar>(v)) * c.drive_improvement[v];
        }
        o["score"] = score;
    }
}
}  // namespace

namespace dashboard {

void begin(Body& body, Physiology& phys, ContingencyMemory& contingency, SpatialMemory& spatial) {
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
            if (act) act->write_manual(value);
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

    server.on("/api/contingency", HTTP_GET, [&body, &contingency](AsyncWebServerRequest* request) {
        JsonDocument doc;
        build_contingency_json(body, contingency, doc);
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/spatial", HTTP_GET, [&phys, &spatial](AsyncWebServerRequest* request) {
        JsonDocument doc;
        build_spatial_json(spatial, phys, doc);
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
