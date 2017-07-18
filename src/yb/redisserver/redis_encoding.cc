//
// Copyright (c) YugaByte, Inc.
//

#include <google/protobuf/repeated_field.h>
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

void DoAppend(size_t* out, const AlphaNum& an) {
  *out += an.size();
}

void DoAppend(std::string* out, const AlphaNum& an) {
  out->append(an.data(), an.size());
}

template<class Out>
void StringAppend(Out* out, const AlphaNum& an) {
  DoAppend(out, an);
}

template<class Out, class... Args>
void StringAppend(Out* out, const AlphaNum& an, Args&&... args) {
  DoAppend(out, an);
  StringAppend(out, std::forward<Args>(args)...);
}

template<class Out>
void DoEncodeAsArrays(const google::protobuf::RepeatedPtrField<string> &elements, Out* out) {
  StringAppend(out, "*", elements.size(), "\r\n");
  for (const auto& elem : elements) {
    if (elem.length() == 0) {
      StringAppend(out, kNilResponse);
    } else {
      StringAppend(out, "$", elem.size(), "\r\n", elem, "\r\n");
    }
  }
}

string EncodeAsArrays(const google::protobuf::RepeatedPtrField<string> &elements) {
  size_t expected_length = 0;
  DoEncodeAsArrays(elements, &expected_length);
  std::string encoded;
  encoded.reserve(expected_length);
  DoEncodeAsArrays(elements, &encoded);
  return encoded;
}

}  // namespace redisserver
}  // namespace yb
