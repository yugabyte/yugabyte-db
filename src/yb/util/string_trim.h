// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_STRING_TRIM_H
#define YB_UTIL_STRING_TRIM_H

#include <string>

namespace yb {
namespace util {

constexpr char kWhitespaceCharacters[] = " \t\f\n\r\v";

// Return a copy of the given string with the given set of characters trimmed from the end of it.
inline std::string RightTrimStr(const std::string& s,
                                const char* chars_to_trim = kWhitespaceCharacters) {
  std::string result(s);
  result.erase(result.find_last_not_of(chars_to_trim) + 1);
  return result;
}

// Returns a copy of the given string with the given set of characters trimmed from the beginning
// of the string.
inline std::string LeftTrimStr(const std::string& s,
                               const char* chars_to_trim = kWhitespaceCharacters) {
  std::string result(s);
  result.erase(0, result.find_first_not_of(chars_to_trim));
  return result;
}

// Returns a copy of the given string with the given set of characters trimmed from both ends of
// the string.
inline std::string TrimStr(const std::string& s,
                           const char* chars_to_trim = kWhitespaceCharacters) {
  return LeftTrimStr(RightTrimStr(s, chars_to_trim), chars_to_trim);
}

}
}

#endif