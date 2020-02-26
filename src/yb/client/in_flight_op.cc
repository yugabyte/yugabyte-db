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

#include <string>
#include "yb/client/in_flight_op.h"
#include "yb/client/meta_cache.h"
#include "yb/client/yb_op.h"
#include "yb/gutil/strings/substitute.h"

namespace yb {
namespace client {
namespace internal {

InFlightOp::InFlightOp(std::shared_ptr<YBOperation> yb_op_)
    : yb_op(std::move(yb_op_)) {
}

std::string InFlightOp::ToString() const {
  return strings::Substitute(
      "op[state=$0, yb_op=$1, remote_tablet=$2]", internal::ToString(state), yb_op->ToString(),
      (tablet ? tablet->ToString() : "null"));
}

} // namespace internal
} // namespace client
} // namespace yb
