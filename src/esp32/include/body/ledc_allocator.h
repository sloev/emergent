#pragma once
//
// Pure bounded-counter logic behind ESP32's 16 hardware LEDC channels,
// factored out of Actuator for host-side testing (see
// test/native/test_ledc_allocator.cpp). The real allocation path
// (actuator.cpp) wraps this with the Serial logging and ledcSetup()/
// ledcAttachPin() hardware calls that can't run on the host.
//
// ESP32 has exactly 16 LEDC channels. Silently handing out a 17th would
// either alias two actuators onto the same channel (they'd fight over its
// duty cycle) or pass ledcSetup() an invalid channel number — this fails
// loudly (returns kNone) instead.

#include <cstdint>

class LedcChannelAllocator {
public:
    static constexpr uint8_t kChannelCount = 16;
    static constexpr uint8_t kNone = 0xFF;

    uint8_t allocate() {
        if (next_ >= kChannelCount) return kNone;
        return next_++;
    }

    uint8_t allocated_count() const { return next_; }

private:
    uint8_t next_ = 0;
};
