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

#include <set>
#include <shared_mutex>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "yb/util/flags.h"

#include "yb/gutil/integral_types.h"
#include "yb/gutil/ref_counted.h"

#include "yb/master/leader_epoch.h"
#include "yb/master/master_admin.fwd.h"
#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_context.h"

#include "yb/util/status_fwd.h"
#include "yb/util/enums.h"
#include "yb/util/locks.h"

namespace yb {
namespace master {

class Master;
class CatalogManager;
class TableInfo;

// Handle Flush-related operations.
class FlushManager {
 public:
  explicit FlushManager(Master* master, CatalogManagerIf* catalog_manager)
      : master_(DCHECK_NOTNULL(master)),
        catalog_manager_(DCHECK_NOTNULL(catalog_manager)) {}

  // API to start a table flushing.
  Status FlushTables(const FlushTablesRequestPB* req,
                     FlushTablesResponsePB* resp,
                     rpc::RpcContext* rpc,
                     const LeaderEpoch& epoch);

  Status IsFlushTablesDone(const IsFlushTablesDoneRequestPB* req,
                           IsFlushTablesDoneResponsePB* resp);

  void HandleFlushTabletsResponse(const FlushRequestId& flush_id,
                                  const TabletServerId& ts_uuid,
                                  const Status& status);

 private:
  // Start the background task to send the FlushTablets RPC to the Tablet Server.
  void SendFlushTabletsRequest(const TabletServerId& ts_uuid,
                               const scoped_refptr<TableInfo>& table,
                               const std::vector<TabletId>& tablet_ids,
                               const FlushRequestId& flush_id,
                               bool is_compaction,
                               const LeaderEpoch& epoch);

  void DeleteCompleteFlushRequests();

  Master* master_;
  CatalogManagerIf* catalog_manager_;

  // Lock protecting the various in memory storage structures.
  typedef rw_spinlock LockType;
  mutable LockType lock_;

  typedef std::unordered_set<TabletServerId> TSIdSet;
  struct TSFlushingInfo {
    void clear() {
      ts_flushing_.clear();
      ts_succeed_.clear();
      ts_failed_.clear();
    }

    TSIdSet ts_flushing_;
    TSIdSet ts_succeed_;
    TSIdSet ts_failed_;
  };

  // Map of flushing requests: flush_request-id -> current per TS info.
  typedef std::unordered_map<FlushRequestId, TSFlushingInfo> FlushRequestMap;
  FlushRequestMap flush_requests_;

  DISALLOW_COPY_AND_ASSIGN(FlushManager);
};

} // namespace master
} // namespace yb
