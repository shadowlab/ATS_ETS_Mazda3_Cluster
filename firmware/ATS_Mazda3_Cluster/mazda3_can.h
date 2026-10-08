// =============================================================================
//  mazda3_can.h - CAN frame layouts for the 2004-2009 (BK) Mazda 3 cluster.
//
//  Kept free of Arduino dependencies so it can be unit-tested on a PC
//  (see test/test_logic.cpp).
//
//  RPM, speed, fuel, coolant, check-engine, oil pressure and parking brake
//  follow the project's integration matrix for the 2005 cluster (see README).
//  The charge lamp bit and the 0x4B0 wheel-speed encoding come from RX-8
//  research (the two cars share much of their CAN matrix) and are worth
//  confirming - use the serial `#` command to experiment without reflashing.
// =============================================================================
#pragma once

#include <stdint.h>

namespace mazda3 {

// ------------------------------------------------------------------ IDs
const uint16_t ID_RPM_SPEED   = 0x201;  // PCM: engine RPM, vehicle speed, throttle
const uint16_t ID_ENGINE_INFO = 0x420;  // PCM: coolant temp, odometer, warning lamps
const uint16_t ID_WHEEL_SPEED = 0x4B0;  // ABS: individual wheel speeds
const uint16_t ID_FUEL_LEVEL  = 0x433;  // fuel level for the fuel gauge
const uint16_t ID_BRAKE_LAMPS = 0x212;  // brake warning lamp (parking brake)

// ------------------------------------------------------- Calibration
// 0x201 bytes 0-1: raw = rpm * RPM_CAN_FACTOR (1:1, big-endian).
const float RPM_CAN_FACTOR = 1.0f;
// 0x201 bytes 4-5: raw = km/h * SPEED_CAN_FACTOR + SPEED_CAN_OFFSET.
// Bench-tested on a 2005 cluster: km/h * 177.6 read 1.78x high (75 km/h showed
// 83 mph), so the cluster takes km/h * 100.
const float SPEED_CAN_FACTOR = 100.0f;
const float SPEED_CAN_OFFSET = 0.0f;

// ------------------------------------------------------------ Lamp bits
const uint8_t ENGINE_B1_CHECK_ENGINE = 0x40;  // 0x420 byte 1
const uint8_t ENGINE_B6_CHARGE       = 0x40;  // 0x420 byte 6 (RX-8 layout)
const uint8_t BRAKE_B4_PARKING_BRAKE = 0x40;  // 0x212 byte 4

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
  bool parkingBrake;
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

// 0x420: [coolant][MIL bits][00][00][oil pressure ok][00][warning bits][00]
inline void buildEngineInfo(const ClusterState &s, uint8_t out[8]) {
  out[0] = encodeCoolant(s.coolantC);
  out[1] = s.checkEngine ? ENGINE_B1_CHECK_ENGINE : 0;
  out[2] = 0;
  out[3] = 0;
  out[4] = s.oilWarning ? 0 : 1;  // 1 = pressure OK, 0 = red oil lamp on
  out[5] = 0;
  out[6] = s.chargeWarning ? ENGINE_B6_CHARGE : 0;
  out[7] = 0;
}

// 0x212: [00][00][00][00][brake lamp bits][00][00][00]
inline void buildBrakeLamps(const ClusterState &s, uint8_t out[8]) {
  for (int i = 0; i < 8; i++) out[i] = 0;
  out[4] = s.parkingBrake ? BRAKE_B4_PARKING_BRAKE : 0;
}

// 0x433: [fuel % 0x00..0x64][00][00][00][00][00][00][00]
inline void buildFuelLevel(const ClusterState &s, uint8_t out[8]) {
  float pct = s.fuelPct > 100.0f ? 100.0f : s.fuelPct;
  out[0] = clampU8(pct);
  for (int i = 1; i < 8; i++) out[i] = 0;
}

}  // namespace mazda3
