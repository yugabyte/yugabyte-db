//
// Copyright (c) YugaByte, Inc.
//

#include "yb/redisserver/redis_encoding.h"

namespace yb {
namespace redisserver {

string EncodeAsInteger(string input) { return StrCat(":", input, "\r\n"); }

string EncodeAsInteger(int64_t input) { return StrCat(":", input, "\r\n"); }

string EncodeAsSimpleString(string input) { return StrCat("+", input, "\r\n"); }

string EncodeAsError(string message) { return StrCat("-", message, "\r\n"); }

string EncodeAsBulkString(string input) {
  size_t len = input.length();
  return StrCat("$", len, "\r\n", input, "\r\n");
}

string EncodeAsArrays(vector<string> encoded_elements) {
  return StrCat("*", encoded_elements.size(), "\r\n", JoinStrings(encoded_elements, ""));
}

}  // namespace redisserver
}  // namespace yb
