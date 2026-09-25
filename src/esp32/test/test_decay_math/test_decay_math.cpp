#include <unity.h>

#include <cstdint>

#include "core/decay_math.h"

void setUp(void) {}
void tearDown(void) {}

void test_bump_age_increments_normally(void) {
    TEST_ASSERT_EQUAL_UINT16(1, bump_age_saturating(0));
    TEST_ASSERT_EQUAL_UINT16(101, bump_age_saturating(100));
}

// This is the exact bug from GH issue #1: age is a uint16_t incremented
// every tick with plain age++, which silently wraps to 0 (looking "just
// reinforced") after 65536 ticks — about 55 minutes at this project's 20 Hz
// behavior tick. bump_age_saturating() must never produce that wraparound.
void test_bump_age_saturates_instead_of_wrapping(void) {
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, bump_age_saturating(UINT16_MAX));
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, bump_age_saturating(UINT16_MAX - 1));
    TEST_ASSERT_NOT_EQUAL(0, bump_age_saturating(UINT16_MAX));
}

void test_decay_strength_applies_multiplicative_rate(void) {
    float decayed = decay_strength(1.0f, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.9f, decayed);

    float zero_rate = decay_strength(0.5f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, zero_rate);
}

void test_decay_strength_approaches_zero_over_repeated_calls(void) {
    float s = 1.0f;
    for (int i = 0; i < 1000; i++) s = decay_strength(s, 0.005f);
    TEST_ASSERT_TRUE(s < 0.01f);
    TEST_ASSERT_TRUE(s >= 0.0f);
}

// This is the other half of GH issue #1's decay complaint: strength decays
// asymptotically and never reaches exactly 0, so without an explicit prune
// threshold a decayed entry would occupy a table slot forever as denormal
// noise.
void test_should_prune_threshold_boundary(void) {
    TEST_ASSERT_TRUE(should_prune(0.01f, 0.02f));
    TEST_ASSERT_FALSE(should_prune(0.02f, 0.02f));
    TEST_ASSERT_FALSE(should_prune(0.5f, 0.02f));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_bump_age_increments_normally);
    RUN_TEST(test_bump_age_saturates_instead_of_wrapping);
    RUN_TEST(test_decay_strength_applies_multiplicative_rate);
    RUN_TEST(test_decay_strength_approaches_zero_over_repeated_calls);
    RUN_TEST(test_should_prune_threshold_boundary);
    return UNITY_END();
}
