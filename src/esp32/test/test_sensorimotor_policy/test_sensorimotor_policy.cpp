#include <unity.h>

#include "core/action_generator.h"
#include "core/sensorimotor_policy.h"
#include "../test_behavior_scenarios/fake_body.h"

void setUp(void) {}
void tearDown(void) {}

static const ActuatorSpec kOneMotor[] = {
    {"motor", ActuatorKind::kPwmBidirectional, 0, 0, -1.0f, 1.0f, 0.0f, 0.0f},
};
static const SensorSpec kOneSensor[] = {
    {"s", SensorKind::kAdcNormalized, 0, 0.0f},
};
static const BoardConfig kBoard = {"policy-test", "t", "t", kOneMotor, 1, kOneSensor, 1, nullptr};

// Reward that consistently follows pushing the motor *up* while the sensor
// reads above its usual level should grow a positive sensor -> motor weight;
// the same reward following pushing it *down* should grow a negative one.
static float train(float sign) {
    FakeBody body(kBoard);
    body.begin();
    Physiology phys;
    phys.begin(body);

    SensorimotorPolicy p;
    p.begin(body);

    // Drive the fatigue variable directly so total_drive() is under our
    // control: lower fatigue = lower drive = reward.
    float fatigue = 0.95f;
    for (int t = 0; t < 36000; t++) {  // 30 simulated minutes
        bool high = (t / 20) % 2 == 0;  // sensor alternates every second
        body.sensor_at(0).set_value(high ? 0.8f : 0.2f);

        phys.set_value(PhysVar::kFatigue, fatigue);
        phys.update(body, 0.05f, 0.0f);
        p.learn(phys, 0.05f);

        float out[SensorimotorPolicy::kMaxOut] = {};
        p.propose(body, phys, 0.05f, out);

        // Exploration holds its sign for 1.5 s at a time (like the real
        // correlated noise), out of phase with the sensor; the "world"
        // rewards the chosen direction only while the sensor is high.
        float xi = ((t / 30) % 2 == 0) ? 0.3f : -0.3f;
        float taken[SensorimotorPolicy::kMaxOut] = {xi};
        p.record(taken, 0.05f);

        bool rewarded = high && (xi * sign > 0.0f);
        fatigue = rewarded ? fatigue - 0.01f : fatigue + 0.002f;
        if (fatigue < 0.7f) fatigue = 0.95f;
        if (fatigue > 0.95f) fatigue = 0.95f;
    }
    return p.weight(0, 0, 0);  // always-on context, sensor 0 -> motor 0
}

void test_reward_following_exploration_shapes_the_weight(void) {
    // Direction is the claim; magnitude is capped by weight decay.
    float up = train(+1.0f);
    float down = train(-1.0f);
    TEST_ASSERT_TRUE(up > 0.01f);
    TEST_ASSERT_TRUE(down < -0.01f);
}

// Pure noise reward (uncorrelated with exploration) must not drive the
// weights anywhere in particular: the runaway this guards against came from
// crediting a constant offset rather than the zero-mean exploration sample.
void test_uncorrelated_reward_does_not_saturate_weights(void) {
    FakeBody body(kBoard);
    body.begin();
    Physiology phys;
    phys.begin(body);
    ContingencyMemory cm;
    cm.begin(body);
    SpatialMemory spatial;
    ActionGenerator gen;
    gen.set_seed(7);
    gen.begin_learning(body);

    // Proposals beyond the motor's range: the old credit rule saw a
    // constant "taken" offset here and ran every weight to the limit.
    gen.policy().set_weight(0, 1, 0, 1.4f);

    uint32_t lcg = 12345;
    for (int t = 0; t < 20000; t++) {
        lcg = lcg * 1664525u + 1013904223u;
        body.sensor_at(0).set_value(static_cast<float>(lcg >> 8) / 16777216.0f);
        phys.update(body, 0.05f, 0.0f);
        gen.tick(body, phys, cm, spatial, static_cast<uint32_t>(t) * 50u);
    }
    float w = gen.policy().weight(0, 0, 0);
    TEST_ASSERT_TRUE(w < 1.0f && w > -1.0f);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_reward_following_exploration_shapes_the_weight);
    RUN_TEST(test_uncorrelated_reward_does_not_saturate_weights);
    return UNITY_END();
}
