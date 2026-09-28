#pragma once
//
// Learned reflexes: a small linear map from sensor channels to actuator
// channels, one map per motivational state, trained only by whether drive
// pressure went down afterwards. Nothing is pre-wired. If a light sensor
// on one side ends up driving the wheel on the other side, it's because that
// wiring kept being followed by energy getting better, i.e. the organism
// built itself a Braitenberg vehicle.
//
// Inputs:  every sensor channel relative to its own slow (minutes) running
//          average, plus a constant 1 (a per-state default action). Slow
//          enough that "much brighter than usual here" can still mean
//          "stay"; centred so that inputs which are all positive don't all
//          learn the same direction together.
// Outputs: a proposed value per actuator channel, in channel units.
// State:   kContexts weight maps. Context 0 is always on; context 1+v is
//          gated by drive v in [0,1]. Hungry, the energy map dominates; tired,
//          the fatigue map does. Motivational states selecting different
//          behavior systems is the ethology picture, just learned here.
//
// Learning is REINFORCE / node perturbation (Williams 1992; Fiete & Seung
// 2006) with an eligibility trace. The trace is what lets a docking
// reward that arrives seconds after an approach still credit that approach.
//   e[k][i][j] <- lambda * e + (1-lambda) * g_k * x_i * xi_j     (xi = exploration actually taken)
//   w[k][i][j] += eta * A * e                        (A = normalized drive drop)
// The reward is homeostatic: the fall in Physiology::total_drive() since
// the previous tick (drive reduction, as in Keramati & Gutkin 2014).
//
// Memory: kContexts * (kMaxIn+1) * kMaxOut floats for weights and again for
// traces, about 16 KB at the maximum channel counts, ~2 KB for 7 in / 5 out.

#include <cmath>
#include <cstddef>
#include <cstring>

#include "core/math_util.h"
#include "core/physiology.h"
#include "core/tuning.h"

class SensorimotorPolicy {
public:
    static constexpr size_t kMaxIn = 16;
    static constexpr size_t kMaxOut = 16;
    static constexpr size_t kInputs = kMaxIn + 1;  // + constant bias input
    static constexpr size_t kContexts = kPhysVarCount + 1;

    template <typename BodyT>
    void begin(BodyT& body) {
        memset(w_, 0, sizeof(w_));
        memset(e_, 0, sizeof(e_));
        n_in_ = body.sensor_count() < kMaxIn ? body.sensor_count() : kMaxIn;
        n_out_ = body.actuator_count() < kMaxOut ? body.actuator_count() : kMaxOut;
        for (size_t i = 0; i < n_in_; i++) mean_[i] = body.sensor_at(i).last_value();
        have_prev_drive_ = false;
        reward_mean_ = 0.0f;
        reward_var_ = 1e-6f;
    }

    // Credit the previous tick's actions with how drive pressure changed
    // since. Call once per tick, before propose().
    void learn(const Physiology& phys, float dt_s) {
        float drive = phys.total_drive();
        if (!have_prev_drive_) {
            prev_drive_ = drive;
            have_prev_drive_ = true;
            return;
        }
        float r = prev_drive_ - drive;  // positive = things got better
        prev_drive_ = drive;

        const auto& t = kTuning.policy;
        float a = dt_s / t.reward_baseline_tau_s;
        float dev = r - reward_mean_;
        reward_mean_ += a * dev;
        reward_var_ += a * (dev * dev - reward_var_);
        float sd = sqrtf(reward_var_);
        if (sd < t.reward_sd_floor) sd = t.reward_sd_floor;
        advantage_ = clampf(dev / sd, -t.advantage_clip, t.advantage_clip);

        float step = t.learn_rate * advantage_;
        float keep = 1.0f - t.weight_decay_per_s * dt_s;
        for (size_t k = 0; k < kContexts; k++) {
            for (size_t i = 0; i <= n_in_; i++) {
                for (size_t j = 0; j < n_out_; j++) {
                    float w = (w_[k][i][j] + step * e_[k][i][j]) * keep;
                    w_[k][i][j] = clampf(w, -t.weight_max, t.weight_max);
                }
            }
        }
    }

    // What the learned reflexes propose for each actuator right now, in
    // channel units. Also latches this tick's inputs and gates for record().
    template <typename BodyT>
    void propose(BodyT& body, const Physiology& phys, float dt_s, float* out) {
        float a = dt_s / kTuning.policy.input_mean_tau_s;
        for (size_t i = 0; i < n_in_; i++) {
            float v = body.sensor_at(i).last_value();
            mean_[i] += a * (v - mean_[i]);
            x_[i] = v - mean_[i];
        }
        x_[n_in_] = 1.0f;

        gate_[0] = 1.0f;
        for (size_t v = 0; v < kPhysVarCount; v++) {
            gate_[1 + v] = clampf(phys.drive(static_cast<PhysVar>(v)), 0.0f, 1.0f);
        }

        for (size_t j = 0; j < n_out_; j++) {
            float u = 0.0f;
            for (size_t k = 0; k < kContexts; k++) {
                if (gate_[k] <= 0.0f) continue;
                float s = 0.0f;
                for (size_t i = 0; i <= n_in_; i++) s += w_[k][i][j] * x_[i];
                u += gate_[k] * s;
            }
            out[j] = u;
        }
    }

    // After actuators were written: `taken[j]` is the exploration sample
    // applied to channel j this tick. Folds it into the eligibility traces.
    void record(const float* taken, float dt_s) {
        // Normalized trace: a running average of recent g*x*xi, not a sum,
        // so its size doesn't scale with the trace length.
        float lambda = expf(-dt_s / kTuning.policy.trace_tau_s);
        for (size_t k = 0; k < kContexts; k++) {
            float g = gate_[k];
            for (size_t i = 0; i <= n_in_; i++) {
                float gx = (1.0f - lambda) * g * x_[i];
                for (size_t j = 0; j < n_out_; j++) {
                    e_[k][i][j] = lambda * e_[k][i][j] + gx * taken[j];
                }
            }
        }
    }

    float weight(size_t context, size_t input, size_t output) const { return w_[context][input][output]; }
    void set_weight(size_t context, size_t input, size_t output, float w) { w_[context][input][output] = w; }
    size_t input_count() const { return n_in_; }
    size_t output_count() const { return n_out_; }
    float last_advantage() const { return advantage_; }

private:
    float w_[kContexts][kInputs][kMaxOut] = {};
    float e_[kContexts][kInputs][kMaxOut] = {};
    float mean_[kMaxIn] = {};
    float x_[kInputs] = {};
    float gate_[kContexts] = {};
    size_t n_in_ = 0;
    size_t n_out_ = 0;
    bool have_prev_drive_ = false;
    float prev_drive_ = 0.0f;
    float reward_mean_ = 0.0f;
    float reward_var_ = 1e-6f;
    float advantage_ = 0.0f;
};
