// Star Destroyer lighting - ARDUINO NANO ESP32
// Control from a phone with the free Adafruit "Bluefruit Connect" app,
// using its Control Pad screen:
//   Button 1 = ON  (starts the light show from the beginning)
//   Button 2 = OFF (engines fade out, everything else switches off)
// The show also switches itself off after AUTO_OFF_MS (default 2 hours).
//
// The Nano ESP32 has Bluetooth built in (it acts like a Nordic UART device).
// All 20 lighting channels go through TBD62083APG driver chips, so a HIGH pin
// switches a channel on. Pins are 3.3 V logic. The hangar bay is on Board 1
// (Nano A2 -> Driver C input 8); the accent lights (6 SMD LEDs) are on
// Board 2 (Nano D9 -> Driver B input 1).
//
// Engine PWM uses the ESP32 LEDC peripheral, documented by Espressif:
//   - Arduino ESP32 core 2.x (Arduino's Nano ESP32 package is based on 2.x):
//       ledcSetup(channel, freq, bits); ledcAttachPin(pin, channel); ledcWrite(channel, duty)
//   - Arduino ESP32 core 3.x:
//       ledcAttach(pin, freq, bits); ledcWrite(pin, duty)
// The sketch picks the right calls automatically. ESP32-S3: 8 LEDC channels,
// 1-14 bit resolution. Settings below (5 kHz, 12-bit) match Espressif's own
// LEDC software-fade example.
//
// Board package: "Arduino ESP32 Boards" > Arduino Nano ESP32
// Keep Tools > Pin Numbering on "By Arduino pin (default)".
//
// ---------------------------------------------------------------------------
// HOW TO READ THIS FILE
// The sketch is organised top to bottom as: pin and timing settings, then
// Bluetooth plumbing, then a handful of small helper functions, and finally
// the two functions every Arduino sketch has - setup() (runs once at power-up)
// and loop() (runs over and over, forever, for as long as the board is on).
// Nearly everything interesting happens inside loop() calling out to the
// helper functions above it. A companion document, "Main sketch explained",
// walks through the trickier parts (the Bluetooth packet format and the
// engine lighting maths) with worked examples, if the short comments here
// are not enough on their own.
// ---------------------------------------------------------------------------

#include <math.h>
#include <BLEDevice.h>     // Bluetooth Low Energy (BLE) support built into the ESP32 core
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>       // a small "descriptor" BLE needs so the phone can subscribe to notifications
#if __has_include("esp_arduino_version.h")
#include "esp_arduino_version.h"   // only exists on newer ("core 3.x") installs - used below to detect which one we have
#endif

// Arduino's ESP32 support has changed its PWM function names between major
// versions. This checks which version is installed and remembers the answer
// in PWM_CORE_V3 (1 = new core 3.x names, 0 = older core 2.x names), so the
// rest of the sketch can just write "#if PWM_CORE_V3" wherever it needs to
// call a PWM function, instead of repeating this check everywhere.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
#define PWM_CORE_V3 1
#else
#define PWM_CORE_V3 0
#endif

// ---------- PINS ----------
// Engines: 7 PWM channels (driver C, inputs 1-7)
// These are the only 7 pins that get dimmed/pulsed (see "Engine PWM" below).
// Every other lighting channel is a simple on/off digital pin.
const uint8_t N_CH = 7;
const uint8_t CH_PINS[N_CH] = {D2, D3, D4, D5, D6, D7, D8};
// Zones - these just switch fully on or fully off,
// they never dim.
const uint8_t BRIDGE_PIN = D10;    // Board 2, driver A, input 8 -> Bridge header
const uint8_t HANGAR_PIN = A2;     // Board 1, driver C, input 8 -> Hangar socket (T6/T7)
// Hull 1-10 in socket order on Board 2: Hull 1-5 left edge, Hull 6-10 right edge.
const uint8_t HULL_PINS[] = {D12, A0, A3, A5, A7, D11, D13, A1, A4, A6};
const uint8_t N_HULL = sizeof(HULL_PINS);   // works out to 10 automatically from the list above
// Accent lights: 6 SMD LEDs on one channel (Board 2, driver B, input 1). This
// pin is dimmed with PWM like the engines, so it can fade in and out smoothly.
const uint8_t ACCENT_PIN = D9;
const uint8_t ACCENT_PWM_CH = 7;   // the 8th (last spare) LEDC channel, used on older core 2.x installs

