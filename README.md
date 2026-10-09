# ATS_Mazda3_Cluster
Bridging physical and simulation - Connect a physical Mazda 3 instrument cluster and obtain readings from ATS on the real gauges!

```
American Truck      SimTools          Arduino Mega 2560          2005 Mazda 3
Simulator   ──────► or SimHub   ────► + Seeed CAN-BUS  ────────► instrument
(telemetry)         (USB serial)      Shield (MCP2515) CAN 500k  cluster
```

SimTools or SimHub reads ATS telemetry and streams it as text over USB serial. The Mega
parses it and continuously sends the CAN frames the cluster would normally get
from the engine computer (PCM) and ABS unit, so the tach, speedo, temperature
gauge, fuel gauge and warning lamps follow the game.

## Repository layout

| Path | What it is |
|---|---|
| `firmware/ATS_Mazda3_Cluster/` | Arduino sketch: open `ATS_Mazda3_Cluster.ino` in the Arduino IDE |
| `firmware/ATS_Mazda3_Cluster/config.h` | Every setting you're likely to change: pins, scaling, timeouts |
| `firmware/ATS_Mazda3_Cluster/mazda3_can.h` | CAN IDs and byte layouts for the cluster |
| `firmware/ATS_Mazda3_Cluster/telemetry_parser.h` | Parser for the serial text SimTools sends |
| `tools/cluster_test.py` | Drive the cluster from a PC without the game (bench testing) |
| `test/test_logic.cpp` | Unit tests for the frame encoding and parser (run on a PC) |

## Hardware

