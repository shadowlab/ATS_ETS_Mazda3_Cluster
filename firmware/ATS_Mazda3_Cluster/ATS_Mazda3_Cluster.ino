// =============================================================================
//  ATS_Mazda3_Cluster
//
//  Drives a real 2004-2009 (BK) Mazda 3 instrument cluster from American Truck
//  Simulator telemetry. SimTools streams game data to this board over USB
//  serial; the board turns it into the CAN frames the cluster expects.
//
//  Hardware: Arduino Mega 2560 + Seeed Studio CAN-BUS Shield (MCP2515).
//  Library:  "CAN-BUS Shield" by Seeed Studio (Seeed_Arduino_CAN) v2.x.
//
//  Serial fields (see README for the full table):
//    R rpm   S speed   T coolant   A throttle   F fuel %
//    E check-engine   B charge   O oil pressure     (0/1 warning lamps)
//    P parking brake                                (0/1, CAN)
//    L left turn   Y right turn   H high beam      (0/1, optional GPIO, off by default)
// =============================================================================

#include <SPI.h>
#include <mcp2515_can.h>

#include "config.h"
#include "mazda3_can.h"
#include "telemetry_parser.h"

mcp2515_can CAN(CAN_CS_PIN);
#if MS_CAN_ENABLED
mcp2515_can MSCAN(MS_CAN_CS_PIN);
#endif
TelemetryParser parser;
mazda3::ClusterState cluster;

unsigned long lastTelemetryMs = 0;
bool telemetryLive = false;

// Fast frames carry the needles; slow frames carry lamps/temperature.
const unsigned long FAST_PERIOD_MS = 20;
const unsigned long SLOW_PERIOD_MS = 100;
unsigned long lastFastMs = 0;
unsigned long lastSlowMs = 0;

// -------------------------------------------------------- Custom frames
// Extra frames added at runtime with the serial `#` command, sent every
// SLOW_PERIOD_MS. Useful for finding the IDs/bits that control lamps on your
// particular cluster without reflashing.
struct CustomFrame {
  bool ms;  // true: body bus (MS-CAN), false: main bus (HS-CAN)
  uint16_t id;
  uint8_t len;
  uint8_t data[8];
};
const uint8_t MAX_CUSTOM_FRAMES = 8;
CustomFrame customFrames[MAX_CUSTOM_FRAMES];
uint8_t customCount = 0;

// ---------------------------------------------------------------- Helpers
void sendFrame(uint16_t id, const uint8_t *data, uint8_t len = 8) {
  CAN.sendMsgBuf(id, 0, len, data);
}

void sendMsFrame(uint16_t id, const uint8_t *data, uint8_t len = 8) {
#if MS_CAN_ENABLED
  MSCAN.sendMsgBuf(id, 0, len, data);
#else
  (void)id;
  (void)data;
  (void)len;
#endif
}

// ---------------------------------------------------------------- Scan
// `#scan m` / `#scan h`: sends all-0xFF frames on every ID, SCAN_BLOCK IDs at
// a time, SCAN_STEP_MS per block, printing each block as it starts. Watch the
// cluster and note the block that lights a lamp, then narrow it down with
// custom frames.
const uint16_t SCAN_BLOCK = 16;
const unsigned long SCAN_STEP_MS = 2000;
bool scanning = false;
bool scanMs = false;
uint16_t scanBase = 0;
unsigned long scanStepStartMs = 0;

void printScanBlock() {
  DEBUG_PORT.print(F("scan "));
  DEBUG_PORT.print(scanMs ? F("MS 0x") : F("HS 0x"));
  DEBUG_PORT.print(scanBase, HEX);
  DEBUG_PORT.print(F("-0x"));
  DEBUG_PORT.println(scanBase + SCAN_BLOCK - 1, HEX);
}