// ---------- ENGINE PWM (LEDC) ----------
// PWM (pulse-width modulation) is how a digital pin - which can only be fully
// on or fully off - is made to look dimmed: it is switched on and off very
// fast (here, 5000 times a second), and the fraction of each cycle spent on
// is the "duty cycle". A duty of 0 looks off, half the maximum looks half
// bright, and the maximum duty looks fully on. The ESP32's LEDC peripheral
// does this switching in hardware, so the sketch only has to choose a duty
// value and the chip handles the actual on/off switching by itself.
const uint32_t PWM_FREQ = 5000;                    // how many on/off cycles per second (Hz)
const uint8_t PWM_BITS = 12;                       // duty is stored as a 12-bit number...
const uint32_t PWM_MAX = (1UL << PWM_BITS) - 1;    // ...so the brightest possible duty is 2^12 - 1 = 4095

// ---------- OPTIONS ----------
const bool START_ON_BOOT = false;             // true = show starts the instant the board is powered, with no button press needed
const unsigned long FADE_OUT_MS = 1500;       // engine fade-out time (in milliseconds) when switched off
const unsigned long AUTO_OFF_MS = 7200000UL;  // auto switch-off time in milliseconds (7200000 = 2 hours, 0 = never)

// ---------- TIMING (milliseconds after pressing ON) ----------
// Everything below is measured as "how many milliseconds after the ON
// button was pressed", not as a clock time - so CH_START_MS[1] = 2500 means
// "engine 2 starts 2.5 seconds into the show", every time the show runs.
const unsigned long CH_START_MS[N_CH] = {0, 2500, 5000, 8000, 8000, 8000, 8000};  // one start time per engine, in the same order as CH_PINS
const unsigned long RAMP_MS = 3000;           // how long each engine takes to smoothly spool up once its start time arrives
const unsigned long BRIDGE_ON_MS = 12500;     // when the bridge lights switch on
const unsigned long HANGAR_ON_MS = 16500;     // when the hangar bay lights switch on
const unsigned long HULL_MIN_MS = 10000;      // hull zones light up one at a time, waiting at least this long between zones...
const unsigned long HULL_MAX_MS = 30000;      // ...and at most this long, picked randomly each time (see loop(), below)
const unsigned long UPDATE_MS = 20;           // how often the engine brightness is recalculated (50 times a second)
const unsigned long ACCENT_ON_MS = 14000;     // when the accent lights start to fade up (just after the bridge)
const unsigned long ACCENT_RAMP_MS = 4000;    // how long the accent lights take to fade up fully

// ---------- ENGINE LOOK (tweak these) ----------
// These five numbers shape how the engines behave once they are running -
// see updateEngines() further down, and the companion "Main sketch
// explained" document, for exactly how each one is used in the maths.
const float BASE = 0.70;             // resting brightness once spooled up (0.0-1.0)
const float BREATH_DEPTH = 0.15;     // how far brightness rises and falls as the engines "breathe"
const float BREATH_PERIOD_S = 6.0;   // how many seconds one breath (one rise-and-fall cycle) takes
const float SHIMMER = 0.06;          // how much small, fast, random-looking flicker is layered on top
const float GAMMA = 2.2;             // reshapes the fade curve so it looks smooth and natural to the eye, not linear
const float ACCENT_LEVEL = 1.0;      // accent brightness once faded up (0.0-1.0) - lower it, or use bigger resistors, for a subtler glow

