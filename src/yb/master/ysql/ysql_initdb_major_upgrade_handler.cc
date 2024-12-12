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

#include "yb/master/ysql/ysql_initdb_major_upgrade_handler.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master.h"
#include "yb/master/ysql/ysql_catalog_config.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/util/async_util.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/util/net/net_util.h"
#include "yb/util/pg_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_string(tmp_dir);
DECLARE_bool(master_join_existing_universe);
DECLARE_string(rpc_bind_addresses);

DEFINE_RUNTIME_uint32(ysql_upgrade_postgres_port, 5434,
  "Port used to start the postgres process for ysql upgrade");

using yb::pgwrapper::PgWrapper;

namespace yb::master {

YsqlInitDBAndMajorUpgradeHandler::YsqlInitDBAndMajorUpgradeHandler(
    Master& master, YsqlCatalogConfig& ysql_catalog_config, CatalogManager& catalog_manager,
    SysCatalogTable& sys_catalog, yb::ThreadPool& thread_pool)
    : master_(master),
      ysql_catalog_config_(ysql_catalog_config),
      catalog_manager_(catalog_manager),
      sys_catalog_(sys_catalog),
      thread_pool_(thread_pool) {}

Status YsqlInitDBAndMajorUpgradeHandler::StartNewClusterGlobalInitDB(const LeaderEpoch& epoch) {
  SCHECK(
      !FLAGS_master_join_existing_universe, IllegalState,
      "Master is joining an existing universe but wants to run initdb. "
      "This should have been done during initial universe creation.");

  return RunOperationAsync([this, epoch]() { RunNewClusterGlobalInitDB(epoch); });
}

Status YsqlInitDBAndMajorUpgradeHandler::StartYsqlMajorCatalogUpgrade(const LeaderEpoch& epoch) {
  RETURN_NOT_OK(ysql_catalog_config_.TransitionMajorCatalogUpgradeState(
      YsqlMajorCatalogUpgradeInfoPB::PERFORMING_INIT_DB, epoch));

  auto status = RunOperationAsync([this, epoch]() { RunMajorVersionUpgrade(epoch); });
  if (!status.ok()) {
    ERROR_NOT_OK(
        ysql_catalog_config_.TransitionMajorCatalogUpgradeState(
            YsqlMajorCatalogUpgradeInfoPB::FAILED, epoch, status),
        "Failed to set major version upgrade state");
  }

  return status;
}

IsOperationDoneResult YsqlInitDBAndMajorUpgradeHandler::IsYsqlMajorCatalogUpgradeDone() const {
  return ysql_catalog_config_.IsYsqlMajorCatalogUpgradeDone();
}

Status YsqlInitDBAndMajorUpgradeHandler::FinalizeYsqlMajorCatalogUpgrade(const LeaderEpoch& epoch) {
  return ysql_catalog_config_.TransitionMajorCatalogUpgradeState(
      YsqlMajorCatalogUpgradeInfoPB::DONE, epoch);
}

Status YsqlInitDBAndMajorUpgradeHandler::RollbackYsqlMajorCatalogVersion(const LeaderEpoch& epoch) {
  // Since Rollback is synchronous, we can perform the state transitions inside the async function.
  // It also ensures there are no inflight operations when the rollback state transition occurs.
  Synchronizer sync;
  auto cb = sync.AsStdStatusCallback();

  auto status = RunOperationAsync(
      [this, epoch, &cb]() mutable { cb(RunRollbackMajorVersionUpgrade(epoch)); });
  if (!status.ok()) {
    // Synchronizer callback must be called.
    cb(status);
  }

  return sync.Wait();
}

bool YsqlInitDBAndMajorUpgradeHandler::IsYsqlMajorCatalogUpgradeInProgress() const {
  return !IsYsqlMajorCatalogUpgradeDone().done();
}

Status YsqlInitDBAndMajorUpgradeHandler::RunOperationAsync(std::function<void()> func) {
  bool expected = false;
  if (!is_running_.compare_exchange_strong(expected, true)) {
    return STATUS(
        IllegalState,
        "Global initdb or ysql major catalog upgrade/rollback is already in progress");
  }

  auto status = thread_pool_.SubmitFunc([this, func = std::move(func)]() mutable {
    func();
    is_running_ = false;
  });

  if (!status.ok()) {
    is_running_ = false;
  }

  return status;
}

void YsqlInitDBAndMajorUpgradeHandler::RunNewClusterGlobalInitDB(const LeaderEpoch& epoch) {
  auto status =
      InitDBAndSnapshotSysCatalog(/*db_name_to_oid_list=*/{}, /*is_major_upgrade=*/false, epoch);
  ERROR_NOT_OK(
      ysql_catalog_config_.SetInitDbDone(status, epoch),
      "Failed to set global initdb as finished in sys catalog");
}

Status YsqlInitDBAndMajorUpgradeHandler::InitDBAndSnapshotSysCatalog(
    const DbNameToOidList& db_name_to_oid_list, bool is_major_upgrade, const LeaderEpoch& epoch) {
  InitialSysCatalogSnapshotWriter* snapshot_writer = nullptr;
  if (FLAGS_create_initial_sys_catalog_snapshot) {
    snapshot_writer = &catalog_manager_.AllocateAndGetInitialSysCatalogSnapshotWriter();
  }

  const auto& master_opts = master_.opts();
  const auto master_addresses_str =
      server::MasterAddressesToString(*master_opts.GetMasterAddresses());

  RETURN_NOT_OK(PgWrapper::InitDbForYSQL(
      master_addresses_str, FLAGS_tmp_dir, master_.GetSharedMemoryFd(), db_name_to_oid_list,
      is_major_upgrade));

  if (!snapshot_writer) {
    return Status::OK();
  }

  auto sys_catalog_tablet = VERIFY_RESULT(sys_catalog_.tablet_peer()->shared_tablet_safe());
  RETURN_NOT_OK(snapshot_writer->WriteSnapshot(
      sys_catalog_tablet.get(), FLAGS_initial_sys_catalog_snapshot_path));

  return Status::OK();
}

void YsqlInitDBAndMajorUpgradeHandler::RunMajorVersionUpgrade(const LeaderEpoch& epoch) {
  auto status = RunMajorVersionUpgradeImpl(epoch);
  if (status.ok()) {
    auto update_state_status = ysql_catalog_config_.TransitionMajorCatalogUpgradeState(
        YsqlMajorCatalogUpgradeInfoPB::MONITORING, epoch);
    if (update_state_status.ok()) {
      LOG(INFO) << "Ysql major catalog upgrade completed successfully";
    } else {
      LOG(ERROR) << "Failed to set major version upgrade state: " << update_state_status;
    }
    return;
  }

  LOG(ERROR) << "Ysql major catalog upgrade failed: " << status;
  ERROR_NOT_OK(
      ysql_catalog_config_.TransitionMajorCatalogUpgradeState(
          YsqlMajorCatalogUpgradeInfoPB::FAILED, epoch, status),
      "Failed to set major version upgrade state");
}

Status YsqlInitDBAndMajorUpgradeHandler::RunMajorVersionUpgradeImpl(const LeaderEpoch& epoch) {
  RETURN_NOT_OK(RunMajorVersionCatalogUpgrade(epoch));
  RETURN_NOT_OK_PREPEND(UpdateCatalogVersions(epoch), "Failed to update catalog versions");

  return Status::OK();
}

// pg_upgrade does not migrate the catalog version table, so we have to explicitly copy the contents
// of the pre-existing catalog table to the current version's catalog version table.
Status YsqlInitDBAndMajorUpgradeHandler::UpdateCatalogVersions(const LeaderEpoch& epoch) {
  RETURN_NOT_OK(sys_catalog_.DeleteAllYsqlCatalogTableRows({kPgYbCatalogVersionTableId},
                                                           epoch.leader_term));
  RETURN_NOT_OK(sys_catalog_.CopyPgsqlTables({kPgYbCatalogVersionTableIdPriorVersion},
                                             {kPgYbCatalogVersionTableId}, epoch.leader_term));
  return Status::OK();
}

Status YsqlInitDBAndMajorUpgradeHandler::RunMajorVersionCatalogUpgrade(const LeaderEpoch& epoch) {
  auto db_name_to_oid_list = VERIFY_RESULT(GetDbNameToOidListForMajorUpgrade());
  RETURN_NOT_OK_PREPEND(
      InitDBAndSnapshotSysCatalog(db_name_to_oid_list, /*is_major_upgrade=*/true, epoch),
      "Failed to run initdb");

  RETURN_NOT_OK(ysql_catalog_config_.TransitionMajorCatalogUpgradeState(
      YsqlMajorCatalogUpgradeInfoPB::PERFORMING_PG_UPGRADE, epoch));

  RETURN_NOT_OK_PREPEND(PerformPgUpgrade(epoch), "Failed to run pg_upgrade");

  return Status::OK();
}

Result<YsqlInitDBAndMajorUpgradeHandler::DbNameToOidList>
YsqlInitDBAndMajorUpgradeHandler::GetDbNameToOidListForMajorUpgrade() {
  DbNameToOidList db_name_to_oid_list;
  // Store DB name to OID mapping for all system databases except template1. This mapping will be
  // passed to initdb so that the system database OIDs will match. The template1 database is
  // special because it's created by the bootstrap phase of initdb (see file comment for initdb.c
  // for more details). The template1 database always has OID 1.
  {
    std::vector<scoped_refptr<NamespaceInfo>> all_namespaces;
    catalog_manager_.GetAllNamespaces(&all_namespaces);
    for (const auto& ns_info : all_namespaces) {
      if (ns_info->database_type() != YQL_DATABASE_PGSQL) {
        continue;
      }
      uint32_t oid = VERIFY_RESULT(GetPgsqlDatabaseOid(ns_info->id()));
      if (oid < kPgFirstNormalObjectId && oid != kTemplate1Oid) {
        db_name_to_oid_list.push_back({ns_info->name(), oid});
      }
    }
  }
  return db_name_to_oid_list;
}

Status YsqlInitDBAndMajorUpgradeHandler::PerformPgUpgrade(const LeaderEpoch& epoch) {
  const auto& master_opts = master_.opts();

  // Run local initdb to prepare the node for starting postgres.
  auto pg_conf = VERIFY_RESULT(pgwrapper::PgProcessConf::CreateValidateAndRunInitDb(
      FLAGS_rpc_bind_addresses, master_opts.fs_opts.data_paths.front() + "/pg_data",
      master_.GetSharedMemoryFd()));

  pg_conf.master_addresses = master_opts.master_addresses_flag;
  pg_conf.pg_port = FLAGS_ysql_upgrade_postgres_port;
  pg_conf.run_in_binary_upgrade = true;
  RETURN_NOT_OK(pg_conf.SetSslConf(master_.options(), *master_.fs_manager()));

  LOG(INFO) << "Starting PostgreSQL server listening on " << pg_conf.listen_addresses << ", port "
            << pg_conf.pg_port;

  pgwrapper::PgSupervisor pg_supervisor(pg_conf, nullptr);
  auto se = ScopeExit([&pg_supervisor]() { pg_supervisor.Stop(); });
  RETURN_NOT_OK(pg_supervisor.Start());

  PgWrapper::PgUpgradeParams pg_upgrade_params;
  pg_upgrade_params.data_dir = pg_conf.data_dir;
  pg_upgrade_params.new_version_socket_dir =
      PgDeriveSocketDir(HostPort(pg_conf.listen_addresses, pg_conf.pg_port));
  pg_upgrade_params.new_version_pg_port = pg_conf.pg_port;

  bool local_ts = false;
  auto closest_ts = VERIFY_RESULT(master_.catalog_manager()->GetClosestLiveTserver(&local_ts));
  auto closest_ts_hp = HostPort(
      VERIFY_RESULT(closest_ts->GetHostPort()).host(),
      narrow_cast<uint16_t>(closest_ts->GetRegistration().pg_port()));

  if (local_ts) {
    pg_upgrade_params.old_version_socket_dir = PgDeriveSocketDir(closest_ts_hp);
  } else {
    pg_upgrade_params.old_version_pg_address = closest_ts_hp.host();
  }
  pg_upgrade_params.old_version_pg_port = closest_ts_hp.port();

  RETURN_NOT_OK(PgWrapper::RunPgUpgrade(pg_upgrade_params));

  return Status::OK();
}

Status YsqlInitDBAndMajorUpgradeHandler::RunRollbackMajorVersionUpgrade(const LeaderEpoch& epoch) {
  if (ysql_catalog_config_.GetMajorCatalogUpgradeState() == YsqlMajorCatalogUpgradeInfoPB::DONE) {
    LOG_WITH_FUNC(INFO)
        << "No inflight Ysql major catalog upgrade in progress. Nothing to rollback.";
    return Status::OK();
  }

  RETURN_NOT_OK(ysql_catalog_config_.TransitionMajorCatalogUpgradeState(
      YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK, epoch));

  auto status = RollbackMajorVersionCatalogImpl(epoch);
  if (status.ok()) {
    RETURN_NOT_OK(ysql_catalog_config_.TransitionMajorCatalogUpgradeState(
        YsqlMajorCatalogUpgradeInfoPB::DONE, epoch));
  } else {
    RETURN_NOT_OK(ysql_catalog_config_.TransitionMajorCatalogUpgradeState(
        YsqlMajorCatalogUpgradeInfoPB::FAILED, epoch, status));
  }

  return status;
}

Status YsqlInitDBAndMajorUpgradeHandler::RollbackMajorVersionCatalogImpl(const LeaderEpoch& epoch) {
  std::vector<scoped_refptr<NamespaceInfo>> namespaces;
  {
    std::vector<scoped_refptr<NamespaceInfo>> all_namespaces;
    catalog_manager_.GetAllNamespaces(&all_namespaces);
    for (const auto& ns_info : all_namespaces) {
      NamespaceInfo::ReadLock ns_l = ns_info->LockForRead();
      if (ns_info->database_type() != YQL_DATABASE_PGSQL) {
        continue;
      }
      uint32_t oid = VERIFY_RESULT(GetPgsqlDatabaseOid(ns_info->id()));
      if (oid < kPgFirstNormalObjectId) {
        namespaces.push_back(ns_info);
      }
    }
  }

  for (const auto& ns_info : namespaces) {
    LOG(INFO) << "Deleting ysql major catalog tables for namespace " << ns_info->name();
    RETURN_NOT_OK(catalog_manager_.DeleteYsqlDBTables(
        ns_info,
        /*is_for_ysql_major_rollback=*/true, epoch));
  }

  // Reset state machines for all YSQL namespaces.
  {
    for (const auto& ns_info : namespaces) {
      NamespaceInfo::WriteLock ns_l = ns_info->LockForWrite();
      if (ns_info->database_type() != YQL_DATABASE_PGSQL) {
        continue;
      }
      SysNamespaceEntryPB* metadata = &ns_l.mutable_data()->pb;
      if (metadata->state() == SysNamespaceEntryPB::DELETED ||
          metadata->state() == SysNamespaceEntryPB::UNKNOWN) {
        continue;
      }
      SCHECK_FORMAT(
          metadata->state() == SysNamespaceEntryPB::PREPARING ||
              metadata->state() == SysNamespaceEntryPB::RUNNING,
          IllegalState,
          "Namespace $0 in state $1 during rollback. Expected PREPARING or "
          "RUNNING.",
          ns_info->name(), SysNamespaceEntryPB::State_Name(metadata->state()));
      metadata->set_state(SysNamespaceEntryPB::RUNNING);
      // NEXT_VER_RUNNING is the initial state for ysql_next_major_version_state.
      metadata->set_ysql_next_major_version_state(SysNamespaceEntryPB::NEXT_VER_RUNNING);

      RETURN_NOT_OK(sys_catalog_.Upsert(epoch, ns_info.get()));
      ns_l.Commit();
      LOG(INFO) << "Reset state metadata for namespace " << ns_info->name();
    }
  }
  return Status::OK();
}

}  // namespace yb::master
