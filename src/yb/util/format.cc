// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/format.h"

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace yb {

namespace {

std::string PrintDouble(const char* format, double value, int precision) {
  auto size = std::snprintf(nullptr, 0, format, precision, value);
  std::string result(size, '\0');
  std::snprintf(result.data(), size + 1, format, precision, value);
  return result;
}

} // namespace

std::string FixedPoint(double value, int precision) {
  return PrintDouble("%.*f", value, precision);
}

std::string Scientific(double value, int precision) {
  return PrintDouble("%.*E", value, precision);
}

std::string PadLeft(std::string_view str, size_t width, char fill) {
  std::string result;
  if (str.size() < width) {
    result.reserve(width);
    result.append(width - str.size(), fill);
  }
  result += str;
  return result;
}

std::string PadRight(std::string_view str, size_t width, char fill) {
  std::string result;
  result.reserve(std::max(width, str.size()));
  result += str;
  if (result.size() < width) {
    result.resize(width, fill);
  }
  return result;
}

namespace internal {

std::string HexString(uint64_t bits, size_t width) {
  char buffer[16];
  auto end = std::to_chars(buffer, buffer + sizeof(buffer), bits, 16).ptr;
  return PadLeft(std::string_view(buffer, end - buffer), width, '0');
}

} // namespace internal

} // namespace yb