// ---------- BLUETOOTH (Nordic UART service) ----------
// These three UUIDs are a published standard called the "Nordic UART
// Service" (NUS). They are not made up for this project - the Bluefruit
// Connect app specifically looks for this exact service, so that it knows
// how to talk to the board. RX is the characteristic the phone writes
// button-press data into; TX is one the board could use to send data back
// to the phone (unused here, since the lights do not need to report
// anything, but BLE requires it to be present to match the standard).
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer = nullptr;
volatile bool bleConnected = false;   // "volatile" because this is changed from a BLE callback, not from loop() itself
bool wasConnected = false;
volatile uint8_t pendingCmd = 0;   // 1 = ON, 2 = OFF (set by Bluetooth, read and cleared at the top of loop())

// The show is always in exactly one of these three states:
//   IDLE    - everything off, waiting for the ON button
//   RUNNING - the full show is playing (engines spooling/breathing, zones lighting up over time)
//   FADING  - OFF was just pressed; engines are fading down before returning to IDLE
enum State { IDLE, RUNNING, FADING };
State state = IDLE;

// Per-engine "shimmer" randomness (see updateEngines()). Each engine gets
// its own pair of random flicker speeds (f1/f2) and starting points (p1/p2)
// so that the seven engines do not all flicker in exact unison.
float f1[N_CH], f2[N_CH], p1[N_CH], p2[N_CH];
float curLevel[N_CH];    // each engine's current brightness (0.0-1.0), updated every tick
float fadeFrom[N_CH];    // snapshot of curLevel[] taken the instant OFF is pressed, used as the starting point for the fade-out
float accentLevel = 0;   // accent lights' current brightness (0.0-1.0)
float accentFrom = 0;    // accent brightness at the moment OFF was pressed
uint8_t order[sizeof(HULL_PINS)];   // a shuffled list of hull-zone indexes - see shuffleHull()
uint8_t nextHull = 0;                // how many hull zones have been switched on so far this run
bool bridgeOn = false, hangarOn = false;
unsigned long t0 = 0;          // millis() value at the moment the show started (ON pressed) - everything else is timed relative to this
unsigned long lastTick = 0;    // millis() value the last time updateEngines()/updateFade() ran
unsigned long nextHullTime = 0; // millis()-since-t0 value at which the next hull zone should switch on
unsigned long fadeStart = 0;    // millis() value at the moment OFF was pressed

// Turns on the LEDC hardware PWM channel for each of the 7 engine pins.
// This only has to run once, in setup() - after this, setEngine() is all
// that is needed to change an engine's brightness.
void pwmBegin() {
  for (uint8_t i = 0; i < N_CH; i++) {
#if PWM_CORE_V3
    ledcAttach(CH_PINS[i], PWM_FREQ, PWM_BITS);   // newer core: pick the pin directly, channel number is handled internally
#else
    ledcSetup(i, PWM_FREQ, PWM_BITS);             // older core: set up PWM "channel" i (0-6, one per engine)...
    ledcAttachPin(CH_PINS[i], i);                 // ...then connect that channel to the actual engine pin
#endif
  }
#if PWM_CORE_V3
  ledcAttach(ACCENT_PIN, PWM_FREQ, PWM_BITS);       // the accent channel gets PWM too
#else
  ledcSetup(ACCENT_PWM_CH, PWM_FREQ, PWM_BITS);
  ledcAttachPin(ACCENT_PIN, ACCENT_PWM_CH);
#endif
}

// Sets the accent lights' brightness from a 0.0-1.0 level, with the same
// gamma correction as the engines so the fade looks smooth.
void setAccent(float level) {
  if (level < 0) level = 0;
  if (level > 1) level = 1;
  accentLevel = level;
  uint32_t duty = (uint32_t)(pow(level, GAMMA) * PWM_MAX);
#if PWM_CORE_V3
  ledcWrite(ACCENT_PIN, duty);
#else
  ledcWrite(ACCENT_PWM_CH, duty);
#endif
}

// Sets one engine's brightness. i is the engine's index into CH_PINS/CH_START_MS
// (0-6), and duty is how bright (0 = off, PWM_MAX = fully on - see "ENGINE
// PWM" above for what duty means).
void setEngine(uint8_t i, uint32_t duty) {
  if (duty > PWM_MAX) duty = PWM_MAX;   // safety clamp - never ask for more than the hardware supports
#if PWM_CORE_V3
  ledcWrite(CH_PINS[i], duty);          // newer core: write is addressed by pin
#else
  ledcWrite(i, duty);                   // older core: write is addressed by the channel number set up in pwmBegin()
#endif
}

