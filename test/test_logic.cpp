// Host-side tests for the Arduino-independent parts of the firmware.
//
//   g++ -std=c++11 -Wall -Wextra -I firmware/ATS_Mazda3_Cluster test/test_logic.cpp -o test_logic && ./test_logic

#include <cmath>
#include <cstdio>
#include <cstring>

#include "mazda3_can.h"
#include "telemetry_parser.h"

static int failures = 0;

#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
      failures++;                                                  \
    }                                                              \
  } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static void feedAll(TelemetryParser &p, const char *s) {
  while (*s) p.feed(*s++);
}

static void testRpmSpeedFrame() {
  mazda3::ClusterState s = {};
  s.rpm = 3000;
  s.speedKmh = 100;
  s.throttlePct = 50;
  uint8_t b[8];
  mazda3::buildRpmSpeed(s, b);
  // 3000 rpm * 0.96 = 2880 = 0x0B40
  CHECK(b[0] == 0x0B && b[1] == 0x40);
  CHECK(b[2] == 0xFF && b[3] == 0xFF);
  // 100 km/h * 100 = 10000 = 0x2710
  CHECK(b[4] == 0x27 && b[5] == 0x10);
  CHECK(b[6] == 100);
  CHECK(b[7] == 0xFF);
}

static void testStoppedAndClamping() {
  mazda3::ClusterState s = {};
  s.rpm = -50;           // negative clamps to 0
  s.speedKmh = 0;        // 0 km/h -> 0
  s.throttlePct = 500;   // clamps to 255
  uint8_t b[8];
  mazda3::buildRpmSpeed(s, b);
  CHECK(b[0] == 0 && b[1] == 0);
  CHECK(b[4] == 0 && b[5] == 0);
  CHECK(b[6] == 255);

  s.rpm = 100000;     // overflows 16 bits -> clamps
  s.speedKmh = 1000;  // 100000 overflows -> clamps
  mazda3::buildRpmSpeed(s, b);
  CHECK(b[0] == 0xFF && b[1] == 0xFF);
  CHECK(b[4] == 0xFF && b[5] == 0xFF);
}

static void testWheelSpeedFrame() {
  mazda3::ClusterState s = {};
  s.speedKmh = 50;  // 15000 = 0x3A98
  uint8_t b[8];
  mazda3::buildWheelSpeed(s, b);
  for (int i = 0; i < 8; i += 2) CHECK(b[i] == 0x3A && b[i + 1] == 0x98);
}

static void testEngineInfoFrame() {
  mazda3::ClusterState s = {};
  s.coolantC = 90;
  uint8_t b[8];
  mazda3::buildEngineInfo(s, b);
  CHECK(b[0] == 130);
  CHECK(b[4] == 0x01);  // no lamps
  for (int i = 1; i < 8; i++) {
    if (i != 4) CHECK(b[i] == 0);
  }

  s.chargeWarning = true;
  mazda3::buildEngineInfo(s, b);
  CHECK(b[4] == 0x03);

  s.chargeWarning = false;
  s.checkEngine = true;
  mazda3::buildEngineInfo(s, b);
  CHECK(b[4] == 0x41);

  s.checkEngineFlash = true;  // flashing wins over steady
  mazda3::buildEngineInfo(s, b);
  CHECK(b[4] == 0x81);

  s.oilWarning = true;  // the oil lamp is wired, not on CAN
  mazda3::buildEngineInfo(s, b);
  CHECK(b[4] == 0x81);
}

static void testBrakeLampsFrame() {
  mazda3::ClusterState s = {};
  uint8_t b[8];
  mazda3::buildBrakeLamps(s, b);
  for (int i = 0; i < 8; i++) CHECK(b[i] == 0);
  s.parkingBrake = true;
  mazda3::buildBrakeLamps(s, b);
  CHECK(b[4] == 0x40);
  for (int i = 0; i < 8; i++) {
    if (i != 4) CHECK(b[i] == 0);
  }
}

static void testBodyLampsFrame() {
  mazda3::ClusterState s = {};
  uint8_t b[8];
  mazda3::buildBodyLamps(s, b);
  for (int i = 0; i < 8; i++) CHECK(b[i] == 0);
  s.highBeam = true;
  mazda3::buildBodyLamps(s, b);
  CHECK(b[3] == 0x40);
  for (int i = 0; i < 8; i++) {
    if (i != 3) CHECK(b[i] == 0);
  }
}

static void testTurnLampsFrame() {
  mazda3::ClusterState s = {};
  uint8_t b[8];
  mazda3::buildTurnLamps(s, b);
  for (int i = 0; i < 8; i++) CHECK(b[i] == 0);
  s.leftTurn = true;
  mazda3::buildTurnLamps(s, b);
  CHECK(b[0] == 0x20);
  s.rightTurn = true;
  mazda3::buildTurnLamps(s, b);
  CHECK(b[0] == 0x60);
  s.leftTurn = false;
  mazda3::buildTurnLamps(s, b);
  CHECK(b[0] == 0x40);
  for (int i = 1; i < 8; i++) CHECK(b[i] == 0);
}

