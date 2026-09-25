#pragma once
//
// Pure rate-limiting math factored out of Actuator::write() — zero
// dependency on Arduino, testable on the host (see
// test/native/test_rate_limit.cpp).

// Caps how far `current` can move toward `target` this call, to at most
// max_rate_per_s * elapsed_s. max_rate_per_s <= 0 means "unlimited".
// `elapsed_s` is expected to already be sanitized by the caller (see
// clamp_elapsed_seconds()) — this function doesn't know or care where it
// came from.
inline float apply_rate_limit(float current, float target, float max_rate_per_s, float elapsed_s) {
    if (max_rate_per_s <= 0.0f) return target;

    float max_delta = max_rate_per_s * elapsed_s;
    float delta = target - current;
    if (delta > max_delta) delta = max_delta;
    if (delta < -max_delta) delta = -max_delta;
    return current + delta;
}

// Bounds an elapsed-time reading to a sane ceiling before it's used as a
// rate-limit multiplier. Two unrelated things can otherwise produce an
// oversized single-tick step: a channel going unwritten for longer than a
// micros() wraparound period (~71 minutes — elapsed_s would actually
// *under*-report in that case, but the principle is the same: don't trust
// an elapsed-time reading that's wildly larger than a normal tick), or a
// genuinely delayed call (a blocked loop(), scheduling jitter).
inline float clamp_elapsed_seconds(float elapsed_s, float max_elapsed_s) {
    return elapsed_s > max_elapsed_s ? max_elapsed_s : elapsed_s;
}
