// =============================================================================
//  telemetry_parser.h - parses the text stream SimTools sends over serial.
//
//  Format: a sequence of fields, each an upper-case letter followed by a
//  number, e.g.
//
//      R2350S88.5T90A42E0B0O0L1Y0H0P0;
//
//  Fields can be separated by anything that isn't a letter or part of a number
//  (';', ',', space, newline...). A field is applied as soon as it ends, so no
//  special packet framing is required - just finish each packet with a
//  separator (';' or a newline) so the last field doesn't wait for the next
//  packet.
//
//  Lines starting with '#' are commands, not telemetry; they are collected
//  whole and handed back via commandReady()/command().
//
//  Kept free of Arduino dependencies so it can be unit-tested on a PC.
// =============================================================================
#pragma once

#include <stdint.h>

class TelemetryParser {
 public:
  static const uint8_t CMD_MAX = 48;

  TelemetryParser() { reset(); }

  void reset() {
    key_ = 0;
    len_ = 0;
    inCommand_ = false;
    cmdLen_ = 0;
    cmdReady_ = false;
    for (int i = 0; i < 26; i++) {
      values_[i] = 0.0f;
    }
    seenMask_ = 0;
  }

  // Feed one byte. Returns true if a telemetry field was just completed.
  bool feed(char c) {
    if (inCommand_) {
      if (c == '\n' || c == '\r') {
        cmd_[cmdLen_] = '\0';
        inCommand_ = false;
        cmdReady_ = true;
      } else if (cmdLen_ < CMD_MAX - 1) {
        cmd_[cmdLen_++] = c;
      }
      return false;
    }

    if (c == '#') {
      bool done = commit();
      inCommand_ = true;
      cmdLen_ = 0;
      cmdReady_ = false;
      return done;
    }

    if (c >= 'A' && c <= 'Z') {
      bool done = commit();
      key_ = c;
      len_ = 0;
      return done;
    }

    bool numeric = (c >= '0' && c <= '9') || c == '.' || (c == '-' && len_ == 0);
    if (numeric && key_ != 0) {
      if (len_ < sizeof(buf_) - 1) buf_[len_++] = c;
      return false;
    }

    // Allow spaces between a key and its number ("R 800").
    if (c == ' ' && key_ != 0 && len_ == 0) return false;

    // Any other character terminates the current field.
    return commit();
  }

  float value(char key) const { return values_[key - 'A']; }
  bool seen(char key) const { return (seenMask_ >> (key - 'A')) & 1UL; }

  bool commandReady() const { return cmdReady_; }
  // Returns the pending command (without the leading '#') and clears it.
  const char *takeCommand() {
    cmdReady_ = false;
    return cmd_;
  }

 private:
  bool commit() {
    if (key_ == 0) return false;
    char k = key_;
    key_ = 0;
    if (len_ == 0) return false;
    buf_[len_] = '\0';
    len_ = 0;
    values_[k - 'A'] = parseFloat(buf_);
    seenMask_ |= 1UL << (k - 'A');
    return true;
  }

  // Minimal float parser (avoids pulling in strtod/atof on AVR).
  static float parseFloat(const char *s) {
    bool neg = false;
    if (*s == '-') {
      neg = true;
      s++;
    }
    float v = 0.0f;
    while (*s >= '0' && *s <= '9') v = v * 10.0f + (float)(*s++ - '0');
    if (*s == '.') {
      s++;
      float scale = 0.1f;
      while (*s >= '0' && *s <= '9') {
        v += (float)(*s++ - '0') * scale;
        scale *= 0.1f;
      }
    }
    return neg ? -v : v;
  }

  float values_[26];
  uint32_t seenMask_;
  char key_;
  char buf_[16];
  uint8_t len_;

  bool inCommand_;
  bool cmdReady_;
  char cmd_[CMD_MAX];
  uint8_t cmdLen_;
};
