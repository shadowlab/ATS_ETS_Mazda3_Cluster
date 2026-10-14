// =============================================================================
//  mazda3_can.h - CAN frame layouts for the 2004-2009 (BK) Mazda 3 cluster.
//
//  Kept free of Arduino dependencies so it can be unit-tested on a PC
//  (see test/test_logic.cpp).
//
//  RPM, speed, coolant and the 0x420 lamp bits are bench-tested on a 2005
//  cluster. The 0x212 parking brake bit (from RX-8 research) works on a
//  2005 cluster once the body bus (MS-CAN) is connected. The 0x4B0
//  wheel-speed encoding comes from RX-8 research (the two cars share much of
//  their CAN matrix). Use the serial `#` commands to
//  experiment without reflashing.
// =============================================================================
#pragma once

#include <stdint.h>
#include <math.h>

namespace mazda3 {

// ------------------------------------------------------------------ IDs
const uint16_t ID_RPM_SPEED   = 0x201;  // PCM: engine RPM, vehicle speed, throttle
const uint16_t ID_ENGINE_INFO = 0x420;  // PCM: coolant temp, odometer, warning lamps
const uint16_t ID_WHEEL_SPEED = 0x4B0;  // ABS: individual wheel speeds
// No HS 0x433 fuel frame: a bench-tested 2005 cluster transmits 0x433 itself
// on HS-CAN, so sending it too would clash on the bus. Its fuel gauge reads
// the analog tank sender (24-pin 2U/2W) instead.
const uint16_t ID_BRAKE_LAMPS = 0x212;  // brake warning lamp (parking brake)
// Bench-tested on a 2005 cluster: the AT warning lamp is on until 0x228 is
// heard, and all-zero data turns it off. Byte 0 is the gear display and
// byte 1 bit 0x80 the manual-mode lamp (codes below). Traction control lives
// in the 0x220-0x22F block too (bits not yet mapped).
const uint16_t ID_TRANSMISSION = 0x228;
// Body bus (MS-CAN, 125 kbit/s, cluster 1M/1O). Separate from the cluster's
// own HS 0x433 above.
const uint16_t ID_BODY_LAMPS  = 0x433;  // high beam indicator
const uint16_t ID_TURN_LAMPS  = 0x265;  // turn signal arrows
// Bench-tested on a 2005 cluster: the SRS airbag lamp is on until MS 0x460 is
// heard, and all-zero data turns it off. Stands in for the airbag module.
// With byte 1 = 0x00 the cluster chimes and flashes the seat belt lamp every
// minute or so; byte 1 = 0xFF stops it (exact bit not narrowed).
const uint16_t ID_AIRBAG      = 0x460;
const uint8_t AIRBAG_B1_NO_BELT_CHIME = 0xFF;

// ------------------------------------------------------- Calibration
// 0x201 bytes 0-1: raw = rpm * RPM_CAN_FACTOR (big-endian).
// Bench-tested on a 2005 cluster: 1:1 read about 4% high (5000 showed 5200),
// so the raw value is trimmed to 0.96.
const float RPM_CAN_FACTOR = 0.96f;
// 0x201 bytes 4-5: raw = km/h * SPEED_CAN_FACTOR + SPEED_CAN_OFFSET.
// Bench-tested on a 2005 cluster: km/h * 177.6 read 1.78x high (75 km/h showed
// 83 mph), so the cluster takes km/h * 100.
const float SPEED_CAN_FACTOR = 100.0f;
const float SPEED_CAN_OFFSET = 0.0f;

// ------------------------------------------------------------ Lamp bits
// Bench-tested on a 2005 cluster: all in 0x420 byte 4. Bit 0 is kept set as
// before (it doesn't drive the oil lamp, which is the wired switch on 1B).
const uint8_t ENGINE_B4_BASE         = 0x01;
const uint8_t ENGINE_B4_CHARGE       = 0x02;  // battery/charge lamp
const uint8_t ENGINE_B4_CHECK_ENGINE = 0x40;  // check-engine lamp, steady
const uint8_t ENGINE_B4_CHECK_FLASH  = 0x80;  // check-engine lamp, flashing
const uint8_t BRAKE_B4_PARKING_BRAKE = 0x40;  // 0x212 byte 4
// Bench-tested on a 2005 cluster: MS-CAN 0x433 byte 3 = 0x40 lights the
// high beam indicator.
const uint8_t BODY_B3_HIGH_BEAM      = 0x40;  // MS 0x433 byte 3
// Bench-tested on a 2005 cluster: MS-CAN 0x265 byte 0 lights the arrows
// (0x10 is the immobilizer lamp, not used). The cluster shows them solid while
// the bit is set, so the flashing comes from TurnBlinker below.
const uint8_t TURN_B0_LEFT           = 0x20;  // MS 0x265 byte 0
const uint8_t TURN_B0_RIGHT          = 0x40;  // MS 0x265 byte 0

// 0x228 byte 0, bench-tested on a 2005 cluster. 0 shows no gear. Gears 1-6
// show as a number: the gear in the high nibble with D's 4 in the low one
// (0x14 = 1st ... 0x64 = 6th).
const uint8_t GEAR_NONE    = 0;
const uint8_t GEAR_PARK    = 1;
const uint8_t GEAR_REVERSE = 2;
const uint8_t GEAR_NEUTRAL = 3;
const uint8_t GEAR_DRIVE   = 4;
const uint8_t GEAR_MAX_NUMBER = 6;
// 0x228 byte 1: manual-mode lamp. Bench-tested; 0x40-0x79 light the AT
// warning lamp instead, so keep the rest of the byte clear.
const uint8_t TRANS_B1_MANUAL = 0x80;

// Game gear (negative = reverse, 0 = neutral, positive = forward gear,
// GEAR_INPUT_PARK = park) to the cluster's gear display. With showNumber,
// forward gears show as 1-6 (anything higher shows 6); without it, as D.
const float GEAR_INPUT_PARK = 99.0f;
inline uint8_t gearFromGame(float gear, bool showNumber = false) {
  if (gear >= GEAR_INPUT_PARK - 0.5f) return GEAR_PARK;
  if (gear <= -0.5f) return GEAR_REVERSE;
  if (gear >= 0.5f) {
    if (!showNumber) return GEAR_DRIVE;
    int n = (int)(gear + 0.5f);
    if (n > GEAR_MAX_NUMBER) n = GEAR_MAX_NUMBER;
    return (uint8_t)((n << 4) | GEAR_DRIVE);
  }
  return GEAR_NEUTRAL;
}

// True when the display code is a numbered gear (1-6) rather than P/R/N/D.
inline bool gearIsNumbered(uint8_t code) { return (code >> 4) != 0; }

// ------------------------------------------------------- Cluster state
struct ClusterState {
  float rpm;          // engine rpm as it should appear on the tach
  float speedKmh;     // vehicle speed, km/h (the cluster converts for mph dials)
  float coolantC;     // coolant temperature, deg C
  float throttlePct;  // 0..100
  float fuelPct;      // 0 (empty) .. 100 (full)
  bool checkEngine;
  bool checkEngineFlash;  // flashing MIL (misfire style)
  bool chargeWarning;
  bool oilWarning;
  bool parkingBrake;
  bool highBeam;
  bool leftTurn;   // arrow lit right now (already blinked)
  bool rightTurn;
  uint8_t gear;    // GEAR_* display code
  bool manualLamp; // 0x228 manual-mode lamp (shows M instead of D)
};

// Turns a turn-signal input into a flashing lamp. The cluster holds an arrow
// solid while its bit is set, so a steady "signal on" from the game is
// flashed here. A game that already sends the blink still works: each new
// rising edge restarts the phase, so the lamp follows the game's own timing.
struct TurnBlinker {
  bool wasOn;
  uint32_t onSinceMs;

