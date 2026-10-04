//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace yb {
namespace ql {

inline constexpr const char* kRedactedPlaceholder = "<REDACTED>";

// Byte range [begin, end) of the original text that RedactPasswordLiterals replaced with
// kRedactedPlaceholder.
struct RedactedRange {
  size_t begin;
  size_t end;
};

// Returns `operation` with the value of every `password = <string constant>` clause (which covers
// HASHED PASSWORD) replaced by kRedactedPlaceholder, regardless of statement type. If `ranges` is
// not null, it receives the replaced ranges of `operation` in ascending order.
std::string RedactPasswordLiterals(
    const std::string& operation, std::vector<RedactedRange>* ranges = nullptr);

}  // namespace ql
}  // namespace yb
