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

#include "yb/tablet/tablet_peer.h"

#include "yb/util/async_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_bool(TEST_online_pg11_to_pg15_upgrade);
DECLARE_string(tmp_dir);
DECLARE_bool(master_join_existing_universe);
DECLARE_string(rpc_bind_addresses);

DEFINE_RUNTIME_uint32(ysql_upgrade_postgres_port, 5434,
  "Port used to start the postgres process for ysql upgrade");

using yb::pgwrapper::PgWrapper;

namespace yb::master {

YsqlInitDBAndMajorUpgradeHandler::YsqlInitDBAndMajorUpgradeHandler(
    Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog,
    yb::ThreadPool& thread_pool)
    : master_(master),
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

Status YsqlInitDBAndMajorUpgradeHandler::StartYsqlMajorVersionUpgrade(const LeaderEpoch& epoch) {
  SCHECK(
      FLAGS_TEST_online_pg11_to_pg15_upgrade, IllegalState,
      "Must be in upgrade mode (FLAGS_TEST_online_pg11_to_pg15_upgrade)");

  // Since StartYsqlMajorVersionUpgrade is idempotent, if run again we need to reset the
  // pg15_initdb flags so that callers that check IsYsqlMajorVersionUpgradeInitdbDone won't get
  // a false positive, while the new invocation goes through rollback and initdb.
  RETURN_NOT_OK(ResetNextVerInitdbStatus(epoch));

  return RunOperationAsync([this, epoch]() { RunMajorVersionUpgrade(epoch); });
}

Status YsqlInitDBAndMajorUpgradeHandler::RollbackYsqlMajorVersionUpgrade(const LeaderEpoch& epoch) {
  // Note that as part of the rollback, we're getting rid of the catalog cache table for pg15, so we
  // have to be in a mode that ensures that we read from a valid catalog table. Also, for simplicity
  // it's best for DDLs to be disabled.
  SCHECK(
      FLAGS_TEST_online_pg11_to_pg15_upgrade, IllegalState,
      "Must be in upgrade mode (FLAGS_TEST_online_pg11_to_pg15_upgrade)");

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

Status YsqlInitDBAndMajorUpgradeHandler::ResetNextVerInitdbStatus(const LeaderEpoch& epoch) {
  auto ysql_catalog_config = catalog_manager_.GetYsqlCatalogConfig();
  auto l = ysql_catalog_config->LockForWrite();
  auto* ysql_major_upgrade_info =
      l.mutable_data()->pb.mutable_ysql_catalog_config()->mutable_ysql_major_upgrade_info();
  ysql_major_upgrade_info->set_next_ver_initdb_done(false);
  ysql_major_upgrade_info->clear_next_ver_initdb_error();
  RETURN_NOT_OK(sys_catalog_.Upsert(epoch, ysql_catalog_config));
  l.Commit();
  return Status::OK();
}

Status YsqlInitDBAndMajorUpgradeHandler::RunOperationAsync(std::function<void()> func) {
  bool expected = false;
  if (!is_running_.compare_exchange_strong(expected, true)) {
    return STATUS(
        IllegalState,
        "Global initdb or ysql major version upgrade rollback is already in progress");
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
  auto status = InitDBAndSnapshotSysCatalog(/*db_name_to_oid_list=*/{}, epoch);
  WARN_NOT_OK(
      catalog_manager_.InitDbFinished(status, epoch.leader_term),
      "Failed to set global initdb as finished in sys catalog");
}

Status YsqlInitDBAndMajorUpgradeHandler::InitDBAndSnapshotSysCatalog(
    const DbNameToOidList& db_name_to_oid_list, const LeaderEpoch& epoch) {
  InitialSysCatalogSnapshotWriter* snapshot_writer = nullptr;
  if (FLAGS_create_initial_sys_catalog_snapshot) {
    snapshot_writer = &catalog_manager_.AllocateAndGetInitialSysCatalogSnapshotWriter();
  }

  const auto& master_opts = master_.opts();
  const auto master_addresses_str =
      server::MasterAddressesToString(*master_opts.GetMasterAddresses());

  RETURN_NOT_OK(PgWrapper::InitDbForYSQL(
      master_addresses_str, FLAGS_tmp_dir, master_.GetSharedMemoryFd(), db_name_to_oid_list));

  if (!snapshot_writer) {
    return Status::OK();
  }

  auto sys_catalog_tablet = VERIFY_RESULT(sys_catalog_.tablet_peer()->shared_tablet_safe());
  RETURN_NOT_OK(snapshot_writer->WriteSnapshot(
      sys_catalog_tablet.get(), FLAGS_initial_sys_catalog_snapshot_path));

  return Status::OK();
}

void YsqlInitDBAndMajorUpgradeHandler::RunMajorVersionUpgrade(const LeaderEpoch& epoch) {
  auto status = RunMajorVersionCatalogUpgrade(epoch);
  WARN_NOT_OK(
      MajorVersionCatalogUpgradeFinished(status, epoch), "Failed to run major version upgrade");
}

Status YsqlInitDBAndMajorUpgradeHandler::RunMajorVersionCatalogUpgrade(const LeaderEpoch& epoch) {
  // This process is idempotent, so it needs to roll back the state before re-running. This is a
  // no-op if this is the first time the upgrade is running.
  RETURN_NOT_OK_PREPEND(RunRollbackMajorVersionUpgrade(epoch), "Rollback failed");

  auto db_name_to_oid_list = VERIFY_RESULT(GetDbNameToOidListForMajorUpgrade());
  RETURN_NOT_OK_PREPEND(
      InitDBAndSnapshotSysCatalog(db_name_to_oid_list, epoch), "Failed to run initdb");

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

Status YsqlInitDBAndMajorUpgradeHandler::MajorVersionCatalogUpgradeFinished(
    const Status& upgrade_status, const LeaderEpoch& epoch) {
  if (upgrade_status.ok()) {
    LOG(INFO) << "Ysql catalog migration for major upgrade completed successfully";
  } else {
    LOG(ERROR) << "Ysql major upgrade failed: " << upgrade_status;
  }

  auto ysql_catalog_config = catalog_manager_.GetYsqlCatalogConfig();
  auto l = ysql_catalog_config->LockForWrite();
  auto* mutable_ysql_catalog_config = l.mutable_data()->pb.mutable_ysql_catalog_config();
  auto* ysql_major_upgrade_info = mutable_ysql_catalog_config->mutable_ysql_major_upgrade_info();
  ysql_major_upgrade_info->set_next_ver_initdb_done(true);

  if (upgrade_status.ok()) {
    ysql_major_upgrade_info->clear_next_ver_initdb_error();
  } else {
    StatusToPB(
        upgrade_status, ysql_major_upgrade_info->mutable_next_ver_initdb_error()->mutable_status());
  }

  RETURN_NOT_OK(sys_catalog_.Upsert(epoch, ysql_catalog_config));
  l.Commit();
  return Status::OK();
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

  PgWrapper::PgUpgradeParams pg_upgrade_params{
      .data_dir = pg_conf.data_dir,
      .old_version_pg_address = VERIFY_RESULT(GetClosestLiveTserverAddress()),
      .old_version_pg_port = pgwrapper::PgProcessConf::kDefaultPort,
      .new_version_pg_address = pg_conf.listen_addresses,
      .new_version_pg_port = pg_conf.pg_port};
  RETURN_NOT_OK(PgWrapper::RunPgUpgrade(pg_upgrade_params));

  return Status::OK();
}

Result<std::string> YsqlInitDBAndMajorUpgradeHandler::GetClosestLiveTserverAddress() {
  ServerRegistrationPB local_reg;
  RETURN_NOT_OK(master_.GetMasterRegistration(&local_reg));

  std::unordered_set<std::string> local_addresses;
  for (const auto& addr : local_reg.private_rpc_addresses()) {
    local_addresses.insert(addr.host());
  }
  const auto& local_cloud_info = local_reg.cloud_info();

  std::vector<std::shared_ptr<TSDescriptor>> descs;
  master_.ts_manager()->GetAllLiveDescriptorsInCluster(
      &descs, VERIFY_RESULT(catalog_manager_.placement_uuid()));

  int best_score = -1;
  std::string best_host;
  for (const auto& desc : descs) {
    if (!desc->IsLive()) {
      continue;
    }
    const auto& ts_info = desc->GetTSInformationPB();

    if (!ts_info.has_registration() ||
        ts_info.registration().common().private_rpc_addresses().empty()) {
      continue;
    }

    const auto& ts_cloud_info = ts_info.registration().common().cloud_info();
    int ts_score = 0;
    if (ts_cloud_info.placement_cloud() == local_cloud_info.placement_cloud()) {
      ts_score++;
      if (ts_cloud_info.placement_region() == local_cloud_info.placement_region()) {
        ts_score++;
        if (ts_cloud_info.placement_zone() == local_cloud_info.placement_zone()) {
          ts_score++;
        }
      }
    }
    if (ts_score < best_score) {
      continue;
    }

    if (ts_score == 3) {
      // If this tserver is on the same node as master pick it.
      for (const auto& addr : ts_info.registration().common().private_rpc_addresses()) {
        if (local_addresses.contains(addr.host())) {
          return addr.host();
        }
      }
    }

    best_score = ts_score;
    best_host = ts_info.registration().common().private_rpc_addresses(0).host();
  }

  SCHECK(!best_host.empty(), IllegalState, "Couldn't find alive tablet server to connect to");

  return best_host;
}

Status YsqlInitDBAndMajorUpgradeHandler::RunRollbackMajorVersionUpgrade(const LeaderEpoch& epoch) {
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
    LOG(INFO) << "Deleting Ysql major version catalog tables for namespace " << ns_info->name();
    RETURN_NOT_OK(catalog_manager_.DeleteYsqlDBTables(ns_info, epoch));
  }

  RETURN_NOT_OK(ResetNextVerInitdbStatus(epoch));

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
