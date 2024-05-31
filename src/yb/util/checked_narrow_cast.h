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

#include "yb/util/result.h"

namespace yb {

template <class Out, class In>
Result<Out> checked_narrow_cast(const In& in) {
  static_assert(sizeof(Out) < sizeof(In), "Wrong narrow cast");
  if (in > static_cast<In>(std::numeric_limits<Out>::max())) {
    return STATUS_FORMAT(
        Corruption, "Bad narrow cast $0 > $1", in, std::numeric_limits<Out>::max());
  }
  if (std::is_signed<In>::value && in < static_cast<In>(std::numeric_limits<Out>::min())) {
    return STATUS_FORMAT(
        Corruption, "Bad narrow cast $0 < $1", in, std::numeric_limits<Out>::min());
  }
  return static_cast<Out>(in);
}

} // namespace yb