void sendScanFrames(unsigned long now) {
  if (!scanning) return;
  if (now - scanStepStartMs >= SCAN_STEP_MS) {
    scanStepStartMs = now;
    scanBase += SCAN_BLOCK;
    if (scanBase > 0x7FF) {
      scanning = false;
      DEBUG_PORT.println(F("scan done"));
      return;
    }
    printScanBlock();
  }
  static const uint8_t ff[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  for (uint16_t id = scanBase; id < scanBase + SCAN_BLOCK; id++) {
    if (scanMs) {
      sendMsFrame(id, ff);
    } else if (id != mazda3::ID_RPM_SPEED && id != mazda3::ID_ENGINE_INFO) {
      // Leave the needle/temperature frames alone so the gauges stay sane.
      sendFrame(id, ff);
    }
  }
}

void setIndicator(int pin, bool on) {
  if (pin < 0) return;
  digitalWrite(pin, (on != (bool)INDICATOR_ACTIVE_LOW) ? HIGH : LOW);
}

void initIndicator(int pin) {
  if (pin < 0) return;
  pinMode(pin, OUTPUT);
  setIndicator(pin, false);
}

float scaled(char key, float scale, float offset) {
  return parser.value(key) * scale + offset;
}

// Copy the latest parsed telemetry into the cluster state.
void applyTelemetry() {
  cluster.rpm = scaled('R', RPM_SCALE, RPM_OFFSET) * RPM_DISPLAY_MULTIPLIER;
  cluster.speedKmh = scaled('S', SPEED_SCALE, SPEED_OFFSET);
  cluster.coolantC = scaled('T', COOLANT_SCALE, COOLANT_OFFSET);
  cluster.throttlePct = scaled('A', THROTTLE_SCALE, THROTTLE_OFFSET);
  // Only once SimTools has sent fuel; otherwise keep the boot default rather
  // than dropping the gauge to empty.
  if (parser.seen('F')) cluster.fuelPct = scaled('F', FUEL_SCALE, FUEL_OFFSET);
  cluster.checkEngine = parser.value('E') >= 0.5f;
  cluster.chargeWarning = parser.value('B') >= 0.5f;
  cluster.oilWarning = parser.value('O') >= 0.5f;
  cluster.parkingBrake = parser.value('P') >= 0.5f;

  setIndicator(PIN_LEFT_TURN, parser.value('L') >= 0.5f);
  setIndicator(PIN_RIGHT_TURN, parser.value('Y') >= 0.5f);
  setIndicator(PIN_HIGH_BEAM, parser.value('H') >= 0.5f);
  setIndicator(PIN_PARK_BRAKE, cluster.parkingBrake);
}

// Game paused or closed: park the needles, lamps off.
void applyIdle() {
  cluster.rpm = 0;
  cluster.speedKmh = 0;
  cluster.throttlePct = 0;
  cluster.checkEngine = false;
  cluster.chargeWarning = false;
  cluster.oilWarning = false;
  cluster.parkingBrake = false;
  // Leave coolant and fuel where they were so those gauges don't plunge when
  // you pause.

  setIndicator(PIN_LEFT_TURN, false);
  setIndicator(PIN_RIGHT_TURN, false);
  setIndicator(PIN_HIGH_BEAM, false);
  setIndicator(PIN_PARK_BRAKE, false);
}

bool hasCustomFrame(uint16_t id) {
  for (uint8_t i = 0; i < customCount; i++) {
    if (!customFrames[i].ms && customFrames[i].id == id) return true;
  }
  return false;
}

// Built-in frames step aside when a custom frame with the same ID is active,
// so the cluster only ever sees one version of that ID.
void sendBuiltIn(uint16_t id, const uint8_t *data) {
  if (!hasCustomFrame(id)) sendFrame(id, data);
}

void sendClusterFrames(unsigned long now) {
  uint8_t buf[8];

  if (now - lastFastMs >= FAST_PERIOD_MS) {
    lastFastMs = now;
    mazda3::buildRpmSpeed(cluster, buf);
    sendBuiltIn(mazda3::ID_RPM_SPEED, buf);
    mazda3::buildWheelSpeed(cluster, buf);
    sendBuiltIn(mazda3::ID_WHEEL_SPEED, buf);
  }

  if (now - lastSlowMs >= SLOW_PERIOD_MS) {
    lastSlowMs = now;
    mazda3::buildEngineInfo(cluster, buf);
    sendBuiltIn(mazda3::ID_ENGINE_INFO, buf);
    mazda3::buildFuelLevel(cluster, buf);
    sendBuiltIn(mazda3::ID_FUEL_LEVEL, buf);
    mazda3::buildBrakeLamps(cluster, buf);
    sendBuiltIn(mazda3::ID_BRAKE_LAMPS, buf);
    for (uint8_t i = 0; i < customCount; i++) {
      const CustomFrame &f = customFrames[i];
      if (f.ms) {
        sendMsFrame(f.id, f.data, f.len);
      } else {
        sendFrame(f.id, f.data, f.len);
      }
    }
    sendScanFrames(now);
  }
}

// ------------------------------------------------------- Serial commands
//   #420 5A 00 00 00 01 00 00 00   add/replace a custom frame (hex)
//   #420                           remove custom frame 0x420
//   #m433 00 00 00 40 00 00 00 00  same, on the body bus (MS-CAN)
//   #-                             remove all custom frames, stop a scan
//   #?                             list custom frames
//   #scan m / #scan h              scan every ID on MS-CAN / HS-CAN
//   #scan                          stop a scan
// A custom frame with a built-in ID (0x201/0x212/0x420/0x433/0x4B0) replaces
// that built-in frame until it is removed.

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Parses the next hex number from *p, advancing it. Returns -1 if none.
long nextHex(const char *&p) {
  while (*p == ' ' || *p == ',') p++;
  if (hexDigit(*p) < 0) return -1;
  long v = 0;
  while (hexDigit(*p) >= 0) v = v * 16 + hexDigit(*p++);
  return v;
}

void printFrame(const CustomFrame &f) {
  DEBUG_PORT.print(f.ms ? F("  MS 0x") : F("  0x"));
  DEBUG_PORT.print(f.id, HEX);
  for (uint8_t i = 0; i < f.len; i++) {
    DEBUG_PORT.print(' ');
    if (f.data[i] < 0x10) DEBUG_PORT.print('0');
    DEBUG_PORT.print(f.data[i], HEX);
  }
  DEBUG_PORT.println();
}

void handleCommand(const char *cmd) {
  if (cmd[0] == '-') {
    customCount = 0;
    scanning = false;
    DEBUG_PORT.println(F("custom frames cleared"));
    return;
  }
  if (strncmp(cmd, "scan", 4) == 0) {
    const char *arg = cmd + 4;
    while (*arg == ' ') arg++;
    if (*arg == 'm' || *arg == 'M' || *arg == 'h' || *arg == 'H') {
      scanMs = (*arg == 'm' || *arg == 'M');
#if !MS_CAN_ENABLED
      if (scanMs) {
        DEBUG_PORT.println(F("MS-CAN not enabled - set MS_CAN_ENABLED 1 in config.h"));
        return;
      }
#endif
      scanning = true;
      scanBase = 0;
      scanStepStartMs = millis();
      printScanBlock();
    } else {
      scanning = false;
      DEBUG_PORT.println(F("scan stopped"));
    }
    return;
  }
  if (cmd[0] == '?') {
    DEBUG_PORT.print(customCount);
    DEBUG_PORT.println(F(" custom frame(s):"));
    for (uint8_t i = 0; i < customCount; i++) printFrame(customFrames[i]);
    return;
  }

  const char *p = cmd;
  bool ms = false;
  if (*p == 'm' || *p == 'M') {
#if !MS_CAN_ENABLED
    DEBUG_PORT.println(F("MS-CAN not enabled - set MS_CAN_ENABLED 1 in config.h"));
    return;
#endif
    ms = true;
    p++;
  }
  long id = nextHex(p);
  if (id < 0 || id > 0x7FF) {
    DEBUG_PORT.println(F("bad command - expected #<id> [bytes...]"));
    return;
  }

  CustomFrame f;
  f.ms = ms;
  f.id = (uint16_t)id;
  f.len = 0;
  long b;
  while (f.len < 8 && (b = nextHex(p)) >= 0) f.data[f.len++] = (uint8_t)b;

  int slot = -1;
  for (uint8_t i = 0; i < customCount; i++) {
    if (customFrames[i].id == f.id && customFrames[i].ms == f.ms) slot = i;
  }

  if (f.len == 0) {
    if (slot >= 0) {
      customFrames[slot] = customFrames[--customCount];
      DEBUG_PORT.println(F("removed"));
    }
    return;
  }

  if (slot < 0) {
    if (customCount >= MAX_CUSTOM_FRAMES) {
      DEBUG_PORT.println(F("custom frame table full"));
      return;
    }
    slot = customCount++;
  }
  customFrames[slot] = f;
  printFrame(f);
}

// --------------------------------------------------------------- Sweep
void bootSweep() {
  unsigned long start = millis();
  unsigned long total = 2UL * SWEEP_DURATION_MS;
  unsigned long now;
  while ((now = millis()) - start < total) {
    unsigned long t = now - start;
    float k = (t < SWEEP_DURATION_MS) ? (float)t / SWEEP_DURATION_MS
                                      : (float)(total - t) / SWEEP_DURATION_MS;
    cluster.rpm = k * SWEEP_MAX_RPM;
    cluster.speedKmh = k * SWEEP_MAX_KMH;
    sendClusterFrames(now);
  }
  cluster.rpm = 0;
  cluster.speedKmh = 0;
}

// -------------------------------------------------------------- Status
void printStatus() {
  DEBUG_PORT.print(telemetryLive ? F("LIVE ") : F("idle "));
  DEBUG_PORT.print(F("rpm="));
  DEBUG_PORT.print(cluster.rpm, 0);
  DEBUG_PORT.print(F(" kmh="));
  DEBUG_PORT.print(cluster.speedKmh, 1);
  DEBUG_PORT.print(F(" coolant="));
  DEBUG_PORT.print(cluster.coolantC, 0);
  DEBUG_PORT.print(F(" thr="));
  DEBUG_PORT.print(cluster.throttlePct, 0);
  DEBUG_PORT.print(F(" fuel="));
  DEBUG_PORT.print(cluster.fuelPct, 0);
  DEBUG_PORT.print(F(" lamps="));
  DEBUG_PORT.print(cluster.checkEngine);
  DEBUG_PORT.print(cluster.chargeWarning);
  DEBUG_PORT.print(cluster.oilWarning);
  DEBUG_PORT.println(cluster.parkingBrake);
}

// --------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(SERIAL_BAUD);
  if ((void *)&DEBUG_PORT != (void *)&Serial) DEBUG_PORT.begin(SERIAL_BAUD);

  initIndicator(PIN_LEFT_TURN);
  initIndicator(PIN_RIGHT_TURN);
  initIndicator(PIN_HIGH_BEAM);
  initIndicator(PIN_PARK_BRAKE);

  // Sensible "engine warm" coolant reading until the game says otherwise.
  cluster.coolantC = 90;
  cluster.fuelPct = 50;

  while (CAN.begin(CAN_SPEED, CAN_CLOCK) != CAN_OK) {
    DEBUG_PORT.println(F("CAN init failed - check shield, CS pin and crystal setting"));
    delay(500);
  }
  DEBUG_PORT.println(F("CAN ready - ATS Mazda 3 cluster bridge"));

#if MS_CAN_ENABLED
  // Don't hang here if the module is missing: the gauges still work without it.
  if (MSCAN.begin(MS_CAN_SPEED, MS_CAN_CLOCK) == CAN_OK) {
    DEBUG_PORT.println(F("MS-CAN ready"));
  } else {
    DEBUG_PORT.println(F("MS-CAN init failed - check module wiring, CS pin and crystal"));
  }
#endif

#if SWEEP_ON_BOOT
  bootSweep();
#endif
}

void loop() {
  unsigned long now = millis();

  while (Serial.available()) {
    if (parser.feed((char)Serial.read())) {
      lastTelemetryMs = now;
      telemetryLive = true;
    }
    if (parser.commandReady()) handleCommand(parser.takeCommand());
  }

  if (telemetryLive && now - lastTelemetryMs > TELEMETRY_TIMEOUT_MS) {
    telemetryLive = false;
  }

  if (telemetryLive) {
    applyTelemetry();
  } else {
    applyIdle();
  }

  sendClusterFrames(now);

#if DEBUG_STATUS
  static unsigned long lastStatusMs = 0;
  if (now - lastStatusMs >= 1000) {
    lastStatusMs = now;
    printStatus();
  }
#endif
}
