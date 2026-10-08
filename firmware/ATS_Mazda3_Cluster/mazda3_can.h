// =============================================================================
//  mazda3_can.h - CAN frame layouts for the 2004-2009 (BK) Mazda 3 cluster.
//
//  Kept free of Arduino dependencies so it can be unit-tested on a PC
//  (see test/test_logic.cpp).
//
//  The IDs and layouts below come from community reverse-engineering of the
//  Mazda 3 / RX-8 high-speed bus (the two share a lot of their CAN matrix).
//  0x201 (RPM/speed) is well established for the Mazda 3. The warning-lamp
//  bits in 0x420 are documented for the RX-8 and believed to match on the
//  Mazda 3, but are worth confirming on your cluster - use the serial `#`
//  command (see README) to experiment without reflashing.
// =============================================================================
#pragma once

#include <stdint.h>

namespace mazda3 {

// ------------------------------------------------------------------ IDs
const uint16_t ID_RPM_SPEED   = 0x201;  // PCM: engine RPM, vehicle speed, throttle
const uint16_t ID_ENGINE_INFO = 0x420;  // PCM: coolant temp, odometer, warning lamps
const uint16_t ID_WHEEL_SPEED = 0x4B0;  // ABS: individual wheel speeds
const uint16_t ID_FUEL_LEVEL  = 0x433;  // fuel level for the fuel gauge

// ------------------------------------------------------- 0x420 lamp bits
const uint8_t B5_CHECK_ENGINE = 0x40;  // byte 5
const uint8_t B6_CHARGE       = 0x40;  // byte 6
const uint8_t B6_OIL_PRESSURE = 0x80;  // byte 6

// ------------------------------------------------------- Cluster state
struct ClusterState {
  float rpm;          // engine rpm as it should appear on the tach
  float speedKmh;     // vehicle speed, km/h (the cluster converts for mph dials)
  float coolantC;     // coolant temperature, deg C
  float throttlePct;  // 0..100
  float fuelPct;      // 0 (empty) .. 100 (full)
  bool checkEngine;
  bool chargeWarning;
  bool oilWarning;
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

// Speed is sent as km/h * 100 with a +10000 offset.
inline uint16_t encodeSpeed(float kmh) { return clampU16(kmh * 100.0f + 10000.0f); }

// RPM is sent as rpm * 4.
inline uint16_t encodeRpm(float rpm) { return clampU16(rpm * 4.0f); }

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

// 0x4B0: four wheel speeds (FL, FR, RL, RR), same encoding as 0x201 speed.
// Sending plausible values here keeps the cluster from flagging an ABS fault.
inline void buildWheelSpeed(const ClusterState &s, uint8_t out[8]) {
  uint16_t v = encodeSpeed(s.speedKmh);
  for (int i = 0; i < 8; i += 2) putU16(&out[i], v);
}

// 0x420: [coolant][odo tick][00][00][oil pressure ok][MIL bits][warning bits][00]
inline void buildEngineInfo(const ClusterState &s, uint8_t out[8]) {
  out[0] = encodeCoolant(s.coolantC);
  out[1] = 0;  // odometer increment counter - left static so the odo doesn't run
  out[2] = 0;
  out[3] = 0;
  out[4] = s.oilWarning ? 0 : 1;
  out[5] = s.checkEngine ? B5_CHECK_ENGINE : 0;
  out[6] = (s.chargeWarning ? B6_CHARGE : 0) | (s.oilWarning ? B6_OIL_PRESSURE : 0);
  out[7] = 0;
}

// 0x433: [fuel % 0x00..0x64][00][00][00][00][00][00][00]
inline void buildFuelLevel(const ClusterState &s, uint8_t out[8]) {
  float pct = s.fuelPct > 100.0f ? 100.0f : s.fuelPct;
  out[0] = clampU8(pct);
  for (int i = 1; i < 8; i++) out[i] = 0;
}

}  // namespace mazda3
