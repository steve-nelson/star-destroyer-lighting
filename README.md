# Star Destroyer 1/2700 - Nano ESP32 fibre-optic lighting

Fibre-optic lighting for the Revell/Zvezda 1/2700 Imperial Star Destroyer, controlled by an
Arduino Nano ESP32 and switched on/off over Bluetooth with Adafruit's free **Bluefruit Connect**
app - no custom app needed.

20 independently-driven channels (7 engines, 10 hull sections, bridge, hangar bay, and six SMD
accent lights along the hull sides) through three TBD62083APG 8-channel driver chips, built on two 5 x 7 cm prototype boards that fit inside the hull. Every channel plugs in
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
- **`pin_test_nano.ino`** - lights every channel in turn (ending with the accent lights on A2),
  then checks the engine PWM fades.
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
  tests: drawings of both boards (component side and underside), wire tables, how each
  channel's LEDs plug in, and the accent-light junction boards. Hole names: columns A-X, rows
  1-18 from the bottom.
- **`board_zone_map.pdf`** - one-page map of both boards showing which socket lights which part
  of the ship (Top, Trench, Under on each side, Bridge, Accent, Main and Aux engines, Hangar).
  Also the last page of `build_guide.pdf`.
- **`main_sketch_explained.pdf`** - a plain-language, worked-example walkthrough of the trickier
  parts of the main sketch: the Bluetooth button protocol and its checksum, and the maths behind
  the engine spool-up, breathing, shimmer and gamma correction.
- **`main_nano_esp32.ino`** - the main sketch (Bluetooth control, all 20 channels, engine and
  accent-light PWM).

## Board layout and channel plan

- **Board 1** (Nano + Driver C): the 7 engine sockets along its bottom edge, plus a right-angle
  **Hangar** socket at T6 (+5 V) / T7 (OUT) for the hangar bay (Nano A2 -> Driver C input 8).
- **Board 2** (Drivers A and B) has its 12 sockets grouped by where the leads go. Mount it with
  column A toward the left side of the hull, column X toward the right and the row 1 edge toward
  the stern and the support pole:
  - left edge: Hull 1-5 (pins in column C)
  - right edge: Hull 6-10 (pins in column V)
  - stern edge: Bridge (upright header, plug from above, lock ramp toward the drivers so pin 1
    is on J3) and Accent (right-angle) on row 3. The bridge is a 4-LED cluster: two LEDs for the
    bridge towers and two for the raised upper-hull section.
  - the driver block is centred: Driver A fitted turned round (notch down, pin 1 at J7), the
    ribbon box header upright between the drivers (L/M, pin 1 at M15 on the key side), Driver B notch up (O15).

### Which socket lights which zone

Left and right are the ship's own sides, looking from the stern towards the bow
(see `build/board_zone_map.pdf`).

| Board 2 left edge | Zone | Board 2 right edge | Zone |
|---|---|---|---|
| Hull 1 | Top L1 | Hull 6 | Top R1 |
| Hull 2 | Top L2 | Hull 7 | Top R2 |
| Hull 3 | Trench L1 | Hull 8 | Trench R1 |
| Hull 4 | Trench L2 | Hull 9 | Trench R2 |
| Hull 5 | Under L | Hull 10 | Under R |

Board 1: Engines 1-3 are the main engines (Main 1-3, right-hand end of the engine row) and
Engines 4-7 are the smaller engines (Aux 1-4); the Hangar socket feeds the underside hangar bay.
The top-hull clusters (Hull 1, 2, 6, 7) have 5 LEDs each, the 5th with its own 330 ohm resistor
like the others. The hull zones light in a random order, so the sketch is unchanged.

## Accent lights (channel 20)

Six pre-wired SMD LEDs, three along each side of the hull, on one channel: Nano **D9** drives
Driver B input 1 on Board 2, and its output goes to the **Accent** socket on Board 2's stern edge
(N3/O3). The fine LED leads run only a few centimetres to a small stripboard junction board on
each side (3 strips x 11 holes, one 1 k resistor per LED), and a sturdier red/black pair runs
from each junction board back to one XH plug. The accent lights fade up to a steady glow 14 s
into the show and fade out with the engines.

## Phone controls and engine presets

In the Bluefruit Connect app choose **Controller > Control Pad**:

| Button | Action |
|---|---|
| 1 | ON - starts the show |
| 2 | OFF - engines and accent lights fade out, everything else switches off |
| 3 (or up arrow) | next engine preset: Low -> Medium -> High -> Battle -> Low |
| 4 (or down arrow) | previous engine preset |

Presets change only the engines (brightness, breathing depth, breathing speed and shimmer) and
blend in over about a second, even mid-show. The chosen preset is saved in the Nano's flash, so
it is still selected after power-off. The preset name is reported on the app's UART screen.
Edit the `PRESETS` table near the top of `main_nano_esp32.ino` to change them. See
`build/engine_presets.pdf`, Stage 7 of the build guide, and `main_sketch_explained.pdf`.

## Status

The circuit and sketches have been proven on a breadboard with the pin and engine test sketches.

## Licence

Personal/hobby project documentation, shared as-is for anyone building the same model.
