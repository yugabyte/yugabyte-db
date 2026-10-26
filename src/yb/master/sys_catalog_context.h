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

#include "yb/client/client_fwd.h"

#include "yb/common/common_net.pb.h"
#include "yb/common/entity_ids_types.h"

#include "yb/consensus/metadata.pb.h"

#include "yb/docdb/docdb_compaction_context.h"

#include "yb/gutil/ref_counted.h"

#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/server/server_base.h"
#include "yb/server/server_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/result.h"

namespace yb {

class AutoFlagsManagerBase;
class FsManager;
class MaintenanceManager;
class MemTracker;

namespace master {

// What SysCatalogTable needs from the process hosting it. The C++ master implements this on top of
// Master and CatalogManager; the Rust master's host (src/yb/master_host) implements it without a
// CatalogManager. Everything here except the last two methods is also needed on a follower, so a
// host must provide real implementations of those.
class SysCatalogContext {
 public:
  virtual ~SysCatalogContext() = default;

  virtual rpc::Messenger* Messenger() = 0;
  virtual rpc::ProxyCache& ProxyCache() = 0;
  virtual const std::shared_future<client::YBClient*>& ClientFuture() const = 0;
  virtual FsManager* GetFsManager() = 0;
  virtual server::Clock* GetClock() = 0;
  virtual const std::shared_ptr<MemTracker>& GetMemTracker() const = 0;
  virtual MaintenanceManager* GetMaintenanceManager() = 0;
  virtual const MasterOptions& Options() const = 0;
  virtual CloudInfoPB MakeCloudInfoPB() const = 0;
  virtual Status GetRegistration(ServerRegistrationPB* reg, server::RpcOnly rpc_only) const = 0;

  virtual void SetMasterAddresses(std::shared_ptr<server::MasterAddresses> master_addresses) = 0;
  // Called on every committed sys-catalog config change, on leader and followers.
  virtual Status ResetMemoryState(const consensus::RaftConfigPB& config) = 0;
  virtual Status InformRemovedMaster(const HostPortPB& hp_pb) = 0;

  // Raft-apply hooks of the sys-catalog tablet. They run on every master and change what is on
  // disk, so a host must run the same code as the C++ master.
  virtual tablet::SnapshotCoordinator* SnapshotCoordinator() = 0;
  virtual AutoFlagsManagerBase* AutoFlagsManager() = 0;
  virtual docdb::HistoryCutoff AllowedHistoryCutoff(tablet::RaftGroupMetadata* metadata) = 0;

  // Used only by YSQL catalog reads and lookups on the master leader. Returns null when the table
  // is unknown or the host keeps no table catalog.
  virtual scoped_refptr<TableInfo> GetTableInfo(const TableId& table_id) = 0;
  virtual Result<TableId> GetVersionSpecificCatalogTableId(const TableId& table_id) const = 0;
};

}  // namespace master
}  // namespace yb