- 2004–2009 (BK) Mazda 3 instrument cluster (the 2005 is a BK)
- Arduino Mega 2560 (ATmega2560)
- Seeed Studio CAN-BUS Shield (MCP2515 + MCP2551, 16 MHz crystal)
- 12 V DC supply, at least 1 A (a bench supply or a 12 V wall adapter)
- A 2–3 A inline fuse for the 12 V feed
- Optional: a 120 Ω resistor, if your cluster turns out not to terminate the
  CAN bus itself (see [Termination](#wiring-the-cluster))

### Shield on a Mega

- **Shield v2.0**: plugs straight in. It takes SPI from the 6-pin ICSP header,
  which the Mega also has.
- **Shield v1.x**: routes SPI to D11–D13, but the Mega's SPI is on D50–D52.
  Jumper **D11→D51, D12→D50, D13→D52**, and bend out or cut the shield's
  D11–D13 pins so they don't fight the Mega.
- Chip-select is **D9** (very early v1.0 boards used D10; see `CAN_CS_PIN`).

### Wiring the cluster

The pin numbers below come from the factory wiring diagram for the 2005
Mazda 3 2.3L instrument cluster (diagrams 0922-1a to 0922-1d). The cluster has
two connectors on the back: a 16-pin one (pins `1A`–`1P`) and a 24-pin one
(pins `2A`–`2X`). Wire colours are the car harness side, so they only help if
you cut the plugs from a donor car. Check each pin with a multimeter on your
own cluster before you power it, because other markets and years may differ.

On the 16-pin plug, the "Position" column counts with the plug held as below,
looking at the wire side with the latch up: the top row is 1–8 from right to
left, and the bottom row is 9–16 from right to left. Factory letters run
`1A`, `1C`, … `1O` along the top and `1B`, `1D`, … `1P` along the bottom, and
the wire colours match a 2005 harness plug in this orientation.

```
 top row:     8   7   6   5   4   3   2   1      (1O 1M 1K 1I 1G 1E 1C 1A)
 bottom row: 16  15  14  13  12  11  10   9      (1P 1N 1L 1J 1H 1F 1D 1B)
```

**16-pin plug, every position:**

| Position | Factory pin | Wire | Signal | Bench |
|---|---|---|---|---|
| 1 | `1A` | — | Vacant | — |
| 2 | `1C` | O/B (orange/black) | B+, constant 12 V | 12 V supply + |
| 3 | `1E` | B/O (black/orange) | Ground | 12 V supply − and Arduino GND |
| 4 | `1G` | G/R (green/red) | IG1, ignition 12 V | 12 V supply + |
| 5 | `1I` | GY/R (grey/red) | HS-CAN H, 500 kbit/s | Shield CAN-H |
| 6 | `1K` | L/R (blue/red) | HS-CAN L, 500 kbit/s | Shield CAN-L |
| 7 | `1M` | GY/V (grey/violet) | MS-CAN H, 125 kbit/s (body bus) | Leave open |
| 8 | `1O` | L/W (blue/white) | MS-CAN L, 125 kbit/s (body bus) | Leave open |
| 9 | `1B` | B/Y (black/yellow) | Oil pressure switch (grounded = oil lamp on) | Leave open |
| 10 | `1D` | — | Vacant | — |
| 11 | `1F` | B/O (black/orange) | Ground | 12 V supply − and Arduino GND |
| 12 | `1H` | — | Vacant | — |
| 13 | `1J` | — | Vacant | — |
| 14 | `1L` | O (orange) | Illumination, +12 V with headlights on | Optional: 12 V via a switch for the backlight |
| 15 | `1N` | — | Vacant | — |
| 16 | `1P` | B/G (black/green) | Washer fluid-level sensor (only on cars with one) | Leave open |

Positions 5 and 6 are a twisted pair in the harness, as are 7 and 8.

**24-pin plug, every position.** Counted with the plug held the same way, from
the wire side: the top row is 1–12 from left to right, and the bottom row is
13–24 from left to right. None of these pins are needed on the bench.

```
 top row:     1   2   3   4   5   6   7   8   9  10  11  12   (2W 2U 2S 2Q 2O 2M 2K 2I 2G 2E 2C 2A)
 bottom row: 13  14  15  16  17  18  19  20  21  22  23  24   (2X 2V 2T 2R 2P 2N 2L 2J 2H 2F 2D 2B)
```

| Position | Factory pin | Wire | Signal | Bench |
|---|---|---|---|---|
| 1 | `2W` | BR/R (brown/red) | Fuel gauge sender, one side | Leave open (fuel is driven over CAN `0x433`) |
| 2 | `2U` | W/R (white/red) | Fuel gauge sender, other side | Leave open |
| 3 | `2S` | GY/O (grey/orange) | Immobilizer coil antenna | Leave open (the security lamp may blink) |
| 4 | `2Q` | W/G (white/green) | Immobilizer coil antenna | Leave open |
| 5 | `2O` | W (white) | Car navigation unit (only on cars with nav) | Leave open |
| 6 | `2M` | R/Y (red/yellow) | Brake switch 2 (brake pedal) | Leave open |
| 11 | `2C` | R/B (red/black) | Key reminder switch (key in the ignition) | Leave open |
| 7–10, 12–24 | | — | Vacant | — |

**Bench minimum** (this is all the cluster needs to wake up and move needles):

| Cluster pin | Position | Wire | Signal | Connect to |
|---|---|---|---|---|
| `1C` | 2 | O/B | B+ (constant 12 V, ROOM fuse in the car) | 12 V supply + |
| `1G` | 4 | G/R | IG1 (ignition 12 V, METER 10 A fuse in the car) | 12 V supply + (the cluster stays dark without it) |
| `1E` | 3 | B/O | Ground | 12 V supply − **and** Arduino GND |
| `1F` | 11 | B/O | Ground | 12 V supply − **and** Arduino GND |
| `1I` | 5 | GY/R | HS-CAN H (500 kbit/s) | Shield CAN-H |
| `1K` | 6 | L/R | HS-CAN L (500 kbit/s) | Shield CAN-L |

Fuse the 12 V feed (a 2–3 A inline fuse is plenty for a bench cluster), since
the car has a 10 A and a 15 A fuse on these lines.

**Optional pins on the 16-pin plug:**

| Cluster pin | Wire | Signal | Notes |
|---|---|---|---|
| `1L` (position 14) | O | Illumination (TNS, +12 V when the headlights are on) | 12 V through a switch if you want the backlight and gauge lighting |
| `1M`, `1O` (positions 7, 8) | GY/V, L/W | MS-CAN H, L (body bus, 125 kbit/s) | Not used by this firmware. See [Turn signals and high beam](#turn-signals-and-high-beam) |
| `1B` (position 9) | B/Y | Oil pressure switch | Grounding it lights the red oil lamp. Leave it open on the bench (open = pressure OK) |
| `1P` (position 16) | B/G | Washer fluid-level sensor (only on cars with one) | Leave open |

All other pins are vacant. In the car, the brake fluid-level and parking brake
switches go to the junction box, not the cluster, which is why the parking
brake lamp comes over CAN (`0x212`).

**Termination:** a CAN bus needs 120 Ω across H and L at each end. The
diagram draws a resistor across HS-CAN H/L (and across MS-CAN H/L) inside the
cluster, so the cluster probably terminates its end already. With everything
unpowered, measure `1I` to `1K` on the bare cluster:

- About **120 Ω**: the cluster is terminated. Don't add a resistor.
- **Open circuit**: add a 120 Ω resistor across `1I` and `1K` at the cluster end.

The Seeed shield has the other 120 Ω fitted (on some revisions it's a solder
jumper or switch). With both ends terminated and the power off, you should
read about 60 Ω between the shield's CAN-H and CAN-L.

Twist the CAN-H/CAN-L pair, and keep it away from the 12 V supply leads.

### Turn signals and high beam

On this cluster the turn signal and high beam lamps are **not** hard-wired
inputs. The diagram shows them driven by the cluster's own microcomputer,
like the warning lamps, and the pins earlier versions of this README gave for
them are wrong: `1N` and `1P` are vacant (or the washer sensor), and `1K` is
HS-CAN L. Don't put a transistor or 12 V on `1K`, because it will take down the
CAN bus.

The cluster gets those lamp states over CAN, most likely the MS-CAN body bus
on `1M`/`1O` (plug positions 7/8). The `L`, `Y` and `H` fields do nothing until
the right frames are found. On a bench-tested 2005 cluster, nothing in `0x433`
on HS-CAN lit them, including the high beam bit (byte 3, `0x40`) that community
RX-8/Mazda decodes list for MS-CAN.

#### Reaching the body bus (MS-CAN)

This needs a second MCP2515. Either option works:

**A second Seeed CAN-BUS shield, stacked on the first** (the `config.h` defaults:
`MS_CAN_CS_PIN 10`, `MS_CAN_CLOCK MCP_16MHz`):

- **Chip select:** the two shields can't share D9. Move the second shield's
  chip select to **D10** with the CS selection pads on its board (cut the D9
  link and bridge D10).
- **SPI:** the shields take SPI from the Mega's 6-pin ICSP header. Check that
  the lower shield passes the ICSP header up to the top one. If it doesn't,
  wire the top shield's ICSP pins to D50 (MISO), D51 (MOSI) and D52 (SCK).
- **Interrupt pin:** both shields drive D2 as their interrupt output, and two
  outputs on one pin fight each other. The firmware doesn't use it, so bend out
  or cut the top shield's D2 pin.
- **Old jumpers:** remove any jumper wires on D10–D13 or D48–D51 from earlier,
  because D10 is now a chip select.
- **Termination:** keep the second shield's 120 Ω termination fitted, since the
  body bus has no other nodes on the bench.
- Wire the second shield's CAN-H/CAN-L to cluster positions 7 (`1M`) / 8 (`1O`).

**Or a loose MCP2515 module**, such as the common blue board with a TJA1050
transceiver:

| Module pin | Mega pin |
|---|---|
| VCC | 5V |
| GND | GND |
| SCK | D52 |
| SI | D51 |
| SO | D50 |
| CS | D53 (set `MS_CAN_CS_PIN 53`) |
| INT | not needed |
| CAN-H / CAN-L | cluster positions 7 (`1M`) / 8 (`1O`) |

Check the crystal on the module: most read `8.000` (set `MS_CAN_CLOCK
MCP_8MHz`), and some read `16.000` (`MCP_16MHz`).

Either way, set `MS_CAN_ENABLED 1` in `config.h`.
At boot the Serial Monitor prints `MS-CAN ready`, or `MS-CAN init failed` if
the module isn't answering. The gauges keep working either way. Fit the
module's 120 Ω termination jumper if it has one, since the body bus has no other
nodes on the bench.

#### Finding the lamp frames

1. Type `#scan m` in the Serial Monitor (or `#scan h` to search the main bus).
   The board sends all-`0xFF` frames on 16 IDs at a time, two seconds per block,
   and prints each block, such as `scan MS 0x430-0x43F`. It takes about four
   minutes to cover every ID.
2. Note the block that lights a turn arrow or the high beam, then type `#scan`
   to stop. It prints where it stopped, for example
   `scan stopped at HS 0x4D0`. Type `#scan resume` to carry on from that
   block, or `#scan h 4D0` (or `#scan m 4D0`) to start from any ID.
3. Narrow it down with custom frames, for example `#m433 FF FF FF FF FF FF FF FF`
   for one ID, then one byte at a time, then one bit.
4. Add the confirmed ID, byte and bit to `mazda3_can.h`.

The GPIO lamp outputs are still in the firmware, off by default
(`PIN_LEFT_TURN`, `PIN_RIGHT_TURN`, `PIN_HIGH_BEAM` are `-1` in `config.h`), for
clusters that do turn out to have hard-wired lamp inputs. If you use them,
never connect an Arduino pin straight to the cluster, because these inputs
work at 12 V. Use a transistor or relay per lamp:

- **Low-side (input pulled to ground):** NPN transistor (2N2222/BC547).
  Arduino pin → 1 kΩ → base, emitter → ground, collector → cluster pin.
- **High-side (input fed +12 V):** a PNP or P-channel MOSFET driven by an
  NPN, or a relay/optocoupler module, switching +12 V to the cluster pin.

If your driver turns the lamp on when the pin is LOW, set
`INDICATOR_ACTIVE_LOW 1`.

## Firmware setup

1. Install the **Arduino IDE** (1.8.x or 2.x).
2. *Library Manager* → install **"CAN-BUS Shield" by Seeed Studio** (v2.x,
   which provides `mcp2515_can.h`). Or install it from
   <https://github.com/Seeed-Studio/Seeed_Arduino_CAN>.
3. Open `firmware/ATS_Mazda3_Cluster/ATS_Mazda3_Cluster.ino`.
4. *Tools → Board → Arduino Mega or Mega 2560*, *Processor → ATmega2560*.
5. Review `config.h`, then upload.

On power-up the board sweeps the tach and speedo to full scale and back, which
shows the CAN link works. If the needles don't move, see
[Troubleshooting](#troubleshooting).

## Bench test without the game

```
pip install pyserial
python tools/cluster_test.py COM5 --sweep            # needles sweep continuously
python tools/cluster_test.py COM5 --rpm 2000 --speed 60 --coolant 90 --fuel 50
python tools/cluster_test.py COM5 --rpm 800 --lamps  # cycle each lamp in turn
```

Replace `COM5` with your Mega's port (check Device Manager or the Arduino
IDE). Close the Serial Monitor and SimTools first, because only one program can
hold the port at a time.

## SimTools setup

1. In **SimTools Game Manager**, install and patch the **American Truck
   Simulator** game plugin. It installs the ATS telemetry SDK plugin into the
   game's `bin\win_x64\plugins` folder.
2. In **SimTools Game Engine → Interface Settings**, choose an interface and set:
   - Interface type: **Serial**
   - COM port: your Mega's port
   - BitsPerSec: **115200**, Data Bits 8, Parity None, Stop Bits 1
   - Output type: **Decimal**
   - Interval: about 10–20 ms
3. Set the **Interface Output** string so each value is a field letter followed
   by the SimTools output token for that value, and end it with `;`. For example:

   ```
   R<rpm token>S<speed token>T<water temp token>A<throttle token>F<fuel token>E<engine warning token>B<battery warning token>O<oil warning token>L<left blinker token>Y<right blinker token>H<high beam token>P<parking brake token>;
   ```

   Replace each `<... token>` with the matching dash/telemetry output from your
   SimTools version's output list (it lists all the values the ATS plugin
   provides, under GameDash or the extra-output functions). You only need the
   fields you want. Leave out any the plugin doesn't provide.
4. Start ATS and start driving. The board treats the game as live as soon as
   fields arrive, and parks the needles if nothing arrives for 1 s (the game
   is paused or closed).

## SimHub setup (alternative to SimTools)

The firmware doesn't care which program sends the text, so SimHub works too.

1. In SimHub, enable the **Custom serial devices** plugin, add a device, and
   pick the Mega's COM port at **115200** baud.
2. Add an **update message** and set it to a computed formula (NCalc):

   ```
   'R' + format([Rpms], '0') +
   'S' + format([SpeedMph], '0.0') +
   'T' + format([WaterTemperature], '0') +
   'A' + format([Throttle], '0') +
   'F' + format([FuelPercent], '0') +
   'E' + if([EngineWarning] > 0, '1', '0') +
   'O' + if([OilPressureWarning] > 0, '1', '0') +
   'P' + if([Handbrake] > 0, '1', '0') +
   'L' + if([TurnIndicatorLeft] > 0, '1', '0') +
   'Y' + if([TurnIndicatorRight] > 0, '1', '0') +
   'H' + if([HighBeam] > 0, '1', '0') +
   ';'
   ```

   Check the preview in SimHub's formula editor while ATS is running. It should
   show something like `R1450S72.0T88A35F64E0O0P0L1Y0H0;`. A property that
   comes through as `True`/`False` or that SimHub doesn't know will show up
   there. Fix it before sending, because stray letters are read as field keys.
3. Set the update rate to 30–60 Hz.

SimHub's `[SpeedMph]` is mph, which matches the default `SPEED_SCALE`. If you
send `[SpeedKmh]` instead, set `SPEED_SCALE 1.0`.

### Serial protocol

Each field is an upper-case letter followed by a number, for example
`R2350S88.5T90A42F75E0B0O0L1Y0H0P0;`. Fields can be in any order, and separators
(`;`, `,`, spaces, newlines) are optional between fields. End each packet with
`;` or a newline so the last field is applied immediately.

| Field | Meaning | Default unit |
|---|---|---|
| `R` | Engine RPM | rpm |
| `S` | Vehicle speed | mph |
| `T` | Coolant temperature | °C |
| `A` | Throttle / accelerator | % |
| `F` | Fuel level | % (0 empty, 100 full) |
| `E` | Check-engine lamp | 0/1 |
| `B` | Battery/charge lamp | 0/1 |
| `O` | Oil pressure lamp | 0/1 |
| `L` | Left turn signal | 0/1 (GPIO, off by default; see [Turn signals and high beam](#turn-signals-and-high-beam)) |
| `Y` | Right turn signal | 0/1 (GPIO, off by default) |
| `H` | High beam | 0/1 (GPIO, off by default) |
| `P` | Parking brake | 0/1 |

### Scaling

Every value goes through `physical = raw × SCALE + OFFSET` (in `config.h`)
before it reaches the cluster:

- **Speed units**: speed is expected in mph, and the default
  `SPEED_SCALE 1.609344` converts it to the km/h the cluster takes on CAN (a
  US cluster converts back to mph on the dial). If your speed arrives in
  km/h, set `SPEED_SCALE 1.0`. If it arrives in m/s, use `3.6`.
- **Bit-range output**: if SimTools sends values scaled to a bit range
  (for example 8-bit, 0–255) rather than real units, set the scale to
  `real_max / 255`.
- **Truck RPM on a car tach**: `RPM_DISPLAY_MULTIPLIER` defaults to `1.0`, so
  the tach shows the true reading. A truck redlines around 2,500 rpm, which
  barely moves the cluster's 8,000 rpm needle. Set it to `3.0` to stretch the truck's
  range across the dial.
- **Fuel in litres or gallons**: the cluster wants a percentage. If SimTools
  gives the amount in the tank, set `FUEL_SCALE` to `100 / tank capacity`.

## CAN frames sent

All frames use 11-bit IDs at 500 kbit/s.

| ID | Every | Bytes | Content |
|---|---|---|---|
| `0x201` | 20 ms | 0–1 | RPM × 0.96 (big-endian; 1:1 read about 4% high) |
| | | 4–5 | Speed: km/h × 100 (big-endian) |
| | | 6 | Throttle × 2 |
| `0x4B0` | 20 ms | 0–7 | Four wheel speeds: km/h × 100 + 10000 (keeps the ABS lamp quiet) |
| `0x420` | 100 ms | 0 | Coolant: °C + 40 |
| | | 1 | `0x40` = check-engine lamp |
| | | 4 | `1` = oil pressure OK, `0` = red oil lamp on |
| | | 6 | `0x40` = charge lamp |
| `0x212` | 100 ms | 4 | `0x40` = brake warning lamp (parking brake) |
| `0x433` | 100 ms | 0 | Fuel level: `0x00` (empty) to `0x64` (100, full); bytes 1–7 are `0x00` |

The calibration factors (`RPM_CAN_FACTOR`, `SPEED_CAN_FACTOR`,
`SPEED_CAN_OFFSET`) and lamp bits are constants at the top of `mazda3_can.h`.
The charge lamp bit and the `0x4B0` wheel-speed encoding come from RX-8
research (the RX-8 shares much of its CAN matrix with the Mazda 3) and haven't
been confirmed on a 2005 cluster. Use the `#` command below to check them on
yours.

### Experimenting with frames over serial

You can add extra frames at runtime without reflashing. Type these in the
Serial Monitor (115200 baud, newline line ending), or send them with
`tools/cluster_test.py --raw`:

| Command | Effect |
|---|---|
| `#420 82 40 00 00 01 00 00 00` | Send this frame every 100 ms (hex). If the ID is a built-in one, the built-in frame stops until you remove the custom one. |
| `#420` | Stop sending custom frame `0x420` |
| `#m433 00 00 00 40 00 00 00 00` | The same, on the body bus (MS-CAN). Needs the second module (`MS_CAN_ENABLED 1`). |
| `#-` | Remove all custom frames and stop a scan |
| `#?` | List custom frames |
| `#scan m` / `#scan h` | Scan every ID on MS-CAN / HS-CAN with all-`0xFF` frames, 16 IDs every 2 s (see [Finding the lamp frames](#finding-the-lamp-frames)) |
| `#scan h 4D0` / `#scan m 4D0` | Start a scan at ID `0x4D0` and carry on up from there (`hs`/`ms` and a `0x` prefix also work) |
| `#scan` | Stop a scan and print the block it stopped at |
| `#scan resume` | Carry on from the block where the last scan stopped |

Each command echoes the frame back (for example `  0x420 82 40 00 00 01 00 00 00`).
If nothing comes back, check that the Serial Monitor's line ending is set to
**Newline**. Up to 8 custom frames can be active. They're lost on reset. Once you've found a
frame that works, add it to `mazda3_can.h` permanently.

## Known limitations

- **Odometer:** not driven, so it won't put real distance on your cluster.
- **Cruise control lamp:** not driven. The matrix lists `0x201` byte 6, which
  is also the throttle byte, and doesn't give a bit value. If the green cruise
  lamp flickers with throttle, that byte is the cause. Try values with
  `#201 ...` to find the cruise bit.
- **Other lamps** (airbag, ABS, TCS, door, seatbelt): may stay lit because
  their modules aren't present. Find their frames with the `#` command if they
  bother you.

## Troubleshooting

| Symptom | Check |
|---|---|
| `CAN init failed` repeats in the Serial Monitor | Wrong CS pin (`CAN_CS_PIN` 9 vs 10), a v1.x shield without the Mega SPI jumpers, or an 8 MHz module (set `CAN_CLOCK MCP_8MHz`) |
| Cluster dark | No 12 V on IG1 (`1G`) or B+ (`1C`), or no ground on `1E`/`1F` |
| Cluster lights up but needles don't move during the boot sweep | CAN-H/L swapped (`1I` is H, `1K` is L), missing termination (aim for ~60 Ω across H/L with power off), or no common ground between the Arduino and the 12 V supply |
| Needles work with `cluster_test.py` but not in game | Wrong COM port or baud in SimTools/SimHub, two programs holding the port, or the output string doesn't end with `;` |
| Speed reads about 1.6× too high or too low | `SPEED_SCALE` doesn't match your units: `1.609344` for mph (default), `1.0` for km/h, `3.6` for m/s |
| Tach barely moves | Raise `RPM_DISPLAY_MULTIPLIER` |
| Tach or speedo reads a fixed ratio off | Adjust `RPM_CAN_FACTOR` or `SPEED_CAN_FACTOR` in `mazda3_can.h` |

## Development

Run the unit tests for the frame encoding and parser on a PC:

```
g++ -std=c++11 -Wall -Wextra -I firmware/ATS_Mazda3_Cluster test/test_logic.cpp -o test_logic && ./test_logic
```