static void testFuelSender() {
  using namespace mazda3;
  CHECK(near(fuelSenderOhms(100, 20, 54, 160), 20));
  CHECK(near(fuelSenderOhms(50, 20, 54, 160), 54));
  CHECK(near(fuelSenderOhms(0, 20, 54, 160), 160));
  CHECK(near(fuelSenderOhms(150, 20, 54, 160), 20));   // clamped
  CHECK(near(fuelSenderOhms(-5, 20, 54, 160), 160));
  // Quarter marks sit halfway between the ends in log terms.
  CHECK(near(fuelSenderOhms(25, 20, 54, 160), 92.95f));
  CHECK(near(fuelSenderOhms(75, 20, 54, 160), 32.86f));
  // Falls steadily as the tank fills.
  CHECK(fuelSenderOhms(30, 20, 54, 160) > fuelSenderOhms(40, 20, 54, 160));
  CHECK(fuelSenderOhms(60, 20, 54, 160) > fuelSenderOhms(70, 20, 54, 160));
  // 390 ohm fixed + 22 ohm switched: empty = MOSFET off, full = fully on.
  CHECK(fuelSenderDuty(400, 390, 22) == 0);
  CHECK(fuelSenderDuty(20, 390, 22) == 255);
  // 210 ohms: (1/210 - 1/390) * 22 = 0.048 -> 12/255.
  CHECK(fuelSenderDuty(210, 390, 22) == 12);
  // Duty rises as the target resistance falls.
  CHECK(fuelSenderDuty(100, 390, 22) > fuelSenderDuty(200, 390, 22));
  // Default 470 ohm fixed resistor: a little duty still reaches empty.
  CHECK(fuelSenderDuty(470, 470, 22) == 0);
  CHECK(fuelSenderDuty(400, 470, 22) == 2);
  CHECK(fuelSenderDuty(20, 470, 22) == 255);
  // Measured empty (160 ohms): (1/160 - 1/470) * 22 = 0.0907 -> 23/255.
  CHECK(fuelSenderDuty(160, 470, 22) == 23);
  // Reworked board: 10 ohm switched resistor. Empty (175) is 9/255 and
  // F (16.8 ohms) is 146/255.
  CHECK(fuelSenderDuty(470, 470, 10) == 0);
  CHECK(fuelSenderDuty(175, 470, 10) == 9);
  CHECK(fuelSenderDuty(16.8f, 470, 10) == 146);
  CHECK(fuelSenderDuty(9, 470, 10) == 255);  // below its ~9.8 ohm floor
}

static void testTurnBlinker() {
  mazda3::TurnBlinker t = {};
  CHECK(!t.update(false, 1000, 333));
  // Steady input: on for the first half period, off for the next, on again.
  CHECK(t.update(true, 1000, 333));
  CHECK(t.update(true, 1332, 333));
  CHECK(!t.update(true, 1333, 333));
  CHECK(!t.update(true, 1665, 333));
  CHECK(t.update(true, 1666, 333));
  // Input off: lamp off straight away.
  CHECK(!t.update(false, 1700, 333));
  // A new rising edge restarts the phase (follows a game that blinks itself).
  CHECK(t.update(true, 1900, 333));
  // Half period 0: lamp just follows the input.
  mazda3::TurnBlinker solid = {};
  CHECK(solid.update(true, 0, 0));
  CHECK(solid.update(true, 5000, 0));
}

static void testTransmissionFrame() {
  mazda3::ClusterState s = {};
  s.rpm = 3000;
  uint8_t b[8];
  for (int i = 0; i < 8; i++) b[i] = 0xAA;
  mazda3::buildTransmission(s, b);
  for (int i = 0; i < 8; i++) CHECK(b[i] == 0);
  s.gear = mazda3::GEAR_DRIVE;
  mazda3::buildTransmission(s, b);
  CHECK(b[0] == 4);
  for (int i = 1; i < 8; i++) CHECK(b[i] == 0);

  CHECK(mazda3::gearFromGame(-1) == mazda3::GEAR_REVERSE);
  CHECK(mazda3::gearFromGame(-2) == mazda3::GEAR_REVERSE);
  CHECK(mazda3::gearFromGame(0) == mazda3::GEAR_NEUTRAL);
  CHECK(mazda3::gearFromGame(1) == mazda3::GEAR_DRIVE);
  CHECK(mazda3::gearFromGame(12) == mazda3::GEAR_DRIVE);
  CHECK(mazda3::gearFromGame(99) == mazda3::GEAR_PARK);

  // Numbered gears: gear in the high nibble, 4 in the low one, capped at 6.
  CHECK(mazda3::gearFromGame(1, true) == 0x14);
  CHECK(mazda3::gearFromGame(3, true) == 0x34);
  CHECK(mazda3::gearFromGame(6, true) == 0x64);
  CHECK(mazda3::gearFromGame(12, true) == 0x64);
  CHECK(mazda3::gearFromGame(0, true) == mazda3::GEAR_NEUTRAL);
  CHECK(mazda3::gearFromGame(-1, true) == mazda3::GEAR_REVERSE);
  CHECK(mazda3::gearFromGame(99, true) == mazda3::GEAR_PARK);
  // The manual lamp (byte 1 = 0x80) comes on only when asked for, and only
  // with a numbered gear.
  s.gear = 0x24;
  mazda3::buildTransmission(s, b);
  CHECK(b[0] == 0x24 && b[1] == 0);
  s.manualLamp = true;
  mazda3::buildTransmission(s, b);
  CHECK(b[0] == 0x24 && b[1] == 0x80);
  s.gear = mazda3::GEAR_NEUTRAL;
  mazda3::buildTransmission(s, b);
  CHECK(b[0] == 3 && b[1] == 0);
}

