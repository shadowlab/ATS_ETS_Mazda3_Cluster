# ATS_Mazda3_Cluster
Bridging physical and simulation - Connect a physical Mazda 3 instrument cluster and obtain readings from ATS on the real gauges!

```
American Truck      SimTools          Arduino Mega 2560          2005 Mazda 3
Simulator   ──────► Game Engine ────► + Seeed CAN-BUS  ────────► instrument
(telemetry)         (USB serial)      Shield (MCP2515) CAN 500k  cluster
                                         │
                                         └── GPIO ─► transistors ─► hard-wired lamps
                                                                    (turn signals, high beam,
                                                                     parking brake)
```

SimTools reads ATS telemetry and streams it as text over USB serial. The Mega
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
- Optional, for the hard-wired lamps: 4× NPN transistors (2N2222/BC547) or a
  small relay/optocoupler board, plus 1 kΩ base resistors

### Shield on a Mega

- **Shield v2.0**: plugs straight in. It takes SPI from the 6-pin ICSP header,
  which the Mega also has.
- **Shield v1.x**: routes SPI to D11–D13, but the Mega's SPI is on D50–D52.
  Jumper **D11→D51, D12→D50, D13→D52**, and bend out or cut the shield's
  D11–D13 pins so they don't fight the Mega.
- Chip-select is **D9** (very early v1.0 boards used D10; see `CAN_CS_PIN`).

### Wiring the cluster

The cluster needs four groups of connections. Find the pin numbers in a BK
Mazda 3 wiring diagram for the instrument cluster connector, and check them
with a multimeter on your cluster. Pinouts vary between markets and model
years, so this README doesn't list pin numbers.

| Cluster signal | Connect to |
|---|---|
| B+ (battery, constant 12 V) | 12 V supply + |
| IG1 (ignition 12 V) | 12 V supply + (the cluster stays dark without it) |
| Ground(s) | 12 V supply − **and** Arduino GND (common ground is required) |
| CAN-H (HS-CAN) | Shield CAN-H |
| CAN-L (HS-CAN) | Shield CAN-L |
| Illumination (optional) | 12 V through a switch, if you want the backlight on |

**Termination:** a CAN bus needs 120 Ω across H and L at each end. The
Seeed shield has one fitted (on some revisions it's a solder jumper or
switch). A bare cluster usually doesn't, so add a 120 Ω resistor across CAN-H
and CAN-L at the cluster end. With both fitted and the power off, you should
read about 60 Ω between H and L.

Twist the CAN-H/CAN-L pair, and keep it away from the 12 V supply leads.

### Hard-wired lamps

On the BK Mazda 3, the turn signals, high beam and parking brake lamps are not
on CAN. They're separate wires into the cluster connector. The firmware drives
pins **D3 (left turn), D5 (right turn), D6 (high beam) and D7 (parking brake)**
(change them in `config.h`). Never connect an Arduino pin straight to the
cluster, because these inputs work at 12 V. Use a transistor or relay per lamp.

- If the cluster input lights when **pulled to ground** (the parking brake
  switch is usually like this), use an NPN transistor: Arduino pin → 1 kΩ →
  base, emitter → ground, collector → cluster input.
- If the input lights when **fed +12 V** (turn signals and high beam usually
  are), use a high-side switch: a PNP or P-channel MOSFET driven by an NPN,
  or a relay/optocoupler module.

Check each input with a fused jumper wire before building the driver. If your
driver turns the lamp on when the pin is LOW, set `INDICATOR_ACTIVE_LOW 1`.

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
python tools/cluster_test.py COM5 --rpm 2000 --speed 100 --coolant 90 --fuel 50
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

### Serial protocol

Each field is an upper-case letter followed by a number, for example
`R2350S88.5T90A42F75E0B0O0L1Y0H0P0;`. Fields can be in any order, and separators
(`;`, `,`, spaces, newlines) are optional between fields. End each packet with
`;` or a newline so the last field is applied immediately.

| Field | Meaning | Default unit |
|---|---|---|
| `R` | Engine RPM | rpm |
| `S` | Vehicle speed | km/h |
| `T` | Coolant temperature | °C |
| `A` | Throttle / accelerator | % |
| `F` | Fuel level | % (0 empty, 100 full) |
| `E` | Check-engine lamp | 0/1 |
| `B` | Battery/charge lamp | 0/1 |
| `O` | Oil pressure lamp | 0/1 |
| `L` | Left turn signal | 0/1 (GPIO) |
| `Y` | Right turn signal | 0/1 (GPIO) |
| `H` | High beam | 0/1 (GPIO) |
| `P` | Parking brake | 0/1 (GPIO) |

