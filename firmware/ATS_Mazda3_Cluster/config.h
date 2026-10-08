// =============================================================================
//  config.h - everything you are likely to want to tweak lives here.
// =============================================================================
#pragma once

// ---------------------------------------------------------------- Serial link
// Must match the baud rate set on the SimTools interface (Game Engine ->
// Interface Settings). 115200 is the fastest SimTools offers and is plenty.
#define SERIAL_BAUD 115200

// If no telemetry arrives for this long the game is treated as paused/closed
// and the needles drop back to zero.
#define TELEMETRY_TIMEOUT_MS 1000

// Print a status line every second (handy in the Arduino Serial Monitor, but
// turn it off once SimTools is driving the board - it doesn't need the chatter).
#define DEBUG_STATUS 0

// Where debug/status text goes. The Mega has three spare hardware UARTs, so
// you can send debug text to Serial1 (TX1 = D18) through a USB-serial adapter
// and keep the main USB port clean for SimTools. Leave as Serial to use the
// same USB port.
#define DEBUG_PORT Serial

// ------------------------------------------------------------- CAN-BUS shield
// Target board: Arduino Mega 2560 (ATmega2560).
//
// Seeed CAN-BUS Shield v1.1+ / v2.0 use D9 for chip-select. The very first
// v1.0 boards used D10.
//
// On the Mega the hardware SPI bus is D50 (MISO), D51 (MOSI), D52 (SCK) - NOT
// D11-D13 like an Uno. Shield v2.0 picks SPI up from the 6-pin ICSP header, so
// it works on the Mega as-is. Shield v1.x routes SPI to D11-D13: on a Mega you
// must jumper D11->D51, D12->D50, D13->D52 (and leave D11-13 unused).
#define CAN_CS_PIN 9

// The shield's MCP2515 runs from a 16 MHz crystal. Some cheap clone modules
// use 8 MHz - change to MCP_8MHz if yours does.
#define CAN_CLOCK MCP_16MHz

// The 2004-2009 (BK) Mazda 3 cluster sits on the 500 kbit/s high-speed bus.
#define CAN_SPEED CAN_500KBPS

// Sweep all needles to full scale and back when the board powers up.
#define SWEEP_ON_BOOT 1
#define SWEEP_MAX_RPM 7000
#define SWEEP_MAX_KMH 220
#define SWEEP_DURATION_MS 1500  // each direction

// ---------------------------------------------------------- Input scaling
// physical = raw * SCALE + OFFSET, applied to every value SimTools sends.
//
// Defaults assume SimTools sends real units (RPM, km/h, deg C, %). If your
// output is set to a bit range instead (e.g. 8-bit = 0..255), rescale here.
// Example: 8-bit RPM output covering 0..3000 rpm -> RPM_SCALE (3000.0 / 255.0)
//
// American Truck Simulator reports speed in whatever SimTools hands over; if
// it arrives in mph use SPEED_SCALE 1.609344, if in m/s use 3.6.
#define RPM_SCALE       1.0f
#define RPM_OFFSET      0.0f
#define SPEED_SCALE     1.0f
#define SPEED_OFFSET    0.0f
#define COOLANT_SCALE   1.0f
#define COOLANT_OFFSET  0.0f
#define THROTTLE_SCALE  1.0f
#define THROTTLE_OFFSET 0.0f

// Trucks idle around 600 rpm and redline near 2500 - on a 7000 rpm car
// tachometer that barely moves the needle. Multiply RPM before it goes to the
// cluster so a truck's range fills the dial (2.5 maps 2500 truck rpm -> 6250).
// Set to 1.0 for a true reading.
#define RPM_DISPLAY_MULTIPLIER 2.5f

// --------------------------------------------- Hard-wired indicator outputs
// On the BK Mazda 3 the turn signals, high beam and parking brake lamps are
// wired directly into the cluster connector rather than sent over CAN. Each
// pin below drives a transistor/relay that switches the matching cluster
// input (see README). Set a pin to -1 to disable it.
//
// Avoid D2 (shield interrupt), D4 (shield SD card CS), D9/D10 (CAN CS),
// D50-D53 (Mega SPI) and D0/D1 (USB serial to SimTools). The Mega has plenty
// of other free pins (D22-D49) if you'd rather move these off the shield area.
#define PIN_LEFT_TURN   3
#define PIN_RIGHT_TURN  5
#define PIN_HIGH_BEAM   6
#define PIN_PARK_BRAKE  7

// Set to 1 if your driver circuit needs the pin LOW to light the lamp.
#define INDICATOR_ACTIVE_LOW 0
