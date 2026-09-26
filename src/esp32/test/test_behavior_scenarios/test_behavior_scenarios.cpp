#include <unity.h>

#include "core/action_generator.h"
#include "core/contingency_memory.h"
#include "core/physiology.h"
#include "core/spatial_memory.h"
#include "fake_body.h"

void setUp(void) {}
void tearDown(void) {}

// --- scenario 1: energy falls under sustained actuator cost -----------------

static const ActuatorSpec kCostActuators[] = {
    {"motor", ActuatorKind::kPwmUnipolar, 0, 0, -1.0f, 1.0f, 0.0f, 1.0f},
};
static const BoardConfig kCostBoard = {
    "cost-test", "test", "test", kCostActuators, 1, nullptr, 0, nullptr,
};

void test_energy_falls_under_sustained_actuator_cost(void) {
    FakeBody body(kCostBoard);
    body.begin();

    Physiology phys;
    phys.begin(body);
    float initial_energy = phys.value(PhysVar::kEnergy);

    body.actuator_at(0).set_value(1.0f);  // pinned at full range -> cost = 1.0 every tick

    for (int i = 0; i < 60; i++) {
        phys.update(body, 0.05f, 0.0f);  // 60 ticks * 50ms = 3s
    }

    TEST_ASSERT_TRUE(phys.value(PhysVar::kEnergy) < initial_energy - 0.15f);
    TEST_ASSERT_TRUE(phys.value(PhysVar::kEnergy) >= 0.0f);
}

// --- scenario 2: energy rises toward a high energy-sensor reading -----------

static const SensorSpec kBatterySensors[] = {
    {"battery", SensorKind::kAdcNormalized, 0, 1.0f},
};
static const BoardConfig kBatteryBoard = {
    "battery-test", "test", "test", nullptr, 0, kBatterySensors, 1, "battery",
};

void test_energy_rises_toward_high_energy_sensor(void) {
    FakeBody body(kBatteryBoard);
    body.begin();

    Physiology phys;
    phys.begin(body);
    float initial_energy = phys.value(PhysVar::kEnergy);

    body.sensor_at(0).set_value(0.95f);

    for (int i = 0; i < 60; i++) {
        phys.update(body, 0.05f, 0.0f);
    }

    TEST_ASSERT_TRUE(phys.value(PhysVar::kEnergy) > initial_energy);
    TEST_ASSERT_TRUE(phys.value(PhysVar::kEnergy) > 0.85f);
    TEST_ASSERT_TRUE(phys.value(PhysVar::kEnergy) <= 1.0f);
}

// --- scenario 3: a repeated (action, sensor-delta, drive-drop) pattern -----
// measurably biases ActionGenerator's output for that actuator.

static const ActuatorSpec kBiasActuators[] = {
    {"motor", ActuatorKind::kPwmUnipolar, 0, 0, -1.0f, 1.0f, 0.0f, 0.0f},  // cost 0: isolates
                                                                            // fatigue from cost
};
static const SensorSpec kBiasSensors[] = {
    {"light", SensorKind::kAdcNormalized, 0, 1.0f},
};
static const BoardConfig kBiasBoard = {
    "bias-test", "test", "test", kBiasActuators, 1, kBiasSensors, 1, nullptr,
};

void test_repeated_pattern_biases_action_generator(void) {
    FakeBody body(kBiasBoard);
    body.begin();

    Physiology phys;
    phys.begin(body);
    // Excess fatigue with actuator cost pinned at 0 for this whole scenario
    // means fatigue can only recover, giving a reliable, unconditioned
    // falling-total-drive ("things got better") signal every tick, real
    // physiology dynamics rather than a hand-set drive value.
    phys.set_value(PhysVar::kFatigue, 0.9f);

    ContingencyMemory cm;
    cm.begin(body);

    // Toggle actuator+sensor together for 31 ticks (30 transitions): the
    // first call only seeds ContingencyMemory's internal "previous state",
    // every call after that observes a real delta. Ends on a rising
    // transition so the memory's last-observed pattern is "rose". The
    // sensor swing is small (0.05, still comfortably above the 0.02
    // deadzone) deliberately — a large swing would itself drive arousal/
    // safety enough to swamp the fatigue-recovery signal this scenario is
    // trying to isolate. The actuator swing can be large (cost_per_unit is
    // 0 for this board, so it has no physiological side effect at all).
    for (int i = 0; i <= 30; i++) {
        bool high = (i % 2) == 0;
        body.actuator_at(0).set_value(high ? 0.6f : 0.0f);
        body.sensor_at(0).set_value(high ? 0.55f : 0.5f);
        phys.update(body, 0.05f, 0.0f);
        cm.update(body, phys, 0.05f);
    }

    uint32_t rose_code = ContingencyMemory::encode_channel(0, 0, 1);
    uint32_t out_action_code = 0;
    float out_confidence = 0.0f;
    bool found = cm.query_bias(rose_code, out_action_code, out_confidence);

    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(out_confidence > 0.0f);
    TEST_ASSERT_EQUAL_INT8(1, ContingencyMemory::decode_channel(out_action_code, 0));

    // Differential: same seed, same starting actuator value, only the
    // ContingencyMemory differs (trained vs. never-reinforced) — isolates
    // the bias term from noise/rest-pull/spatial terms, which are identical
    // in both runs.
    // Default-constructed and never updated -> best_cell() finds nothing
    // either run (SpatialCell's in-class initializers already zero it).
    SpatialMemory spatial;
    ContingencyMemory cm_fresh;
    cm_fresh.begin(body);

    body.actuator_at(0).set_value(0.0f);
    ActionGenerator gen_trained;
    gen_trained.set_seed(123);
    gen_trained.tick(body, phys, cm, spatial, 1000);
    float trained_output = body.actuator_at(0).value();

    body.actuator_at(0).set_value(0.0f);
    ActionGenerator gen_fresh;
    gen_fresh.set_seed(123);
    gen_fresh.tick(body, phys, cm_fresh, spatial, 1000);
    float fresh_output = body.actuator_at(0).value();

    TEST_ASSERT_TRUE(trained_output > fresh_output + 0.05f);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_energy_falls_under_sustained_actuator_cost);
    RUN_TEST(test_energy_rises_toward_high_energy_sensor);
    RUN_TEST(test_repeated_pattern_biases_action_generator);
    return UNITY_END();
}
