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

#pragma once

#include <future>
#include <memory>
#include <optional>
#include <string>

#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids_types.h"

#include "yb/master/catalog_entity_info.pb.h"

#include "yb/server/clock.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_util_fwd.h"

#include "yb/util/concurrent_value.h"
#include "yb/util/status_callback.h"

namespace yb {

class MemTracker;
class NodeInstancePB;

namespace client {
class YBClient;
}

namespace master {

// What MasterTabletServer and MasterTabletServiceImpl need from the master they run in: the C++
// Master, or the host of a Rust master (src/yb/master_host). They serve the sys catalog tablet to
// YSQL (catalog reads and writes, catalog version checks) and the master's CDC service.
class MasterTabletServerContext {
 public:
  virtual ~MasterTabletServerContext() = default;

  // The master's readiness to serve a Read or Write, held until `hold` is released, as
  // ScopedLeaderSharedLock does.
  struct LeaderGuard {
    Status catalog_status;
    Status leader_status;
    std::shared_ptr<void> hold;

    const Status& first_failed_status() const {
      return catalog_status.ok() ? leader_status : catalog_status;
    }
  };
  virtual LeaderGuard LockLeader() = 0;

  virtual server::Clock* clock() = 0;
  virtual const std::string& permanent_uuid() const = 0;
  virtual const NodeInstancePB& NodeInstance() const = 0;
  virtual const std::shared_ptr<MemTracker>& mem_tracker() const = 0;
  virtual const std::shared_future<client::YBClient*>& client_future() const = 0;
  virtual ConcurrentPointerReference<tserver::TServerSharedData> shared_object() = 0;

  virtual Result<tablet::TabletPeerPtr> GetServingTablet(const Slice& tablet_id) const = 0;
  virtual Result<std::shared_ptr<tablet::AbstractTablet>> GetSystemTablet(TabletIdView id) = 0;

  virtual Status GetYsqlCatalogVersion(
      uint64_t* catalog_version, uint64_t* last_breaking_version, bool use_cache) = 0;
  virtual Status GetYsqlDBCatalogVersion(
      uint32_t db_oid, uint64_t* catalog_version, uint64_t* last_breaking_version,
      bool use_cache) = 0;
  virtual Result<uint64_t> IncrementYsqlCatalogVersion() = 0;
  virtual bool SkipCatalogVersionChecks() = 0;
  virtual Status ValidateWriteToCatalogTableAllowed(
      TableIdView table_id, bool is_forced_update) const = 0;

  // initdb run by this master (master_auto_run_initdb) reads and writes these.
  virtual Status get_ysql_db_oid_to_cat_version_info_map(
      const tserver::GetTserverCatalogVersionInfoRequestPB& req,
      tserver::GetTserverCatalogVersionInfoResponsePB* resp) const = 0;
  virtual Status GetTserverCatalogMessageLists(
      const tserver::GetTserverCatalogMessageListsRequestPB& req,
      tserver::GetTserverCatalogMessageListsResponsePB* resp) const = 0;
  virtual Status SetTserverCatalogMessageList(
      uint32_t db_oid, bool is_breaking_change, uint64_t new_catalog_version,
      const std::optional<std::string>& message_list) = 0;
  virtual void TriggerRelcacheInitConnection(
      const tserver::TriggerRelcacheInitConnectionRequestPB& req, StdStatusCallback callback) = 0;

  virtual Result<SysClusterConfigEntryPB> GetClusterConfig() = 0;
};

}  // namespace master
}  // namespace yb
