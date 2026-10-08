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
  // 3000 * 4 = 12000 = 0x2EE0
  CHECK(b[0] == 0x2E && b[1] == 0xE0);
  CHECK(b[2] == 0xFF && b[3] == 0xFF);
  // 100 * 100 + 10000 = 20000 = 0x4E20
  CHECK(b[4] == 0x4E && b[5] == 0x20);
  CHECK(b[6] == 100);
  CHECK(b[7] == 0xFF);
}

static void testStoppedAndClamping() {
  mazda3::ClusterState s = {};
  s.rpm = -50;           // negative clamps to 0
  s.speedKmh = 0;        // 0 km/h -> 10000 = 0x2710
  s.throttlePct = 500;   // clamps to 255
  uint8_t b[8];
  mazda3::buildRpmSpeed(s, b);
  CHECK(b[0] == 0 && b[1] == 0);
  CHECK(b[4] == 0x27 && b[5] == 0x10);
  CHECK(b[6] == 255);

  s.rpm = 100000;  // overflows 16 bits -> clamps
  mazda3::buildRpmSpeed(s, b);
  CHECK(b[0] == 0xFF && b[1] == 0xFF);
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
  CHECK(b[4] == 1);  // oil pressure OK
  CHECK(b[5] == 0 && b[6] == 0);

  s.checkEngine = true;
  s.chargeWarning = true;
  s.oilWarning = true;
  mazda3::buildEngineInfo(s, b);
  CHECK(b[4] == 0);
  CHECK(b[5] == mazda3::B5_CHECK_ENGINE);
  CHECK(b[6] == (mazda3::B6_CHARGE | mazda3::B6_OIL_PRESSURE));
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