  bool update(bool on, uint32_t nowMs, uint32_t halfPeriodMs) {
    if (on && !wasOn) onSinceMs = nowMs;
    wasOn = on;
    if (!on) return false;
    if (halfPeriodMs == 0) return true;
    return ((nowMs - onSinceMs) / halfPeriodMs) % 2 == 0;
  }
};

inline uint16_t clampU16(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 65535.0f) return 65535;
  return (uint16_t)(v + 0.5f);
}

inline uint8_t clampU8(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 255.0f) return 255;
  return (uint8_t)(v + 0.5f);
}

// ------------------------------------------------- Fuel sender emulation
// The fuel gauge isn't on CAN: the cluster reads the tank sender's resistance
// on 24-pin positions 1/2 (low = full, high = empty). The board stands in for
// it with a fixed resistor across the pins plus a second, smaller resistor
// switched in by a MOSFET on a PWM pin, with a capacitor to smooth the
// switching. Averaged, the pair looks like 1 / (1/fixed + duty/switched).
// Tested on a real 2005 cluster.

// Fuel % to the sender resistance it should show. The 2005 cluster's needle
// moves about evenly with the log of the resistance, so this steps
// geometrically from empty to half (0-50 %) and from half to full (50-100 %).
inline float fuelSenderOhms(float pct, float fullOhms, float halfOhms, float emptyOhms) {
  if (pct <= 0.0f) return emptyOhms;
  if (pct >= 100.0f) return fullOhms;
  if (pct <= 50.0f) return emptyOhms * powf(halfOhms / emptyOhms, pct / 50.0f);
  return halfOhms * powf(fullOhms / halfOhms, (pct - 50.0f) / 50.0f);
}

