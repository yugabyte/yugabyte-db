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

#include "yb/util/enums.h"
#include "yb/util/tostring.h"
#include "yb/util/status.h"
#include "yb/util/status_ec.h"

using namespace std::literals;

namespace yb::client {

YB_DEFINE_ENUM(
    ClientErrorCode,
    // Special value used to indicate no error of this type.
    (kNone)
    (kTablePartitionListIsStale)
    (kExpiredRequestToBeRetried)
    (kTabletNotYetRunning)
    (kTablePartitionListVersionDoesNotMatch)
    (kMetaCacheInvalidated)
    (kTablePartitionListRefreshed)
);

struct ClientErrorTag : IntegralErrorTag<ClientErrorCode> {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{12, "client error"sv};

  static std::string ToMessage(Value value) {
    return ToString(value);
  }
};

using ClientError = StatusErrorCodeImpl<ClientErrorTag>;

// Returns whether status is a client error that should be retried at YBSession level internally.
// If status is OK, also returns false.
bool IsRetryableClientError(const Status& status);

} // namespace yb::client
