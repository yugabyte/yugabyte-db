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

#include "yb/master/sys_catalog_initialization.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/sys_catalog.h"

#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/env_util.h"
#include "yb/util/flags.h"
#include "yb/util/flag_tags.h"

DEFINE_string(initial_sys_catalog_snapshot_path, "",
    "If this is specified, system catalog RocksDB is checkpointed at this location after initdb "
    "is done.");

DEFINE_bool(use_initial_sys_catalog_snapshot, false,
    "DEPRECATED: use --enable_ysql instead. "
    "Initialize sys catalog tablet from a pre-existing snapshot instead of running initdb. "
    "Only takes effect if --initial_sys_catalog_snapshot_path is specified or can be "
    "auto-detected.");

DEFINE_bool(enable_ysql, true,
    "Enable YSQL on cluster. This will initialize sys catalog tablet from a pre-existing snapshot "
    "and start YSQL proxy. "
    "Only takes effect if --initial_sys_catalog_snapshot_path is specified or can be auto-detected."
    );

DEFINE_bool(create_initial_sys_catalog_snapshot, false,
    "Run initdb and create an initial sys catalog data snapshot");

TAG_FLAG(create_initial_sys_catalog_snapshot, advanced);
TAG_FLAG(create_initial_sys_catalog_snapshot, hidden);

using yb::CountDownLatch;
using yb::tserver::TabletSnapshotOpRequestPB;
using yb::tserver::TabletSnapshotOpResponsePB;
using yb::tablet::SnapshotOperation;
using yb::pb_util::ReadPBContainerFromPath;

