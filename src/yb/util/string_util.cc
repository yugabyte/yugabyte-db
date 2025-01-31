//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include "yb/util/string_util.h"

#include <regex>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include <boost/preprocessor/cat.hpp>

#include "yb/util/logging.h"

using std::vector;
using std::regex;
using std::regex_match;
using std::string;
using std::stringstream;
using boost::algorithm::iequals;

namespace yb {

bool IsBigInteger(const Slice& s) {
  static const regex int_regex("[+-]?[0-9]+");
  return regex_match(s.cdata(), int_regex);
}

bool IsDecimal(const Slice& s) {
  // Regexes are based (but do not match exactly) the definition of Decimal::FromString
  static const string optional_exp_suffix = "([eE][+-]?[0-9]+)?";
  static const regex decimal_regex_1("[+-]?[0-9]*\\.[0-9]+" + optional_exp_suffix);
  static const regex decimal_regex_2("[+-]?[0-9]+\\.?" + optional_exp_suffix);
  return IsBigInteger(s)
      || regex_match(s.cdata(), decimal_regex_1)
      || regex_match(s.cdata(), decimal_regex_2);
}

bool IsBoolean(const Slice& s) {
  return iequals(s.cdata(), "true") || iequals(s.cdata(), "false");
}

bool IsIdLikeUuid(const Slice& s) {
  static const regex uuid_regex("[0-9a-f]{32}");
  return regex_match(s.cdata(), uuid_regex);
}

vector<string> StringSplit(const string& arg, char delim) {
  vector<string> splits;
  stringstream ss(arg);
  string item;
  while (getline(ss, item, delim)) {
    splits.push_back(std::move(item));
  }
  return splits;
}

bool EqualsIgnoreCase(Slice a, Slice b) {
  if (a.size() != b.size()) {
    return false;
  }

  const uint8_t* a_ptr = a.data();
  const uint8_t* b_ptr = b.data();
  size_t size = a.size();

  for (size_t i = 0; i < size; ++i) {
    if (std::tolower(a_ptr[i]) != std::tolower(b_ptr[i])) {
      return false;
    }
  }
  return true;
}

bool StringStartsWithOrEquals(const string& s, const char* start, size_t start_len) {
  return s.rfind(start, 0) == 0;
}

bool StringEndsWith(const string& s, const char* end, size_t end_len, string* left) {
  // For our purpose, s should always have at least one character before the string we are looking
  // for.
  if (s.length() <= end_len) {
    return false;
  }
  if (s.find(end, s.length() - end_len) != string::npos) {
    if (left != nullptr) {
      *left = s.substr(0, s.length() - end_len);
    }
    return true;
  }
  return false;
}

void AppendWithSeparator(const string& to_append, string* dest, const char* separator) {
  CHECK_NOTNULL(dest);
  if (!dest->empty()) {
    *dest += separator;
  }
  *dest += to_append;
}

void AppendWithSeparator(const char* to_append, string* dest, const char* separator) {
  CHECK_NOTNULL(dest);
  if (!dest->empty()) {
    *dest += separator;
  }
  *dest += to_append;
}

std::vector<std::string> SplitAndFlatten(
    const std::vector<std::string>& input,
    const char* separators) {
  std::vector<std::string> result_vec;
  for (const auto& dir : input) {
    std::vector<std::string> temp;
    boost::split(temp, dir, boost::is_any_of(separators));
    result_vec.insert(result_vec.end(), temp.begin(), temp.end());
  }
  return result_vec;
}

}  // namespace yb
