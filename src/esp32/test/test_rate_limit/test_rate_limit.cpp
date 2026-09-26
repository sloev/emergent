#include <unity.h>

#include "body/rate_limit.h"

void setUp(void) {}
void tearDown(void) {}

void test_unlimited_rate_jumps_straight_to_target(void) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, apply_rate_limit(0.0f, 1.0f, 0.0f, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, apply_rate_limit(0.5f, -1.0f, -1.0f, 100.0f));
}

void test_rate_limit_clamps_step_upward(void) {
    // max_rate_per_s=4, elapsed=0.05s (one 20 Hz tick) -> max step 0.2
    float result = apply_rate_limit(0.0f, 1.0f, 4.0f, 0.05f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.2f, result);
}

void test_rate_limit_clamps_step_downward(void) {
    float result = apply_rate_limit(1.0f, -1.0f, 4.0f, 0.05f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.8f, result);
}

void test_rate_limit_does_not_overshoot_when_close_to_target(void) {
    // Already within one tick's worth of the target: should land exactly on
    // it, not overshoot past it.
    float result = apply_rate_limit(0.95f, 1.0f, 4.0f, 0.05f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, result);
}

void test_zero_elapsed_time_means_no_movement(void) {
    float result = apply_rate_limit(0.3f, 1.0f, 4.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.3f, result);
}

// A channel that's gone unwritten a long time, or a blocked loop(), can
// produce an anomalously large elapsed_s reading. clamp_elapsed_seconds()
// bounds it to a sane ceiling before it's ever used as a rate-limit
// multiplier.
void test_clamp_elapsed_seconds_bounds_oversized_readings(void) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, clamp_elapsed_seconds(4200.0f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, clamp_elapsed_seconds(1.0f, 1.0f));
}

void test_clamp_elapsed_seconds_passes_normal_readings_through(void) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.05f, clamp_elapsed_seconds(0.05f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, clamp_elapsed_seconds(0.0f, 1.0f));
}

void test_clamped_elapsed_time_bounds_a_single_ticks_step(void) {
    // End-to-end: even a wildly oversized elapsed-time reading can only
    // move the actuator by max_rate_per_s * kMaxElapsedS in one call, never
    // further — this is what actually prevents the "huge delta" failure
    // mode described in the issue, regardless of what produced the bad
    // elapsed-time reading.
    float elapsed = clamp_elapsed_seconds(9999.0f, 1.0f);
    float result = apply_rate_limit(0.0f, 1.0f, 4.0f, elapsed);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, result);  // capped by target, not by an oversized delta
    float result2 = apply_rate_limit(0.0f, 100.0f, 4.0f, elapsed);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 4.0f, result2);  // max_rate_per_s * kMaxElapsedS(1.0), not 100
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_unlimited_rate_jumps_straight_to_target);
    RUN_TEST(test_rate_limit_clamps_step_upward);
    RUN_TEST(test_rate_limit_clamps_step_downward);
    RUN_TEST(test_rate_limit_does_not_overshoot_when_close_to_target);
    RUN_TEST(test_zero_elapsed_time_means_no_movement);
    RUN_TEST(test_clamp_elapsed_seconds_bounds_oversized_readings);
    RUN_TEST(test_clamp_elapsed_seconds_passes_normal_readings_through);
    RUN_TEST(test_clamped_elapsed_time_bounds_a_single_ticks_step);
    return UNITY_END();
}