static void testOdoCounter() {
  mazda3::OdoCounter o;
  const float step = 0.05f / 207.0f;
  // Stopped: no counts.
  o.advance(0, 1000, step);
  CHECK(o.value == 0);
  // 60 mph for one minute = 1 mile = 4140 counts, sent every 100 ms.
  unsigned long total = 0;
  uint8_t last = 0;
  for (int i = 0; i < 600; i++) {
    o.advance(96.56064f, 100, step);
    total += (uint8_t)(o.value - last);
    last = o.value;
  }
  CHECK(total >= 4138 && total <= 4141);
  // Slow speeds still count via the carried remainder.
  mazda3::OdoCounter slow;
  for (int i = 0; i < 100; i++) slow.advance(1.0f, 100, step);
  CHECK(slow.value == 7);  // 1 km/h for 10 s = 0.001726 mi = 7.1 counts
  // 0x420 carries the counter in byte 1.
  mazda3::ClusterState s = {};
  s.odoCounter = 0x5A;
  uint8_t b[8];
  mazda3::buildEngineInfo(s, b);
  CHECK(b[1] == 0x5A);
}

static void testCruiseLamps() {
  mazda3::ClusterState s = {};
  uint8_t b[8];
  mazda3::buildCruiseLamps(s, b);
  CHECK(b[0] == 0);
  s.cruiseMain = true;
  mazda3::buildCruiseLamps(s, b);
  CHECK(b[0] == 0x80);
  s.cruiseMain = false;
  s.cruiseSet = true;
  mazda3::buildCruiseLamps(s, b);
  CHECK(b[0] == 0xC0);
  CHECK(b[1] == 0 && b[7] == 0);
}

static void testAirbag() {
  uint8_t b[8];
  for (int i = 0; i < 8; i++) b[i] = 0x55;
  mazda3::buildAirbag(b);
  CHECK(b[1] == 0xFF);
  CHECK(b[0] == 0 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0 &&
        b[6] == 0 && b[7] == 0);
}

static void testParserBasic() {
  TelemetryParser p;
  feedAll(p, "R2350S88.5T90A42E1;");
  CHECK(near(p.value('R'), 2350));
  CHECK(near(p.value('S'), 88.5f));
  CHECK(near(p.value('T'), 90));
  CHECK(near(p.value('A'), 42));
  CHECK(near(p.value('E'), 1));
  CHECK(p.seen('R') && p.seen('E') && !p.seen('B'));
}

static void testParserSeparatorsAndNegatives() {
  TelemetryParser p;
  feedAll(p, "R 800, S -3.25, T 7\r\n");
  CHECK(near(p.value('R'), 800));
  CHECK(near(p.value('S'), -3.25f));
  CHECK(near(p.value('T'), 7));
}

static void testParserLastFieldNeedsTerminator() {
  TelemetryParser p;
  feedAll(p, "R1000S20");
  CHECK(near(p.value('R'), 1000));
  CHECK(!p.seen('S'));  // still pending
  CHECK(p.feed('\n'));  // terminator completes it
  CHECK(near(p.value('S'), 20));
}

static void testParserCommand() {
  TelemetryParser p;
  feedAll(p, "R1500;#420 5A 00 01\nS30;");
  CHECK(p.commandReady());
  CHECK(std::strcmp(p.takeCommand(), "420 5A 00 01") == 0);
  CHECK(!p.commandReady());
  CHECK(near(p.value('R'), 1500));
  CHECK(near(p.value('S'), 30));
}

static void testParserIgnoresEmptyField() {
  TelemetryParser p;
  feedAll(p, "R;S12;");
  CHECK(!p.seen('R'));
  CHECK(near(p.value('S'), 12));
}

int main() {
  testRpmSpeedFrame();
  testStoppedAndClamping();
  testWheelSpeedFrame();
  testEngineInfoFrame();
  testBrakeLampsFrame();
  testBodyLampsFrame();
  testTransmissionFrame();
  testTurnLampsFrame();
  testTurnBlinker();
  testFuelSender();
  testAirbag();
  testCruiseLamps();
  testOdoCounter();
  testParserBasic();
  testParserSeparatorsAndNegatives();
  testParserLastFieldNeedsTerminator();
  testParserCommand();
  testParserIgnoresEmptyField();

  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
