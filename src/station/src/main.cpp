// Charging station reference firmware (docs/synth-behavior.md §11).
//
// Deliberately simple: this is a state machine, not an organism — no
// physiology, no drives, no memory, no dashboard. It measures contact
// voltage/current, derives IDLE/CHARGING/FULL (§11.6's pseudocode,
// translated to real code below), and emits the sensory signature §11.5
// describes: a fixed RF beacon, an LED pattern, and an audio click, each
// changing with state. Nothing here ever sends a robot the string
// "CHARGING" or "FULL" — the robot only ever sees the raw sensor shifts,
// same as every other channel in this project.
//
// This firmware does NOT deliver charge current — that's a real battery
// charger IC/module (e.g. a TP4056 for single-cell LiPo, sized for your
// pack), wired in parallel with the sense pins below. This only watches
// and signals. See README.md for the mechanical/circuit build notes this
// firmware assumes.

#include <Arduino.h>
#include <WiFi.h>

namespace {
// --- pins — adjust for your build ------------------------------------------
constexpr uint8_t kVoltageSensePin = 34;  // ADC: contact/pack voltage, scaled into 0..3.3V
constexpr uint8_t kCurrentSensePin = 35;  // ADC: current-sense (e.g. ACS712/INA219 analog out)
constexpr uint8_t kLedPin = 2;            // status LED, PWM-capable pin
constexpr uint8_t kBuzzerPin = 25;        // passive buzzer or piezo disc

constexpr uint8_t kLedChannel = 0;        // LEDC channel for the status LED
constexpr int kLedFreqHz = 5000;
constexpr int kLedResolutionBits = 8;

// --- thresholds — tune for your battery chemistry/pack/sense resistor -----
constexpr float kCurrentActiveThreshold = 0.05f;  // normalized current above which charging reads "active"
constexpr float kVoltageFullThreshold = 0.95f;    // normalized voltage above which the pack reads "full"
constexpr float kCurrentTaperThreshold = 0.02f;   // normalized current below which charge current has tapered

// RF beacon: a plain, fixed SoftAP. The robot side tracks this SSID's
// signal strength as a long-range gradient (see src/esp32's SensorKind::
// kWifiRssi and set a board profile's target_ssid to this same string).
// State (IDLE/CHARGING/FULL) is deliberately NOT encoded in the SSID —
// per §11, the near-field LED/audio pattern carries that, RF only says
// "the station is over here".
constexpr const char* kApSsid = "emergent-station";

enum class StationState { kIdle, kCharging, kFull };
StationState g_state = StationState::kIdle;

float read_normalized(uint8_t pin) { return analogRead(pin) / 4095.0f; }

// Slow pulse (idle, "come find me"), fast pulse (charging, "working"),
// solid (full, "done") — one PWM channel, three waveforms. The waveform is
// the signal; the LED's color/brightness level is incidental.
void update_led(StationState state) {
    static uint32_t last_toggle_ms = 0;
    static bool on = false;

    if (state == StationState::kFull) {
        ledcWrite(kLedChannel, 255);
        return;
    }

    uint32_t half_period_ms = (state == StationState::kIdle) ? 800 : 150;
    uint32_t now = millis();
    if (now - last_toggle_ms >= half_period_ms) {
        on = !on;
        ledcWrite(kLedChannel, on ? 220 : 10);
        last_toggle_ms = now;
    }
}

// A short digital pulse rather than tone() — tone() isn't reliably
// available across ESP32 Arduino core versions, and the article's own
// description ("faint click", "rare heartbeat tick") matches a simple
// click better than a musical tone anyway.
void click(uint8_t pin) {
    digitalWrite(pin, HIGH);
    delay(15);
    digitalWrite(pin, LOW);
}

void update_audio(StationState state) {
    static uint32_t last_click_ms = 0;
    uint32_t interval_ms = (state == StationState::kIdle) ? 0  // silent until something's happening
                            : (state == StationState::kCharging) ? 4000
                                                                  : 15000;  // full: rare "heartbeat"
    if (interval_ms == 0) return;

    uint32_t now = millis();
    if (now - last_click_ms >= interval_ms) {
        click(kBuzzerPin);
        last_click_ms = now;
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(kBuzzerPin, OUTPUT);
    ledcSetup(kLedChannel, kLedFreqHz, kLedResolutionBits);
    ledcAttachPin(kLedPin, kLedChannel);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid);

    Serial.print("[station] beacon up: ");
    Serial.println(kApSsid);
}

void loop() {
    float voltage = read_normalized(kVoltageSensePin);
    float current = read_normalized(kCurrentSensePin);

    bool charging_active = current > kCurrentActiveThreshold;
    if (charging_active) {
        bool tapered_and_full = voltage > kVoltageFullThreshold && current < kCurrentTaperThreshold;
        g_state = tapered_and_full ? StationState::kFull : StationState::kCharging;
    } else {
        g_state = StationState::kIdle;
    }

    update_led(g_state);
    update_audio(g_state);

    delay(20);
}
