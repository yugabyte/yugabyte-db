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

#include "yb/util/stol_utils.h"

#include <cstring>

#include "yb/util/string_util.h"

using namespace std::placeholders;

namespace yb {

namespace {

Status StatusForInvalidNumber(Slice input, int err, const char* type_name) {
  auto message = Format("$0 is not a valid number", input.ToDebugString());
  if (*type_name) {
    message += Format(" for type $0", type_name);
  }
  if (err != 0) {
    message += ": ";
    message += std::strerror(err);
  }
  return STATUS(InvalidArgument, message);
}

template <class T>
std::enable_if_t<std::is_integral_v<T>, std::from_chars_result> FromCharsHelper(
    Slice input, T& result, int base = 10) {
  return std::from_chars(input.cdata(), input.cend(), result, base);
}

// TODO All helpers could be removed when libc++ will support std::from_chars for floats
template <class T>
std::enable_if_t<std::is_same_v<T, long double>, std::from_chars_result> FromCharsHelper(
    Slice input, T& result) {
  if (input.empty() || isspace(*input.cdata())) {
    // disable skip of spaces.
    return {input.cdata(), std::errc::invalid_argument};
  }
  auto maybe_special_value = TryParsingNonNumberValue<T>(input);
  if (maybe_special_value) {
    result = *maybe_special_value;
    return {input.cend(), std::errc{}};
  }
  // Ensure input is zero terminated.
  std::string input_copy(input);
  char* end;
  errno = 0;
  result = std::strtold(input_copy.c_str(), &end);
  return std::from_chars_result{
    .ptr = input.cdata() + (end - input_copy.c_str()),
    .ec = static_cast<std::errc>(errno),
  };
}

template <class T, class... Args>
Result<T> CheckedSton(Slice input, Args&&... args) {
  T result;
  auto [ptr, ec] = FromCharsHelper(input, result, std::forward<Args>(args)...);
  if (ec == std::errc{} && ptr == input.cend()) { // NOLINT
    return result;
  }
  return StatusForInvalidNumber(input, std::to_underlying(ec), typeid(T).name());
}

} // namespace

Result<int64_t> CheckedStoll(Slice slice) {
  return CheckedSton<int64_t>(slice);
}

Result<uint64_t> CheckedStoull(Slice slice, int base) {
  return CheckedSton<uint64_t>(slice, base);
}

Result<uint32_t> CheckedStoul(Slice slice, int base) {
  return CheckedSton<uint32_t>(slice, base);
}

Result<int64_t> DoCheckedStol(Slice value, int64_t*) { return CheckedStoll(value); }
Result<uint64_t> DoCheckedStol(Slice value, uint64_t*) { return CheckedStoull(value); }
Result<uint64_t> DoCheckedStol(Slice value, uint32_t*) { return CheckedStoul(value); }

Result<long double> CheckedStold(Slice slice) {
  return CheckedSton<long double>(slice);
}

} // namespace yb
