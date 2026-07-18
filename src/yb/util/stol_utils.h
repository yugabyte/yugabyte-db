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

#pragma once

#include <charconv>
#include <cmath>
#include <optional>

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/strip.h"

#include "yb/util/concepts.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"

namespace yb {

Result<int64_t> CheckedStoll(Slice slice);
Result<uint64_t> CheckedStoull(Slice slice, int base = 10);
Result<uint32_t> CheckedStoul(Slice slice, int base = 10);

Result<int64_t> DoCheckedStol(Slice value, int64_t*);
Result<uint64_t> DoCheckedStol(Slice value, uint64_t*);
Result<uint64_t> DoCheckedStol(Slice value, uint32_t*);

template <class T>
Result<T> CheckedStol(Slice value) {
  return DoCheckedStol(value, static_cast<T*>(nullptr));
}

template <class Int>
Result<Int> CheckedStoInt(Slice slice) {
  auto long_value = CheckedStoll(slice);
  RETURN_NOT_OK(long_value);
  auto result = static_cast<Int>(*long_value);
  if (result != *long_value) {
    return STATUS_FORMAT(InvalidArgument,
                         "$0 is out of range: [$1; $2]",
                         std::numeric_limits<Int>::min(),
                         std::numeric_limits<Int>::max());
  }
  return result;
}

inline Result<int32_t> CheckedStoi(Slice slice) {
  return CheckedStoInt<int32_t>(slice);
}

inline Result<uint32_t> CheckedStoui(Slice slice) {
  return CheckedStoInt<uint32_t>(slice);
}

Result<long double> CheckedStold(Slice slice);

// Concept for integral types that can be parsed
template<typename T>
concept ParseableIntegral = std::is_integral_v<T>;

// Concept for floating-point types that can be parsed
template<typename T>
concept ParseableFloatingPoint = std::is_floating_point_v<T>;

// Concept for all parseable number types
template<typename T>
concept ParseableNumber = ParseableIntegral<T> || ParseableFloatingPoint<T>;

template<ParseableFloatingPoint T>
std::optional<T> TryParsingNonNumberValue(Slice slice) {
  if constexpr (std::is_floating_point_v<T>) {
    if (EqualsIgnoreCase(slice, "+inf") ||
        EqualsIgnoreCase(slice, "inf")) {
      return std::numeric_limits<T>::infinity();
    }
    if (EqualsIgnoreCase(slice, "-inf")) {
      return -std::numeric_limits<T>::infinity();
    }
    if (EqualsIgnoreCase(slice, "nan")) {
      return std::numeric_limits<T>::quiet_NaN();
    }
  }
  return std::nullopt;
}

template<ParseableNumber T>
Result<T> CheckedParseNumber(Slice slice) {
  if constexpr (std::is_floating_point_v<T>) {
    auto maybe_special_value = TryParsingNonNumberValue<T>(slice);
    if (maybe_special_value) {
      return *maybe_special_value;
    }
  }
  if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>) {
    return CheckedStoInt<T>(slice);
  } else if constexpr (std::is_same_v<T, int64_t>) {
    return CheckedStoll(slice);
  } else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, size_t>) { // NOLINT
    static_assert(sizeof(uint64_t) == sizeof(size_t),
                  "Assuming size_t is the same as uint64_t");
    return CheckedStoull(slice);
  } else if constexpr (std::is_same_v<T, long double>) {
    // For double, use CheckedStold directly
    return CheckedStold(slice);
  } else if constexpr (std::is_floating_point_v<T>) { // NOLINT
    // For float and double, parse as double and cast
    auto long_double_value = VERIFY_RESULT(CheckedStold(slice));
    T result = static_cast<T>(long_double_value);
    // We allow infinite/NaN values, unless they become such after an overflow
    if (std::isfinite(long_double_value) && !std::isfinite(result)) {
      return STATUS_FORMAT(InvalidArgument,
                           "Value '$0' is out of range for type '$1'",
                           slice.ToString(), typeid(T).name());
    }
    return result;
  } else {
    static_assert(sizeof(T) == 0, "Trying to parse a number of an unsupported type");
  }
}

// Generalized function to parse comma-separated lists of numbers of a supported type into an
// arbitrary container and enforce inclusive lower and upper bounds.
template<ParseableNumber T, ContainerOf<T> Container = std::vector<T>>
Result<Container> ParseCommaSeparatedListOfNumbers(
    const std::string& input,
    std::optional<T> lower_bound = std::nullopt,
    std::optional<T> upper_bound = std::nullopt) {

  Container result;
  auto split_strings = StringSplit(input, ',');

  for (const auto& str_raw : split_strings) {
    auto str = str_raw;
    StripWhiteSpace(&str);
    if (str.empty()) {
      continue;
    }
    auto value = VERIFY_RESULT(CheckedParseNumber<T>(str));
    if (lower_bound && value < *lower_bound) {
      return STATUS_FORMAT(
          InvalidArgument, "Value $0 is less than lower bound $1", value, *lower_bound);
    }
    if (upper_bound && value > *upper_bound) {
      return STATUS_FORMAT(
          InvalidArgument, "Value $0 is greater than upper bound $1", value, *upper_bound);
    }
    InsertIntoContainer(result, value);
  }

  return result;
}

} // namespace yb
