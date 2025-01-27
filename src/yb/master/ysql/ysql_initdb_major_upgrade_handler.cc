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
#include "yb/common/version_info.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_string(tmp_dir);
DECLARE_bool(master_join_existing_universe);
DECLARE_string(rpc_bind_addresses);
DECLARE_bool(ysql_enable_auth);

DEFINE_RUNTIME_uint32(ysql_upgrade_postgres_port, 5434,
  "Port used to start the postgres process for ysql upgrade");

DEFINE_test_flag(
    string, fail_ysql_catalog_upgrade_state_transition_from, "",
    "When set fail the transition to the provided state");

DEFINE_RUNTIME_string(ysql_major_upgrade_user, "yugabyte_upgrade",
    "The ysql user to use for ysql major upgrade operations when both:"
    " authentication is enabled, "
    " no yb-tserver process running on the yb-master nodes. "
    "This user should have superuser privileges and the password must be placed in the `.pgpass` "
    "file on all yb-master nodes.");

DEFINE_test_flag(bool, ysql_fail_cleanup_previous_version_catalog, false,
    "Fail the cleanup of the previous version ysql catalog");

using yb::pgwrapper::PgWrapper;

namespace yb::master {

namespace {

bool IsYsqlMajorCatalogOperationRunning(YsqlMajorCatalogUpgradeInfoPB::State state) {
  return state == YsqlMajorCatalogUpgradeInfoPB::PERFORMING_PG_UPGRADE ||
         state == YsqlMajorCatalogUpgradeInfoPB::PERFORMING_INIT_DB ||
         state == YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK;
}

}  // namespace

YsqlInitDBAndMajorUpgradeHandler::YsqlInitDBAndMajorUpgradeHandler(
    Master& master, YsqlCatalogConfig& ysql_catalog_config, CatalogManager& catalog_manager,
    SysCatalogTable& sys_catalog, yb::ThreadPool& thread_pool)
    : master_(master),
      ysql_catalog_config_(ysql_catalog_config),
      catalog_manager_(catalog_manager),
      sys_catalog_(sys_catalog),
      thread_pool_(thread_pool) {}

void YsqlInitDBAndMajorUpgradeHandler::Load(scoped_refptr<SysConfigInfo> config) {
  auto& ysql_catalog_config = config->mutable_metadata()->mutable_dirty()->pb.ysql_catalog_config();
  if (ysql_catalog_config.has_ysql_major_catalog_upgrade_info()) {
    const auto persisted_version =
        ysql_catalog_config.ysql_major_catalog_upgrade_info().catalog_version();
    const auto major_version_of_current_build = VersionInfo::YsqlMajorVersion();
    LOG_IF(FATAL, persisted_version > major_version_of_current_build)
        << "Persisted major version in YSQL catalog config is not supported. Restart "
           "the process in the correct version. Min required major version: "
        << persisted_version << ", Current major version: " << VersionInfo::GetShortVersionString();

    // If the persisted version is different from the current version then we are in the middle of
    // ysql major upgrade.
    ysql_major_upgrade_in_progress_ = persisted_version != major_version_of_current_build;

    // A new yb-master leader has started. If we were in the middle of the ysql major catalog
    // upgrade (initdb, pg_upgrade, or rollback) then mark the major upgrade as failed. No action
    // is taken if we are in the monitoring phase.
    // We cannot update the config right now, so do so after the sys_catalog is loaded.
    auto state = ysql_catalog_config.ysql_major_catalog_upgrade_info().state();

    if (!ysql_major_upgrade_in_progress_ && state != YsqlMajorCatalogUpgradeInfoPB::DONE) {
      LOG(FATAL) << "Ysql catalog is in state " << YsqlMajorCatalogUpgradeInfoPB::State_Name(state)
                 << " but the persisted major ysql version " << persisted_version
                 << " is the same as the current major version supported by this build. yb-master "
                    "process seems to have been incorrectly downgraded in the middle of an ysql "
                    "major upgrade. Restart the yb-master in the newer version and rollback the "
                    "ysql catalog before downgrading. Current process version: "
                 << VersionInfo::GetShortVersionString();
    }

    restarted_during_major_upgrade_ = IsYsqlMajorCatalogOperationRunning(state);
  } else {
    // This is Pg11 to Pg15. major_catalog_upgrade_info was only added in pg15.
    ysql_major_upgrade_in_progress_ = true;
    restarted_during_major_upgrade_ = false;
  }
}

void YsqlInitDBAndMajorUpgradeHandler::SysCatalogLoaded(const LeaderEpoch& epoch) {
  if (restarted_during_major_upgrade_) {
    ERROR_NOT_OK(
        TransitionMajorCatalogUpgradeState(
            YsqlMajorCatalogUpgradeInfoPB::FAILED, epoch,
            STATUS(InternalError, "yb-master restarted during ysql major catalog upgrade")),
        "Failed to set major version upgrade state to FAILED");
    restarted_during_major_upgrade_ = false;
  }

  if (ysql_catalog_config_.IsPreviousVersionCatalogCleanupRequired()) {
    ScheduleCleanupPreviousYsqlMajorCatalog(epoch);
  }
}

Status YsqlInitDBAndMajorUpgradeHandler::StartNewClusterGlobalInitDB(const LeaderEpoch& epoch) {
  SCHECK(
      !FLAGS_master_join_existing_universe, IllegalState,
      "Master is joining an existing universe but wants to run initdb. "
      "This should have been done during initial universe creation.");

  return RunOperationAsync([this, epoch]() { RunNewClusterGlobalInitDB(epoch); });
}

Status YsqlInitDBAndMajorUpgradeHandler::StartYsqlMajorCatalogUpgrade(const LeaderEpoch& epoch) {
  RETURN_NOT_OK(
      TransitionMajorCatalogUpgradeState(YsqlMajorCatalogUpgradeInfoPB::PERFORMING_INIT_DB, epoch));

  auto status = RunOperationAsync([this, epoch]() { RunMajorVersionUpgrade(epoch); });
  if (!status.ok()) {
    ERROR_NOT_OK(
        TransitionMajorCatalogUpgradeState(YsqlMajorCatalogUpgradeInfoPB::FAILED, epoch, status),
        "Failed to set major version upgrade state");
  }

  return status;
}

IsOperationDoneResult YsqlInitDBAndMajorUpgradeHandler::IsYsqlMajorCatalogUpgradeDone() const {
  const auto state = ysql_catalog_config_.GetMajorCatalogUpgradeState();
  if (IsYsqlMajorCatalogOperationRunning(state)) {
    return IsOperationDoneResult::NotDone();
  }

  return IsOperationDoneResult::Done(ysql_catalog_config_.GetMajorCatalogUpgradePreviousError());
}

Status YsqlInitDBAndMajorUpgradeHandler::FinalizeYsqlMajorCatalogUpgrade(const LeaderEpoch& epoch) {
  return TransitionMajorCatalogUpgradeState(YsqlMajorCatalogUpgradeInfoPB::DONE, epoch);
}

Status YsqlInitDBAndMajorUpgradeHandler::RollbackYsqlMajorCatalogVersion(const LeaderEpoch& epoch) {
  // Since Rollback is synchronous, we can perform the state transitions inside the async
  // function. It also ensures there are no inflight operations when the rollback state transition
  // occurs.
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

Result<YsqlMajorCatalogUpgradeState>
YsqlInitDBAndMajorUpgradeHandler::GetYsqlMajorCatalogUpgradeState() const {
  const auto state = ysql_catalog_config_.GetMajorCatalogUpgradeState();
  switch (state) {
    case YsqlMajorCatalogUpgradeInfoPB::INVALID:
      // Bad enum, fail the call.
      break;
    case YsqlMajorCatalogUpgradeInfoPB::DONE:
      return ysql_major_upgrade_in_progress_ ? YSQL_MAJOR_CATALOG_UPGRADE_PENDING
                                             : YSQL_MAJOR_CATALOG_UPGRADE_DONE;
    case YsqlMajorCatalogUpgradeInfoPB::FAILED:
      return YSQL_MAJOR_CATALOG_UPGRADE_PENDING_ROLLBACK;
    case YsqlMajorCatalogUpgradeInfoPB::PERFORMING_INIT_DB:
      FALLTHROUGH_INTENDED;
    case YsqlMajorCatalogUpgradeInfoPB::PERFORMING_PG_UPGRADE:
      return YSQL_MAJOR_CATALOG_UPGRADE_IN_PROGRESS;
    case YsqlMajorCatalogUpgradeInfoPB::MONITORING:
      return YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK;
    case YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK:
      return YSQL_MAJOR_CATALOG_UPGRADE_ROLLBACK_IN_PROGRESS;
  }

  return STATUS_FORMAT(IllegalState, "Unknown ysql major upgrade state: $0", state);
}

bool YsqlInitDBAndMajorUpgradeHandler::IsWriteToCatalogTableAllowed(
    const TableId& table_id, bool is_forced_update) const {
  // During the upgrade only allow special updates to the catalog.
  if (IsMajorUpgradeInProgress()) {
    return is_forced_update;
  }

  // If we are not in the middle of a major upgrade then only allow updates to the current
  // version.
  return IsCurrentVersionYsqlCatalogTable(table_id);
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
    auto update_state_status =
        TransitionMajorCatalogUpgradeState(YsqlMajorCatalogUpgradeInfoPB::MONITORING, epoch);
    if (update_state_status.ok()) {
      LOG(INFO) << "Ysql major catalog upgrade completed successfully";
    } else {
      LOG(ERROR) << "Failed to set major version upgrade state: " << update_state_status;
    }
    return;
  }

  LOG(ERROR) << "Ysql major catalog upgrade failed: " << status;
  ERROR_NOT_OK(
      TransitionMajorCatalogUpgradeState(YsqlMajorCatalogUpgradeInfoPB::FAILED, epoch, status),
      "Failed to set major version upgrade state");
}

Status YsqlInitDBAndMajorUpgradeHandler::RunMajorVersionUpgradeImpl(const LeaderEpoch& epoch) {
  RETURN_NOT_OK(RunMajorVersionCatalogUpgrade(epoch));
  RETURN_NOT_OK_PREPEND(UpdateCatalogVersions(epoch), "Failed to update catalog versions");

  return Status::OK();
}

// pg_upgrade does not migrate the catalog version table, so we have to explicitly copy the
// contents of the pre-existing catalog table to the current version's catalog version table.
Status YsqlInitDBAndMajorUpgradeHandler::UpdateCatalogVersions(const LeaderEpoch& epoch) {
  RETURN_NOT_OK(
      sys_catalog_.DeleteAllYsqlCatalogTableRows({kPgYbCatalogVersionTableId}, epoch.leader_term));
  RETURN_NOT_OK(sys_catalog_.CopyPgsqlTables(
      {kPgYbCatalogVersionTableIdPriorVersion}, {kPgYbCatalogVersionTableId}, epoch.leader_term));
  return Status::OK();
}

Status YsqlInitDBAndMajorUpgradeHandler::RunMajorVersionCatalogUpgrade(const LeaderEpoch& epoch) {
  auto db_name_to_oid_list = VERIFY_RESULT(GetDbNameToOidListForMajorUpgrade());
  RETURN_NOT_OK_PREPEND(
      InitDBAndSnapshotSysCatalog(db_name_to_oid_list, /*is_major_upgrade=*/true, epoch),
      "Failed to run initdb");

  RETURN_NOT_OK(TransitionMajorCatalogUpgradeState(
      YsqlMajorCatalogUpgradeInfoPB::PERFORMING_PG_UPGRADE, epoch));

  RETURN_NOT_OK_PREPEND(PerformPgUpgrade(epoch), "Failed to run pg_upgrade");

  return Status::OK();
}

Result<YsqlInitDBAndMajorUpgradeHandler::DbNameToOidList>
YsqlInitDBAndMajorUpgradeHandler::GetDbNameToOidListForMajorUpgrade() {
  DbNameToOidList db_name_to_oid_list;
  // Retrieve the OID for template0 and yugabyte databases. These two and the template1 are the only
  // databases created by initdb in major upgrade mode. template1 is always hardcoded to use oid 1
  // and we cannot asign a different oid for it, so it is skipped.
  for (const auto& namespace_name : {"template0", "yugabyte"}) {
    auto namespace_id =
        VERIFY_RESULT(catalog_manager_.GetNamespaceId(YQL_DATABASE_PGSQL, namespace_name));
    auto oid = VERIFY_RESULT(GetPgsqlDatabaseOid(namespace_id));
    db_name_to_oid_list.push_back({namespace_name, oid});
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
  pg_upgrade_params.ysql_user_name = "yugabyte";
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
    // Remote tserver.
    pg_upgrade_params.old_version_pg_address = closest_ts_hp.host();

    if (FLAGS_ysql_enable_auth || pg_conf.enable_tls) {
      pg_upgrade_params.ysql_user_name = FLAGS_ysql_major_upgrade_user;
      LOG(INFO) << "Running ysql major upgrade on a authentication enabled universe which does not "
                   "have a yb-tserver on the same node as the yb-master. Upgrade will be performed "
                   "using yb-tserver hosted on "
                << closest_ts_hp << " by user " << pg_upgrade_params.ysql_user_name;
    }
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

  RETURN_NOT_OK(TransitionMajorCatalogUpgradeState(
      YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK, epoch));

  auto status = RollbackMajorVersionCatalogImpl(epoch);
  if (status.ok()) {
    RETURN_NOT_OK(TransitionMajorCatalogUpgradeState(YsqlMajorCatalogUpgradeInfoPB::DONE, epoch));
  } else {
    RETURN_NOT_OK(
        TransitionMajorCatalogUpgradeState(YsqlMajorCatalogUpgradeInfoPB::FAILED, epoch, status));
  }

  return status;
}

Status YsqlInitDBAndMajorUpgradeHandler::RollbackMajorVersionCatalogImpl(const LeaderEpoch& epoch) {
  std::vector<scoped_refptr<NamespaceInfo>> ysql_namespaces;
  {
    std::vector<scoped_refptr<NamespaceInfo>> all_namespaces;
    catalog_manager_.GetAllNamespaces(&all_namespaces);
    for (const auto& ns_info : all_namespaces) {
      NamespaceInfo::ReadLock ns_l = ns_info->LockForRead();
      if (ns_info->database_type() != YQL_DATABASE_PGSQL) {
        continue;
      }
      ysql_namespaces.push_back(ns_info);
    }
  }

  for (const auto& ns_info : ysql_namespaces) {
    LOG(INFO) << "Deleting ysql major catalog tables for namespace " << ns_info->name();
    RETURN_NOT_OK(catalog_manager_.DeleteYsqlDBTables(
        ns_info->id(), DeleteYsqlDBTablesType::kMajorUpgradeRollback, epoch));
  }

  // Reset state machines for all YSQL namespaces.
  {
    for (const auto& ns_info : ysql_namespaces) {
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

const std::unordered_map<
    YsqlMajorCatalogUpgradeInfoPB::State, std::unordered_set<YsqlMajorCatalogUpgradeInfoPB::State>>
    kAllowedTransitions = {
        {YsqlMajorCatalogUpgradeInfoPB::INVALID, {}},

        {YsqlMajorCatalogUpgradeInfoPB::DONE, {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_INIT_DB}},

        {YsqlMajorCatalogUpgradeInfoPB::FAILED,
         {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK}},

        {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_INIT_DB,
         {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_PG_UPGRADE,
          YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK,
          YsqlMajorCatalogUpgradeInfoPB::FAILED}},

        {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_PG_UPGRADE,
         {YsqlMajorCatalogUpgradeInfoPB::MONITORING,
          YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK,
          YsqlMajorCatalogUpgradeInfoPB::FAILED}},

        {YsqlMajorCatalogUpgradeInfoPB::MONITORING,
         {YsqlMajorCatalogUpgradeInfoPB::DONE, YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK,
          YsqlMajorCatalogUpgradeInfoPB::FAILED}},

        {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK,
         {YsqlMajorCatalogUpgradeInfoPB::DONE, YsqlMajorCatalogUpgradeInfoPB::FAILED}},
};

Status YsqlInitDBAndMajorUpgradeHandler::TransitionMajorCatalogUpgradeState(
    const YsqlMajorCatalogUpgradeInfoPB::State new_state, const LeaderEpoch& epoch,
    const Status& failed_status) {
  DCHECK_EQ(kAllowedTransitions.size(), YsqlMajorCatalogUpgradeInfoPB::State_ARRAYSIZE);

  SCHECK_FORMAT(
      ysql_major_upgrade_in_progress_, IllegalState,
      "Ysql Catalog is already on the current major version: $0", VersionInfo::YsqlMajorVersion());

  const auto new_state_str = YsqlMajorCatalogUpgradeInfoPB::State_Name(new_state);

  RSTATUS_DCHECK_EQ(
      failed_status.ok(), new_state != YsqlMajorCatalogUpgradeInfoPB::FAILED, IllegalState,
      Format("Bad status must be set if and only if transitioning to FAILED state", failed_status));

  bool ysql_major_upgrade_done = false;

  auto [l, pb] = ysql_catalog_config_.LockForWrite(epoch);

  auto* ysql_major_catalog_upgrade_info = pb.mutable_ysql_major_catalog_upgrade_info();

  SCHECK(
      !ysql_major_catalog_upgrade_info->previous_version_catalog_cleanup_required(), IllegalState,
      "Previous version catalog cleanup has not completed yet. Cannot start a new major catalog "
      "upgrade.");

  const auto current_state = ysql_major_catalog_upgrade_info->state();
  SCHECK_NE(
      current_state, new_state, IllegalState,
      Format("Major upgrade state already set to $0", new_state_str));

  const auto current_state_str = YsqlMajorCatalogUpgradeInfoPB::State_Name(current_state);

  SCHECK_NE(
      current_state_str, FLAGS_TEST_fail_ysql_catalog_upgrade_state_transition_from, IllegalState,
      "Failed due to FLAGS_TEST_fail_ysql_catalog_upgrade_state_transition_from");

  auto allowed_states_it = FindOrNull(kAllowedTransitions, current_state);
  RSTATUS_DCHECK(allowed_states_it, IllegalState, Format("Invalid state $0", current_state_str));

  SCHECK(
      allowed_states_it->contains(new_state), IllegalState,
      Format("Invalid state transition from $0 to $1", current_state_str, new_state_str));

  if (current_state == YsqlMajorCatalogUpgradeInfoPB::MONITORING &&
      new_state == YsqlMajorCatalogUpgradeInfoPB::DONE) {
    ysql_major_catalog_upgrade_info->set_catalog_version(VersionInfo::YsqlMajorVersion());
    ysql_major_catalog_upgrade_info->set_previous_version_catalog_cleanup_required(true);
    ysql_major_upgrade_done = true;
  }

  ysql_major_catalog_upgrade_info->set_state(new_state);

  if (!failed_status.ok()) {
    StatusToPB(failed_status, ysql_major_catalog_upgrade_info->mutable_previous_error());
  } else {
    ysql_major_catalog_upgrade_info->clear_previous_error();
  }

  LOG(INFO) << "Transitioned major upgrade state from " << current_state_str << " to "
            << new_state_str;

  RETURN_NOT_OK(l.UpsertAndCommit());

  if (ysql_major_upgrade_done) {
    ysql_major_upgrade_in_progress_ = false;
    ScheduleCleanupPreviousYsqlMajorCatalog(epoch);
  }

  return Status::OK();
}

void YsqlInitDBAndMajorUpgradeHandler::ScheduleCleanupPreviousYsqlMajorCatalog(
    const LeaderEpoch& epoch) {
  // We do not expect the scheduling itself to fail. But in the rare case it does, the yb-master
  // leader will have to ben bounced. This is most likely to happen anyway if scheduling is failing,
  // so we do not need more complex code to handle it.
  ERROR_NOT_OK(
      RunOperationAsync([this, epoch]() mutable {
        while (true) {
          auto is_leader = catalog_manager_.CheckIsLeaderAndReady();
          if (!is_leader.ok()) {
            LOG(INFO) << "No longer the leader. New leader will retry the cleanup of the previous "
                         "YSQL major version catalog: "
                      << is_leader;
            return;
          }

          auto status = CleanupPreviousYsqlMajorCatalog(epoch);
          if (status.ok()) {
            return;
          }
          LOG(WARNING) << "Failed to cleanup previous version ysql major catalog. Retrying: "
                       << status;
        }
      }),
      "Failed to schedule cleanup of previous version ysql major catalog");
}

Status YsqlInitDBAndMajorUpgradeHandler::CleanupPreviousYsqlMajorCatalog(const LeaderEpoch& epoch) {
  if (!ysql_catalog_config_.IsPreviousVersionCatalogCleanupRequired()) {
    VLOG(1) << "Previous version catalog cleanup not required.";
    return Status::OK();
  }

  SCHECK(
      !FLAGS_TEST_ysql_fail_cleanup_previous_version_catalog, IllegalState,
      "Failed due to FLAGS_TEST_ysql_fail_cleanup_previous_version_catalog");

  std::vector<scoped_refptr<NamespaceInfo>> ysql_namespaces;
  {
    std::vector<scoped_refptr<NamespaceInfo>> all_namespaces;
    catalog_manager_.GetAllNamespaces(&all_namespaces);
    for (const auto& ns_info : all_namespaces) {
      NamespaceInfo::ReadLock ns_l = ns_info->LockForRead();
      if (ns_info->database_type() != YQL_DATABASE_PGSQL) {
        continue;
      }
      ysql_namespaces.push_back(ns_info);
    }
  }

  for (const auto& ns_info : ysql_namespaces) {
    LOG(INFO) << "Deleting previous ysql major catalog tables for namespace "
              << ns_info->ToString();
    RETURN_NOT_OK(catalog_manager_.DeleteYsqlDBTables(
        ns_info->id(), DeleteYsqlDBTablesType::kMajorUpgradeCleanup, epoch));
  }

  {
    auto [l, pb] = ysql_catalog_config_.LockForWrite(epoch);
    pb.mutable_ysql_major_catalog_upgrade_info()->set_previous_version_catalog_cleanup_required(
        false);
    RETURN_NOT_OK(l.UpsertAndCommit());
  }

  return Status::OK();
}

}  // namespace yb::master