namespace yb {
namespace master {

namespace {

const char* kDefaultInitialSysCatalogSnapshotDir = "initial_sys_catalog_snapshot";
const char* kSysCatalogSnapshotRocksDbSubDir = "rocksdb";
const char* kSysCatalogSnapshotTabletMetadataChangesFile =
    "exported_tablet_metadata_changes";
const char* kUseInitialSysCatalogSnapshotEnvVar = "YB_USE_INITIAL_SYS_CATALOG_SNAPSHOT";
}  // anonymous namespace

// ------------------------------------------------------------------------------------------------
// InitialSysCatalogSnapshotWriter
// ------------------------------------------------------------------------------------------------

InitialSysCatalogSnapshotWriter::InitialSysCatalogSnapshotWriter() = default;
InitialSysCatalogSnapshotWriter::~InitialSysCatalogSnapshotWriter() = default;

void InitialSysCatalogSnapshotWriter::AddMetadataChange(
    tablet::ChangeMetadataRequestPB metadata_change) {
  initdb_metadata_changes_.push_back(std::move(metadata_change));
}

Status InitialSysCatalogSnapshotWriter::WriteSnapshot(
    tablet::Tablet* sys_catalog_tablet,
    const std::string& dest_path) {
  RETURN_NOT_OK(sys_catalog_tablet->Flush(yb::tablet::FlushMode::kSync));
  RETURN_NOT_OK(Env::Default()->CreateDir(dest_path));
  RETURN_NOT_OK(sys_catalog_tablet->snapshots().CreateCheckpoint(
      JoinPathSegments(dest_path, kSysCatalogSnapshotRocksDbSubDir)));

  tserver::ExportedTabletMetadataChanges exported_tablet_metadata_changes;
  for (size_t i = 0; i < initdb_metadata_changes_.size(); ++i) {
    *exported_tablet_metadata_changes.add_metadata_changes() = std::move(
        initdb_metadata_changes_[i]);
  }

  const string metadata_changes_file = JoinPathSegments(
      dest_path,
      kSysCatalogSnapshotTabletMetadataChangesFile);
  RETURN_NOT_OK(WritePBContainerToPath(
      Env::Default(),
      metadata_changes_file,
      exported_tablet_metadata_changes,
      pb_util::CreateMode::NO_OVERWRITE,
      pb_util::SyncMode::NO_SYNC));
  LOG(INFO) << "Wrote " << initdb_metadata_changes_.size() << " tablet metadata changes to file "
            << metadata_changes_file;

  LOG(INFO) << "Created initial sys catalog snapshot at " << dest_path;
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// End of InitialSysCatalogSnapshotWriter
// ------------------------------------------------------------------------------------------------

Status RestoreInitialSysCatalogSnapshot(
    const std::string& initial_snapshot_path,
    tablet::TabletPeer* sys_catalog_tablet_peer,
    int64_t term) {
  TabletSnapshotOpRequestPB tablet_snapshot_req;
  tablet_snapshot_req.set_operation(yb::tserver::TabletSnapshotOpRequestPB::RESTORE_ON_TABLET);
  tablet_snapshot_req.add_tablet_id(kSysCatalogTabletId);
  tablet_snapshot_req.set_snapshot_dir_override(
      JoinPathSegments(initial_snapshot_path, kSysCatalogSnapshotRocksDbSubDir));

  TabletSnapshotOpResponsePB tablet_snapshot_resp;
  auto operation = std::make_unique<SnapshotOperation>(
      sys_catalog_tablet_peer->tablet(), &tablet_snapshot_req);

  CountDownLatch latch(1);
  operation->set_completion_callback(
      tablet::MakeLatchOperationCompletionCallback(&latch, &tablet_snapshot_resp));

  sys_catalog_tablet_peer->Submit(std::move(operation), term);

  // Now restore tablet metadata.
  tserver::ExportedTabletMetadataChanges tablet_metadata_changes;
  RETURN_NOT_OK(ReadPBContainerFromPath(
      Env::Default(),
      JoinPathSegments(initial_snapshot_path, kSysCatalogSnapshotTabletMetadataChangesFile),
      &tablet_metadata_changes));
  for (const auto& change_metadata_req : tablet_metadata_changes.metadata_changes()) {
    RETURN_NOT_OK(tablet::SyncReplicateChangeMetadataOperation(
        &change_metadata_req,
        sys_catalog_tablet_peer,
        term));
  }
  LOG(INFO) << "Imported " << tablet_metadata_changes.metadata_changes_size()
            << " tablet metadata changes";

  latch.Wait();
  return Status::OK();
}

void SetDefaultInitialSysCatalogSnapshotFlags() {
  // Allowing to turn off the use of initial catalog snapshot with an env variable -- useful in
  // tests.
  const char* env_var_value = getenv(kUseInitialSysCatalogSnapshotEnvVar);
  if (env_var_value && strcmp(env_var_value, "0") == 0) {
    LOG(INFO) << "Disabling the use of initial sys catalog snapshot: env var "
              << kUseInitialSysCatalogSnapshotEnvVar << " is set to 0";
    FLAGS_use_initial_sys_catalog_snapshot = 0;
    FLAGS_enable_ysql = 0;
  }

  if (FLAGS_initial_sys_catalog_snapshot_path.empty() &&
      !FLAGS_create_initial_sys_catalog_snapshot &&
      (FLAGS_use_initial_sys_catalog_snapshot || FLAGS_enable_ysql)) {
    const char* kStaticDataParentDir = "share";
    const std::string search_for_dir = JoinPathSegments(
        kStaticDataParentDir, kDefaultInitialSysCatalogSnapshotDir,
        kSysCatalogSnapshotRocksDbSubDir);
    VLOG(1) << "Searching for directory containing subdirectory " << search_for_dir;
    const string candidate_dir =
        JoinPathSegments(
            env_util::GetRootDir(search_for_dir),
            kStaticDataParentDir,
            kDefaultInitialSysCatalogSnapshotDir);
    VLOG(1) << "candidate_dir=" << candidate_dir;

    // The metadata changes file is written last, so its presence indicates that the snapshot
    // was successful.
    const string candidate_metadata_changes_path =
        JoinPathSegments(candidate_dir, kSysCatalogSnapshotTabletMetadataChangesFile);
    VLOG(1) << "candidate_metadata_changes_path=" << candidate_metadata_changes_path;

    if (Env::Default()->FileExists(candidate_metadata_changes_path)) {
      VLOG(1) << "Found initial sys catalog snapshot directory: " << candidate_dir;
      CHECK_OK(SetFlagDefaultAndCurrent("initial_sys_catalog_snapshot_path", candidate_dir));
      return;
    } else {
      VLOG(1) << "File " << candidate_metadata_changes_path << " does not exist";
    }
  } else {
    VLOG(1)
        << "Not attempting initial sys catalog snapshot auto-detection: "
        << "FLAGS_initial_sys_catalog_snapshot_path="
        << FLAGS_initial_sys_catalog_snapshot_path << ", "
        << "FLAGS_create_initial_sys_catalog_snapshot="
        << FLAGS_create_initial_sys_catalog_snapshot << ", "
        << "FLAGS_use_initial_sys_catalog_snapshot="
        << FLAGS_use_initial_sys_catalog_snapshot << ", "
        << "FLAGS_enable_ysql="
        << FLAGS_enable_ysql;
  }
}

Status MakeYsqlSysCatalogTablesTransactional(
    TableInfoMap* table_ids_map,
    SysCatalogTable* sys_catalog,
    SysConfigInfo* ysql_catalog_config,
    int64_t term) {
  {
    auto ysql_catalog_config_lock = ysql_catalog_config->LockForRead();
    const auto& ysql_catalog_config_pb = ysql_catalog_config_lock->pb.ysql_catalog_config();
    if (ysql_catalog_config_pb.transactional_sys_catalog_enabled()) {
      LOG(INFO) << "YSQL catalog tables are already transactional";
      return Status::OK();
    }
  }

  int num_updated_tables = 0;
  for (const auto& iter : *table_ids_map) {
    const auto& table_id = iter.first;
    auto& table_info = *iter.second;

    if (!IsPgsqlId(table_id)) {
      continue;
    }

    {
      TabletInfos tablet_infos = table_info.GetTablets();
      if (tablet_infos.size() != 1 || tablet_infos.front()->tablet_id() != kSysCatalogTabletId) {
        continue;
      }
     }

    auto table_lock = table_info.LockForWrite();
    auto& schema = *table_lock.mutable_data()->mutable_schema();
    auto& table_properties = *schema.mutable_table_properties();

    bool should_modify = false;
    if (!table_properties.is_ysql_catalog_table()) {
      table_properties.set_is_ysql_catalog_table(true);
      should_modify = true;
    }
    if (!table_properties.is_transactional()) {
      table_properties.set_is_transactional(true);
      should_modify = true;
    }
    if (!should_modify) {
      continue;
    }

    num_updated_tables++;
    LOG(INFO) << "Making YSQL system catalog table transactional: " << table_info.ToString();

    // Change table properties in tablet metadata.
    tablet::ChangeMetadataRequestPB change_req;
    change_req.set_tablet_id(kSysCatalogTabletId);
    auto& add_table = *change_req.mutable_add_table();
    VERIFY_RESULT(sys_catalog->tablet_peer()->tablet_metadata()->GetTableInfo(table_id))->ToPB(
        &add_table);
    auto& metadata_table_properties = *add_table.mutable_schema()->mutable_table_properties();
    metadata_table_properties.set_is_ysql_catalog_table(true);
    metadata_table_properties.set_is_transactional(true);

    RETURN_NOT_OK(tablet::SyncReplicateChangeMetadataOperation(
        &change_req, sys_catalog->tablet_peer().get(), term));

    // Change table properties in the sys catalog. We do this after updating tablet metadata, so
    // that if a restart happens before this step succeeds, we'll retry updating both next time.
    RETURN_NOT_OK(sys_catalog->Upsert(term, &table_info));
    table_lock.Commit();
  }

  if (num_updated_tables > 0) {
    LOG(INFO) << "Made " << num_updated_tables << " YSQL sys catalog tables transactional";
  }

  LOG(INFO) << "Marking YSQL system catalog as transactional in YSQL catalog config";
  {
    auto ysql_catalog_lock = ysql_catalog_config->LockForWrite();
    auto* ysql_catalog_config_pb =
        ysql_catalog_lock.mutable_data()->pb.mutable_ysql_catalog_config();
    ysql_catalog_config_pb->set_transactional_sys_catalog_enabled(true);
    RETURN_NOT_OK(sys_catalog->Upsert(term, ysql_catalog_config));
    ysql_catalog_lock.Commit();
  }

  return Status::OK();
}

}  // namespace master
}  // namespace yb
