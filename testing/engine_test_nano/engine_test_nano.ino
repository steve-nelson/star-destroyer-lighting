// ENGINE-ONLY TEST - ARDUINO NANO ESP32
// For Board 1 only (Nano + Driver C + the 7 engine LEDs); Boards 2 and 3 do not need to be built.
// Step 1: each engine lights on its own for 1 s, engines 1-7 (D2-D8).
// Step 2: all 7 together, full brightness for 1 s.
// Step 3: each engine fades up and down in turn (PWM check).
// Step 4: the PRODUCTION engine look - same spool-up, breathing, shimmer and gamma maths as
//         main_nano_esp32.ino, for 30 s, then the same 1.5 s fade-out. Tune the look here.
// Repeats forever. Engine n = Driver C pin n.
// Engine PWM uses Espressif's LEDC calls (core 2.x or 3.x, chosen automatically).

#if __has_include("esp_arduino_version.h")
#include "esp_arduino_version.h"
#endif
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
#define PWM_CORE_V3 1
#else
#define PWM_CORE_V3 0
#endif

const uint32_t PWM_FREQ = 5000;
const uint8_t PWM_BITS = 12;
const uint32_t PWM_MAX = (1UL << PWM_BITS) - 1;

const uint8_t N_ENG = 7;
const uint8_t ENG_PINS[N_ENG] = {D2, D3, D4, D5, D6, D7, D8};

// ---- Production look: keep these identical to main_nano_esp32.ino (copy any values you tune back there) ----
const float BASE = 0.70;
const float BREATH_DEPTH = 0.15;
const float BREATH_PERIOD_S = 6.0;
const float SHIMMER = 0.06;
const float GAMMA = 2.2;
const unsigned long CH_START_MS[N_ENG] = {0, 2500, 5000, 8000, 8000, 8000, 8000};
const unsigned long RAMP_MS = 3000;
const unsigned long FADE_OUT_MS = 1500;
const unsigned long UPDATE_MS = 20;
const unsigned long LOOK_MS = 30000;   // how long step 4 runs before fading out
float f1[N_ENG], f2[N_ENG], p1[N_ENG], p2[N_ENG], curLevel[N_ENG];

float randFloat(float lo, float hi) { return lo + (hi - lo) * (random(0, 10000) / 10000.0); }

void setEngine(uint8_t i, uint32_t duty) {
  if (duty > PWM_MAX) duty = PWM_MAX;
#if PWM_CORE_V3
  ledcWrite(ENG_PINS[i], duty);
#else
  ledcWrite(i, duty);
#endif
}

void setup() {
  for (uint8_t i = 0; i < N_ENG; i++) {
    pinMode(ENG_PINS[i], OUTPUT);
#if PWM_CORE_V3
    ledcAttach(ENG_PINS[i], PWM_FREQ, PWM_BITS);
#else
    ledcSetup(i, PWM_FREQ, PWM_BITS);
    ledcAttachPin(ENG_PINS[i], i);
#endif
    setEngine(i, 0);
  }
  randomSeed(analogRead(A2) ^ micros());   // A2 left unconnected, as in the main sketch
  for (uint8_t i = 0; i < N_ENG; i++) {
    f1[i] = randFloat(0.3, 0.9); f2[i] = randFloat(1.1, 2.3);
    p1[i] = randFloat(0, TWO_PI); p2[i] = randFloat(0, TWO_PI);
  }
}

// Same maths as updateEngines() in main_nano_esp32.ino
void updateEngines(unsigned long now) {
  float t = now / 1000.0;
  float breath = BREATH_DEPTH * sin(TWO_PI * t / BREATH_PERIOD_S);
  for (uint8_t i = 0; i < N_ENG; i++) {
    if (now < CH_START_MS[i]) { setEngine(i, 0); curLevel[i] = 0; continue; }
    float x = (now - CH_START_MS[i]) / (float)RAMP_MS;
    if (x > 1.0) x = 1.0;
    float ramp = x * x * (3.0 - 2.0 * x);
    float shimmer = SHIMMER * (0.6 * sin(TWO_PI * f1[i] * t + p1[i]) + 0.4 * sin(TWO_PI * f2[i] * t + p2[i]));
    float level = ramp * (BASE + breath + shimmer);
    if (level < 0) level = 0;
    if (level > 1) level = 1;
    curLevel[i] = level;
    setEngine(i, (uint32_t)(pow(level, GAMMA) * PWM_MAX));
  }
}

void productionLook() {
  unsigned long t0 = millis();
  while (millis() - t0 < LOOK_MS) { updateEngines(millis() - t0); delay(UPDATE_MS); }
  float from[N_ENG]; for (uint8_t i = 0; i < N_ENG; i++) from[i] = curLevel[i];
  unsigned long tf = millis();
  while (millis() - tf < FADE_OUT_MS) {
    float x = (millis() - tf) / (float)FADE_OUT_MS;
    for (uint8_t i = 0; i < N_ENG; i++) setEngine(i, (uint32_t)(pow(from[i] * (1.0 - x), GAMMA) * PWM_MAX));
    delay(UPDATE_MS);
  }
  for (uint8_t i = 0; i < N_ENG; i++) setEngine(i, 0);
}

void loop() {
  for (uint8_t i = 0; i < N_ENG; i++) {               // step 1: one at a time
    setEngine(i, PWM_MAX); delay(1000);
    setEngine(i, 0);       delay(300);
  }
  for (uint8_t i = 0; i < N_ENG; i++) setEngine(i, PWM_MAX);   // step 2: all together
  delay(1000);
  for (uint8_t i = 0; i < N_ENG; i++) setEngine(i, 0);
  delay(500);
  for (uint8_t i = 0; i < N_ENG; i++) {               // step 3: fade each
    for (int v = 0; v <= 255; v += 5) { setEngine(i, (uint32_t)v * PWM_MAX / 255); delay(15); }
    for (int v = 255; v >= 0; v -= 5) { setEngine(i, (uint32_t)v * PWM_MAX / 255); delay(15); }
  }
  delay(500);
  productionLook();                                   // step 4: production look
  delay(1500);
}