// ---------------------------------------------------------------------------
// BLUETOOTH CONTROL PAD PROTOCOL
// The Bluefruit Connect app's "Control Pad" screen sends a fixed 5-byte
// packet every time a button is pressed or released:
//     byte 0: '!'              - a fixed start marker
//     byte 1: 'B'              - says "this is a button packet"
//     byte 2: the button number as a text character, e.g. '1' for button 1
//     byte 3: '1' if the button was just pressed, '0' if just released
//     byte 4: a checksum byte, used to catch corrupted packets
// For example, pressing button 1 sends the 5 bytes: ! B 1 1 <checksum>.
// Bluetooth delivers these bytes one at a time (onWrite() below may be
// called with only part of a packet, or with several packets stuck
// together), so feedByte() below collects bytes into a small buffer and
// only acts once it has a complete, valid 5-byte packet. See the companion
// "Main sketch explained" document for a full worked example of the
// checksum calculation.
// ---------------------------------------------------------------------------
void feedByte(uint8_t c) {
  static uint8_t buf[5];
  static uint8_t n = 0;              // how many bytes of the current packet we have collected so far
  if (n == 0 && c != '!') return;    // ignore stray bytes until we see a fresh start marker
  buf[n++] = c;
  if (n == 2 && buf[1] != 'B') { n = 0; return; }   // second byte must be 'B' (a button packet) - anything else, start over
  if (n == 5) {                      // we now have a complete 5-byte packet
    n = 0;                           // reset ready for the next packet, whatever we find below
    uint8_t sum = buf[0] + buf[1] + buf[2] + buf[3];
    if ((uint8_t)~sum == buf[4] && buf[3] == '1') {   // checksum matches, AND this is a "pressed" (not "released") event
      if (buf[2] == '1') pendingCmd = 1;   // button 1 -> ON
      else if (buf[2] == '2') pendingCmd = 2;   // button 2 -> OFF
      // any other button number is simply ignored
    }
  }
}

// Required by the BLE library: called automatically when the phone connects
// or disconnects. These just update a flag; loop() checks that flag.
class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer *s) { bleConnected = true; }
  void onDisconnect(BLEServer *s) { bleConnected = false; }
};

// Required by the BLE library: called automatically whenever the phone
// writes data to the RX characteristic. Simply hands each byte received
// to feedByte() above, in order.
class RxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    auto v = c->getValue();
    for (size_t i = 0; i < v.length(); i++) feedByte((uint8_t)v[i]);
  }
};

// A random float between lo and hi (inclusive-ish). random(0, 10000) gives a
// whole number from 0 to 9999, which is then scaled down to a 0.0-1.0
// fraction and stretched out to sit between lo and hi.
float randFloat(float lo, float hi) {
  return lo + (hi - lo) * (random(0, 10000) / 10000.0);
}

// Switches every single channel off - all 7 engines, all 12 zones and the
// accent lights - and resets the engines' remembered brightness to 0. Used both at power-up and
// at the end of a fade-out.
void allOff() {
  for (uint8_t i = 0; i < N_CH; i++) { setEngine(i, 0); curLevel[i] = 0; }
  digitalWrite(BRIDGE_PIN, LOW);
  digitalWrite(HANGAR_PIN, LOW);
  for (uint8_t i = 0; i < N_HULL; i++) digitalWrite(HULL_PINS[i], LOW);
  setAccent(0);
}

// Fills order[] with 0, 1, 2, ... N_HULL-1 and then shuffles that list into
// a random order, using the standard "Fisher-Yates shuffle" algorithm: walk
// backwards through the list, and at each position swap it with a random
// earlier-or-equal position. The result is used by loop() to decide which
// physical hull zone lights up 1st, 2nd, 3rd and so on each time the show
// runs, so the zones do not always light up in the same predictable order.
void shuffleHull() {
  for (uint8_t i = 0; i < N_HULL; i++) order[i] = i;
  for (uint8_t i = N_HULL - 1; i > 0; i--) {
    uint8_t j = random(i + 1);
    uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
  }
}

