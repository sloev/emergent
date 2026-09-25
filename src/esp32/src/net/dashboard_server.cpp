#include "net/dashboard_server.h"

#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include <cstdio>
#include <cstring>

#include "net/wifi_ap.h"

namespace {
constexpr uint32_t kBroadcastIntervalMs = 200;  // 5 Hz
constexpr size_t kContingencyTopN = 20;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
uint32_t last_broadcast_ms = 0;

void build_channels_json(Body& body, Physiology& phys, SafetyMonitor& safety, JsonDocument& doc) {
    JsonArray actuators = doc["actuators"].to<JsonArray>();
    for (size_t i = 0; i < body.actuator_count(); i++) {
        Actuator& a = body.actuator_at(i);
        JsonObject o = actuators.add<JsonObject>();
        o["name"] = a.name();
        o["min"] = a.spec().range_min;
        o["max"] = a.spec().range_max;
        o["value"] = a.value();
        o["manual"] = a.manual_override_active(millis());
        o["safety_tripped"] = safety.actuator_tripped(i);
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
    doc["battery_critical"] = safety.battery_critical();
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

    // `static` to avoid a stack allocation for what's a small fixed buffer,
    // not for persistence between calls — each call fully overwrites what
    // it uses. Fine at these sizes (BSS, not stack), but note it's not
    // reentrant: two concurrent requests to this handler would stomp on
    // each other's buffer. Acceptable for a single-operator dashboard;
    // revisit if that ever changes, or if kContingencyTopN/kCapacity grow
    // enough to matter for RAM budget.
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

// Identifies a body's channel *layout* (names + counts), not its physical
// identity — two boards with the same channel names hash the same. Purely
// informational in the life-state envelope; restore is name-based regardless
// of whether this matches, so an "organism" can move to a different body
// (docs/synth-behavior.md §13) and keep whatever channels still exist there.
String compute_fingerprint(Body& body) {
    uint32_t h = 2166136261u;  // FNV-1a
    auto mix_str = [&](const char* s) {
        for (const char* p = s; *p; p++) {
            h ^= static_cast<uint8_t>(*p);
            h *= 16777619u;
        }
    };
    for (size_t i = 0; i < body.actuator_count(); i++) mix_str(body.actuator_at(i).name());
    for (size_t i = 0; i < body.sensor_count(); i++) mix_str(body.sensor_at(i).name());

    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(h));
    return String(buf);
}

// Life-state envelope (docs/synth-behavior.md §13). Contingency/spatial
// entries are keyed by channel *name*, not bit position, specifically so a
// state captured on one board restores sensibly on another: matching
// channels carry over, channels that don't exist on the new body are simply
// absent from the map and their contribution to that entry is dropped ("the
// remapping layer... gradually forgets mismatched contingencies").
constexpr uint32_t kStateVersion = 1;
constexpr size_t kMaxStateUploadBytes = 262144;  // 256KB guard against a runaway/bad upload

void build_state_json(Body& body, Physiology& phys, ContingencyMemory& contingency, SpatialMemory& spatial,
                       uint32_t age_ms, JsonDocument& doc) {
    doc["version"] = kStateVersion;
    doc["age_ms"] = age_ms;
    doc["channel_fingerprint"] = compute_fingerprint(body);

    JsonObject physObj = doc["physiology"].to<JsonObject>();
    for (size_t v = 0; v < kPhysVarCount; v++) {
        physObj[Physiology::var_name(static_cast<PhysVar>(v))] = phys.value(static_cast<PhysVar>(v));
    }

    // Same not-reentrant tradeoff as build_contingency_json's buffer above —
    // fine for a single-operator dashboard, sized directly off kCapacity so
    // it can't silently fall out of sync if that changes.
    static ContingencyEntry all_entries[ContingencyMemory::kCapacity];
    size_t n = contingency.top_entries(all_entries, ContingencyMemory::kCapacity);
    JsonArray contArr = doc["contingency"].to<JsonArray>();
    for (size_t i = 0; i < n; i++) {
        JsonObject o = contArr.add<JsonObject>();
        JsonObject act = o["actuators"].to<JsonObject>();
        for (size_t c = 0; c < body.actuator_count(); c++) {
            int8_t d = ContingencyMemory::decode_channel(all_entries[i].action_code, c);
            if (d != 0) act[body.actuator_at(c).name()] = d;
        }
        JsonObject sen = o["sensors"].to<JsonObject>();
        for (size_t c = 0; c < body.sensor_count(); c++) {
            int8_t d = ContingencyMemory::decode_channel(all_entries[i].sensor_code, c);
            if (d != 0) sen[body.sensor_at(c).name()] = d;
        }
        o["ctx_hash"] = all_entries[i].ctx_hash;
        o["strength"] = all_entries[i].strength;
        o["mean_drive_delta"] = all_entries[i].mean_drive_delta;
        o["age"] = all_entries[i].age;
    }

    JsonArray spatArr = doc["spatial"].to<JsonArray>();
    for (size_t i = 0; i < SpatialMemory::capacity(); i++) {
        const SpatialCell& c = spatial.cell_at(i);
        if (!c.occupied) continue;
        JsonObject o = spatArr.add<JsonObject>();
        JsonObject sen = o["sensors"].to<JsonObject>();
        for (size_t ch = 0; ch < body.sensor_count(); ch++) {
            sen[body.sensor_at(ch).name()] = SpatialMemory::decode_level(c.signature, ch);
        }
        o["visit_count"] = c.visit_count;
        o["age"] = c.age;
        JsonObject di = o["drive_improvement"].to<JsonObject>();
        for (size_t v = 0; v < kPhysVarCount; v++) {
            di[Physiology::var_name(static_cast<PhysVar>(v))] = c.drive_improvement[v];
        }
    }
}

// Returns {contingency_restored, spatial_restored, fingerprint_matched}.
struct RestoreResult {
    size_t contingency_restored = 0;
    size_t spatial_restored = 0;
    bool fingerprint_matched = false;
};

RestoreResult restore_state_json(Body& body, Physiology& phys, ContingencyMemory& contingency,
                                  SpatialMemory& spatial, JsonDocument& doc, uint32_t& out_age_ms) {
    RestoreResult result;

    out_age_ms = doc["age_ms"] | 0;
    const char* fp = doc["channel_fingerprint"] | "";
    result.fingerprint_matched = compute_fingerprint(body) == fp;

    JsonObject physObj = doc["physiology"];
    for (size_t v = 0; v < kPhysVarCount; v++) {
        PhysVar pv = static_cast<PhysVar>(v);
        JsonVariant val = physObj[Physiology::var_name(pv)];
        if (!val.isNull()) phys.set_value(pv, val.as<float>());
    }

    contingency.clear();
    for (JsonVariant item : doc["contingency"].as<JsonArray>()) {
        JsonObject o = item.as<JsonObject>();
        uint32_t action_code = 0, sensor_code = 0;

        for (JsonPair kv : o["actuators"].as<JsonObject>()) {
            for (size_t c = 0; c < body.actuator_count(); c++) {
                if (strcmp(body.actuator_at(c).name(), kv.key().c_str()) == 0) {
                    action_code = ContingencyMemory::encode_channel(action_code, c, kv.value().as<int>());
                    break;
                }
            }
        }
        for (JsonPair kv : o["sensors"].as<JsonObject>()) {
            for (size_t c = 0; c < body.sensor_count(); c++) {
                if (strcmp(body.sensor_at(c).name(), kv.key().c_str()) == 0) {
                    sensor_code = ContingencyMemory::encode_channel(sensor_code, c, kv.value().as<int>());
                    break;
                }
            }
        }

        contingency.restore_raw(action_code, sensor_code, o["ctx_hash"] | 0, o["strength"] | 0.0f,
                                 o["mean_drive_delta"] | 0.0f, o["age"] | 0);
        result.contingency_restored++;
    }

    spatial.clear();
    for (JsonVariant item : doc["spatial"].as<JsonArray>()) {
        JsonObject o = item.as<JsonObject>();
        uint32_t signature = 0;

        for (JsonPair kv : o["sensors"].as<JsonObject>()) {
            for (size_t c = 0; c < body.sensor_count(); c++) {
                if (strcmp(body.sensor_at(c).name(), kv.key().c_str()) == 0) {
                    signature = SpatialMemory::encode_level(signature, c, kv.value().as<uint32_t>());
                    break;
                }
            }
        }

        float drive_improvement[kPhysVarCount] = {};
        JsonObject di = o["drive_improvement"];
        for (size_t v = 0; v < kPhysVarCount; v++) {
            JsonVariant val = di[Physiology::var_name(static_cast<PhysVar>(v))];
            if (!val.isNull()) drive_improvement[v] = val.as<float>();
        }

        spatial.restore_raw(signature, o["visit_count"] | 1, o["age"] | 0, drive_improvement);
        result.spatial_restored++;
    }

    return result;
}

// Age since first boot, surviving restores: total_age = millis() + offset,
// where offset gets set to (restored_age - millis()) on a successful
// restore so the reported age keeps accumulating instead of resetting.
//
// If restored_age_ms < millis() (restoring an "older" state onto a board
// that's already been running longer than that), this subtraction wraps in
// uint32_t — that's fine, not a bug: current_age_ms()'s addition wraps back
// by the same amount, and modular arithmetic makes the two cancel exactly,
// leaving restored_age_ms + (elapsed time since restore). Same safe idiom
// used for every millis()-delta in this codebase (Actuator's rate limiter,
// manual-override window, main.cpp's tick scheduling); it only breaks if
// the elapsed *real* time itself exceeds ~49.7 days (2^32 ms), which is a
// millis() wraparound concern shared by all of them equally, not specific
// to this offset.
uint32_t g_age_offset_ms = 0;
uint32_t current_age_ms() { return millis() + g_age_offset_ms; }

// POST /api/state can be tens of KB — larger than a single TCP segment — so
// ESPAsyncWebServer's body callback fires multiple times per request. This
// accumulates chunks until the full body has arrived. One upload at a time;
// fine for a single-operator dashboard.
String g_upload_buffer;
}  // namespace

namespace dashboard {

void begin(Body& body, Physiology& phys, ContingencyMemory& contingency, SpatialMemory& spatial,
           SafetyMonitor& safety) {
    // Mount without formatting first — LittleFS.begin(true) reformats on
    // any mount failure, which would silently wipe a saved life-state and
    // the flight log on a field device that hit a corrupted filesystem
    // (power loss mid-write, etc.) instead of surfacing the problem.
    // Formatting is a last resort, tried only if the safe mount fails too,
    // and logged loudly either way.
    if (!LittleFS.begin(false)) {
        Serial.println("[dashboard] LittleFS mount failed; retrying with format (any saved state/log will be lost)");
        if (!LittleFS.begin(true)) {
            Serial.println("[dashboard] LittleFS mount failed even after formatting — dashboard/log/state unavailable");
        }
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

    server.on("/api/channels", HTTP_GET, [&body, &phys, &safety](AsyncWebServerRequest* request) {
        JsonDocument doc;
        build_channels_json(body, phys, safety, doc);
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

    server.on("/api/state", HTTP_GET,
              [&body, &phys, &contingency, &spatial](AsyncWebServerRequest* request) {
                  JsonDocument doc;
                  build_state_json(body, phys, contingency, spatial, current_age_ms(), doc);
                  String out;
                  serializeJson(doc, out);
                  AsyncWebServerResponse* response = request->beginResponse(200, "application/json", out);
                  response->addHeader("Content-Disposition", "attachment; filename=\"emergent-state.json\"");
                  request->send(response);
              });

    server.on(
        "/api/state", HTTP_POST, [](AsyncWebServerRequest* request) {},
        nullptr,
        [&body, &phys, &contingency, &spatial](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                                                size_t index, size_t total) {
            if (total > kMaxStateUploadBytes) {
                request->send(413, "application/json", R"({"error":"state too large"})");
                g_upload_buffer = String();
                return;
            }
            if (index == 0) g_upload_buffer = String();
            g_upload_buffer.concat(reinterpret_cast<const char*>(data), len);
            if (index + len < total) return;  // wait for the rest

            JsonDocument doc;
            if (deserializeJson(doc, g_upload_buffer)) {
                request->send(400, "application/json", R"({"error":"bad json"})");
                g_upload_buffer = String();
                return;
            }
            g_upload_buffer = String();

            uint32_t restored_age_ms = 0;
            RestoreResult r = restore_state_json(body, phys, contingency, spatial, doc, restored_age_ms);
            g_age_offset_ms = restored_age_ms - millis();

            JsonDocument resp;
            resp["ok"] = true;
            resp["fingerprint_matched"] = r.fingerprint_matched;
            resp["contingency_restored"] = r.contingency_restored;
            resp["spatial_restored"] = r.spatial_restored;
            String out;
            serializeJson(resp, out);
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

void loop_tick(Body& body, Physiology& phys, SafetyMonitor& safety) {
    ws.cleanupClients();

    uint32_t now = millis();
    if (now - last_broadcast_ms < kBroadcastIntervalMs) return;
    last_broadcast_ms = now;

    if (ws.count() == 0) return;

    JsonDocument doc;
    build_channels_json(body, phys, safety, doc);
    String out;
    serializeJson(doc, out);
    ws.textAll(out);
}

}  // namespace dashboard
