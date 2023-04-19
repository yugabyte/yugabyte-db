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

#include "yb/util/status_fwd.h"
#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/status_ec.h"

namespace rocksdb {

YB_DEFINE_ENUM(TimeoutCode, (kMutex)(kLock)(kLockLimit));

struct TimeoutErrorTag : yb::IntegralErrorTag<TimeoutCode> {
  // This category id is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 3;

  static const std::string& ToMessage(Value value);
};

typedef yb::StatusErrorCodeImpl<TimeoutErrorTag> TimeoutError;


} // namespace rocksdb
