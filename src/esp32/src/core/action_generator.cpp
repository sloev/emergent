#include "core/action_generator.h"

#include <Arduino.h>
#include <esp_system.h>

void ActionGenerator::begin(Body& body) {
    (void)body;
    rng_state_ = esp_random();
    if (rng_state_ == 0) rng_state_ = 1;  // xorshift is fixed at 0 forever; never let it land there
    Serial.print("[action_gen] rng seed: ");
    Serial.println(rng_state_);
}
