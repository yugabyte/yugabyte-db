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

#include "yb/util/tostring.h"

#include <chrono>

#include "yb/util/format.h"

namespace yb {

std::string MillisecondsToString(int64_t milliseconds) {
  const char* sign = "";
  if (milliseconds < 0) {
    sign = "-";
    milliseconds = -milliseconds;
  }
  int64_t seconds = milliseconds / 1000;
  milliseconds -= seconds * 1000;
  return StringPrintf("%s%" PRId64 ".%03" PRId64 "s", sign, seconds, milliseconds);
}

std::string CStringArrayToString(char** elements, size_t length) {
  std::string result = "[";
  for (size_t i = 0; i < length; ++i) {
    result += Format("$0$1", (i) ? "," : "", elements[i]);
  }
  result += "]";
  return result;
}

namespace {

template <class Clock, class Duration>
std::string ToStringTimePoint(const std::chrono::time_point<Clock, Duration>& tp) {
  int64_t micros = std::chrono::duration_cast<std::chrono::microseconds>(
    tp.time_since_epoch()).count();

  const char* sign = "";
  if (micros < 0) {
    sign = "-";
    micros = -micros;
  }

  int64_t seconds = micros / 1'000'000;
  int64_t remainder_micros = micros % 1'000'000;

  // Format as "<seconds>.<microseconds padded to 6 digits>s", mirroring MillisecondsToString.
  return StringPrintf("%s%" PRId64 ".%06" PRId64 "s", sign, seconds, remainder_micros);
}

} // namespace

std::string ToString(const std::chrono::steady_clock::time_point& time_point) {
  return ToStringTimePoint(time_point);
}

std::string ToString(const std::chrono::system_clock::time_point& time_point) {
  return ToStringTimePoint(time_point);
}

} // namespace yb
