#pragma once
//
// Organism: the whole behavior program in one place. It knows nothing about
// the world, its body, or what any channel means. Each tick it gets inputs in
// 0..1 and writes outputs in -1..1 (bipolar channels) or 0..1. Two facts
// about the body are allowed: which input is its energy (the battery) and
// which inputs hurt (e.g. a bumper). Everything it does has to come from
// the handful of mechanisms below.
//
//   model      a learned forward model predicting its own inputs; surprise is
//              what the model didn't expect, so repetition becomes boring.
//   brain      a fixed random recurrent network ("reservoir") of neurons with
//              timescales from 0.1 s to 10 s, driven by the inputs and by a
//              copy of its own outputs. Gives it short-term memory and
//              rhythms without either being written.
//   places     remembered situations: a map with no coordinates.
//   needs      hunger (energy below a set-point), fullness (above another),
//              pain (from hurting inputs, fading), boredom (rises while it
//              learns nothing; relieved by learning progress and by arriving
//              somewhere unfamiliar). Need = sum of squares. Reward = need
//              going down.
//   hormones   dopamine   = surprise in reward (temporal-difference error of a
//                           learned value estimate): the learning signal
//              adrenaline = recent surprise and pain: more exploration, faster learning
//              (hunger itself also widens exploration: foraging)
//              cortisol   = need left unmet for minutes: more exploration
//              serotonin  = contentment for minutes: less exploration, longer patience
//   action     outputs = learned readout of (inputs, needs, hormones, brain)
//              + smooth exploration noise whose size the hormones set.
//   learning   one three-factor rule: readout weight += rate x dopamine x
//              trace(exploration x feature). The value estimate learns by TD.
//
// Grounding: reward-modulated learning of a readout from a chaotic recurrent
// network (Hoerzer, Legenstein & Maass 2014); homeostatic reward as drive
// reduction (Keramati & Gutkin 2014); neuromodulators as the parameters of
// learning (Doya 2002).
//
// Header-only, no Arduino, no allocation: runs unchanged on the ESP32 and in
// the simulator. ~20 KB of state at the maximum channel counts.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace life {

// What makes one individual different from another: its constitution.
// Nothing here names a behavior.
struct Genome {
    uint32_t seed = 1;              // wiring of the brain
    float hunger_setpoint = 0.65f;  // energy input below this is hunger
    float boredom_rate = 0.05f;     // per second, toward 1, while nothing surprises it
    float pain_fade_s = 10.0f;
    float energy_buffer_s = 30.0f;  // how slowly felt energy follows the battery
    float explore = 0.5f;           // base exploration size
    float explore_tau_s = 1.0f;     // how smooth exploration is
    float learn_rate = 0.1f;
    float value_rate = 0.05f;
    float model_rate = 0.02f;       // how fast it learns to predict its senses
    float horizon_s = 20.0f;        // how far ahead the value estimate looks
    float trace_s = 2.0f;           // how far back a dopamine burst credits
    float brain_gain = 1.1f;        // recurrent strength: <1 calm, >1 lively
    float satiety_setpoint = 0.97f; // energy input above this is fullness
    float place_radius = 0.12f;     // how different a situation must look to be a new place
};

class Organism {
public:
    static constexpr size_t kMaxIn = 16;
    static constexpr size_t kMaxOut = 16;
    static constexpr size_t kNeurons = 24;
    static constexpr size_t kInner = 8;  // hunger, fullness, pain, boredom, 4 hormones
    static constexpr size_t kPlaces = 12;
    static constexpr size_t kFeatures = kMaxIn + kInner + kNeurons + kPlaces + 1;

