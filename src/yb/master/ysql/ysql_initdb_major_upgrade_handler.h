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

#include "yb/util/status_fwd.h"

#include "yb/master/master_fwd.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {
class ThreadPool;

namespace master {
struct LeaderEpoch;

// Helper class to handle global initdb and major version upgrade for YSQL.
// Only one operation can be run at a time.
// TODO: Move IsDone functions from CatalogManager to this class after pg15-upgrade merges into
// master.
class YsqlInitDBAndMajorUpgradeHandler {
 public:
  YsqlInitDBAndMajorUpgradeHandler(
      Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog,
      yb::ThreadPool& thread_pool);

  ~YsqlInitDBAndMajorUpgradeHandler() = default;

  // Starts the global initdb procedure to create the initial universe level ysql sys catalog using
  // the initdb process.
  Status StartNewClusterGlobalInitDB(const LeaderEpoch& epoch);

  // Starts the ysql major version upgrade procedure which will run a major version initdb, and run
  // pg_upgrade using a temporary major version postgres process.
  Status StartYsqlMajorVersionUpgrade(const LeaderEpoch& epoch);

  // Rolls back the operations performed by major version upgrade procedure and return the cluster
  // to a clean state.
  Status RollbackYsqlMajorVersionUpgrade(const LeaderEpoch& epoch);

 private:
  using DbNameToOidList = std::vector<std::pair<std::string, YBCPgOid>>;

  // Executes the given function asynchronously. Ensures that only one function is running at a
  // time.
  Status RunOperationAsync(std::function<void()> func);

  void RunNewClusterGlobalInitDB(const LeaderEpoch& epoch);

  void RunMajorVersionUpgrade(const LeaderEpoch& epoch);

  Status RunMajorVersionCatalogUpgrade(const LeaderEpoch& epoch);

  // Runs the initdb process to create the initial ysql sys catalog and snapshot the sys_catalog if
  // needed.
  // ysql major version upgrade provides db_name_to_oid_list. The clean install initdb process will
  // choose new OIDs.
  Status InitDBAndSnapshotSysCatalog(
      const DbNameToOidList& db_name_to_oid_list, const LeaderEpoch& epoch);

  // Starts a new postgres process to run pg_upgrade to migrate the old catalog to the new version
  // catalog.
  Status PerformPgUpgrade(const LeaderEpoch& epoch);

  Result<DbNameToOidList> GetDbNameToOidListForMajorUpgrade();

  Status PerformMajorUpgrade(const DbNameToOidList& db_name_to_oid_list, const LeaderEpoch& epoch);

  Status MajorVersionCatalogUpgradeFinished(const Status& upgrade_status, const LeaderEpoch& epoch);

  Status RunRollbackMajorVersionUpgrade(const LeaderEpoch& epoch);

  Status ResetNextVerInitdbStatus(const LeaderEpoch& epoch);

  // Get the address to a live tserver process that is closest to the master.
  Result<std::string> GetClosestLiveTserverAddress();

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;
  yb::ThreadPool& thread_pool_;

  // Indicates a global initdb, ysql major catalog upgrade, or ysql major catalog rollback is in
  // progress.
  std::atomic<bool> is_running_{false};

  DISALLOW_COPY_AND_ASSIGN(YsqlInitDBAndMajorUpgradeHandler);
};

}  // namespace master
}  // namespace yb
