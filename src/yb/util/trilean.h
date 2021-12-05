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

#ifndef YB_UTIL_TRILEAN_H_
#define YB_UTIL_TRILEAN_H_

#include <ostream>
#include <string>

namespace yb {

// A "three-value boolean". We need this for some sanity checks.
// https://en.wikipedia.org/wiki/Three-valued_logic
enum class Trilean : uint8_t {
  kFalse = 0,
  kTrue = 1,
  kUnknown = 2
};

std::string TrileanToStr(Trilean value);

inline constexpr Trilean ToTrilean(bool b) noexcept {
  return b ? Trilean::kTrue : Trilean::kFalse;
}

inline std::ostream& operator << (std::ostream& out, Trilean trilean) {
  return out << TrileanToStr(trilean);
}

} // namespace yb

#endif // YB_UTIL_TRILEAN_H_
