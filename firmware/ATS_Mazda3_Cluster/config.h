// =============================================================================
//  config.h - everything you are likely to want to tweak lives here.
// =============================================================================
#pragma once

// ---------------------------------------------------------------------- Board
// Autosport Labs ESP32-CAN-X2 ("AutosportLabs ESP-CAN-X2" in the esp32 board
// package). HS-CAN goes on CAN1, the ESP32's built-in controller (connector
// X1), and MS-CAN on CAN2, the board's MCP2515 (connector X2).
#if !defined(ARDUINO_ARCH_ESP32)
#error "Select Tools > Board > esp32 > AutosportLabs ESP-CAN-X2"
#endif

// ---------------------------------------------------------------- Serial link
// Must match the baud rate in the SimHub serial device profile (115200).
#define SERIAL_BAUD 115200

// If no telemetry arrives for this long the game is treated as paused/closed
// and the needles drop back to zero.
#define TELEMETRY_TIMEOUT_MS 1000

// Print a status line every second (handy in the Arduino Serial Monitor, but
// turn it off once SimHub is driving the board - it doesn't need the chatter).
#define DEBUG_STATUS 0

// Where debug/status text goes. Serial is the board's USB-C port, the same
// port SimHub uses.
#define DEBUG_PORT Serial

// ------------------------------------------------------------- HS-CAN (X1)
// HS-CAN runs on the ESP32's built-in controller (CAN1, connector X1), wired
// to GPIO7 (TX) and GPIO6 (RX) on the board.
#define CAN_TX_PIN 7
#define CAN_RX_PIN 6

// The 2004-2009 (BK) Mazda 3 cluster sits on the 500 kbit/s high-speed bus.
#define CAN_SPEED CAN_500KBPS

// ------------------------------------------------------------- MS-CAN (X2)
// The cluster also sits on the 125 kbit/s body bus (MS-CAN, cluster pins 1M/1O,
// plug positions 7/8), which drives the turn arrows, high beam and SRS lamp.
// It runs on the board's MCP2515 (CAN2, connector X2). `#m...` serial commands
// send custom frames on this bus and `#scan m` searches it (see README). On by
// default: without it the SRS and ABS lamps stay lit and BRAKE won't clear.
#define MS_CAN_ENABLED 1
// The board's MCP2515 has its chip select on GPIO10 and a 16 MHz crystal.
#define MS_CAN_CS_PIN  10
#define MS_CAN_CLOCK   MCP_16MHz
#define MS_CAN_SPEED   CAN_125KBPS

// Sweep all needles to full scale and back when the board powers up.
#define SWEEP_ON_BOOT 1
#define SWEEP_MAX_RPM 8000
#define SWEEP_MAX_KMH 225  // 140 mph, the top of the speedo
#define SWEEP_DURATION_MS 1500  // each direction

// ---------------------------------------------------------- Input scaling
// physical = raw * SCALE + OFFSET, applied to every value SimHub sends.
//
// Defaults assume SimHub sends real units (RPM, mph, deg C, %).
// Example: 8-bit RPM output covering 0..3000 rpm -> RPM_SCALE (3000.0 / 255.0)
//
// Speed is expected in mph: SPEED_SCALE converts it to the km/h the cluster
// takes on CAN. If your speed arrives in km/h use 1.0, if in m/s use 3.6.
#define RPM_SCALE       1.0f
#define RPM_OFFSET      0.0f
#define SPEED_SCALE     1.609344f
#define SPEED_OFFSET    0.0f
#define COOLANT_SCALE   1.0f
#define COOLANT_OFFSET  0.0f
#define THROTTLE_SCALE  1.0f
#define THROTTLE_OFFSET 0.0f
// Fuel is expected as a percentage (0 = empty, 100 = full). If SimHub gives
// litres or gallons instead, scale by 100 / tank capacity.
#define FUEL_SCALE      1.0f
#define FUEL_OFFSET     0.0f

// Multiplies RPM before it goes to the cluster. 1.0 shows the true reading.
// Trucks redline near 2500 rpm, which barely moves the cluster's 8000 rpm tach;
// 3.0 stretches a truck's range across the dial (2500 truck rpm -> 7500).
#define RPM_DISPLAY_MULTIPLIER 1.0f

// Gear display. 1 shows forward gears as D with the gear number 1-6 (gears
// above 6 show 6, since the cluster stops there). 0 shows every forward gear
// as plain D, like an automatic.
#define GEAR_SHOW_NUMBER 1
// 1 lights the manual-mode lamp with a numbered gear, so it reads M instead
// of D. 0 keeps D.
#define GEAR_MANUAL_LAMP 0

// Trip meter / odometer: 0x420 byte 1 counts up with distance and the cluster
// adds it to the trip and odometer. ODO_MILES_PER_COUNT is the distance the
// cluster adds per count, about 1/8085 mile (~0.2 m), measured on a 2005
// cluster: 5 minutes at 60 mph reads 5.0 miles.
// 0 = off.
#define ODO_MILES_PER_COUNT (1.0f / 8085.0f)