    // energy_input: index of the battery input, or -1. pain_mask: bit i set
    // if input i hurts. bipolar[j]: output j ranges -1..1 (else 0..1).
    void begin(const Genome& g, size_t n_in, size_t n_out, int energy_input, uint32_t pain_mask, const bool* bipolar) {
        g_ = g;
        n_in_ = n_in < kMaxIn ? n_in : kMaxIn;
        n_out_ = n_out < kMaxOut ? n_out : kMaxOut;
        energy_ = energy_input;
        pain_mask_ = pain_mask;
        for (size_t j = 0; j < n_out_; j++) bipolar_[j] = bipolar[j];
        rng_ = g.seed ? g.seed : 1;
        wire_brain();
        memset(w_, 0, sizeof(w_));
        memset(w0_, 0, sizeof(w0_));
        memset(e_, 0, sizeof(e_));
        memset(v_, 0, sizeof(v_));
        memset(m_, 0, sizeof(m_));
        for (size_t i = 0; i < n_in_; i++) m_[i][i] = 1.0f;  // start by expecting "no change"
        memset(proto_, 0, sizeof(proto_));
        memset(visits_, 0, sizeof(visits_));
        n_places_ = 0;
        reset_state();
    }

    // Everything that isn't learned: what a reboot loses.
    void reset_state() {
        memset(y_, 0, sizeof(y_));
        memset(pred_, 0, sizeof(pred_));
        memset(jitter_, 0, sizeof(jitter_));
        memset(prev_hurt_, 0, sizeof(prev_hurt_));
        memset(noise_, 0, sizeof(noise_));
        memset(out_, 0, sizeof(out_));
        memset(f_, 0, sizeof(f_));
        memset(err_fast_, 0, sizeof(err_fast_));
        memset(err_slow_, 0, sizeof(err_slow_));
        memset(situation_, 0, sizeof(situation_));
        memset(place_act_, 0, sizeof(place_act_));
        place_ = -1;
        arrival_ = 0.0f;
        progress_ = interest_ = 0.0f;
        interest_avg_ = 0.0f;
        fullness_ = 0.0f;
        pain_ = boredom_ = 0.0f;
        adrenaline_ = cortisol_ = serotonin_ = dopamine_ = 0.0f;
        need_ = value_ = 0.0f;
        td_var_ = 1e-4f;
        started_ = false;
    }

    void tick(const float* in, float* out, float dt) {
        sense(in, dt);
        if (traits_.places) locate(in, dt);
        float r = feel(in, dt);
        features(in);
        learn(r, dt);
        think(in, dt);
        act(out, dt);
        predict();
        started_ = true;
    }

    // --- introspection (firmware log, simulator, tests) -----------------------
    float hunger() const { return hunger_; }
    float fullness() const { return fullness_; }
    float pain() const { return pain_; }
    float boredom() const { return boredom_; }
    float need() const { return need_; }
    float dopamine() const { return dopamine_; }
    float adrenaline() const { return adrenaline_; }
    float cortisol() const { return cortisol_; }
    float serotonin() const { return serotonin_; }
    float value() const { return value_; }
    float surprise() const { return surprise_; }
    int place() const { return place_; }  // -1 before the first
    size_t place_count() const { return n_places_; }
    float familiarity(size_t p) const { return p < kPlaces ? visits_[p] : 0.0f; }  // seconds there, fading
    float exploration() const { return sigma_; }
    float neuron(size_t i) const { return y_[i]; }
    size_t input_count() const { return n_in_; }
    size_t output_count() const { return n_out_; }

    // Readout weight from feature k to output j. Features, in order: the
    // inputs, then hunger, fullness, pain, boredom, dopamine, adrenaline,
    // cortisol, serotonin, then the brain's neurons, then the places, then
    // a constant.
    float weight(size_t j, size_t k) const { return w_[j][k]; }
    void set_weight(size_t j, size_t k, float w) { w_[j][k] = w; }
    // Innate weight: what it's born with and what forgetting relaxes toward.
    void set_innate(size_t j, size_t k, float w) { w_[j][k] = w0_[j][k] = w; }
    size_t feature_count() const { return n_in_ + kInner + kNeurons + kPlaces + 1; }
    static const char* inner_name(size_t i) {
        static const char* names[kInner] = {"hunger",     "fullness", "pain",     "boredom",
                                            "dopamine", "adrenaline", "cortisol", "serotonin"};
        return i < kInner ? names[i] : "?";
    }

