#include "loxone_structure.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

struct PsramAllocator {
  void* allocate(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void* pointer) { heap_caps_free(pointer); }
  void* reallocate(void* pointer, size_t new_size) {
    return heap_caps_realloc(pointer, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};

using PsramJsonDocument = BasicJsonDocument<PsramAllocator>;

void copy_trunc(char* dst, size_t n, const char* src) {
  if (n == 0) {
    return;
  }
  snprintf(dst, n, "%s", src != nullptr ? src : "");
}

bool is_switch_type(const char* type) {
  return strcmp(type, "Switch") == 0 || strcmp(type, "TimedSwitch") == 0 ||
         strcmp(type, "LightController") == 0 || strcmp(type, "Dimmer") == 0;
}

int compare_controls(const void* left, const void* right) {
  const LoxoneControl* a = static_cast<const LoxoneControl*>(left);
  const LoxoneControl* b = static_cast<const LoxoneControl*>(right);
  int room = strcasecmp(a->room, b->room);
  if (room != 0) {
    return room;
  }
  return strcasecmp(a->name, b->name);
}

}  // namespace

bool loxone_parse_structure(const uint8_t* json, size_t length, LoxoneControl* out, size_t cap, size_t* shown,
                            size_t* supported, char* server, size_t server_n, char* error, size_t error_n) {
  if (shown != nullptr) {
    *shown = 0;
  }
  if (supported != nullptr) {
    *supported = 0;
  }
  if (server != nullptr && server_n > 0) {
    server[0] = '\0';
  }
  if (json == nullptr || length == 0 || out == nullptr || cap == 0) {
    snprintf(error, error_n, "Empty structure");
    return false;
  }

  size_t capacity = length * 2 + 4096;
  if (capacity < 8192) {
    capacity = 8192;
  }
  PsramJsonDocument doc(capacity);
  if (doc.capacity() == 0) {
    snprintf(error, error_n, "Not enough memory for LoxAPP3.json");
    return false;
  }

  const uint8_t* body = json;
  size_t body_len = length;
  if (body_len >= 3 && body[0] == 0xEF && body[1] == 0xBB && body[2] == 0xBF) {
    body += 3;
    body_len -= 3;
  }

  DeserializationError err = deserializeJson(doc, body, body_len);
  if (err) {
    snprintf(error, error_n, "JSON parse error (%s)", err.c_str());
    return false;
  }

  if (!doc.containsKey("controls") && doc.containsKey("LL")) {
    JsonVariant code = doc["LL"]["Code"];
    if (code.is<const char*>()) {
      snprintf(error, error_n, "Miniserver code %s", code.as<const char*>());
    } else if (code.is<int>()) {
      snprintf(error, error_n, "Miniserver code %d", code.as<int>());
    } else {
      snprintf(error, error_n, "Miniserver did not return a structure");
    }
    return false;
  }

  if (server != nullptr && server_n > 0) {
    copy_trunc(server, server_n, doc["msInfo"]["msName"] | "");
  }

  JsonObject controls = doc["controls"].as<JsonObject>();
  if (controls.isNull()) {
    snprintf(error, error_n, "LoxAPP3.json has no controls");
    return false;
  }

  size_t count = 0;
  size_t total = 0;
  for (JsonPair kv : controls) {
    JsonObject ctrl = kv.value().as<JsonObject>();
    const char* type = ctrl["type"] | "";
    LoxoneKind kind;
    if (is_switch_type(type)) {
      kind = kLoxoneSwitch;
    } else if (strcmp(type, "Pushbutton") == 0) {
      kind = kLoxonePulse;
    } else {
      continue;
    }
    total++;

    const char* action = ctrl["uuidAction"] | "";
    if (!loxone_id_ok(action, sizeof(out[0].action))) {
      action = kv.key().c_str();
    }
    if (!loxone_id_ok(action, sizeof(out[0].action))) {
      continue;
    }
    if (count >= cap) {
      continue;
    }

    const char* name = ctrl["name"] | "";
    if (name[0] == '\0') {
      name = type;
    }
    const char* room = "";
    const char* room_id = ctrl["room"] | "";
    if (room_id[0] != '\0') {
      room = doc["rooms"][room_id]["name"] | "";
    }

    copy_trunc(out[count].name, sizeof(out[count].name), name);
    copy_trunc(out[count].room, sizeof(out[count].room), room);
    copy_trunc(out[count].action, sizeof(out[count].action), action);
    out[count].kind = kind;
    count++;
  }

  if (count > 1) {
    qsort(out, count, sizeof(LoxoneControl), compare_controls);
  }
  if (shown != nullptr) {
    *shown = count;
  }
  if (supported != nullptr) {
    *supported = total;
  }
  return true;
}
