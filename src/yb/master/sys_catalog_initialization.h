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

#include <vector>
#include <string>

#include "yb/util/flags.h"

#include "yb/master/master_fwd.h"
#include "yb/master/table_index.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_admin.fwd.h"

#include "yb/util/status_fwd.h"

DECLARE_string(initial_sys_catalog_snapshot_path);
DECLARE_bool(enable_ysql);
DECLARE_bool(create_initial_sys_catalog_snapshot);

namespace yb {
namespace master {

struct LeaderEpoch;

// Used by the catalog manager to prepare an initial sys catalog snapshot.
class InitialSysCatalogSnapshotWriter {
 public:
  InitialSysCatalogSnapshotWriter();
  ~InitialSysCatalogSnapshotWriter();

  // Collect all Raft group metadata changes needed by PostgreSQL tables so we can replay them
  // when creating a new cluster (to avoid running initdb).
  void AddMetadataChange(tablet::ChangeMetadataRequestPB metadata_change);

  Status WriteSnapshot(
      tablet::Tablet* sys_catalog_tablet,
      const std::string& dest_path);

 private:
  std::vector<tablet::ChangeMetadataRequestPB> initdb_metadata_changes_;
};

Status RestoreInitialSysCatalogSnapshot(
    const std::string& initial_snapshot_path,
    tablet::TabletPeer* sys_catalog_tablet_peer,
    int64_t term);

void SetDefaultInitialSysCatalogSnapshotFlags();

// A one-time migration procedure for existing clusters to set is_ysql_catalog_table and
// is_transactional flags to true on YSQL system catalog tables.
Status MakeYsqlSysCatalogTablesTransactional(
    TableIndex::TablesRange tables,
    SysCatalogTable* sys_catalog,
    SysConfigInfo* ysql_catalog_config,
    const LeaderEpoch& epoch);

}  // namespace master
}  // namespace yb
