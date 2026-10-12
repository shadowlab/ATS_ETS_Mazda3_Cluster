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

// One flasher for both arrows, so hazards (left and right together) flash in
// step even if one side was already on.
mazda3::TurnBlinker turnBlinker = {};

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

// ------------------------------------------------------------ Bus health
// Counts what happens to our frames so `#stat` can tell whether the cluster is
// on the wire. CAN needs a second node to acknowledge every frame; with no
// acknowledgement the MCP2515 keeps retrying, its transmit error count climbs
// and it goes "error passive". So a clean error flag while frames are going
// out means the cluster is answering at this bitrate, whatever the IDs mean.
const uint8_t MAX_RX_IDS = 16;
struct BusStats {
  unsigned long sent;    // handed to the controller
  unsigned long slow;    // still retrying when the library stopped waiting
  unsigned long stuck;   // all three transmit buffers full (nothing getting out)
  unsigned long rxFrames;
  uint16_t rxIds[MAX_RX_IDS];  // first IDs heard from the cluster
  uint8_t rxData[MAX_RX_IDS][8];  // and the last data seen on each
  uint8_t rxIdCount;
};
// `#watch`: also print a cluster frame whenever its data changes. The cluster
// reports some of its own state (e.g. HS 0x190), so a change can show a lamp
// flipping even when it's hard to see.
bool watchChanges = false;
BusStats mainStats = {};
#if MS_CAN_ENABLED
BusStats msStats = {};
#endif

void recordSend(BusStats &s, byte rc) {
  if (rc == CAN_OK) s.sent++;
  else if (rc == CAN_SENDMSGTIMEOUT) s.slow++;
  else s.stuck++;
}

// ---------------------------------------------------------------- Helpers
void sendFrame(uint16_t id, const uint8_t *data, uint8_t len = 8) {
  recordSend(mainStats, CAN.sendMsgBuf(id, 0, len, data));
}

