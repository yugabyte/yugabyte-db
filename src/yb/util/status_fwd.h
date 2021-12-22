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

#ifndef YB_UTIL_STATUS_FWD_H
#define YB_UTIL_STATUS_FWD_H

#include "yb/gutil/port.h"

namespace yb {

class Status;

#define CHECKED_STATUS MUST_USE_RESULT ::yb::Status

#ifdef __clang__
#define NODISCARD_CLASS [[nodiscard]] // NOLINT
#else
#define NODISCARD_CLASS // NOLINT
#endif

template<class TValue>
class NODISCARD_CLASS Result;

}  // namespace yb

#endif  // YB_UTIL_STATUS_FWD_H
