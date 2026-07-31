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

#include <glog/logging.h>
#include <unordered_set>
#include <string>
#include <unordered_map>
#include <functional>

#include "yb/util/locks.h"
#include "yb/common/entity_ids_types.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/util/status.h"

namespace yb {
namespace master {
class CatalogManagerIf;
class FlushTablesRequestPB;
class FlushTablesResponsePB;
class IsFlushTablesDoneRequestPB;
class IsFlushTablesDoneResponsePB;
struct LeaderEpoch;
}  // namespace master
namespace rpc {
class RpcContext;
}  // namespace rpc
}  // namespace yb

namespace yb::master {

class Master;

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
                                  const Status& status) EXCLUDES(lock_);

  void HandleFlushTabletsRpcFinish(const FlushRequestId& flush_id,
                                   const TabletServerId& ts_uuid,
                                   const Status& status) EXCLUDES(lock_);

 private:
  void UpdateFlushRequestsUnlocked(const FlushRequestId& flush_id,
                                   const TabletServerId& ts_uuid,
                                   const Status& status) REQUIRES(lock_);


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

} // namespace yb::master