void sendMsFrame(uint16_t id, const uint8_t *data, uint8_t len = 8) {
#if MS_CAN_ENABLED
  recordSend(msStats, MSCAN.sendMsgBuf(id, 0, len, data));
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
// custom frames. `#scan h 4D0` starts at an ID, `#scan` stops and says where,
// and `#scan resume` carries on from there. `#scan h 0 00` fills the frames
// with 0x00 instead: use it to find a lamp that is on until its module is
// heard (watch for it going out), where all-0xFF may read as a fault.
#if BODY_BUS_TEST
#define MAIN_BUS_LABEL "MS (main shield) 0x"
#else
#define MAIN_BUS_LABEL "HS 0x"
#endif
const uint16_t SCAN_BLOCK = 16;
const unsigned long SCAN_STEP_MS = 2000;
bool scanning = false;
bool scanStarted = false;  // a scan has run since boot, so resume makes sense
bool scanMs = false;
uint16_t scanBase = 0;
uint8_t scanFill = 0xFF;
unsigned long scanStepStartMs = 0;

void printScanBlock() {
  DEBUG_PORT.print(F("scan "));
  DEBUG_PORT.print(scanMs ? F("MS 0x") : F(MAIN_BUS_LABEL));
  DEBUG_PORT.print(scanBase, HEX);
  DEBUG_PORT.print(F("-0x"));
  uint16_t last = scanBase + SCAN_BLOCK - 1;
  DEBUG_PORT.println(last > 0x7FF ? 0x7FF : last, HEX);
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
  uint8_t ff[8];
  for (uint8_t i = 0; i < 8; i++) ff[i] = scanFill;
  for (uint16_t id = scanBase; id < scanBase + SCAN_BLOCK && id <= 0x7FF; id++) {
    if (scanMs) {
      sendMsFrame(id, ff);
    } else if (BODY_BUS_TEST ||
               (id != mazda3::ID_RPM_SPEED && id != mazda3::ID_ENGINE_INFO)) {
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
  cluster.highBeam = parser.value('H') >= 0.5f;
  // No G field: leave the gear display blank rather than guess.
  cluster.gear = parser.seen('G') ? mazda3::gearFromGame(parser.value('G')) : mazda3::GEAR_NONE;
  unsigned long now = millis();
  bool left = parser.value('L') >= 0.5f;
  bool right = parser.value('Y') >= 0.5f;
  bool lit = turnBlinker.update(left || right, now, TURN_BLINK_HALF_MS);
  cluster.leftTurn = left && lit;
  cluster.rightTurn = right && lit;

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
  cluster.highBeam = false;
  cluster.gear = mazda3::GEAR_PARK;
  turnBlinker.update(false, millis(), TURN_BLINK_HALF_MS);
  cluster.leftTurn = false;
  cluster.rightTurn = false;
  // Leave coolant and fuel where they were so those gauges don't plunge when
  // you pause.

  setIndicator(PIN_LEFT_TURN, false);
  setIndicator(PIN_RIGHT_TURN, false);
  setIndicator(PIN_HIGH_BEAM, false);
  setIndicator(PIN_PARK_BRAKE, false);
}

// ----------------------------------------------------------- Byte sweep
// `#sweep h 212 4`: sends 0x212 with every byte 0x00 except byte 4, which
// steps through 0x00-0xFF, one value every SWEEP_STEP_MS, printing each value
// as it goes out. For lamps that need one particular value rather than all
// zeros or all 0xFF. `#sweep` stops and prints where it was. The swept frame
// replaces any built-in or custom frame with the same ID.
const unsigned long SWEEP_STEP_MS = 400;
bool sweeping = false;
bool sweepMs = false;
uint16_t sweepId = 0;
uint8_t sweepByte = 0;
uint8_t sweepValue = 0;
unsigned long sweepStepStartMs = 0;

void printSweepStep() {
  DEBUG_PORT.print(F("sweep "));
  DEBUG_PORT.print(sweepMs ? F("MS 0x") : F(MAIN_BUS_LABEL));
  DEBUG_PORT.print(sweepId, HEX);
  DEBUG_PORT.print(F(" byte "));
  DEBUG_PORT.print(sweepByte);
  DEBUG_PORT.print(F(" = "));
  if (sweepValue < 0x10) DEBUG_PORT.print('0');
  DEBUG_PORT.println(sweepValue, HEX);
}

void sendSweepFrame(unsigned long now) {
  if (!sweeping) return;
  if (now - sweepStepStartMs >= SWEEP_STEP_MS) {
    sweepStepStartMs = now;
    if (sweepValue == 0xFF) {
      sweeping = false;
      DEBUG_PORT.println(F("sweep done"));
      return;
    }
    sweepValue++;
    printSweepStep();
  }
  uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  data[sweepByte] = sweepValue;
  if (sweepMs) {
    sendMsFrame(sweepId, data);
  } else {
    sendFrame(sweepId, data);
  }
}

bool hasCustomFrame(uint16_t id, bool ms = false) {
  if (sweeping && sweepMs == ms && sweepId == id) return true;
  for (uint8_t i = 0; i < customCount; i++) {
    if (customFrames[i].ms == ms && customFrames[i].id == id) return true;
  }
  return false;
}

// Body-bus frames go to the second module, or to the main shield while it is
// on the body bus for BODY_BUS_TEST. Without either they have nowhere to go.
void sendBodyFrame(uint16_t id, const uint8_t *data) {
#if BODY_BUS_TEST
  if (!hasCustomFrame(id)) sendFrame(id, data);
#elif MS_CAN_ENABLED
  if (!hasCustomFrame(id, true)) sendMsFrame(id, data);
#else
  (void)id;
  (void)data;
#endif
}

// Built-in frames step aside when a custom frame with the same ID is active,
// so the cluster only ever sees one version of that ID. In the body bus test
// they're off entirely, so they can't mask a scan or custom frame.
void sendBuiltIn(uint16_t id, const uint8_t *data) {
  if (BODY_BUS_TEST) return;
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
    // With the fast frames so the flashing stays even.
    mazda3::buildTurnLamps(cluster, buf);
    sendBodyFrame(mazda3::ID_TURN_LAMPS, buf);
  }

  if (now - lastSlowMs >= SLOW_PERIOD_MS) {
    lastSlowMs = now;
    mazda3::buildEngineInfo(cluster, buf);
    sendBuiltIn(mazda3::ID_ENGINE_INFO, buf);
    mazda3::buildBrakeLamps(cluster, buf);
    sendBuiltIn(mazda3::ID_BRAKE_LAMPS, buf);
    mazda3::buildTransmission(cluster, buf);
    sendBuiltIn(mazda3::ID_TRANSMISSION, buf);
    mazda3::buildBodyLamps(cluster, buf);
    sendBodyFrame(mazda3::ID_BODY_LAMPS, buf);
    memset(buf, 0, sizeof(buf));  // airbag module present, no faults
    sendBodyFrame(mazda3::ID_AIRBAG, buf);
    for (uint8_t i = 0; i < customCount; i++) {
      const CustomFrame &f = customFrames[i];
      if (f.ms) {
        sendMsFrame(f.id, f.data, f.len);
      } else {
        sendFrame(f.id, f.data, f.len);
      }
    }
    sendScanFrames(now);
    sendSweepFrame(now);
  }
}

// ------------------------------------------------------- Serial commands
//   #420 5A 00 00 00 01 00 00 00   add/replace a custom frame (hex)
//   #420                           remove custom frame 0x420
//   #m433 00 00 00 40 00 00 00 00  same, on the body bus (MS-CAN)
//   #-                             remove all custom frames, stop a scan
//   #?                             list custom frames
//   #scan m / #scan h              scan every ID on MS-CAN / HS-CAN
//   #scan h 4D0                    scan HS-CAN starting at 0x4D0
//   #scan h 0 00                   scan HS-CAN from 0 with all-0x00 frames
//   #scan                          stop a scan (prints where it stopped)
//   #scan resume                   carry on from where it stopped
//   #stat                          bus health: is the cluster acknowledging?
//   #watch                         toggle printing cluster frames when they change
//   #sweep h 212 4                 step byte 4 of 0x212 through 00-FF
//   #sweep h 212 4 80              same, starting at 0x80
//   #sweep                         stop a sweep (prints where it stopped)
// A custom frame with a built-in ID (0x201/0x212/0x228/0x420/0x4B0) replaces
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

// Reads whatever the cluster sends so the receive buffers never fill, and
// prints each new ID the first time it is heard. Any frame from the cluster
// proves the wiring and bitrate on its own.
void pollReceive(mcp2515_can &bus, BusStats &s, const __FlashStringHelper *label) {
  while (bus.checkReceive() == CAN_MSGAVAIL) {
    unsigned long id = 0;
    byte len = 0;
    byte buf[8];
    bus.readMsgBufID(&id, &len, buf);
    s.rxFrames++;
    if (len > 8) len = 8;
    int8_t slot = -1;
    for (uint8_t i = 0; i < s.rxIdCount; i++) {
      if (s.rxIds[i] == id) slot = i;
    }
    if (slot >= 0) {
      // Known ID: print it again only when watching and the data changed.
      if (!watchChanges || memcmp(s.rxData[slot], buf, len) == 0) continue;
      memcpy(s.rxData[slot], buf, len);
      DEBUG_PORT.print(F("changed "));
    } else {
      if (s.rxIdCount >= MAX_RX_IDS) continue;
      slot = s.rxIdCount++;
      s.rxIds[slot] = id;
      memset(s.rxData[slot], 0, 8);
      memcpy(s.rxData[slot], buf, len);
      DEBUG_PORT.print(F("heard "));
    }
    DEBUG_PORT.print(label);
    DEBUG_PORT.print(id, HEX);
    for (uint8_t i = 0; i < len; i++) {
      DEBUG_PORT.print(' ');
      if (buf[i] < 0x10) DEBUG_PORT.print('0');
      DEBUG_PORT.print(buf[i], HEX);
    }
    DEBUG_PORT.println();
  }
}

// `#stat`: one bus's counts and a plain verdict on the wiring.
void printBusStats(mcp2515_can &bus, BusStats &s, const __FlashStringHelper *name) {
  uint8_t eflg = 0;
  bus.checkError(&eflg);
  DEBUG_PORT.print(name);
  DEBUG_PORT.print(F(": sent "));
  DEBUG_PORT.print(s.sent);
  DEBUG_PORT.print(F(", slow "));
  DEBUG_PORT.print(s.slow);
  DEBUG_PORT.print(F(", stuck "));
  DEBUG_PORT.print(s.stuck);
  DEBUG_PORT.print(F(", heard "));
  DEBUG_PORT.print(s.rxFrames);
  DEBUG_PORT.print(F(" frames / "));
  DEBUG_PORT.print(s.rxIdCount);
  DEBUG_PORT.print(F(" IDs, EFLG 0x"));
  DEBUG_PORT.println(eflg, HEX);

  DEBUG_PORT.print(F("  -> "));
  if (eflg & (MCP_EFLG_TXBO | MCP_EFLG_TXEP) || (s.stuck > 0 && s.sent == 0)) {
    DEBUG_PORT.println(F("NOT acknowledged: nothing on the wire is answering. Check H/L aren't swapped, the right pins, ignition 12 V, and the bitrate"));
  } else if (eflg & MCP_EFLG_TXWAR) {
    DEBUG_PORT.println(F("acknowledged with lots of errors: loose wire, missing ground or termination"));
  } else if (s.rxFrames > 0 || s.sent > 0) {
    DEBUG_PORT.println(F("OK: the cluster is acknowledging at this bitrate"));
  } else {
    DEBUG_PORT.println(F("nothing sent yet - add a frame (e.g. #7FF 00) and try again"));
  }
}

void printStats() {
#if BODY_BUS_TEST
  printBusStats(CAN, mainStats, F("MS-CAN (main shield, 125k)"));
#else
  printBusStats(CAN, mainStats, F("HS-CAN (500k)"));
#endif
#if MS_CAN_ENABLED
  printBusStats(MSCAN, msStats, F("MS-CAN (second shield, 125k)"));
#endif
}

void handleCommand(const char *cmd) {
  if (cmd[0] == '-') {
    customCount = 0;
    scanning = false;
    sweeping = false;
    DEBUG_PORT.println(F("custom frames cleared"));
    return;
  }
  if (strncmp(cmd, "sweep", 5) == 0) {
    const char *arg = cmd + 5;
    while (*arg == ' ') arg++;
    if (*arg == 0) {
      if (sweeping) {
        sweeping = false;
        DEBUG_PORT.print(F("sweep stopped at "));
        printSweepStep();
      } else {
        DEBUG_PORT.println(F("no sweep running"));
      }
      return;
    }
    bool ms = (*arg == 'm' || *arg == 'M');
    if (!ms && *arg != 'h' && *arg != 'H') {
      DEBUG_PORT.println(F("use #sweep h <id> <byte> or #sweep m <id> <byte>"));
      return;
    }
#if !MS_CAN_ENABLED
    if (ms) {
      DEBUG_PORT.println(F("MS-CAN not enabled - set MS_CAN_ENABLED 1 in config.h"));
      return;
    }
#endif
    arg++;
    if (*arg == 's' || *arg == 'S') arg++;
    while (*arg == ' ') arg++;
    if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) arg += 2;
    long id = nextHex(arg);
    long byteIdx = nextHex(arg);
    long start = nextHex(arg);
    if (id < 0 || id > 0x7FF || byteIdx < 0 || byteIdx > 7 || start > 0xFF) {
      DEBUG_PORT.println(F("use #sweep h <id 0-7FF> <byte 0-7> [start 00-FF]"));
      return;
    }
    sweepMs = ms;
    sweepId = (uint16_t)id;
    sweepByte = (uint8_t)byteIdx;
    sweepValue = start < 0 ? 0 : (uint8_t)start;
    sweeping = true;
    sweepStepStartMs = millis();
    printSweepStep();
    return;
  }
  if (strncmp(cmd, "watch", 5) == 0) {
    watchChanges = !watchChanges;
    DEBUG_PORT.println(watchChanges ? F("watch on: cluster frames print when they change")
                                    : F("watch off"));
    return;
  }
  if (strncmp(cmd, "stat", 4) == 0) {
    printStats();
    return;
  }
  if (strncmp(cmd, "scan", 4) == 0) {
    const char *arg = cmd + 4;
    while (*arg == ' ') arg++;
    if (*arg == 'r' || *arg == 'R') {
      // #scan resume: carry on from the block where the last scan stopped.
      if (!scanStarted) {
        DEBUG_PORT.println(F("nothing to resume - start with #scan h or #scan m"));
        return;
      }
      scanning = true;
      scanStepStartMs = millis();
      printScanBlock();
      return;
    }
    if (*arg == 'm' || *arg == 'M' || *arg == 'h' || *arg == 'H') {
      bool ms = (*arg == 'm' || *arg == 'M');
#if !MS_CAN_ENABLED
      if (ms) {
        DEBUG_PORT.println(F("MS-CAN not enabled - set MS_CAN_ENABLED 1 in config.h"));
        return;
      }
#endif
      arg++;
      if (*arg == 's' || *arg == 'S') arg++;  // accept "hs" / "ms"
      while (*arg == ' ') arg++;
      if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) arg += 2;
      long start = nextHex(arg);
      if (start > 0x7FF) {
        DEBUG_PORT.println(F("start ID must be 0-7FF"));
        return;
      }
      long fill = nextHex(arg);
      if (fill > 0xFF) {
        DEBUG_PORT.println(F("fill byte must be 00-FF"));
        return;
      }
      scanMs = ms;
      scanBase = start < 0 ? 0 : (uint16_t)start;
      scanFill = fill < 0 ? 0xFF : (uint8_t)fill;
      scanning = true;
      scanStarted = true;
      scanStepStartMs = millis();
      printScanBlock();
      return;
    }
    if (scanning) {
      scanning = false;
      DEBUG_PORT.print(F("scan stopped at "));
      DEBUG_PORT.print(scanMs ? F("MS 0x") : F(MAIN_BUS_LABEL));
      DEBUG_PORT.print(scanBase, HEX);
      DEBUG_PORT.println(F(" - #scan resume to continue"));
    } else {
      DEBUG_PORT.println(F("no scan running"));
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

  pollReceive(CAN, mainStats, F(MAIN_BUS_LABEL));
#if MS_CAN_ENABLED
  pollReceive(MSCAN, msStats, F("MS 0x"));
#endif
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
#if BODY_BUS_TEST
  DEBUG_PORT.println(F("BODY BUS TEST: 125 kbit/s, gauge frames off - use #scan h"));
#endif

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
