#include "loxone_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include <stdio.h>
#include <string.h>

#include "config_load.h"
#include "loxone_types.h"

namespace {

void set_detail(LoxoneHttpResult* result, const char* text) {
  snprintf(result->detail, sizeof(result->detail), "%s", text != nullptr ? text : "");
}

// Host only. Rejects HTTPS. Strips a leading http:// and any path.
bool normalize_host(char* out, size_t out_n, char* error, size_t error_n) {
  const char* host = LOXONE_HOST;
  if (strncmp(host, "https://", 8) == 0) {
    snprintf(error, error_n, "HTTPS is not implemented");
    return false;
  }
  if (strncmp(host, "http://", 7) == 0) {
    host += 7;
  }
  snprintf(out, out_n, "%s", host);
  size_t n = strlen(out);
  while (n > 0 && out[n - 1] == '/') {
    out[--n] = '\0';
  }
  char* slash = strchr(out, '/');
  if (slash != nullptr) {
    *slash = '\0';
  }
  if (out[0] == '\0') {
    snprintf(error, error_n, "Miniserver host is empty");
    return false;
  }
  return true;
}

bool code_is_200(JsonVariant code) {
  if (code.is<int>()) {
    return code.as<int>() == 200;
  }
  if (code.is<const char*>()) {
    return strcmp(code.as<const char*>(), "200") == 0;
  }
  return false;
}

bool copy_scalar(JsonVariant value, char* out, size_t out_n) {
  if (out == nullptr || out_n == 0) {
    return false;
  }
  out[0] = '\0';
  if (value.is<const char*>()) {
    snprintf(out, out_n, "%s", value.as<const char*>());
    return true;
  }
  if (value.is<bool>()) {
    snprintf(out, out_n, "%d", value.as<bool>() ? 1 : 0);
    return true;
  }
  if (value.is<int>() || value.is<long>()) {
    snprintf(out, out_n, "%ld", static_cast<long>(value.as<long>()));
    return true;
  }
  if (value.is<float>()) {
    snprintf(out, out_n, "%.4f", value.as<float>());
    return true;
  }
  return false;
}

}  // namespace

LoxoneHttpResult loxone_get(LoxoneAuthorizer& auth, const char* path, uint8_t* body, size_t cap,
                            size_t* out_len) {
  LoxoneHttpResult result = {};
  result.ok = false;
  result.status = 0;
  if (out_len != nullptr) {
    *out_len = 0;
  }
  if (path == nullptr || path[0] != '/' || body == nullptr || cap == 0) {
    set_detail(&result, "Bad request");
    return result;
  }
  if (WiFi.status() != WL_CONNECTED) {
    set_detail(&result, "Wi-Fi is down");
    return result;
  }

  char host[80];
  if (!normalize_host(host, sizeof(host), result.detail, sizeof(result.detail))) {
    return result;
  }

  WiFiClient wifi;
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(20000);
  if (!http.begin(wifi, host, static_cast<uint16_t>(LOXONE_PORT), path)) {
    set_detail(&result, "Could not start HTTP");
    return result;
  }
  http.useHTTP10(true);
  if (!auth.apply(http)) {
    snprintf(result.detail, sizeof(result.detail), "Auth scheme \"%s\" is not available", auth.name());
    http.end();
    return result;
  }

  int code = http.GET();
  result.status = code;
  if (code <= 0) {
    String err = http.errorToString(code);
    snprintf(result.detail, sizeof(result.detail), "No HTTP response (%s)", err.c_str());
    http.end();
    return result;
  }
  if (code == 401) {
    set_detail(&result, "Miniserver rejected the login");
    http.end();
    return result;
  }

  int remaining = http.getSize();
  if (remaining > 0 && static_cast<size_t>(remaining) > cap) {
    snprintf(result.detail, sizeof(result.detail), "Response is %d bytes (limit %u)", remaining,
             static_cast<unsigned>(cap));
    http.end();
    return result;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    set_detail(&result, "No HTTP stream");
    http.end();
    return result;
  }
  size_t total = 0;
  uint32_t last_data = millis();
  while ((http.connected() || stream->available() > 0) && (remaining > 0 || remaining == -1)) {
    size_t avail = stream->available();
    if (avail > 0) {
      if (total >= cap) {
        snprintf(result.detail, sizeof(result.detail), "Response exceeded %u bytes",
                 static_cast<unsigned>(cap));
        http.end();
        return result;
      }
      size_t room = cap - total;
      if (avail < room) {
        room = avail;
      }
      int n = stream->readBytes(reinterpret_cast<char*>(body + total), room);
      if (n > 0) {
        total += static_cast<size_t>(n);
        if (remaining > 0) {
          remaining -= n;
        }
        last_data = millis();
      }
    } else if (total > 0 && remaining < 0 && millis() - last_data > 1500) {
      break;
    } else if (millis() - last_data > 20000) {
      set_detail(&result, "Timed out reading the response");
      http.end();
      return result;
    } else {
      delay(1);
    }
    if (remaining == 0) {
      break;
    }
  }
  http.end();

  if (code != 200) {
    snprintf(result.detail, sizeof(result.detail), "HTTP %d", code);
    return result;
  }
  if (total == 0) {
    set_detail(&result, "Empty response");
    return result;
  }
  if (out_len != nullptr) {
    *out_len = total;
  }
  result.ok = true;
  set_detail(&result, "OK");
  return result;
}

