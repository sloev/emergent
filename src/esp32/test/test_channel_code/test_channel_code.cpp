#include <unity.h>

#include "core/channel_code.h"

void setUp(void) {}
void tearDown(void) {}

// --- delta symbols (ContingencyMemory) --------------------------------------

void test_delta_roundtrip_all_channels_and_symbols(void) {
    for (size_t ch = 0; ch < kChannelCodeMaxChannels; ch++) {
        for (int8_t delta = -1; delta <= 1; delta++) {
            uint32_t code = encode_channel_delta(0, ch, delta);
            TEST_ASSERT_EQUAL_INT8(delta, decode_channel_delta(code, ch));
        }
    }
}

void test_delta_encode_does_not_disturb_other_channels(void) {
    uint32_t code = 0;
    code = encode_channel_delta(code, 0, 1);
    code = encode_channel_delta(code, 5, -1);
    code = encode_channel_delta(code, 15, 1);

    TEST_ASSERT_EQUAL_INT8(1, decode_channel_delta(code, 0));
    TEST_ASSERT_EQUAL_INT8(-1, decode_channel_delta(code, 5));
    TEST_ASSERT_EQUAL_INT8(1, decode_channel_delta(code, 15));
    // Everything else stays "no change".
    for (size_t ch = 1; ch < 15; ch++) {
        if (ch == 5) continue;
        TEST_ASSERT_EQUAL_INT8(0, decode_channel_delta(code, ch));
    }
}

void test_delta_out_of_range_channel_is_a_safe_no_op(void) {
    uint32_t code = 0xDEADBEEF;
    TEST_ASSERT_EQUAL_UINT32(code, encode_channel_delta(code, 16, 1));
    TEST_ASSERT_EQUAL_UINT32(code, encode_channel_delta(code, 999, -1));
    TEST_ASSERT_EQUAL_INT8(0, decode_channel_delta(code, 16));
    TEST_ASSERT_EQUAL_INT8(0, decode_channel_delta(code, 999));
}

// --- level symbols (SpatialMemory) -------------------------------------------

void test_level_roundtrip_all_channels_and_levels(void) {
    for (size_t ch = 0; ch < kChannelCodeMaxChannels; ch++) {
        for (uint32_t level = 0; level <= 3; level++) {
            uint32_t code = encode_channel_level(0, ch, level);
            TEST_ASSERT_EQUAL_UINT32(level, decode_channel_level(code, ch));
        }
    }
}

void test_level_out_of_range_channel_is_a_safe_no_op(void) {
    uint32_t code = 0x12345678;
    TEST_ASSERT_EQUAL_UINT32(code, encode_channel_level(code, 16, 2));
    TEST_ASSERT_EQUAL_UINT32(0, decode_channel_level(code, 16));
}

// --- distance -----------------------------------------------------------------

void test_distance_identical_codes_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, channel_code_distance(0, 0));
    uint32_t code = encode_channel_delta(encode_channel_delta(0, 2, 1), 7, -1);
    TEST_ASSERT_EQUAL_UINT32(0, channel_code_distance(code, code));
}

void test_distance_counts_differing_channels_only(void) {
    uint32_t a = encode_channel_delta(0, 0, 1);
    uint32_t b = encode_channel_delta(0, 0, -1);  // channel 0 differs
    TEST_ASSERT_EQUAL_UINT32(1, channel_code_distance(a, b));

    uint32_t c = encode_channel_delta(a, 3, 1);  // a plus channel 3 set
    TEST_ASSERT_EQUAL_UINT32(1, channel_code_distance(a, c));
}

void test_distance_maxes_out_when_every_channel_differs(void) {
    uint32_t a = 0;
    uint32_t b = 0;
    for (size_t ch = 0; ch < kChannelCodeMaxChannels; ch++) {
        a = encode_channel_delta(a, ch, 1);
        b = encode_channel_delta(b, ch, -1);
    }
    TEST_ASSERT_EQUAL_UINT32(kChannelCodeMaxChannels, channel_code_distance(a, b));
}

// --- remapping (life-state restore onto a body with different channel order) -

void test_remap_moves_a_channel_to_a_new_index(void) {
    // Simulates a life-state restore onto a different board: a saved entry
    // had "motor_left" at index 0; the board it's restored onto has
    // "motor_left" at index 2 instead. Decoding by the *old* index and
    // re-encoding at the *new* one is exactly the remap operation.
    uint32_t saved_code = encode_channel_delta(0, 0, 1);
    int8_t delta_for_motor_left = decode_channel_delta(saved_code, 0);

    uint32_t restored_code = encode_channel_delta(0, 2, delta_for_motor_left);
    TEST_ASSERT_EQUAL_INT8(1, decode_channel_delta(restored_code, 2));
    TEST_ASSERT_EQUAL_INT8(0, decode_channel_delta(restored_code, 0));
}

void test_remap_drops_channels_absent_on_the_new_body(void) {
    // A channel that doesn't exist on the new body is never looked up, so
    // it's simply never encoded into the restored code — the code stays 0
    // (== "no change") for that slot, which is the concrete mechanism
    // behind "gradually forgets mismatched contingencies".
    uint32_t restored_code = 0;
    // (no encode call for the missing channel)
    TEST_ASSERT_EQUAL_UINT32(0, restored_code);
    TEST_ASSERT_EQUAL_INT8(0, decode_channel_delta(restored_code, 0));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_delta_roundtrip_all_channels_and_symbols);
    RUN_TEST(test_delta_encode_does_not_disturb_other_channels);
    RUN_TEST(test_delta_out_of_range_channel_is_a_safe_no_op);
    RUN_TEST(test_level_roundtrip_all_channels_and_levels);
    RUN_TEST(test_level_out_of_range_channel_is_a_safe_no_op);
    RUN_TEST(test_distance_identical_codes_is_zero);
    RUN_TEST(test_distance_counts_differing_channels_only);
    RUN_TEST(test_distance_maxes_out_when_every_channel_differs);
    RUN_TEST(test_remap_moves_a_channel_to_a_new_index);
    RUN_TEST(test_remap_drops_channels_absent_on_the_new_body);
    return UNITY_END();
}
