#pragma once

#include <stddef.h>
#include <stdint.h>

// Controls this panel can show. Other LoxAPP3.json types are skipped.
enum LoxoneKind : uint8_t {
  kLoxoneSwitch = 0,  // On / Off
  kLoxonePulse = 1,   // Pulse
};

struct LoxoneControl {
  char name[64];
  char room[40];
  char action[40];
  LoxoneKind kind;
};

constexpr size_t kLoxoneControlCap = 32;

// Ids that are safe to place in a Miniserver URL path.
inline bool loxone_id_ok(const char* s, size_t max_len) {
  if (s == nullptr || s[0] == '\0') {
    return false;
  }
  size_t n = 0;
  for (const char* p = s; *p != '\0'; ++p, ++n) {
    if (n >= max_len) {
      return false;
    }
    char c = *p;
    bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (!alnum && c != '-' && c != '_' && c != '.' && c != ':') {
      return false;
    }
  }
  return true;
}
