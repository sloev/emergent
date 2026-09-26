#pragma once
//
// One clampf, shared by every header/source that needs it, instead of a
// file-local copy per translation unit.

inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
