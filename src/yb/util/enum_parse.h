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

// Result-returning helpers for converting to/from enum values. These require yb/util/result.h, so
// they live in a separate header from yb/util/enums.h -- the (very widely included) enum-definition
// macros must not pull in Status/Result. Include this header only in translation units that need
// the Result<> overloads below. Streaming an enum in from an std::istream does NOT require this
// header (the bool-returning parser backing operator>> lives in enums.h).

#pragma once

#include <string>
#include <type_traits>

#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {

// Convert from the underlying type to enum value. This is slow as it takes linear time.
template <typename EnumType>
Result<EnumType> UnderlyingToEnumSlow(const typename std::underlying_type<EnumType>::type int_val) {
  for (auto value : List(static_cast<EnumType*>(nullptr))) {
    if (static_cast<typename std::underlying_type<EnumType>::type>(value) == int_val) {
      return value;
    }
  }
  return STATUS_FORMAT(InvalidArgument, "$0 invalid value: $1", GetTypeName<EnumType>(), int_val);
}

// Parses string representation to enum value, returning a descriptive Status on failure.
template <typename EnumType>
Result<EnumType> ParseEnumInsensitive(const char* str) {
  EnumType value{};
  if (internal::TryParseEnumInsensitive(str, &value)) {
    return value;
  }
  return STATUS_FORMAT(InvalidArgument, "$0 invalid value: $1", GetTypeName<EnumType>(), str);
}

template<typename EnumType>
Result<EnumType> ParseEnumInsensitive(const std::string& str) {
  return ParseEnumInsensitive<EnumType>(str.c_str());
}

}  // namespace yb
