#include <unity.h>

#include <cmath>

#include "life/organism.h"

void setUp(void) {}
void tearDown(void) {}

using life::Genome;
using life::Organism;

static const bool kBip[2] = {true, false};

// Same genome, same inputs -> same outputs: an individual is reproducible.
void test_same_genome_same_life(void) {
    Genome g;
    g.seed = 42;
    Organism a, b;
    a.begin(g, 2, 2, 0, 0, kBip);
    b.begin(g, 2, 2, 0, 0, kBip);
    float in[2] = {0.8f, 0.3f}, oa[2], ob[2];
    for (int t = 0; t < 2000; t++) {
        in[1] = 0.5f + 0.4f * sinf(t * 0.01f);
        a.tick(in, oa, 0.05f);
        b.tick(in, ob, 0.05f);
    }
    TEST_ASSERT_EQUAL_FLOAT(oa[0], ob[0]);
    TEST_ASSERT_EQUAL_FLOAT(oa[1], ob[1]);
}

// Outputs stay in range and the brain stays bounded for a long run.
void test_outputs_and_brain_stay_bounded(void) {
    Genome g;
    g.brain_gain = 1.5f;
    Organism o;
    o.begin(g, 2, 2, 0, 0, kBip);
    float in[2] = {0.5f, 0.5f}, out[2];
    for (int t = 0; t < 72000; t++) {  // one simulated hour
        in[1] = (t / 40) % 2 ? 0.9f : 0.1f;
        o.tick(in, out, 0.05f);
        TEST_ASSERT_TRUE(out[0] >= -1.0f && out[0] <= 1.0f);
        TEST_ASSERT_TRUE(out[1] >= 0.0f && out[1] <= 1.0f);
    }
    for (size_t i = 0; i < Organism::kNeurons; i++) TEST_ASSERT_TRUE(fabsf(o.neuron(i)) <= 1.0f);
}

// Energy below the set-point is hunger; the battery is the only energy fact.
// Felt energy is buffered, so a brief dip (a motor's voltage sag) isn't
// hunger but a sustained low is.
void test_low_energy_is_hunger(void) {
    Genome g;
    Organism o;
    o.begin(g, 2, 2, 0, 0, kBip);
    float in[2] = {0.9f, 0.5f}, out[2];
    o.tick(in, out, 0.05f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, o.hunger());
    in[0] = 0.2f;
    for (int t = 0; t < 10; t++) o.tick(in, out, 0.05f);  // half a second
    TEST_ASSERT_TRUE(o.hunger() < 0.1f);
    for (int t = 0; t < 2400; t++) o.tick(in, out, 0.05f);  // two minutes
    TEST_ASSERT_TRUE(o.hunger() > 0.5f);
}

// A jolt on an input declared painful hurts, and the hurt fades.
void test_pain_from_declared_input_fades(void) {
    Genome g;
    Organism o;
    o.begin(g, 2, 2, 0, 1u << 1, kBip);
    float in[2] = {0.9f, 0.0f}, out[2];
    for (int t = 0; t < 20; t++) o.tick(in, out, 0.05f);
    in[1] = 1.0f;
    o.tick(in, out, 0.05f);
    in[1] = 0.0f;
    float hurt = o.pain();
    TEST_ASSERT_TRUE(hurt > 0.3f);
    for (int t = 0; t < 400; t++) o.tick(in, out, 0.05f);  // 20 s
    TEST_ASSERT_TRUE(o.pain() < hurt * 0.3f);
}

// Nothing happening -> boredom rises; surprises -> it falls again.
void test_boredom_is_habituation(void) {
    Genome g;
    Organism o;
    o.begin(g, 2, 2, 0, 0, kBip);
    float in[2] = {0.9f, 0.5f}, out[2];
    for (int t = 0; t < 1200; t++) o.tick(in, out, 0.05f);  // 60 s of nothing
    float bored = o.boredom();
    TEST_ASSERT_TRUE(bored > 0.8f);
    for (int t = 0; t < 400; t++) {
        in[1] = (t % 7 == 0) ? 0.95f : 0.05f;  // irregular events
        o.tick(in, out, 0.05f);
    }
    TEST_ASSERT_TRUE(o.boredom() < bored - 0.2f);
}

// The core claim of the learning rule: if exploring output 0 upward while
// input 1 is high keeps being followed by energy rising (need falling),
// the readout weight from input 1 to output 0 grows positive.
static float train(bool reward_up) {
    Genome g;
    g.seed = 7;
    g.explore = 0.5f;
    Organism o;
    o.begin(g, 2, 2, 0, 0, kBip);
    float energy = 0.4f, out[2];
    for (int t = 0; t < 72000; t++) {
        float cue = ((t / 60) % 2) ? 0.9f : 0.1f;
        float in[2] = {energy, cue};
        o.tick(in, out, 0.05f);
        bool fed = cue > 0.5f && (reward_up ? out[0] > 0.2f : out[0] < -0.2f);
        energy += fed ? 0.004f : -0.001f;
        if (energy > 0.6f) energy = 0.3f;  // stay hungry so feeding matters
        if (energy < 0.05f) energy = 0.05f;
    }
    // input 1 is feature 1
    return o.weight(0, 1);
}

void test_dopamine_shapes_the_readout(void) {
    float up = train(true);
    float down = train(false);
    TEST_ASSERT_TRUE(up > down);
    TEST_ASSERT_TRUE(up > 0.0f);
    TEST_ASSERT_TRUE(down < 0.0f);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_same_genome_same_life);
    RUN_TEST(test_outputs_and_brain_stay_bounded);
    RUN_TEST(test_low_energy_is_hunger);
    RUN_TEST(test_pain_from_declared_input_fades);
    RUN_TEST(test_boredom_is_habituation);
    RUN_TEST(test_dopamine_shapes_the_readout);
    return UNITY_END();
}
