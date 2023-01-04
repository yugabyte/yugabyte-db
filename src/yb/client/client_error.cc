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

#include "yb/client/client_error.h"

namespace yb {
namespace client {

const std::string kClientErrorCategoryName = "client error";

StatusCategoryRegisterer client_error_category_registerer(
    StatusCategoryDescription::Make<ClientErrorTag>(&kClientErrorCategoryName));

bool IsRetryableClientError(const Status& s) {
  if (s.ok()) {
    return false;
  }
  switch (ClientError(s).value()) {
    case ClientErrorCode::kNone:
    case ClientErrorCode::kTablePartitionListVersionDoesNotMatch:
      // This error can't be retried at YBClient/YBSession level, because YBClient/YBSession caller
      // used knowledge about table partitions for preparing set of YBOperation instances.
      // However, when the partition list version does not match, it means that the tablet has
      // been split and each of child tablets is only serving the part of the original tablet key
      // range.
      // Upper layers might need to know the updated partition list to rebuild YBOperation instances
      // again to cover desired key range.
      return false;
    case ClientErrorCode::kTablePartitionListIsStale:
    case ClientErrorCode::kTablePartitionListRefreshed:
    case ClientErrorCode::kExpiredRequestToBeRetried:
    case ClientErrorCode::kTabletNotYetRunning:
    case ClientErrorCode::kMetaCacheInvalidated:
      return true;
  }
  FATAL_INVALID_ENUM_VALUE(ClientErrorCode, ClientError(s).value());
}

} // namespace client
} // namespace yb