// Called once, the moment the ON button is pressed (or at boot, if
// START_ON_BOOT is true). Resets everything the show needs and switches
// into the RUNNING state - loop() takes it from here.
void startShow() {
  shuffleHull();
  nextHull = 0;
  bridgeOn = false;
  hangarOn = false;
  t0 = millis();       // "time zero" for this run - every timing check in loop() measures from here
  state = RUNNING;
}

// Called once, the moment the OFF button is pressed. Zones switch off
// immediately, but the engines are left running so updateFade() can wind
// them down smoothly over FADE_OUT_MS rather than cutting them instantly.
void stopShow() {
  digitalWrite(BRIDGE_PIN, LOW);
  digitalWrite(HANGAR_PIN, LOW);
  for (uint8_t i = 0; i < N_HULL; i++) digitalWrite(HULL_PINS[i], LOW);
  bridgeOn = false;
  hangarOn = false;
  accentFrom = accentLevel;                                       // the accent lights fade out with the engines
  for (uint8_t i = 0; i < N_CH; i++) fadeFrom[i] = curLevel[i];   // remember where each engine is right now...
  fadeStart = millis();                                           // ...and when the fade began...
  state = FADING;                                                 // ...so updateFade() can fade from there down to 0
}

// ---------------------------------------------------------------------------
// ENGINE BRIGHTNESS MATHS
// Called roughly 50 times a second (every UPDATE_MS) while the show is
// RUNNING. For each of the 7 engines it works out a brightness between 0.0
// and 1.0 by combining four effects, then converts that into a PWM duty
// value. The four effects, in the order they are combined below:
//   1. ramp   - a smooth 0->1 spool-up over RAMP_MS once the engine's start
//               time (CH_START_MS[i]) has arrived, using "smoothstep" easing
//               (x*x*(3-2*x)) instead of a straight line, which accelerates
//               gently into and out of the motion rather than snapping on.
//   2. breath - a slow sine wave shared by all engines, so brightness rises
//               and falls together like slow breathing, by up to
//               BREATH_DEPTH each way, once every BREATH_PERIOD_S seconds.
//   3. shimmer- a faster, smaller wobble, unique to each engine (because it
//               uses that engine's own f1/f2/p1/p2 values from setup()), so
//               the engines flicker independently rather than in lockstep.
//   4. gamma  - right at the end, pow(level, GAMMA) reshapes the 0-1 level
//               before it becomes a PWM duty. This is not an "effect" like
//               the others - it corrects for the fact that human eyes don't
//               perceive brightness in a straight line against duty cycle,
//               so without it a dimmed LED looks much brighter than its
//               number would suggest. See the companion document for a
//               worked example with real numbers.
// The companion "Main sketch explained" document walks through this
// function line by line with example numbers plugged in.
// ---------------------------------------------------------------------------
void updateEngines(unsigned long now) {
  float t = now / 1000.0;   // current time in seconds since the show started, for the sine waves below
  float breath = BREATH_DEPTH * sin(TWO_PI * t / BREATH_PERIOD_S);   // shared slow breathing wave, same for every engine
  for (uint8_t i = 0; i < N_CH; i++) {
    if (now < CH_START_MS[i]) { setEngine(i, 0); curLevel[i] = 0; continue; }   // this engine hasn't started yet - stay off
    float x = (now - CH_START_MS[i]) / (float)RAMP_MS;   // 0.0 at this engine's start time, reaching 1.0 after RAMP_MS
    if (x > 1.0) x = 1.0;                                 // ...and staying at 1.0 (fully spooled up) after that
    float ramp = x * x * (3.0 - 2.0 * x);                 // "smoothstep" - turns the straight 0->1 ramp into an eased S-curve
    float shimmer = SHIMMER * (0.6 * sin(TWO_PI * f1[i] * t + p1[i]) +
                               0.4 * sin(TWO_PI * f2[i] * t + p2[i]));   // two sine waves of different speed/phase, blended together, give an irregular-looking flicker rather than a simple pulse
    float level = ramp * (BASE + breath + shimmer);   // combine: still spooling up? scaled down by ramp. fully up? ramp=1 and this is just the breathing+shimmer brightness
    if (level < 0) level = 0;    // clamp - breath/shimmer could in theory push level slightly outside 0-1
    if (level > 1) level = 1;
    curLevel[i] = level;         // remember this, in case OFF is pressed next tick and stopShow() needs a fade-from value
    setEngine(i, (uint32_t)(pow(level, GAMMA) * PWM_MAX));   // gamma-correct, then scale 0.0-1.0 up to an actual PWM duty value
  }
  // Accent lights: a steady glow that fades up (with the same smoothstep
  // easing as the engines) starting at ACCENT_ON_MS. No breathing or shimmer.
  if (now < ACCENT_ON_MS) setAccent(0);
  else {
    float x = (now - ACCENT_ON_MS) / (float)ACCENT_RAMP_MS;
    if (x > 1.0) x = 1.0;
    setAccent(ACCENT_LEVEL * x * x * (3.0 - 2.0 * x));
  }
}

