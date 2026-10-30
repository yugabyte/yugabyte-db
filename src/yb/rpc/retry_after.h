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

#include <string>
#include <string_view>

#include "yb/util/monotime.h"
#include "yb/util/status_ec.h"

using namespace std::literals;

namespace yb::rpc {

// How long a service wants the client to wait before retrying the call it is failing. A service
// attaches this to the status it rejects with, and RespondFailure copies it into ErrorStatusPB.
//
// It says nothing about which peer to retry. A rejection that carries it still travels as whatever
// ErrorStatusPB code the service chose, so the client's choice of peer is unchanged.
class RetryAfterTraits {
 public:
  using ValueType = MonoDelta;
  using RepresentationType = int64_t;

  static MonoDelta FromRepresentation(RepresentationType source) {
    return MonoDelta::FromNanoseconds(source);
  }

  static RepresentationType ToRepresentation(MonoDelta value) {
    return value.ToNanoseconds();
  }

  static std::string ToString(MonoDelta value) {
    return value.ToString();
  }
};

struct RetryAfterTag : IntegralBackedErrorTag<RetryAfterTraits> {
  // This category id is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{25, "retry after"sv};

  static std::string ToMessage(MonoDelta value) {
    return value.ToString();
  }
};

using RetryAfter = StatusErrorCodeImpl<RetryAfterTag>;

}  // namespace yb::rpc
