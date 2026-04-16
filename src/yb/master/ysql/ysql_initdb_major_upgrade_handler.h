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

#include "yb/master/master_admin.pb.h"
#include "yb/util/status_fwd.h"

#include "yb/master/master_fwd.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {

class IsOperationDoneResult;
class ThreadPool;
class VersionInfoPB;

namespace master {
struct LeaderEpoch;
class YsqlCatalogConfig;

// Helper class to handle global initdb and major version upgrade for YSQL.
// Only one operation can be run at a time.
class YsqlInitDBAndMajorUpgradeHandler {
 public:
  YsqlInitDBAndMajorUpgradeHandler(
      Master& master, YsqlCatalogConfig& ysql_catalog_config, CatalogManager& catalog_manager,
      SysCatalogTable& sys_catalog, yb::ThreadPool& thread_pool);

  ~YsqlInitDBAndMajorUpgradeHandler() = default;

  void Load(scoped_refptr<SysConfigInfo> config);

  void SysCatalogLoaded(const LeaderEpoch& epoch);

  // Starts the global initdb procedure to create the initial universe level ysql sys catalog using
  // the initdb process.
  Status StartNewClusterGlobalInitDB(const LeaderEpoch& epoch);

  // Starts the ysql major catalog upgrade procedure which will run a major version initdb, and run
  // pg_upgrade using a temporary major version postgres process.
  Status StartYsqlMajorCatalogUpgrade(const LeaderEpoch& epoch);

  IsOperationDoneResult IsYsqlMajorCatalogUpgradeDone() const;

  Status FinalizeYsqlMajorCatalogUpgrade(const LeaderEpoch& epoch);

  // Rolls back the operations performed by major version upgrade procedure and return the cluster
  // to a clean state.
  Status RollbackYsqlMajorCatalogVersion(const LeaderEpoch& epoch);

  // Are we in a ysql major upgrade?
  // The upgrade is considered to have started when the yb-master leader has upgraded to a new major
  // catalog version.
  // The upgrade is completed after it has been finalized.
  bool IsMajorUpgradeInProgress() const { return ysql_major_upgrade_in_progress_; }

  Result<YsqlMajorCatalogUpgradeState> GetYsqlMajorCatalogUpgradeState() const;

  // Are we allowed to perform updates to the ysql catalog?
  // True for the current version if the ysql major upgrade completed.
  // The upgrade is considered to have started when the yb-master leader has upgraded to a new major
  // catalog version.
  // The upgrade is completed after it has been finalized.
  // During the upgrade only is_forced_update operations are allowed.
  bool IsWriteToCatalogTableAllowed(TableIdView table_id, bool is_forced_update) const;

  // Delete the previous ysql major version catalog after the upgrade to the new version has
  // completed.
  Status CleanupPreviousYsqlMajorCatalog(const LeaderEpoch& epoch);
  void ScheduleCleanupPreviousYsqlMajorCatalog(const LeaderEpoch& epoch);

  Status ValidateTServerVersion(const VersionInfoPB& version) const;

 private:
  using DbNameToOidList = std::vector<std::pair<std::string, YbcPgOid>>;

  // Executes the given function asynchronously. Ensures that only one function is running at a
  // time.
  Status RunOperationAsync(std::function<void()> func);

  void RunNewClusterGlobalInitDB(const LeaderEpoch& epoch);

  void RunMajorVersionUpgrade(const LeaderEpoch& epoch);

  Status RunMajorVersionUpgradeImpl(const LeaderEpoch& epoch);

  Status UpdateCatalogVersions(const LeaderEpoch& epoch);

  Status RunMajorVersionCatalogUpgrade(const LeaderEpoch& epoch);

  // Runs the initdb process to create the initial ysql sys catalog and snapshot the sys_catalog if
  // needed.
  // ysql major catalog upgrade provides db_name_to_oid_list. The clean install initdb process will
  // choose new OIDs.
  Status InitDBAndSnapshotSysCatalog(
      const DbNameToOidList& db_name_to_oid_list, bool is_major_upgrade, const LeaderEpoch& epoch);

  // Starts a new postgres process to run pg_upgrade to migrate the old catalog to the new version
  // catalog.
  Status PerformPgUpgrade(const LeaderEpoch& epoch);

  Result<DbNameToOidList> GetDbNameToOidListForMajorUpgrade();

  Status PerformMajorUpgrade(const DbNameToOidList& db_name_to_oid_list, const LeaderEpoch& epoch);

  Status RunRollbackMajorVersionUpgrade(const LeaderEpoch& epoch);

  Status RollbackMajorVersionCatalogImpl(const LeaderEpoch& epoch);

  // Get the address to a live tserver process that is closest to the master.
  Result<std::string> GetClosestLiveTserverAddress();

  // Transition the ysql major catalog upgrade to a new state if allowed.
  // failed_status must be set to a NonOk status if and only if transitioning to FAILED state.
  // Check kAllowedTransitions for list of allowed transitions.
  Status TransitionMajorCatalogUpgradeState(
      const YsqlMajorCatalogUpgradeInfoPB::State new_state, const LeaderEpoch& epoch,
      const Status& failed_status = Status::OK());

  Master& master_;
  YsqlCatalogConfig& ysql_catalog_config_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;
  yb::ThreadPool& thread_pool_;

  // Indicates a global initdb, ysql major catalog upgrade, or ysql major catalog rollback is in
  // progress.
  std::atomic<bool> is_running_{false};

  std::atomic<bool> restarted_during_major_upgrade_ = false;
  std::atomic<bool> ysql_major_upgrade_in_progress_ = false;

  DISALLOW_COPY_AND_ASSIGN(YsqlInitDBAndMajorUpgradeHandler);
};

}  // namespace master
}  // namespace yb
