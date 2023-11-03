//
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
//
#pragma once

#include <stddef.h>

namespace yb {

namespace size_literals {

inline constexpr size_t operator "" _KB(unsigned long long kilobytes) { // NOLINT
  return kilobytes * 1024;
}

inline constexpr size_t operator "" _MB(unsigned long long megabytes) { // NOLINT
  return megabytes * 1024 * 1024;
}

inline constexpr size_t operator "" _GB(unsigned long long gigabytes) { // NOLINT
  return gigabytes * 1024 * 1024 * 1024;
}

} // namespace size_literals

using size_literals::operator"" _KB;
using size_literals::operator"" _MB;
using size_literals::operator"" _GB;

} // namespace yb
