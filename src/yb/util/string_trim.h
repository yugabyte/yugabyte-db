// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

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

// Remove the maximum number of leading spaces found in any non-empty line of the given multi-line
// text block from each of its lines. Lines only containing spaces are considered empty.
//
// Note: lines containing whitespace-only characters but not all spaces (e.g. those with tabs, etc.)
// are currently not considered empty by this function.
std::string LeftShiftTextBlock(const std::string& s);

// Concatenates lines if the final character of a line is "\". Also removes whitespace in the
// next line after such continuation backslash (hence "eager" in the name). This is useful in
// respecting the maximum line length rule in expected test output specified using raw literals.
std::string ApplyEagerLineContinuation(const std::string& s);

// Trim C++-style comments from each line of the given string. Used to allow including comments
// inside DocDB debug dump string constants.
std::string TrimCppComments(const std::string& s);

// Treating the given string as a multi-line piece of text, trims trailing whitespace from every
// line of it and returns the resulting multi-line piece of text as a string.
std::string TrimTrailingWhitespaceFromEveryLine(std::string s);

}  // namespace util
}  // namespace yb
