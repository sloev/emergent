#include <unity.h>

#include "body/ledc_allocator.h"

void setUp(void) {}
void tearDown(void) {}

void test_allocates_sequential_channels(void) {
    LedcChannelAllocator alloc;
    for (uint8_t i = 0; i < LedcChannelAllocator::kChannelCount; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, alloc.allocate());
    }
}

// ESP32 has 16 LEDC channels; a board profile with more PWM/servo
// actuators than that must fail loudly (kNone) rather than silently
// wrapping into a reused/invalid channel number.
void test_exhaustion_returns_none_instead_of_wrapping_or_colliding(void) {
    LedcChannelAllocator alloc;
    for (uint8_t i = 0; i < LedcChannelAllocator::kChannelCount; i++) {
        alloc.allocate();
    }
    uint8_t seventeenth = alloc.allocate();
    TEST_ASSERT_EQUAL_UINT8(LedcChannelAllocator::kNone, seventeenth);

    // Still refuses on subsequent calls too, not just the first overflow.
    TEST_ASSERT_EQUAL_UINT8(LedcChannelAllocator::kNone, alloc.allocate());
}

void test_allocated_count_tracks_successful_allocations_only(void) {
    LedcChannelAllocator alloc;
    for (int i = 0; i < 5; i++) alloc.allocate();
    TEST_ASSERT_EQUAL_UINT8(5, alloc.allocated_count());

    for (uint8_t i = 5; i < LedcChannelAllocator::kChannelCount; i++) alloc.allocate();
    alloc.allocate();  // exhausted; must not increment past kChannelCount
    TEST_ASSERT_EQUAL_UINT8(LedcChannelAllocator::kChannelCount, alloc.allocated_count());
}

void test_two_allocators_are_independent(void) {
    // Actuator::begin() previously used a function-local `static uint8_t
    // next_channel` shared across every Actuator instance forever — fine
    // for the real singleton use case, but worth confirming the extracted
    // class itself has no hidden shared state making that assumption for
    // it, since a future caller might reasonably expect independent
    // instances to behave independently.
    LedcChannelAllocator a;
    LedcChannelAllocator b;
    a.allocate();
    a.allocate();
    TEST_ASSERT_EQUAL_UINT8(0, b.allocate());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_allocates_sequential_channels);
    RUN_TEST(test_exhaustion_returns_none_instead_of_wrapping_or_colliding);
    RUN_TEST(test_allocated_count_tracks_successful_allocations_only);
    RUN_TEST(test_two_allocators_are_independent);
    return UNITY_END();
}