// --------------------------------------------- Optional GPIO indicator outputs
// The factory wiring diagram for the 2005 Mazda 3 cluster (0922-1b/1c) shows the
// turn signal and high beam lamps driven by the cluster's own microcomputer, so
// they arrive over CAN (probably the MS-CAN body bus on pins 1M/1O), not as
// hard-wired inputs. Pins 1N and 1P are vacant, and 1K is HS-CAN L - never
// drive 1K from a transistor or relay.
//
// These outputs are kept, off by default, for clusters that do turn out to have
// hard-wired lamp inputs. Each pin drives a transistor/relay that switches the
// cluster input (see README). Set a pin to -1 to disable it.
//
// The parking brake lamp is sent over CAN (0x212). PIN_PARK_BRAKE is only for
// clusters where it turns out to be hard-wired instead.
//
// The pins are 3.3 V. Avoid GPIO2 (CAN activity LED), 3 (MCP2515 interrupt),
// 6/7 (CAN1), 10-13 (MCP2515 SPI), 19/20 (USB), 43/44 (X4 serial) and the
// boot pins 0, 45 and 46. GPIO 21, 47 and 48 (SV1 pins 8-10) are free.
#define PIN_LEFT_TURN   -1
#define PIN_RIGHT_TURN  -1
#define PIN_HIGH_BEAM   -1
#define PIN_PARK_BRAKE  -1
// The oil pressure lamp isn't on CAN: the cluster reads the oil pressure
// switch on 16-pin position 9 (1B), grounded = lamp on. To drive it from the
// game, wire an NPN transistor (or relay) from position 9 to ground and give
// its base pin here (through ~1 kOhm), e.g. 21.
#define PIN_OIL_LAMP    -1

// ------------------------------------------------------------ Fuel gauge
// The fuel gauge reads the tank sender's resistance on 24-pin positions 1/2,
// not CAN. To drive it from the game, fit the sender stand-in from the README
// (a FUEL_FIXED_OHMS resistor across the pins, plus a FUEL_SWITCHED_OHMS
// resistor switched by a logic-level MOSFET, plus a capacitor) and give the
// driver input pin here. -1 = off. Defaults to GPIO14 (SV1 pin 7), as on the
// tested board. The stand-in's GND wire must go to an SV2 GND pin, not power
// ground, or the MOSFET can't switch properly. The board's pins only reach
// 3.3 V, so a 2N7000 stage drives the main MOSFET's gate from the board's 5 V
// (see FUEL_PWM_INVERT and the protoboard drawing). Tested on a real 2005
// cluster.
#define PIN_FUEL_SENDER 14
// Sender resistance the needle shows as full, half and empty. Measured on a
// 2005 cluster with fixed resistors: about 160-175 ohms sits on E, 54 on half,
// 20 one tick under F, 16.8 right on F, and 10 goes past F. (`#ohms <n>` holds
// the stand-in at n ohms to re-check any of these values.)
#define FUEL_SENDER_FULL_OHMS  16.8f
#define FUEL_SENDER_HALF_OHMS  54.0f
#define FUEL_SENDER_EMPTY_OHMS 175.0f
// The two resistors in the stand-in. Anything at or above
// FUEL_SENDER_EMPTY_OHMS works for the fixed one: the PWM pulls it down to the
// empty reading. The switched one sets the lowest reading: 10 ohms reaches
// about 10 ohms, past F, so 100 % lands on F (checked after a power cycle).
#define FUEL_FIXED_OHMS    470.0f
#define FUEL_SWITCHED_OHMS 10.0f
// PWM resolution: 10-bit, so the small switched resistor still gets fine steps
// near empty.
#define FUEL_PWM_BITS 10
// 1 when a driver stage between the pin and the MOSFET inverts the signal:
// pin high = MOSFET off. The 2N7000 stage (gate pulled up to 5 V, 2N7000 pulls
// it down) does, because a 3.3 V pin left the bench IRLB8721 barely on. Set 0
// if the pin drives the MOSFET gate directly.
#define FUEL_PWM_INVERT 1

// CAN activity LED: the board's data LED on GPIO2 flickers while frames go out
// to or come in from the cluster on either bus, and stays dark when nothing
// gets through (no cluster, wrong wiring). -1 = off.
#define PIN_CAN_LED 2
// How fast the LED flickers while there is traffic: it toggles this often.
#define CAN_LED_FLICKER_MS 50

// Set to 1 if your driver circuit needs the pin LOW to light the lamp.
#define INDICATOR_ACTIVE_LOW 0

// Turn arrows are sent over the body bus (MS-CAN 0x265), and the cluster holds
// them solid, so the firmware flashes them: on for this long, off for this
// long (333 ms is about 1.5 flashes a second). If your game output already
// blinks, the lamp follows its timing anyway. 0 = show the input as-is.
#define TURN_BLINK_HALF_MS 333