// Called roughly 50 times a second while the show is FADING (i.e. after
// OFF was pressed). x counts from 0.0 (fade just started) to 1.0 (fade
// complete) over FADE_OUT_MS, and each engine's brightness is simply its
// fadeFrom[] level scaled down by (1.0 - x) - so an engine that was dim
// when OFF was pressed reaches zero brightness at the same moment as one
// that was bright, they just both fade down proportionally together.
void updateFade(unsigned long nowAbs) {
  float x = (nowAbs - fadeStart) / (float)FADE_OUT_MS;
  if (x >= 1.0) { allOff(); state = IDLE; return; }   // fade finished - make sure everything is properly off, and go back to waiting for ON
  for (uint8_t i = 0; i < N_CH; i++) {
    float level = fadeFrom[i] * (1.0 - x);
    setEngine(i, (uint32_t)(pow(level, GAMMA) * PWM_MAX));   // same gamma correction as updateEngines(), for a consistent look
  }
  setAccent(accentFrom * (1.0 - x));
}

// Runs once, automatically, when the board is powered up or reset.
void setup() {
  // --- Set every lighting pin to OUTPUT mode and make sure everything starts off ---
  for (uint8_t i = 0; i < N_CH; i++) pinMode(CH_PINS[i], OUTPUT);
  pwmBegin();
  pinMode(BRIDGE_PIN, OUTPUT);
  pinMode(HANGAR_PIN, OUTPUT);
  for (uint8_t i = 0; i < N_HULL; i++) pinMode(HULL_PINS[i], OUTPUT);
  allOff();

  // --- Seed the random number generator ---
  // esp_random() reads the ESP32's built-in hardware random number
  // generator, which gives a different starting point for random()/randFloat()
  // every time the board starts, so the shimmer pattern and hull order are
  // different each run. (A2 used to be read for noise here; it now drives the
  // hangar bay.)
  randomSeed(esp_random() ^ micros());
  for (uint8_t i = 0; i < N_CH; i++) {
    f1[i] = randFloat(0.3, 0.9);    // each engine's two shimmer frequencies (in Hz-ish units - see updateEngines())...
    f2[i] = randFloat(1.1, 2.3);
    p1[i] = randFloat(0, TWO_PI);   // ...and its two shimmer starting phases, so engines don't flicker in sync
    p2[i] = randFloat(0, TWO_PI);
  }

  // --- Bluetooth setup ---
  // Creates a BLE "server" (the Nano ESP32 itself) advertising one service
  // (the Nordic UART Service defined above) with two characteristics: RX,
  // which the phone writes button presses into, and TX, which is required
  // by the standard but not used by this sketch. Once advertising starts,
  // the board shows up in the Bluefruit Connect app's device list as
  // "StarDestroyer".
  BLEDevice::init("StarDestroyer");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCb());
  BLEService *svc = pServer->createService(SERVICE_UUID);
  BLECharacteristic *tx = svc->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  tx->addDescriptor(new BLE2902());   // required so the phone can "subscribe" to TX notifications, even though we never send any
  BLECharacteristic *rx = svc->createCharacteristic(
      RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCb());       // RxCb::onWrite() fires every time the phone sends button data
  svc->start();
  BLEAdvertising *adv = pServer->getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();

  if (START_ON_BOOT) startShow();
}