### Scaling

Every value goes through `physical = raw × SCALE + OFFSET` (in `config.h`)
before it reaches the cluster:

- **Speed in mph**: if your SimTools output gives mph, set
  `SPEED_SCALE 1.609344`. The cluster always takes km/h on CAN, and a US
  cluster converts to mph itself. If speed arrives in m/s, use `3.6`.
- **Bit-range output**: if SimTools sends values scaled to a bit range
  (for example 8-bit, 0–255) rather than real units, set the scale to
  `real_max / 255`.
- **Truck RPM on a car tach**: a truck redlines around 2,500 rpm, which barely
  moves a 7,000 rpm needle. `RPM_DISPLAY_MULTIPLIER` (default `2.5`) stretches
  the truck's range across the dial. Set it to `1.0` for a true reading.
- **Fuel in litres or gallons**: the cluster wants a percentage. If SimTools
  gives the amount in the tank, set `FUEL_SCALE` to `100 / tank capacity`.

## CAN frames sent

All frames use 11-bit IDs at 500 kbit/s.

| ID | Every | Bytes | Content |
|---|---|---|---|
| `0x201` | 20 ms | 0–1 | RPM × 4 (big-endian) |
| | | 4–5 | Speed: km/h × 100 + 10000 |
| | | 6 | Throttle × 2 |
| `0x4B0` | 20 ms | 0–7 | Four wheel speeds, same encoding as `0x201` speed (keeps the ABS lamp quiet) |
| `0x420` | 100 ms | 0 | Coolant: °C + 40 |
| | | 4 | Oil pressure OK = 1 |
| | | 5 | `0x40` = check-engine lamp |
| | | 6 | `0x40` = charge lamp, `0x80` = oil pressure lamp |
| `0x433` | 100 ms | 0 | Fuel level: `0x00` (empty) to `0x64` (100, full); bytes 1–7 are `0x00` |

`0x201` comes from community reverse-engineering of the Mazda 3. The `0x420`
lamp bits are documented for the RX-8, which shares most of its CAN matrix with
the Mazda 3. They're expected to match but haven't been confirmed on a BK
cluster. Use the `#` command below to check them on yours.

### Experimenting with frames over serial

You can add extra frames at runtime without reflashing. Type these in the
Serial Monitor (115200 baud, newline line ending), or send them with
`tools/cluster_test.py --raw`:

| Command | Effect |
|---|---|
| `#420 82 00 00 00 01 40 00 00` | Send this frame every 100 ms (hex). A built-in ID is overridden. |
| `#420` | Stop sending custom frame `0x420` |
| `#-` | Remove all custom frames |
| `#?` | List custom frames |

Up to 8 custom frames can be active. They're lost on reset. Once you've found a
frame that works, add it to `mazda3_can.h` permanently.

## Known limitations

- **Odometer:** left static on purpose. Byte 1 of `0x420` is the odometer
  increment counter. Ticking it would put real kilometres on your cluster's
  odometer.
- **Other lamps** (airbag, ABS, TCS, door, seatbelt): may stay lit because
  their modules aren't present. Find their frames with the `#` command if they
  bother you.

## Troubleshooting

| Symptom | Check |
|---|---|
| `CAN init failed` repeats in the Serial Monitor | Wrong CS pin (`CAN_CS_PIN` 9 vs 10), a v1.x shield without the Mega SPI jumpers, or an 8 MHz module (set `CAN_CLOCK MCP_8MHz`) |
| Cluster dark | No 12 V on IG1/B+, or no ground |
| Cluster lights up but needles don't move during the boot sweep | CAN-H/L swapped, missing termination (aim for ~60 Ω across H/L with power off), or no common ground between the Arduino and the 12 V supply |
| Needles work with `cluster_test.py` but not in game | Wrong COM port or baud in SimTools, SimTools and another program both holding the port, or the output string doesn't end with `;` |
| Speed reads about 60% low | Telemetry is in mph and needs `SPEED_SCALE 1.609344` |
| Tach barely moves | Raise `RPM_DISPLAY_MULTIPLIER` |

## Development

Run the unit tests for the frame encoding and parser on a PC:

```
g++ -std=c++11 -Wall -Wextra -I firmware/ATS_Mazda3_Cluster test/test_logic.cpp -o test_logic && ./test_logic
```
