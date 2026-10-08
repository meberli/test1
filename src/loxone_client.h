#pragma once

#include <stddef.h>
#include <stdint.h>

#include "loxone_auth.h"

struct LoxoneHttpResult {
  bool ok;
  int status;
  char detail[120];
};

// Local Miniserver HTTP. Paths are absolute ("/data/LoxAPP3.json").
LoxoneHttpResult loxone_get(LoxoneAuthorizer& auth, const char* path, uint8_t* body, size_t cap,
                            size_t* out_len);

// GET /jdev/sps/io/<action>/<command>
// action is a Loxone uuidAction. command is On, Off, or Pulse.
LoxoneHttpResult loxone_send_command(LoxoneAuthorizer& auth, const char* action, const char* command);