    void set_plasticity(bool on) { plastic_ = on; }

    // Ablation: switch a mechanism off to measure what it's worth. Without
    // progress, boredom is relieved by raw surprise, as before.
    struct Traits {
        bool satiety = true, progress = true, places = true;
    };
    void set_traits(const Traits& t) { traits_ = t; }

private:
    // --- brain wiring: random, sparse, fixed for life ----------------------
    float rand01() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(rng_) / 4294967295.0f;
    }
    float randn() { return (rand01() + rand01() + rand01() + rand01() - 2.0f) * 1.7320508f; }

    void wire_brain() {
        // ~25% connectivity, scaled so each neuron's input variance is
        // brain_gain^2: the edge between quiet and chaotic, where recurrent
        // networks hold the most memory.
        const float p = 0.25f;
        float scale = g_.brain_gain / sqrtf(p * kNeurons);
        for (size_t i = 0; i < kNeurons; i++) {
            for (size_t k = 0; k < kNeurons; k++) r_[i][k] = (rand01() < p && i != k) ? randn() * scale : 0.0f;
            for (size_t k = 0; k < kMaxIn; k++) u_[i][k] = rand01() < 0.3f ? randn() * 1.5f : 0.0f;
            for (size_t k = 0; k < kMaxOut; k++) fb_[i][k] = rand01() < 0.3f ? randn() * 0.8f : 0.0f;
            bias_[i] = randn() * 0.2f;
            // Timescales log-spread from 0.1 s to 10 s.
            tau_[i] = 0.1f * powf(100.0f, static_cast<float>(i) / (kNeurons - 1));
        }
    }

    // --- sense: how surprising was this tick? ------------------------------
    // A learned forward model predicts each input from last tick's features
    // (inputs, needs, hormones, brain; the brain carries a copy of what it
    // just did). Surprise is the miss beyond each input's usual residual
    // jitter. Anything repeated, like a steady spin, becomes predictable and
    // stops being surprising; only new situations stay surprising.
    void sense(const float* in, float dt) {
        float s = 0.0f;
        float adapt = dt / 10.0f;
        if (started_) {
            float norm = 1.0f;
            for (size_t k = 0; k < nf_; k++) norm += f_[k] * f_[k];
            float fast = dt / 2.0f, slow = dt / 20.0f;
            progress_ = 0.0f;
            for (size_t i = 0; i < n_in_; i++) {
                float miss = in[i] - pred_[i];
                float a = fabsf(miss);
                // Learning progress: recent error below older error. Noise
                // keeps both equal and earns nothing.
                err_fast_[i] += fast * (a - err_fast_[i]);
                err_slow_[i] += slow * (a - err_slow_[i]);
                float gain = err_slow_[i] - err_fast_[i] - 0.15f * err_slow_[i] - 0.002f;
                if (gain > 0.0f) progress_ += gain;
                float floor = 3.0f * jitter_[i] + 0.005f;
                if (a > floor) s += a - floor;
                float capped = a < 2.0f * floor ? a : 2.0f * floor;
                jitter_[i] += adapt * (capped - jitter_[i]);
                // Normalized LMS: nudge the prediction toward what happened.
                float step = g_.model_rate * miss / norm;
                for (size_t k = 0; k < nf_; k++) m_[i][k] += step * f_[k];
            }
        }
        surprise_ = s > 1.0f ? 1.0f : s;
    }

    // --- locate: which remembered place is this? ---------------------------
    // A situation is the senses minus energy, smoothed over ~1 s. The nearest
    // remembered one is the current place; one unlike all becomes a new place
    // (evicting the least familiar). Familiarity = time there, fading ~1 h.
    void locate(const float* in, float dt) {
        float a = started_ ? dt / 1.0f : 1.0f;
        size_t dims = 0;
        for (size_t i = 0; i < n_in_; i++) {
            if (static_cast<int>(i) == energy_) continue;
            situation_[dims] += a * (in[i] - situation_[dims]);
            dims++;
        }
        if (dims == 0) return;
        float best = 1e9f;
        int bi = -1;
        float r2 = g_.place_radius * g_.place_radius;
        for (size_t p = 0; p < n_places_; p++) {
            float d = 0.0f;
            for (size_t k = 0; k < dims; k++) {
                float e = situation_[k] - proto_[p][k];
                d += e * e;
            }
            d /= static_cast<float>(dims);  // mean squared difference per sense
            place_act_[p] = expf(-d / r2);
            if (d < best) {
                best = d;
                bi = static_cast<int>(p);
            }
        }
        if (bi < 0 || best > 4.0f * r2) {
            // Somewhere new.
            size_t slot = n_places_;
            if (slot >= kPlaces) {
                slot = 0;
                for (size_t p = 1; p < kPlaces; p++)
                    if (visits_[p] < visits_[slot]) slot = p;
            } else {
                n_places_++;
            }
            for (size_t k = 0; k < dims; k++) proto_[slot][k] = situation_[k];
            visits_[slot] = 0.0f;
            place_act_[slot] = 1.0f;
            bi = static_cast<int>(slot);
        } else {
            float rate = dt / (5.0f + visits_[bi]);
            for (size_t k = 0; k < dims; k++) proto_[bi][k] += rate * (situation_[k] - proto_[bi][k]);
        }
        // Arriving somewhere unfamiliar is interesting; staying put isn't.
        if (bi != place_) arrival_ += 0.05f / (1.0f + visits_[bi] / 30.0f);
        arrival_ -= arrival_ * (dt / 3.0f < 1.0f ? dt / 3.0f : 1.0f);
        place_ = bi;
        float fade = dt / 3600.0f;
        for (size_t p = 0; p < n_places_; p++) visits_[p] -= visits_[p] * fade;
        visits_[bi] += dt;
    }

    // Predict next tick's inputs from this tick's features.
    void predict() {
        for (size_t i = 0; i < n_in_; i++) {
            float p = 0.0f;
            for (size_t k = 0; k < nf_; k++) p += m_[i][k] * f_[k];
            pred_[i] = p;
        }
    }

    // --- feel: needs, reward, hormones -------------------------------------
    float feel(const float* in, float dt) {
        // Hunger follows a buffered energy level ("blood sugar"), not the
        // instantaneous battery: voltage sags the moment motors draw
        // current, and feeling that as hunger would punish every movement
        // long before any meal could reward one.
        float energy = energy_ >= 0 && static_cast<size_t>(energy_) < n_in_ ? in[energy_] : 1.0f;
        if (!started_) sugar_ = energy;
        sugar_ += (energy - sugar_) * dt / g_.energy_buffer_s;
        hunger_ = clamp((g_.hunger_setpoint - sugar_) / g_.hunger_setpoint, 0.0f, 1.0f);
        // Satiety: nothing is pleasant forever.
        fullness_ = !traits_.satiety ? 0.0f : clamp((sugar_ - g_.satiety_setpoint) / (1.0f - g_.satiety_setpoint + 1e-3f), 0.0f, 1.0f);

        float hurt = 0.0f;
        for (size_t i = 0; i < n_in_; i++) {
            if (!(pain_mask_ & (1u << i))) continue;
            float onset = in[i] - prev_hurt_[i];
            if (onset > 0.0f) hurt += onset;       // a new jolt hurts
            hurt += 0.2f * in[i] * dt;             // sustained contact aches a little
            prev_hurt_[i] = in[i];
        }
        pain_ = clamp(pain_ + 0.5f * hurt - pain_ * dt / g_.pain_fade_s, 0.0f, 1.0f);

        // Boredom is habituation: relieved by more interest (learning progress,
        // arrivals) than it is used to over half an hour.
        interest_ = traits_.progress ? progress_ + arrival_ : surprise_;
        interest_avg_ += (interest_ - interest_avg_) * dt / 1800.0f;
        float relief = clamp(interest_ / (interest_avg_ + 1e-3f), 0.0f, 2.0f);
        boredom_ = clamp(boredom_ + (g_.boredom_rate * (1.0f - boredom_) - 0.5f * relief * boredom_) * dt, 0.0f, 1.0f);
        float bored = clamp((boredom_ - 0.4f) / 0.6f, 0.0f, 1.0f);

        float full = 0.5f * fullness_;
        float need = hunger_ * hunger_ + full * full + pain_ * pain_ + bored * bored;
        float r = started_ ? need_ - need : 0.0f;
        need_ = need;

        float n1 = clamp(need, 0.0f, 1.0f);
        adrenaline_ += (clamp(3.0f * surprise_ + pain_, 0.0f, 1.0f) - adrenaline_) * dt / 3.0f;
        cortisol_ += (n1 - cortisol_) * dt / 120.0f;
        serotonin_ += ((1.0f - n1) - serotonin_) * dt / 120.0f;
        bored_ = bored;
        return r;
    }

    void features(const float* in) {
        size_t k = 0;
        for (size_t i = 0; i < n_in_; i++) fnext_[k++] = in[i];
        fnext_[k++] = hunger_;
        fnext_[k++] = fullness_;
        fnext_[k++] = pain_;
        fnext_[k++] = bored_;
        fnext_[k++] = dopamine_;
        fnext_[k++] = adrenaline_;
        fnext_[k++] = cortisol_;
        fnext_[k++] = serotonin_;
        for (size_t i = 0; i < kNeurons; i++) fnext_[k++] = y_[i];
        for (size_t p = 0; p < kPlaces; p++) fnext_[k++] = p < n_places_ ? place_act_[p] : 0.0f;
        fnext_[k++] = 1.0f;
        nf_ = k;
    }

    // --- learn: value by TD, readout by dopamine x trace --------------------
    void learn(float r, float dt) {
        float v_next = 0.0f;
        for (size_t k = 0; k < nf_; k++) v_next += v_[k] * fnext_[k];
        if (started_) {
            // Serotonin lengthens patience: contentment values the future more.
            float horizon = g_.horizon_s * (1.0f + serotonin_);
            float gamma = 1.0f - dt / horizon;
            float td = r + gamma * v_next - value_;
            td_var_ += (td * td - td_var_) * dt / 60.0f;
            float sd = sqrtf(td_var_);
            dopamine_ = clamp(td / (sd > 1e-4f ? sd : 1e-4f), -5.0f, 5.0f);
            if (plastic_) {
                float vr = g_.value_rate * dt;
                for (size_t k = 0; k < nf_; k++) v_[k] = clamp(v_[k] + vr * td / (sd + 1e-4f) * f_[k], -5.0f, 5.0f);
                // Adrenaline speeds learning: what happened in a fright is learned fast.
                float step = g_.learn_rate * (1.0f + adrenaline_) * dopamine_ * dt;
                // Forgetting relaxes toward what it was born with, not toward
                // blank: learned changes fade, instincts don't.
                float relax = 1e-4f * dt;
                for (size_t j = 0; j < n_out_; j++)
                    for (size_t k = 0; k < nf_; k++) {
                        float w = w_[j][k] + step * e_[j][k];
                        w_[j][k] = clamp(w + (w0_[j][k] - w) * relax, -2.0f, 2.0f);
                    }
            }
        }
        value_ = 0.0f;
        for (size_t k = 0; k < nf_; k++) {
            f_[k] = fnext_[k];
            value_ += v_[k] * f_[k];
        }
    }

    // --- think: advance the brain ------------------------------------------
    void think(const float* in, float dt) {
        float ynew[kNeurons];
        for (size_t i = 0; i < kNeurons; i++) {
            float a = bias_[i];
            for (size_t k = 0; k < kNeurons; k++) a += r_[i][k] * y_[k];
            for (size_t k = 0; k < n_in_; k++) a += u_[i][k] * (in[k] - 0.5f);
            for (size_t k = 0; k < n_out_; k++) a += fb_[i][k] * out_[k];
            float step = dt / tau_[i];
            if (step > 1.0f) step = 1.0f;
            ynew[i] = y_[i] + step * (tanhf(a) - y_[i]);
        }
        memcpy(y_, ynew, sizeof(y_));
    }

    // --- act: readout + hormone-sized exploration --------------------------
    void act(float* out, float dt) {
        // Hunger itself makes it restless (food deprivation raises activity
        // in animals: foraging); so do surprise, lasting need and boredom.
        // Contentment calms it.
        float s = g_.explore * (0.3f + hunger_ + adrenaline_ + cortisol_ + bored_) * (1.0f - 0.7f * serotonin_);
        sigma_ = clamp(s, 0.02f, 0.8f);
        float decay = expf(-dt / g_.explore_tau_s);
        float kick = sqrtf(1.0f - decay * decay);
        float lambda = expf(-dt / g_.trace_s);
        for (size_t j = 0; j < n_out_; j++) {
            float u = 0.0f;
            for (size_t k = 0; k < nf_; k++) u += w_[j][k] * f_[k];
            float mean = tanhf(u);
            if (!bipolar_[j] && mean < 0.0f) mean = 0.0f;
            noise_[j] = decay * noise_[j] + kick * randn();
            float x = sigma_ * noise_[j];
            float o = clamp(mean + x, bipolar_[j] ? -1.0f : 0.0f, 1.0f);
            out[j] = out_[j] = o;
            // Trace: which features were active while exploring which way.
            for (size_t k = 0; k < nf_; k++) e_[j][k] = lambda * e_[j][k] + (1.0f - lambda) * x * f_[k];
        }
    }

    static float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    Genome g_;
    size_t n_in_ = 0, n_out_ = 0, nf_ = 0;
    int energy_ = -1;
    uint32_t pain_mask_ = 0;
    bool bipolar_[kMaxOut] = {};
    bool plastic_ = true;
    Traits traits_;
    bool started_ = false;
    uint32_t rng_ = 1;

    // brain (fixed)
    float r_[kNeurons][kNeurons] = {};
    float u_[kNeurons][kMaxIn] = {};
    float fb_[kNeurons][kMaxOut] = {};
    float bias_[kNeurons] = {};
    float tau_[kNeurons] = {};
    // brain state
    float y_[kNeurons] = {};
    // learned
    float w_[kMaxOut][kFeatures] = {};
    float w0_[kMaxOut][kFeatures] = {};  // innate
    float e_[kMaxOut][kFeatures] = {};
    float v_[kFeatures] = {};
    // senses
    float m_[kMaxIn][kFeatures] = {};  // forward model
    float pred_[kMaxIn] = {};
    float jitter_[kMaxIn] = {};
    float prev_hurt_[kMaxIn] = {};
    float surprise_ = 0.0f;
    float err_fast_[kMaxIn] = {};
    float err_slow_[kMaxIn] = {};
    float progress_ = 0.0f, interest_ = 0.0f, interest_avg_ = 0.0f;  // curiosity
    // places
    float situation_[kMaxIn] = {};
    float proto_[kPlaces][kMaxIn] = {};
    float visits_[kPlaces] = {};
    float place_act_[kPlaces] = {};
    size_t n_places_ = 0;
    int place_ = -1;
    float arrival_ = 0.0f;
    // needs and hormones
    float sugar_ = 1.0f;
    float hunger_ = 0.0f, fullness_ = 0.0f, pain_ = 0.0f, boredom_ = 0.0f, bored_ = 0.0f, need_ = 0.0f;
    float dopamine_ = 0.0f, adrenaline_ = 0.0f, cortisol_ = 0.0f, serotonin_ = 0.0f;
    float value_ = 0.0f, td_var_ = 1e-4f;
    float sigma_ = 0.0f;
    // features
    float f_[kFeatures] = {};
    float fnext_[kFeatures] = {};
    float noise_[kMaxOut] = {};
    float out_[kMaxOut] = {};
};

}  // namespace life