// PWM duty (0-255) that makes the fixed/switched resistor pair look like
// targetOhms. Clamped to what the pair can reach.
inline uint8_t fuelSenderDuty(float targetOhms, float fixedOhms, float switchedOhms) {
  if (targetOhms <= 0.0f) return 255;
  float duty = (1.0f / targetOhms - 1.0f / fixedOhms) * switchedOhms;
  return clampU8(duty * 255.0f);
}

inline uint16_t encodeSpeed(float kmh) {
  return clampU16(kmh * SPEED_CAN_FACTOR + SPEED_CAN_OFFSET);
}

inline uint16_t encodeRpm(float rpm) { return clampU16(rpm * RPM_CAN_FACTOR); }

// ABS wheel speeds use km/h * 100 with a +10000 offset (RX-8 layout).
inline uint16_t encodeWheelSpeed(float kmh) { return clampU16(kmh * 100.0f + 10000.0f); }

// Coolant is sent as deg C + 40 (same offset OBD-II uses).
inline uint8_t encodeCoolant(float c) { return clampU8(c + 40.0f); }

inline void putU16(uint8_t *buf, uint16_t v) {
  buf[0] = (uint8_t)(v >> 8);
  buf[1] = (uint8_t)(v & 0xFF);
}

// 0x201: [rpm hi][rpm lo][FF][FF][speed hi][speed lo][throttle*2][FF]
inline void buildRpmSpeed(const ClusterState &s, uint8_t out[8]) {
  putU16(&out[0], encodeRpm(s.rpm));
  out[2] = 0xFF;
  out[3] = 0xFF;
  putU16(&out[4], encodeSpeed(s.speedKmh));
  out[6] = clampU8(s.throttlePct * 2.0f);
  out[7] = 0xFF;
}

// 0x4B0: four wheel speeds (FL, FR, RL, RR).
// Sending plausible values here keeps the cluster from flagging an ABS fault.
inline void buildWheelSpeed(const ClusterState &s, uint8_t out[8]) {
  uint16_t v = encodeWheelSpeed(s.speedKmh);
  for (int i = 0; i < 8; i += 2) putU16(&out[i], v);
}

// 0x420: [coolant][00][00][00][lamp bits][00][00][00]
inline void buildEngineInfo(const ClusterState &s, uint8_t out[8]) {
  out[0] = encodeCoolant(s.coolantC);
  out[1] = 0;
  out[2] = 0;
  out[3] = 0;
  out[4] = ENGINE_B4_BASE;
  if (s.chargeWarning) out[4] |= ENGINE_B4_CHARGE;
  if (s.checkEngineFlash) out[4] |= ENGINE_B4_CHECK_FLASH;
  else if (s.checkEngine) out[4] |= ENGINE_B4_CHECK_ENGINE;
  out[5] = 0;
  out[6] = 0;
  out[7] = 0;
}

// 0x212: [00][00][00][00][brake lamp bits][00][00][00]
inline void buildBrakeLamps(const ClusterState &s, uint8_t out[8]) {
  for (int i = 0; i < 8; i++) out[i] = 0;
  out[4] = s.parkingBrake ? BRAKE_B4_PARKING_BRAKE : 0;
}

// 0x228: [gear display][manual lamp][00][00][00][00][00][00]. Hearing it at
// all turns the AT lamp off. The manual lamp only shows with a numbered gear.
inline void buildTransmission(const ClusterState &s, uint8_t out[8]) {
  for (int i = 0; i < 8; i++) out[i] = 0;
  out[0] = s.gear;
  if (s.manualLamp && gearIsNumbered(s.gear)) out[1] = TRANS_B1_MANUAL;
}

// MS 0x460: airbag module present, no faults, seat belt chime silenced.
inline void buildAirbag(uint8_t out[8]) {
  for (int i = 0; i < 8; i++) out[i] = 0;
  out[1] = AIRBAG_B1_NO_BELT_CHIME;
}

// MS 0x433: [00][00][00][high beam bits][00][00][00][00]
inline void buildBodyLamps(const ClusterState &s, uint8_t out[8]) {
  for (int i = 0; i < 8; i++) out[i] = 0;
  out[3] = s.highBeam ? BODY_B3_HIGH_BEAM : 0;
}

// MS 0x265: [turn arrow bits][00][00][00][00][00][00][00]
inline void buildTurnLamps(const ClusterState &s, uint8_t out[8]) {
  for (int i = 0; i < 8; i++) out[i] = 0;
  if (s.leftTurn) out[0] |= TURN_B0_LEFT;
  if (s.rightTurn) out[0] |= TURN_B0_RIGHT;
}

}  // namespace mazda3
