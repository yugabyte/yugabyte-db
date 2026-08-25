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

#include "yb/common/hybrid_time.h"

#include "yb/util/format.h"
#include "yb/util/status_ec.h"

using namespace std::literals;

namespace yb {

// Attached to the Expired status a tablet leader returns when it rejects a write whose
// ignore_after_hybrid_time (PgsqlWriteRequestPB / WritePB) had already passed.
//
// The point of a distinct code is that the caller must be able to tell a fenced write -- which
// definitively did not take effect -- from the other Expired statuses on the write path, which may
// still have landed (RetryableRequests::Register's "less than min running" / "too old", and
// DeadlineInfo's per-query deadline on reads). Matching Status::IsExpired() alone conflates them.
//
// This lives in common/ rather than consensus/ so that clients which deliberately do not link
// consensus -- notably src/yb/thin_client -- can still decode it. Merely naming the type registers
// the category, so no separate registration is needed.
struct WriteFenceExpiredTag : IntegralErrorTag<uint64_t> {
  // This category id is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{25, "write fence expired"sv};

  // The fence the write carried, i.e. the hybrid time it was not allowed to be applied after.
  static std::string ToMessage(Value value) {
    return Format("Write fence (ignore_after_hybrid_time): $0", HybridTime(value));
  }
};

using WriteFenceExpiredError = StatusErrorCodeImpl<WriteFenceExpiredTag>;

}  // namespace yb
