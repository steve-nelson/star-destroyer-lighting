# Star Destroyer 1/2700 - Nano ESP32 fibre-optic lighting

Fibre-optic lighting for the Revell/Zvezda 1/2700 Imperial Star Destroyer, controlled by an
Arduino Nano ESP32 and switched on/off over Bluetooth with Adafruit's free **Bluefruit Connect**
app - no custom app needed.

19 independently-driven zones (7 engines, 10 hull sections, bridge, hangar bay) through three
TBD62083APG 8-channel driver chips, built on two 5 x 7 cm prototype boards that fit inside the hull. Every channel plugs in
with a 2-pin JST XH connector, so LEDs can be swapped on and off for testing.
Power comes from a single USB-C charger via a USB-C Power Delivery breakout board, with only two
wires (+5V and GND) running up through the model's display-stand support pole - including to the
Nano itself, powered through its own USB-C port rather than its VIN pin (VIN needs 6-21V; this
design's shared bus is 5V).

## Two phases, two folders

This repo is split into the two stages of actually building this: prove it works, then build it
for real.

### `testing/` - prove it on a breadboard first

Do this before you solder anything. Wire the whole circuit (all three driver boards, plus the
Nano itself, plugged straight into a breadboard like a chip would) exactly as drawn, upload the
pin-tester sketch, and confirm every channel lights correctly and the engines fade individually.
Any design mistake should surface here, where it costs nothing to fix.

- **`breadboard_test_diagrams.pdf`** - full wiring diagrams for the breadboard prototype, plus the
  short test procedure that goes with them.
- **`pin_test_nano.ino`** - lights every channel in turn, then checks the engine PWM fades.
- **`engine_test_nano/engine_test_nano.ino`** - engines only (Board 1 alone): one at a time, all
  together, a fade on each, then the main sketch's spool-up, breathing and shimmer for 30 seconds.

### `build/` - the permanent build

Once the breadboard proves the design, this is the actual build: soldering the final two
prototype boards, wiring them into the hull, fitting the fibre optics, and powering it all up for
good. `pin_test_nano.ino` from `testing/` gets used again partway through, to re-check the
soldered boards - at that point it's only catching soldering mistakes, since the design itself
was already proven.

- **`build_guide.pdf`** - the full build guide: parts list, board wiring, the two-board layout,
  bench-testing the soldered boards, Bluetooth pairing, fibre-optic installation, final assembly
  and power-up, plus troubleshooting.
- **`perfboard_build_stages.pdf`** - the soldered boards in short sessions, each with its own
  tests: drawings of both boards (component side and underside), wire tables, and how each
  channel's LEDs plug in. Hole names: columns A-X, rows 1-18 from the bottom.
- **`main_sketch_explained.pdf`** - a plain-language, worked-example walkthrough of the trickier
  parts of the main sketch: the Bluetooth button protocol and its checksum, and the maths behind
  the engine spool-up, breathing, shimmer and gamma correction.
- **`main_nano_esp32.ino`** - the main sketch (Bluetooth control, all 19 channels, engine PWM).

## Status

This design has not yet been proven on real hardware. The build guide flags the specific checks
to run (Stage 2, Stage 5 and Stage 10) before you rely on it - do them before gluing anything
shut, since the hull is permanent once closed.

## Licence

Personal/hobby project documentation, shared as-is for anyone building the same model.
