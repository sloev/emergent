// Unit tests for the organism: make -C src/sim test
#include <cmath>
#include <cstdio>

#include "organism.h"

static int failures = 0;
#define CHECK(c) \
    do { \
        if (!(c)) { \
            printf("  line %d: %s\n", __LINE__, #c); \
            failures++; \
            return; \
        } \
    } while (0)
#define RUN(f) \
    do { \
        int before = failures; \
        f(); \
        printf("%s %s\n", failures == before ? "ok  " : "FAIL", #f); \
    } while (0)

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
    CHECK(fabsf((oa[0]) - (ob[0])) < 1e-6f);
    CHECK(fabsf((oa[1]) - (ob[1])) < 1e-6f);
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
        CHECK(out[0] >= -1.0f && out[0] <= 1.0f);
        CHECK(out[1] >= 0.0f && out[1] <= 1.0f);
    }
    for (size_t i = 0; i < Organism::kNeurons; i++) CHECK(fabsf(o.neuron(i)) <= 1.0f);
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
    CHECK(fabsf((0.0f) - (o.hunger())) < 1e-6f);
    in[0] = 0.2f;
    for (int t = 0; t < 10; t++) o.tick(in, out, 0.05f);  // half a second
    CHECK(o.hunger() < 0.1f);
    for (int t = 0; t < 2400; t++) o.tick(in, out, 0.05f);  // two minutes
    CHECK(o.hunger() > 0.5f);
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
    CHECK(hurt > 0.3f);
    for (int t = 0; t < 400; t++) o.tick(in, out, 0.05f);  // 20 s
    CHECK(o.pain() < hurt * 0.3f);
}

// Nothing happening -> boredom rises; surprises -> it falls again.
void test_boredom_is_habituation(void) {
    Genome g;
    Organism o;
    o.begin(g, 2, 2, 0, 0, kBip);
    float in[2] = {0.9f, 0.5f}, out[2];
    for (int t = 0; t < 1200; t++) o.tick(in, out, 0.05f);  // 60 s of nothing
    float bored = o.boredom();
    CHECK(bored > 0.8f);
    for (int t = 0; t < 400; t++) {
        in[1] = (t % 7 == 0) ? 0.95f : 0.05f;  // irregular events
        o.tick(in, out, 0.05f);
    }
    CHECK(o.boredom() < bored - 0.2f);
}

// Curiosity can't be held by noise: an input that flickers at random is
// surprising forever but teaches nothing, so it gets boring; a new pattern
// the forward model can learn relieves boredom while it is being learned.
static float boredom_after(bool learnable) {
    Genome g;
    g.seed = 3;
    Organism o;
    o.begin(g, 2, 2, 0, 0, kBip);
    float in[2] = {0.9f, 0.5f}, out[2];
    uint32_t r = 12345;
    for (int t = 0; t < 1200; t++) o.tick(in, out, 0.05f);  // 60 s of nothing: bored
    float peak = 0.0f;
    for (int t = 0; t < 2400; t++) {                        // then 2 minutes of the new input
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        in[1] = learnable ? 0.5f + 0.4f * sinf(t * 0.05f) : static_cast<float>(r % 1000) / 1000.0f;
        o.tick(in, out, 0.05f);
        if (t > 1200 && o.boredom() > peak) peak = o.boredom();
    }
    return peak;
}

void test_noise_is_boring_learnable_is_not(void) {
    float noise = boredom_after(false);
    float pattern = boredom_after(true);
    CHECK(noise > 0.8f);
    CHECK(pattern < noise - 0.1f);
}

// Satiety: energy above its set-point is fullness, a need of its own, so
// a full battery is no longer a reason to stay where it charges.
void test_full_energy_is_fullness(void) {
    Genome g;
    g.satiety_setpoint = 0.9f;
    Organism o;
    o.begin(g, 2, 2, 0, 0, kBip);
    float in[2] = {0.8f, 0.5f}, out[2];
    for (int t = 0; t < 2400; t++) o.tick(in, out, 0.05f);
    CHECK(fabsf((0.0f) - (o.fullness())) < 1e-6f);
    in[0] = 1.0f;
    for (int t = 0; t < 2400; t++) o.tick(in, out, 0.05f);
    CHECK(o.fullness() > 0.8f);
    CHECK(fabsf((0.0f) - (o.hunger())) < 1e-6f);
}

// Places: a situation that looks different becomes a new place; coming
// back to the first one recognizes it instead of making a third.
void test_places_are_recognized(void) {
    Genome g;
    Organism o;
    o.begin(g, 3, 2, 0, 0, kBip);
    float a[3] = {0.9f, 0.2f, 0.2f}, b[3] = {0.9f, 0.8f, 0.7f}, out[2];
    for (int t = 0; t < 200; t++) o.tick(a, out, 0.05f);
    int pa = o.place();
    for (int t = 0; t < 200; t++) o.tick(b, out, 0.05f);
    int pb = o.place();
    for (int t = 0; t < 200; t++) o.tick(a, out, 0.05f);
    CHECK(pa >= 0 && pb >= 0 && pa != pb);
    CHECK((pa) == (o.place()));
    // Settling from one place to the other passes through in-between
    // situations, which may count as places; it doesn't grow without bound.
    CHECK(o.place_count() >= 2 && o.place_count() <= 6);
    CHECK(o.familiarity(static_cast<size_t>(pa)) > o.familiarity(static_cast<size_t>(pb)));
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
    CHECK(up > down);
    CHECK(up > 0.0f);
    CHECK(down < 0.0f);
}

int main(int, char**) {
    RUN(test_same_genome_same_life);
    RUN(test_outputs_and_brain_stay_bounded);
    RUN(test_low_energy_is_hunger);
    RUN(test_pain_from_declared_input_fades);
    RUN(test_boredom_is_habituation);
    RUN(test_noise_is_boring_learnable_is_not);
    RUN(test_full_energy_is_fullness);
    RUN(test_places_are_recognized);
    RUN(test_dopamine_shapes_the_readout);
    return failures ? 1 : 0;
}