LoxoneHttpResult loxone_send_command(LoxoneAuthorizer& auth, const char* action, const char* command) {
  LoxoneHttpResult result = {};
  if (!loxone_id_ok(action, 40) || !loxone_id_ok(command, 16)) {
    set_detail(&result, "Control id was rejected");
    return result;
  }

  char path[96];
  int wrote = snprintf(path, sizeof(path), "/jdev/sps/io/%s/%s", action, command);
  if (wrote <= 0 || static_cast<size_t>(wrote) >= sizeof(path)) {
    set_detail(&result, "Control path was too long");
    return result;
  }

  uint8_t body[640];
  size_t length = 0;
  result = loxone_get(auth, path, body, sizeof(body), &length);
  if (!result.ok) {
    return result;
  }

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, body, length) != DeserializationError::Ok) {
    return result;
  }
  JsonVariant code = doc["LL"]["Code"];
  if (code.isNull()) {
    code = doc["LL"]["@Code"];
  }
  if (code.isNull()) {
    return result;
  }
  if (!code_is_200(code)) {
    result.ok = false;
    if (code.is<int>()) {
      snprintf(result.detail, sizeof(result.detail), "Miniserver code %d", code.as<int>());
    } else if (code.is<const char*>()) {
      snprintf(result.detail, sizeof(result.detail), "Miniserver code %s", code.as<const char*>());
    } else {
      set_detail(&result, "Miniserver rejected the command");
    }
  }
  return result;
}

LoxoneHttpResult loxone_read_state(LoxoneAuthorizer& auth, const char* state, char* value, size_t value_n) {
  LoxoneHttpResult result = {};
  if (value != nullptr && value_n > 0) {
    value[0] = '\0';
  }
  if (!loxone_id_ok(state, 40) || value == nullptr || value_n == 0) {
    set_detail(&result, "State id was rejected");
    return result;
  }

  char path[96];
  int wrote = snprintf(path, sizeof(path), "/jdev/sps/io/%s/state", state);
  if (wrote <= 0 || static_cast<size_t>(wrote) >= sizeof(path)) {
    set_detail(&result, "State path was too long");
    return result;
  }

  uint8_t body[640];
  size_t length = 0;
  result = loxone_get(auth, path, body, sizeof(body), &length);
  if (!result.ok) {
    return result;
  }

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, body, length) != DeserializationError::Ok) {
    result.ok = false;
    set_detail(&result, "State response was not JSON");
    return result;
  }
  JsonObject ll = doc["LL"].as<JsonObject>();
  JsonVariant code = ll["Code"];
  if (code.isNull()) {
    code = ll["@Code"];
  }
  if (!code_is_200(code)) {
    result.ok = false;
    set_detail(&result, "State was not available");
    return result;
  }
  JsonVariant raw = ll["value"];
  if (raw.isNull()) {
    raw = ll["@value"];
  }
  if (!copy_scalar(raw, value, value_n)) {
    result.ok = false;
    set_detail(&result, "State had no value");
    return result;
  }
  set_detail(&result, "OK");
  return result;
}
