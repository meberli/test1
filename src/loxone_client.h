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
// action is a Loxone uuidAction. command is On, Off, Pulse, FullUp, FullDown, UpOff, or DownOff.
LoxoneHttpResult loxone_send_command(LoxoneAuthorizer& auth, const char* action, const char* command);

// GET /jdev/sps/io/<state>/state
// state is the uuid from a control's states object, not uuidAction.
// On success, value is the LL.value text ("0", "1", "0.42", "9", ...).
LoxoneHttpResult loxone_read_state(LoxoneAuthorizer& auth, const char* state, char* value, size_t value_n);
