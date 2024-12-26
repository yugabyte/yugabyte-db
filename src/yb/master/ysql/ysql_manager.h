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

#include "yb/master/ysql/ysql_manager_if.h"
#include "yb/master/ysql/ysql_catalog_config.h"

#include "yb/master/master_admin.pb.h"

namespace yb {

namespace rpc {
class RpcContext;
}  // namespace rpc

namespace master {

class YsqlInitDBAndMajorUpgradeHandler;

class YsqlManager : public YsqlManagerIf {
 public:
  YsqlManager(Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog);

  virtual ~YsqlManager() = default;

  void Clear();

  Status PrepareDefaultSysConfig(const LeaderEpoch& epoch);

  void LoadConfig(scoped_refptr<SysConfigInfo> config);

  void SysCatalogLoaded(const LeaderEpoch& epoch);

  uint64_t GetYsqlCatalogVersion() const;

  Result<uint64_t> IncrementYsqlCatalogVersion(const LeaderEpoch& epoch) override;

  // Starts an asynchronous run of initdb. Errors are handled in the callback. Returns true
  // if started running initdb, false if decided that it is not needed.
  Result<bool> StartRunningInitDbIfNeeded(const LeaderEpoch& epoch);

  YsqlCatalogConfig& GetYsqlCatalogConfig() override { return ysql_catalog_config_; }
  const YsqlCatalogConfig& GetYsqlCatalogConfig() const override { return ysql_catalog_config_; }

  IsOperationDoneResult IsInitDbDone() const;

  Status SetInitDbDone(const LeaderEpoch& epoch);

  bool IsYsqlMajorCatalogUpgradeInProgress() const;

  void HandleNewTableId(const TableId& table_id);

  bool IsTransactionalSysCatalogEnabled() const override;
  Status SetTransactionalSysCatalogEnabled(const LeaderEpoch& epoch) override;

  // Initiates ysql major catalog upgrade which involves global initdb, pg_upgrade, and catalog
  // version fixups.
  // IsYsqlMajorCatalogUpgradeDone must be called to track the completion status of the
  // operation.
  Status StartYsqlMajorCatalogUpgrade(
      const StartYsqlMajorCatalogUpgradeRequestPB* req,
      StartYsqlMajorCatalogUpgradeResponsePB* resp, rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  // Checks if ysql major catalog upgrade has completed.
  Status IsYsqlMajorCatalogUpgradeDone(
      const IsYsqlMajorCatalogUpgradeDoneRequestPB* req,
      IsYsqlMajorCatalogUpgradeDoneResponsePB* resp, rpc::RpcContext* rpc);

  // Returns the prior version's table if we are in the middle of a ysql major upgrade.
  // In all other cases, returns the current version.
  Result<TableId> GetVersionSpecificCatalogTableId(const TableId& current_table_id) const override;

  Status FinalizeYsqlMajorCatalogUpgrade(
      const FinalizeYsqlMajorCatalogUpgradeRequestPB* req,
      FinalizeYsqlMajorCatalogUpgradeResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  // Rolls back the major YSQL catalog to the previous version. Deletes all of the new YSQL
  // version's catalog tables, and resets all upgrade-related state to initial values. Blocks until
  // the rollback is finished or fails.
  // Takes a long time to run. Use a timeout of at least 5 minutes when calling.
  Status RollbackYsqlMajorCatalogVersion(
      const RollbackYsqlMajorCatalogVersionRequestPB* req,
      RollbackYsqlMajorCatalogVersionResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  Status CreateYbAdvisoryLocksTableIfNeeded(const LeaderEpoch& epoch);

  Status ValidateWriteToCatalogTableAllowed(const TableId& table_id, bool is_forced_update) const;

 private:
  Result<bool> StartRunningInitDbIfNeededInternal(const LeaderEpoch& epoch);

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;

  YsqlCatalogConfig ysql_catalog_config_;

  std::unique_ptr<YsqlInitDBAndMajorUpgradeHandler> ysql_initdb_and_major_upgrade_helper_;

  // This is used for tracking that initdb has started running previously.
  std::atomic<bool> pg_proc_exists_{false};

  bool advisory_locks_table_created_ = false;

  DISALLOW_COPY_AND_ASSIGN(YsqlManager);
};

}  // namespace master

}  // namespace yb
