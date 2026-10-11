3D model · PresenceCam S3 · snap-fit enclosure

# PresenceCam S3 Case

Two printed parts. The back plate screws to the wall. The front shell holds everything and snaps onto the plate with four hooks, and one screw underneath locks it closed. The coloured runs in the model follow the wiring diagram.

Outer **77.8 × 92.3 × 32.4 mm**2 printed parts5 heat-set inserts0 collisions in the model

Loading model…

Drag to orbit · scroll or pinch to zoom · right-drag to pan

[x] Front shell [ ] See-through [x] Back plate [x] Parts [x] 18650 [x] Wires

Wire colours: **red** power (3V3, CAM_3V3, VSYS, BAT+), **dark** ground, **blue** SPI and I²C, **green** signals (PIR, RAIL_EN, USB_SENSE, VIEW). Turn on See-through and Exploded to follow them. The runs show the routing, not exact wire lengths.

## How each part is held

no glue anywhere

Arducam Mega 3MP4 × M2 heat-set inserts

Four 14 mm standoffs on the inside of the face, with M2 × 5 screws from behind. These fix the lens position, so it stays centred in its hole.

ESP32-S3-LCD-1.47Brails + 2 back-plate posts

Drops screen-first into a locating frame. When the shell snaps on, two posts on the back plate press it against the face. Its USB-C lines up with the bottom-wall opening.

HC-SR501standoffs + pin posts

The dome pushes through the round hole and the PCB rests on two standoffs. Back-plate posts with 1.7 mm pins drop into its mounting holes.

VL53L0Xrails + spacer ribs + posts

The ribs hold the sensor 0.2 mm behind its window so it never touches the face. Peel the film off first.

18650 + BMS3 snap clips

C-clips wrap about 230° around the cell, so it pushes in with a click and pulls out by hand. Tape the BMS to the cell with Kapton.

Power perfboardpads + end rails + posts

4 × 10 holes of stripboard (10.2 × 25.4 mm) carrying Q1, the 100k pull-up and the USB-sense divider.

VIEW button7 mm panel nut

A 7 mm momentary button (PBS-110 type) through the bottom wall, wired across the board's BOOT button pads.

Back plate4 snap hooks + 1 M3 insert

Hooks: 1.6 × 6 mm arms, 12 mm long, 0.9 mm catch with a 45° back face, so the case opens with firm pressure from a spudger. One M3 screw from below into a heat-set insert locks it shut.

## Materials

| Part | Print in | Why |
| --- | --- | --- |
| Front shell | PETG | The battery clips and snap pockets need some give, and PETG holds heat-set inserts well. Use ASA instead if the case will sit in direct sun. PLA softens on a sunny windowsill and slowly lets go of inserts. |
| Back plate | PETG | The hooks bend about 2% each time the case is opened. PETG survives that many times. PLA cracks after a few openings. |
| Lens collar, optional | TPU 95A, black | A thin ring around the camera lens stops light from the LCD backlight bouncing into the lens. Print it 1 mm thick, 15 mm inside and 20 mm outside diameter. |

## Hardware

what to order

| Qty | Part | Where | Hole in the print |
| --- | --- | --- | --- |
| 4 | M2 × 3 mm heat-set insert (Ruthex RX-M2x3 or similar) | Camera standoffs | Ø3.2 × 4.5 deep |
| 4 | M2 × 5 mm screw | Camera to standoffs | — |
| 1 | M3 × 5.7 mm heat-set insert (Ruthex RX-M3x5.7) | Locking tab on the back plate | Ø4.0 × 6 deep |
| 1 | M3 × 8 mm button-head screw | Through the bottom wall into the tab | Ø3.4 + Ø6.2 counterbore |
| 2 | No. 6 (3.5 mm) pan-head screw + wall plug | Back plate to the wall, behind the battery | Ø4.2 + Ø8.4 counterbore |
| 1 | 7 mm panel-mount momentary button (PBS-110) | VIEW, bottom wall | Ø7.2 |
| — | 26 AWG silicone wire (battery, power) · 28–30 AWG (signals) · Kapton tape | Wiring | — |
| 1 | Tamper-evident "VOID" label, optional | Across the seam over the locking screw | — |

**Fitting the heat-set inserts:** set the iron to about 230 °C for PETG (245 °C for ASA) and use an insert tip if you have one. Press straight down until the insert sits flush, then hold a flat block on it for a few seconds while it cools. The camera standoffs are tall, so support the face from below while you press. Fit all five inserts before anything else goes in.

## Print settings

### front-shell.stl

- Face down on the bed. No supports: the snap pockets and the USB, button and screw holes are short bridges.
- 0.2 mm layers, **4 walls** so the insert holes have solid plastic around them, 25% gyroid infill.
- A textured PEI sheet gives the face a matte finish that hides fingerprints.

### back-plate.stl

- Already flipped: the wall side goes on the bed, with hooks and posts pointing up.
- Set the hooks to **100% infill** (a modifier or the small-area infill setting) and print them a little slower. They bend across their layer lines, so solid layers matter.
- If a hook ever snaps, reprint the plate. Nothing else depends on it.

## Assembly order

1. Fit the four M2 inserts in the camera standoffs and the M3 insert in the back plate's locking tab.
2. Screw the camera onto its standoffs, lens through the face.
3. Fit the PIR (dome through the hole), the VL53L0X and the S3 board (screen first, USB-C to the bottom opening).
4. Fit the VIEW button and the perfboard, then wire everything as on the wiring page. Run wires in the 3–4 mm channel just inside the back plate.
5. Clip in the 18650 with its BMS last, and keep the battery leads short.
6. Screw the back plate to the wall, hooks facing out. Hang the shell on it and press until all four hooks click. Fit the M3 locking screw from below.

## Measure before you print

These are estimates. Waveshare and Arducam only publish their exact sizes as drawings. Change the value at the top of `s3-147b/case/case.js` and run `node build.js`.

| Parameter | Now | What to measure |
| --- | --- | --- |
| s3W × s3H | 22 × 47.5 | S3 board outline. |
| s3Stack | 4.0 | Front of the screen glass to the back of the PCB. |
| lcdOffY | 2.0 | How far the screen's centre sits above the board's centre, toward the end without the USB-C. |
| camHoleSpacing | 28 | Centre-to-centre spacing of the camera's corner holes. |
| camLensD · camHeight | 14.5 · 17 | Lens barrel diameter, and the camera's height from the back of the PCB to the lens tip. |
| tofW × tofH | 25 × 10.7 | VL53L0X board outline. |

Files on your server: `/root/esp32-presence-case/s3-147b/case/print/` (front-shell.stl, back-plate.stl). The model source is `case.js`, built with JSCAD.