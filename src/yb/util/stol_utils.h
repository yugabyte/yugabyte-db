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
#include "yb/util/status_format.h"

namespace yb {

Result<int64_t> CheckedStoll(Slice slice);
Result<uint64_t> CheckedStoull(Slice slice);
Result<int64_t> DoCheckedStol(Slice value, int64_t*);
Result<uint64_t> DoCheckedStol(Slice value, uint64_t*);

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

Result<long double> CheckedStold(Slice slice);

} // namespace yb