// Runs over and over, forever, for as long as the board has power. Each
// pass through loop() is very fast (much less than a millisecond), so all
// the "do this every so often" logic below works by checking millis()
// (the number of milliseconds since the board started) rather than by
// pausing with delay() - that way Bluetooth and button presses are never
// kept waiting.
void loop() {
  // --- Act on the most recent button press, if any ---
  // pendingCmd is set by feedByte() (called from the Bluetooth RX
  // callback, which can happen at any moment) and is read and cleared
  // here so the actual response happens in the main loop, not inside the
  // Bluetooth callback itself.
  if (pendingCmd) {
    uint8_t cmd = pendingCmd;
    pendingCmd = 0;
    if (cmd == 1 && state == IDLE) startShow();      // ON only does anything if we're currently idle
    if (cmd == 2 && state == RUNNING) stopShow();    // OFF only does anything if the show is currently running
  }

  // --- Keep advertising after the phone disconnects ---
  // Once a phone connects, the board stops advertising itself (that's
  // normal BLE behaviour). If it doesn't restart advertising afterwards,
  // no other phone - or the same phone reconnecting - would ever find it
  // again, so this explicitly restarts advertising the moment a
  // disconnection is noticed.
  if (bleConnected && !wasConnected) wasConnected = true;
  if (!bleConnected && wasConnected) {
    delay(300);   // a short pause some BLE stacks need to settle before advertising again
    pServer->startAdvertising();
    wasConnected = false;
  }

  // --- Update engine brightness, at most every UPDATE_MS ---
  unsigned long nowAbs = millis();
  if (nowAbs - lastTick >= UPDATE_MS) {
    lastTick = nowAbs;
    if (state == RUNNING) updateEngines(nowAbs - t0);   // nowAbs - t0 = milliseconds since the show started
    else if (state == FADING) updateFade(nowAbs);
  }

  // --- Timeline: switch zones on at their scheduled moments ---
  // All the times below (BRIDGE_ON_MS etc.) are measured the same way as
  // CH_START_MS - milliseconds since the show started (t0), not a clock
  // time - so this check simply asks "has enough time passed yet?" on
  // every pass through loop(), and only acts the first time the answer
  // becomes yes (the bridgeOn/hangarOn flags make sure of that).
  if (state == RUNNING) {
    unsigned long now = millis() - t0;
    if (AUTO_OFF_MS > 0 && now >= AUTO_OFF_MS) { stopShow(); return; }   // show has been running long enough - fade out automatically
    if (!bridgeOn && now >= BRIDGE_ON_MS) {
      digitalWrite(BRIDGE_PIN, HIGH);
      bridgeOn = true;
      // The first hull zone's reveal time is only decided now, once the
      // bridge has come on - this is what makes the hull zones start
      // appearing only after the bridge, never before it.
      nextHullTime = now + random(HULL_MIN_MS, HULL_MAX_MS + 1);
    }
    if (!hangarOn && now >= HANGAR_ON_MS) {
      digitalWrite(HANGAR_PIN, HIGH);
      hangarOn = true;
    }
    // Reveal hull zones one at a time, in the shuffled order[] from
    // shuffleHull(), each one at a random gap (between HULL_MIN_MS and
    // HULL_MAX_MS) after the previous one - so all 10 eventually light up,
    // but never on a predictable schedule.
    if (bridgeOn && nextHull < N_HULL && now >= nextHullTime) {
      digitalWrite(HULL_PINS[order[nextHull]], HIGH);
      nextHull++;
      nextHullTime = now + random(HULL_MIN_MS, HULL_MAX_MS + 1);
    }
  }
}
