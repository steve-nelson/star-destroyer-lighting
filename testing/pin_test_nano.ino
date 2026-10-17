// PIN TESTER - ARDUINO NANO ESP32
// Part 1: lights EVERY one of the 20 channels in turn, in order:
//   engines 1-7, bridge, hangar bay, hull zones 1-10, accent lights.
//   This alone confirms every wire and every LED cluster is connected correctly.
// Part 2: additionally fades the 7 engines up and down one after another (PWM
//   check) - engines are the only channels that use PWM, so this is an extra
//   check on top of Part 1, not a replacement for it.
// Repeats forever.
//
// Engine PWM uses Espressif's LEDC calls (core 2.x: ledcSetup/ledcAttachPin/ledcWrite(channel),
// core 3.x: ledcAttach/ledcWrite(pin)); the right ones are chosen automatically.

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
const uint8_t ZONE_PINS[] = {D9, D10,                                   // bridge, hangar
                             D11, D12, D13, A0, A1, A3, A4, A5, A6, A7,  // hull 1-10
                             A2};                                        // accent lights (Driver C input 8)
const uint8_t N_ZONE = sizeof(ZONE_PINS);

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
  for (uint8_t i = 0; i < N_ZONE; i++) { pinMode(ZONE_PINS[i], OUTPUT); digitalWrite(ZONE_PINS[i], LOW); }
}

void loop() {
  // Part 1: one at a time
  for (uint8_t i = 0; i < N_ENG; i++) {
    setEngine(i, PWM_MAX); delay(1000);
    setEngine(i, 0);       delay(300);
  }
  for (uint8_t i = 0; i < N_ZONE; i++) {
    digitalWrite(ZONE_PINS[i], HIGH); delay(1000);
    digitalWrite(ZONE_PINS[i], LOW);  delay(300);
  }
  // Part 2: PWM fade check on each engine
  for (uint8_t i = 0; i < N_ENG; i++) {
    for (int v = 0; v <= 255; v += 5) { setEngine(i, (uint32_t)v * PWM_MAX / 255); delay(15); }
    for (int v = 255; v >= 0; v -= 5) { setEngine(i, (uint32_t)v * PWM_MAX / 255); delay(15); }
  }
  delay(500);
}
