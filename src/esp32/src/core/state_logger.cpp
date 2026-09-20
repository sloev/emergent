#include "core/state_logger.h"

#include <Arduino.h>
#include <LittleFS.h>

namespace {
void write_header(File& f) {
    f.print("t_ms");
    for (size_t i = 0; i < kPhysVarCount; i++) {
        f.print(",");
        f.print(Physiology::var_name(static_cast<PhysVar>(i)));
    }
    f.println(",fuzz,actuator_cost");
}
}  // namespace

void StateLogger::begin() {
    if (LittleFS.exists(kLogPath)) return;

    File f = LittleFS.open(kLogPath, "w");
    if (!f) {
        Serial.println("[log] could not create log file");
        return;
    }
    write_header(f);
    f.close();
}

void StateLogger::tick(Body& body, Physiology& phys, uint32_t age_ms) {
    File check = LittleFS.open(kLogPath, "r");
    size_t size = check ? check.size() : 0;
    if (check) check.close();

    if (size >= kMaxLogBytes) {
        LittleFS.remove(kLogPath);
        begin();
    }

    File f = LittleFS.open(kLogPath, "a");
    if (!f) return;

    float total_cost = 0.0f;
    for (size_t i = 0; i < body.actuator_count(); i++) total_cost += body.actuator_at(i).cost();

    f.print(age_ms);
    for (size_t i = 0; i < kPhysVarCount; i++) {
        f.print(",");
        f.print(phys.value(static_cast<PhysVar>(i)), 4);
    }
    f.print(",");
    f.print(phys.fuzz_scale(), 4);
    f.print(",");
    f.println(total_cost, 4);
    f.close();
}
