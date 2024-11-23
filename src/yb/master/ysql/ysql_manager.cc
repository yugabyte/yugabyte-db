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

#include "yb/master/ysql/ysql_manager.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/ysql/ysql_initdb_major_upgrade_handler.h"
#include "yb/util/is_operation_done_result.h"

// TODO (mbautin, 2019-12): switch the default to true after updating all external callers
// (yb-ctl, YugaWare) and unit tests.
DEFINE_RUNTIME_bool(master_auto_run_initdb, false,
    "Automatically run initdb on master leader initialization");

namespace yb::master {

YsqlManager::YsqlManager(
    Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog)
    : master_(master),
      catalog_manager_(catalog_manager),
      sys_catalog_(sys_catalog),
      ysql_catalog_config_(sys_catalog) {
  ysql_initdb_and_major_upgrade_helper_ = std::make_unique<YsqlInitDBAndMajorUpgradeHandler>(
      master, ysql_catalog_config_, catalog_manager, sys_catalog, *catalog_manager.AsyncTaskPool());
}

void YsqlManager::Clear() { ysql_catalog_config_.Reset(); }

Status YsqlManager::PrepareDefaultSysConfig(const LeaderEpoch& epoch) {
  return ysql_catalog_config_.PrepareDefaultIfNeeded(epoch);
}

void YsqlManager::LoadConfig(scoped_refptr<SysConfigInfo> config) {
  return ysql_catalog_config_.SetConfig(config);
}

void YsqlManager::SysCatalogLoaded(const LeaderEpoch& epoch) {
  ysql_initdb_and_major_upgrade_helper_->SysCatalogLoaded(epoch);
}

Result<bool> YsqlManager::StartRunningInitDbIfNeeded(const LeaderEpoch& epoch) {
  if (IsInitDbDone()) {
    LOG(INFO) << "Cluster configuration indicates that initdb has already completed";
    return false;
  }

  if (pg_proc_exists_.load(std::memory_order_acquire)) {
    LOG(INFO) << "Table pg_proc exists, assuming initdb has already been run";
    // Mark initdb as done, in case it was done externally.
    // We assume pg_proc table means initdb is done.
    // We do NOT handle the case when initdb was terminated mid-run (neither here nor in
    // MakeYsqlSysCatalogTablesTransactional).
    RETURN_NOT_OK(SetInitDbDone(epoch));
    return false;
  }

  if (!FLAGS_master_auto_run_initdb) {
    LOG(INFO) << "--master_auto_run_initdb is set to false, not running initdb";
    return false;
  }

  LOG(INFO) << "initdb has never been run on this cluster, running it";

  RETURN_NOT_OK(ysql_initdb_and_major_upgrade_helper_->StartNewClusterGlobalInitDB(epoch));

  LOG(INFO) << "Successfully started initdb";
  return true;
}

bool YsqlManager::IsTransactionalSysCatalogEnabled() const {
  return ysql_catalog_config_.IsTransactionalSysCatalogEnabled();
}

Status YsqlManager::SetTransactionalSysCatalogEnabled(const LeaderEpoch& epoch) {
  return ysql_catalog_config_.SetTransactionalSysCatalogEnabled(epoch);
}

IsOperationDoneResult YsqlManager::IsInitDbDone() const {
  return ysql_catalog_config_.IsInitDbDone();
}

Status YsqlManager::SetInitDbDone(const LeaderEpoch& epoch) {
  return ysql_catalog_config_.SetInitDbDone(Status::OK(), epoch);
}

bool YsqlManager::IsYsqlMajorCatalogUpgradeInProgress() const {
  return !ysql_catalog_config_.IsYsqlMajorCatalogUpgradeDone().done();
}

uint64_t YsqlManager::GetYsqlCatalogVersion() const { return ysql_catalog_config_.GetVersion(); }

Result<uint64_t> YsqlManager::IncrementYsqlCatalogVersion(const LeaderEpoch& epoch) {
  return ysql_catalog_config_.IncrementVersion(epoch);
}

void YsqlManager::HandleNewTableId(const TableId& table_id) {
  if (table_id == kPgProcTableId) {
    // Needed to track whether initdb has started running.
    pg_proc_exists_.store(true, std::memory_order_release);
  }
}

Status YsqlManager::StartYsqlMajorCatalogUpgrade(
    const StartYsqlMajorCatalogUpgradeRequestPB* req, StartYsqlMajorCatalogUpgradeResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG(INFO) << "Running ysql major catalog upgrade";
  RETURN_NOT_OK(ysql_initdb_and_major_upgrade_helper_->StartYsqlMajorCatalogUpgrade(epoch));

  return Status::OK();
}

Status YsqlManager::IsYsqlMajorCatalogUpgradeDone(
    const IsYsqlMajorCatalogUpgradeDoneRequestPB* req,
    IsYsqlMajorCatalogUpgradeDoneResponsePB* resp, rpc::RpcContext* rpc) {
  LOG(INFO) << "Checking if ysql major catalog upgrade is done";
  auto is_operation_done = ysql_initdb_and_major_upgrade_helper_->IsYsqlMajorCatalogUpgradeDone();
  if (is_operation_done.done()) {
    resp->set_done(true);
    if (!is_operation_done.status().ok()) {
      return is_operation_done.status();
    }
  } else {
    resp->set_done(false);
  }
  return Status::OK();
}

Status YsqlManager::FinalizeYsqlMajorCatalogUpgrade(
    const FinalizeYsqlMajorCatalogUpgradeRequestPB* req,
    FinalizeYsqlMajorCatalogUpgradeResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG(INFO) << "Finalizing ysql major catalog upgrade";
  return ysql_initdb_and_major_upgrade_helper_->FinalizeYsqlMajorCatalogUpgrade(epoch);
}

// Note that this function should be able to be called any number of times while in upgrade mode.
Status YsqlManager::RollbackYsqlMajorCatalogVersion(
    const RollbackYsqlMajorCatalogVersionRequestPB* req,
    RollbackYsqlMajorCatalogVersionResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG(INFO) << "YSQL major catalog upgrade rollback initiated";

  RETURN_NOT_OK(ysql_initdb_and_major_upgrade_helper_->RollbackYsqlMajorCatalogVersion(epoch));

  LOG(INFO) << "YSQL major catalog upgrade rollback completed";
  return Status::OK();
}

}  // namespace yb::master
