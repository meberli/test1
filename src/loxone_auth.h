#pragma once

#include <HTTPClient.h>

// LoxoneClient calls apply() and does not know how the request is authorized.
// LoxoneBasicAuthorizer is the scheme this firmware runs.
// LoxoneTokenAuthorizer is the seam for a later Miniserver token flow
// (getkey2, gettoken, then send the token instead of the password).
// It is not implemented and is not selected unless LOXONE_AUTH is "token".

class LoxoneAuthorizer {
 public:
  virtual ~LoxoneAuthorizer() = default;
  virtual const char* name() const = 0;
  virtual bool apply(HTTPClient& http) = 0;
};

class LoxoneBasicAuthorizer : public LoxoneAuthorizer {
 public:
  LoxoneBasicAuthorizer(const char* user, const char* password)
      : user_(user != nullptr ? user : ""), password_(password != nullptr ? password : "") {}

  const char* name() const override { return "basic"; }

  bool apply(HTTPClient& http) override {
    if (user_[0] == '\0') {
      return false;
    }
    http.setAuthorization(user_, password_);
    return true;
  }

 private:
  const char* user_;
  const char* password_;
};

class LoxoneTokenAuthorizer : public LoxoneAuthorizer {
 public:
  const char* name() const override { return "token"; }

  bool apply(HTTPClient&) override { return false; }
};
