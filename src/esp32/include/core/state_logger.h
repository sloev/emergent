#pragma once
//
// Slow task, called from main.cpp's loop() at ~1 Hz — decoupled from the
// 20 Hz behavior tick since flash writes are far slower than sensing/acting.
// Appends a compact CSV snapshot to LittleFS for later offline analysis.
// Bounded size — once the log exceeds kMaxLogBytes it starts over rather
// than growing unbounded on a small flash partition.
//
// The log file lives in the same LittleFS root the dashboard serves static
// files from, so it's downloadable at /log.csv with no extra server code.

#include "body/body.h"
#include "core/physiology.h"

class StateLogger {
public:
    void begin();

    // Call at ~1 Hz. age_ms is just millis() at call time — logged as a
    // column so entries can be placed on a timeline after the fact.
    void tick(Body& body, Physiology& phys, uint32_t age_ms);

private:
    static constexpr const char* kLogPath = "/log.csv";
    static constexpr size_t kMaxLogBytes = 65536;  // 64KB cap
};
