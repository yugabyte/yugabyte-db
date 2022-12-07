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

#include <float.h>
#include <stdint.h>

#include <chrono>
#include <sstream>
#include <string>
#include <type_traits>

#include <boost/functional/hash/hash.hpp>
#include <boost/mpl/and.hpp>
#include <boost/optional/optional.hpp>

#include "yb/util/format.h"
#include "yb/util/status_ec.h"
#include "yb/util/strongly_typed_uuid.h"
#include "yb/util/tostring.h"
#include "yb/util/type_traits.h"

namespace yb {

YB_STRONGLY_TYPED_UUID_DECL(ClientId);
typedef int64_t RetryableRequestId;

struct MinRunningRequestIdTag : IntegralErrorTag<int64_t> {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 13;

  static std::string ToMessage(Value value) {
    return Format("Min running request ID: $0", value);
  }
};

using MinRunningRequestIdStatusData = StatusErrorCodeImpl<MinRunningRequestIdTag>;

}  // namespace yb
