#pragma once
//
// Charge-state thresholds — tune for your battery chemistry/pack/sense
// resistor. Kept in one place, separate from pins, since these are the
// values you'd actually adjust per battery, not per board.

struct Tuning {
    float current_active_threshold = 0.05f;  // normalized current above which charging reads "active"
    float voltage_full_threshold = 0.95f;    // normalized voltage above which the pack reads "full"
    float current_taper_threshold = 0.02f;   // normalized current below which charge current has tapered
};

constexpr Tuning kTuning{};
