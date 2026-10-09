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
// yb::client::TransactionStatusTablets - description of transaction-status-tablet locations
// returned by YBClient::GetTransactionStatusTablets().
//
// Defined in its own header (rather than inline in yb/client/client.h) because the nested
// TablespaceInfo struct holds a PlacementInfoPB by value, which forces yb/common/common_net.pb.h
// into the consumer's translation unit. client.h is included by 400+ TUs, so the transitive cost
// is substantial. Callers that only refer to TransactionStatusTablets by pointer or reference
// (e.g. function return types) can rely on a forward declaration in client.h; only consumers
// that read/construct the struct need to include this header directly.

#pragma once

#include <unordered_map>
#include <vector>

#include "yb/common/common_net.pb.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/pg_types.h"

namespace yb::client {

struct TransactionStatusTablets {
  std::vector<TabletId> global_tablets;
  std::vector<TabletId> region_local_tablets;
  struct TablespaceInfo {
    PlacementInfoPB placement_info;
    std::vector<TabletId> tablets;
  };
  std::unordered_map<PgOid, TablespaceInfo> tablespaces;
};

}  // namespace yb::client
