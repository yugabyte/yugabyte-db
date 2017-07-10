//
// Copyright (c) YugaByte, Inc.
//

#include "yb/redisserver/redis_encoding.h"

namespace yb {
namespace redisserver {

string EncodeAsInteger(const std::string& input) { return StrCat(":", input, "\r\n"); }

string EncodeAsInteger(int64_t input) { return StrCat(":", input, "\r\n"); }

string EncodeAsSimpleString(const std::string& input) { return StrCat("+", input, "\r\n"); }

string EncodeAsError(const std::string& message) { return StrCat("-", message, "\r\n"); }

string EncodeAsBulkString(const std::string& input) {
  size_t len = input.length();
  return StrCat("$", len, "\r\n", input, "\r\n");
}

string EncodeAsArrays(const std::vector<std::string>& encoded_elements) {
  return StrCat("*", encoded_elements.size(), "\r\n", JoinStrings(encoded_elements, ""));
}

}  // namespace redisserver
}  // namespace yb
