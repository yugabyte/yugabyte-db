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

#include "yb/master/clone/clone_state_manager.h"

#include <mutex>

#include "yb/common/snapshot.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/map-util.h"
#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/clone/clone_state_entity.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/master_types.pb.h"
#include "yb/master/sys_catalog.h"
#include "yb/rpc/rpc_context.h"
#include "yb/util/oid_generator.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

DEFINE_RUNTIME_PREVIEW_bool(enable_db_clone, false, "Enable DB cloning.");
DECLARE_int32(ysql_clone_pg_schema_rpc_timeout_ms);

namespace yb {
namespace master {

using std::string;
using namespace std::literals;
using namespace std::placeholders;

std::unique_ptr<CloneStateManager> CloneStateManager::Create(
    CatalogManagerIf* catalog_manager, Master* master, SysCatalogTable* sys_catalog) {
  ExternalFunctions external_functions = {
      .ListSnapshotSchedules =
          [&snapshot_coordinator =
               catalog_manager->snapshot_coordinator()](ListSnapshotSchedulesResponsePB* resp) {
            auto schedule_id = SnapshotScheduleId::Nil();
            return snapshot_coordinator.ListSnapshotSchedules(schedule_id, resp);
          },
      .Restore =
          [catalog_manager, &snapshot_coordinator = catalog_manager->snapshot_coordinator()](
              const TxnSnapshotId& snapshot_id, HybridTime restore_at) {
            return snapshot_coordinator.Restore(
                snapshot_id, restore_at, catalog_manager->leader_ready_term());
          },
      .ListRestorations =
          [&snapshot_coordinator = catalog_manager->snapshot_coordinator()](
              const TxnSnapshotId& snapshot_id, ListSnapshotRestorationsResponsePB* resp) {
            return snapshot_coordinator.ListRestorations(
                TxnSnapshotRestorationId::Nil(), snapshot_id, resp);
          },

      .GetTabletInfo =
          [catalog_manager](const TabletId& tablet_id) {
            return catalog_manager->GetTabletInfo(tablet_id);
          },
      .FindNamespace =
          [catalog_manager](const NamespaceIdentifierPB& ns_identifier) {
            return catalog_manager->FindNamespace(ns_identifier);
          },
      .ScheduleCloneTabletCall =
          [catalog_manager, master](
              const TabletInfoPtr& source_tablet, LeaderEpoch epoch,
              tablet::CloneTabletRequestPB req) {
            auto call = std::make_shared<AsyncCloneTablet>(
                master, catalog_manager->AsyncTaskPool(), source_tablet, epoch, std::move(req));
            return catalog_manager->ScheduleTask(call);
          },
      .ScheduleClonePGSchemaTask =
          [catalog_manager, master](
              const std::string& ts_permanent_uuid, const std::string& source_db_name,
              const std::string& clone_db_name, HybridTime restore_ht,
              AsyncClonePgSchema::ClonePgSchemaCallbackType callback, MonoTime deadline) {
            auto task = std::make_shared<AsyncClonePgSchema>(
                master, catalog_manager->AsyncTaskPool(), ts_permanent_uuid, source_db_name,
                clone_db_name, restore_ht, callback, deadline);
            return catalog_manager->ScheduleTask(task);
          },
      .DoCreateSnapshot =
          [catalog_manager](
              const CreateSnapshotRequestPB* req, CreateSnapshotResponsePB* resp,
              CoarseTimePoint deadline, const LeaderEpoch& epoch) {
            return catalog_manager->DoCreateSnapshot(req, resp, deadline, epoch);
          },
      .GenerateSnapshotInfoFromSchedule =
          [catalog_manager](
              const SnapshotScheduleId& snapshot_schedule_id, HybridTime export_time,
              CoarseTimePoint deadline) {
            return catalog_manager->GenerateSnapshotInfoFromSchedule(
                snapshot_schedule_id, export_time, deadline);
          },
      .DoImportSnapshotMeta =
          [catalog_manager](
              const SnapshotInfoPB& snapshot_pb, const LeaderEpoch& epoch,
              const std::optional<std::string>& clone_target_namespace_name,
              NamespaceMap* namespace_map, UDTypeMap* type_map,
              ExternalTableSnapshotDataMap* tables_data, CoarseTimePoint deadline) {
            return catalog_manager->DoImportSnapshotMeta(
          snapshot_pb, epoch, clone_target_namespace_name, namespace_map, type_map, tables_data,
          deadline);
          },
      // Pick tserver to execute ClonePgSchema operation
      // TODO(Yamen): modify to choose the tserver the closest to the master leader.
      .PickTserver =
          [catalog_manager]() { return catalog_manager->GetAllLiveNotBlacklistedTServers()[0]; },

      .Upsert =
          [catalog_manager, sys_catalog](const CloneStateInfoPtr& clone_state) {
            return sys_catalog->Upsert(catalog_manager->leader_ready_term(), clone_state);
          },
      .Load =
          [sys_catalog](
              const std::string& type,
              std::function<Status(const std::string&, const SysCloneStatePB&)> inserter) {
            return sys_catalog->Load<CloneStateLoader, SysCloneStatePB>(type, inserter);
          }};

  return std::unique_ptr<CloneStateManager>(new CloneStateManager(std::move(external_functions)));
}

CloneStateManager::CloneStateManager(ExternalFunctions external_functions):
    external_funcs_(std::move(external_functions)) {}

Status CloneStateManager::IsCloneDone(
    const IsCloneDoneRequestPB* req, IsCloneDoneResponsePB* resp) {
  auto clone_state = VERIFY_RESULT(GetCloneStateFromSourceNamespace(req->source_namespace_id()));
  auto lock = clone_state->LockForRead();
  auto seq_no = req->seq_no();
  auto current_seq_no = lock->pb.clone_request_seq_no();
  if (current_seq_no > seq_no) {
    resp->set_is_done(true);
  } else if (current_seq_no == seq_no) {
    resp->set_is_done(lock->pb.aggregate_state() == SysCloneStatePB::RESTORED);
  } else {
    return STATUS_FORMAT(IllegalState,
        "Clone seq_no $0 never started for namespace $1 (current seq no $2)",
        seq_no, req->source_namespace_id(), current_seq_no);
  }
  return Status::OK();
}

Status CloneStateManager::CloneNamespace(
    const CloneNamespaceRequestPB* req,
    CloneNamespaceResponsePB* resp,
    rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  SCHECK(!req->target_namespace_name().empty(), InvalidArgument, "Got empty target namespace name");
  LOG(INFO) << "Servicing CloneNamespace request: " << req->ShortDebugString();
  auto restore_time = HybridTime(req->restore_ht());
  auto [source_namespace_id, seq_no] = VERIFY_RESULT(CloneNamespace(
      req->source_namespace(), restore_time, req->target_namespace_name(), rpc->GetClientDeadline(),
      epoch));
  resp->set_source_namespace_id(source_namespace_id);
  resp->set_seq_no(seq_no);
  return Status::OK();
}

Result<std::pair<NamespaceId, uint32_t>> CloneStateManager::CloneNamespace(
    const NamespaceIdentifierPB& source_namespace_identifier,
    const HybridTime& restore_time,
    const std::string& target_namespace_name,
    CoarseTimePoint deadline,
    const LeaderEpoch& epoch) {
  if (!FLAGS_enable_db_clone) {
    return STATUS_FORMAT(ConfigurationError, "FLAGS_enable_db_clone is disabled");
  }

  if (!source_namespace_identifier.has_database_type() || !source_namespace_identifier.has_name()) {
    return STATUS_FORMAT(
        InvalidArgument, "Expected source namespace identifier to have database type and name. "
        "Got: $0", source_namespace_identifier.ShortDebugString());
  }
  ListSnapshotSchedulesResponsePB resp;
  RETURN_NOT_OK(external_funcs_.ListSnapshotSchedules(&resp));
  auto snapshot_schedule_id = SnapshotScheduleId::Nil();
  for (const auto& schedule : resp.schedules()) {
    auto& tables = schedule.options().filter().tables().tables();
    if (!tables.empty() &&
        tables[0].namespace_().name() == source_namespace_identifier.name() &&
        tables[0].namespace_().database_type() == source_namespace_identifier.database_type()) {
      snapshot_schedule_id = VERIFY_RESULT(FullyDecodeSnapshotScheduleId(schedule.id()));
      break;
    }
  }
  if (snapshot_schedule_id.IsNil()) {
    return STATUS_FORMAT(
        InvalidArgument, "Could not find snapshot schedule for namespace $0",
        source_namespace_identifier.name());
  }

  auto source_namespace = VERIFY_RESULT(external_funcs_.FindNamespace(source_namespace_identifier));
  auto seq_no = source_namespace->FetchAndIncrementCloneSeqNo();
  const auto source_namespace_id = source_namespace->id();

  // Set up persisted clone state.
  auto clone_state = VERIFY_RESULT(CreateCloneState(seq_no, source_namespace, restore_time));

  // Clone PG Schema objects first in case of PGSQL databases. Tablets cloning is initiated in the
  // callback of ClonePGSchemaObjects async task.
  if (source_namespace->database_type() == YQL_DATABASE_PGSQL) {
    RETURN_NOT_OK(ClonePgSchemaObjects(
        clone_state, source_namespace->name(), target_namespace_name, snapshot_schedule_id,
        restore_time, epoch));
  } else {
    // For YCQL start tablets cloning directly
    RETURN_NOT_OK(StartTabletsCloning(
        clone_state, snapshot_schedule_id, restore_time, target_namespace_name, deadline, epoch));
  }
  return make_pair(source_namespace_id, seq_no);
}

Status CloneStateManager::StartTabletsCloningYsql(
    CloneStateInfoPtr clone_state, const SnapshotScheduleId& snapshot_schedule_id,
    const HybridTime& restore_time, const std::string& target_namespace_name,
    CoarseTimePoint deadline, const LeaderEpoch& epoch, Status pg_schema_cloning_status) {
  if (!pg_schema_cloning_status.ok()) {
    return pg_schema_cloning_status;
  }
  // Transition the clone state from CLONE_SCHEMA_STARTED to CREATING
  auto lock = clone_state->LockForWrite();
  auto& pb = lock.mutable_data()->pb;
  pb.set_aggregate_state(SysCloneStatePB::CREATING);
  RETURN_NOT_OK(external_funcs_.Upsert(clone_state));
  lock.Commit();
  return StartTabletsCloning(
      clone_state, snapshot_schedule_id, restore_time, target_namespace_name, deadline, epoch);
}

Status CloneStateManager::StartTabletsCloning(
    CloneStateInfoPtr clone_state, const SnapshotScheduleId& snapshot_schedule_id,
    const HybridTime& restore_time, const std::string& target_namespace_name,
    CoarseTimePoint deadline, const LeaderEpoch& epoch) {
  // Export snapshot info.
  auto snapshot_info = VERIFY_RESULT(external_funcs_.GenerateSnapshotInfoFromSchedule(
      snapshot_schedule_id, restore_time, deadline));
  auto source_snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(snapshot_info.id()));

  // Import snapshot info.
  NamespaceMap namespace_map;
  UDTypeMap type_map;
  ExternalTableSnapshotDataMap tables_data;
  RETURN_NOT_OK(external_funcs_.DoImportSnapshotMeta(
      snapshot_info, epoch, target_namespace_name, &namespace_map, &type_map, &tables_data,
      deadline));
  if (namespace_map.size() != 1) {
    return STATUS_FORMAT(IllegalState, "Expected 1 namespace, got $0", namespace_map.size());
  }

  // Generate a new snapshot.
  // All indexes already are in the request. Do not add them twice.
  // It is safe to trigger the clone op immediately after this since imported snapshots are created
  // synchronously.
  // TODO: Change this and xrepl_catalog_manager to use CreateSnapshot so all our snapshots go
  // through the same path.
  CreateSnapshotRequestPB create_snapshot_req;
  CreateSnapshotResponsePB create_snapshot_resp;
  for (const auto& [old_table_id, table_data] : tables_data) {
    const auto& table_meta = *table_data.table_meta;
    const string& new_table_id = table_meta.table_ids().new_id();
    if (!ImportSnapshotMetaResponsePB_TableType_IsValid(table_meta.table_type())) {
      return STATUS_FORMAT(InternalError, "Found unknown table type: ", table_meta.table_type());
    }

    create_snapshot_req.mutable_tables()->Add()->set_table_id(new_table_id);
  }
  create_snapshot_req.set_add_indexes(false);
  create_snapshot_req.set_transaction_aware(true);
  create_snapshot_req.set_imported(true);
  RETURN_NOT_OK(external_funcs_.DoCreateSnapshot(
      &create_snapshot_req, &create_snapshot_resp, deadline, epoch));
  if (create_snapshot_resp.has_error()) {
    return StatusFromPB(create_snapshot_resp.error().status());
  }
  auto target_snapshot_id =
      VERIFY_RESULT(FullyDecodeTxnSnapshotId(create_snapshot_resp.snapshot_id()));

  // Set up the rest of the clone state fields.
  RETURN_NOT_OK(UpdateCloneStateWithSnapshotInfo(
      clone_state, source_snapshot_id, target_snapshot_id, tables_data));

  RETURN_NOT_OK(ScheduleCloneOps(clone_state, epoch));
  return Status::OK();
}

Status CloneStateManager::ClonePgSchemaObjects(
    CloneStateInfoPtr clone_state, const std::string& source_db_name,
    const std::string& target_db_name, const SnapshotScheduleId& snapshot_schedule_id,
    const HybridTime& restore_time, const LeaderEpoch& epoch) {
  // Pick one of the live tservers to send ysql_dump and ysqlsh requests to.
  auto ts = external_funcs_.PickTserver();
  auto ts_permanent_uuid = ts->permanent_uuid();
  auto read_lock = clone_state->LockForRead();
  // Deadline passed to the ClonePGSchemaTask (including rpc time and callback execution deadline)
  auto deadline = MonoTime::Now() + FLAGS_ysql_clone_pg_schema_rpc_timeout_ms * 1ms;
  RETURN_NOT_OK(external_funcs_.ScheduleClonePGSchemaTask(
      ts_permanent_uuid, source_db_name, target_db_name, HybridTime(read_lock->pb.restore_time()),
      MakeDoneClonePGSchemaCallback(
          clone_state, snapshot_schedule_id, restore_time, target_db_name, ToCoarse(deadline),
          epoch),
      deadline));
  return Status::OK();
}

Status CloneStateManager::ClearAndRunLoaders() {
  {
    std::lock_guard l(mutex_);
    source_clone_state_map_.clear();
  }
  RETURN_NOT_OK(external_funcs_.Load(
      "Clone states",
      std::function<Status(const std::string&, const SysCloneStatePB&)>(
          std::bind(&CloneStateManager::LoadCloneState, this, _1, _2))));

  return Status::OK();
}

Status CloneStateManager::LoadCloneState(const std::string& id, const SysCloneStatePB& metadata) {
  auto clone_state = CloneStateInfoPtr(new CloneStateInfo(id));
  clone_state->Load(metadata);
  std::lock_guard lock(mutex_);
  auto read_lock = clone_state->LockForRead();
  auto& source_namespace_id = read_lock->pb.source_namespace_id();
  auto seq_no = read_lock->pb.clone_request_seq_no();

  auto it = source_clone_state_map_.find(source_namespace_id);
  if (it != source_clone_state_map_.end()) {
    auto existing_seq_no = it->second->LockForRead()->pb.clone_request_seq_no();
    LOG(INFO) << Format(
        "Found existing clone state for source namespace $0 with seq_no $1. This clone "
        "state's seq_no is $2", source_namespace_id, existing_seq_no, seq_no);
    if (seq_no < existing_seq_no) {
      // Do not overwrite the higher seq_no clone state.
      return Status::OK();
    }
    // TODO: Delete clone state with lower seq_no from sys catalog.
  }
  source_clone_state_map_[source_namespace_id] = clone_state;
  return Status::OK();
}

Result<CloneStateInfoPtr> CloneStateManager::CreateCloneState(
    uint32_t seq_no, const scoped_refptr<NamespaceInfo>& source_namespace,
    const HybridTime& restore_time) {
  NamespaceId source_namespace_id = source_namespace->id();
  CloneStateInfoPtr clone_state;
  {
    std::lock_guard lock(mutex_);
    auto it = source_clone_state_map_.find(source_namespace_id);
    if (it != source_clone_state_map_.end()) {
      auto state = it->second->LockForRead()->pb.aggregate_state();
      if (state != SysCloneStatePB::RESTORED) {
        return STATUS_FORMAT(
            AlreadyPresent,
            "Cannot create new clone state because there is already an ongoing "
            "clone for source namespace $0 in state $1",
            source_namespace_id, state);
      }
      // One day we might want to clean up the replaced clone state object here instead of at load
      // time.
    }
    clone_state = CloneStateInfoPtr(new CloneStateInfo(GenerateObjectId()));
    source_clone_state_map_[source_namespace_id] = clone_state;
  }
  clone_state->mutable_metadata()->StartMutation();
  auto* pb = &clone_state->mutable_metadata()->mutable_dirty()->pb;
  pb->set_source_namespace_id(source_namespace_id);
  pb->set_clone_request_seq_no(seq_no);
  pb->set_restore_time(restore_time.ToUint64());
  // Clone PG schema is needed for PGSQL, otherwise start directly with the next step.
  pb->set_aggregate_state(
      source_namespace->database_type() == YQL_DATABASE_PGSQL
          ? SysCloneStatePB::CLONE_SCHEMA_STARTED
          : SysCloneStatePB::CREATING);

  RETURN_NOT_OK(external_funcs_.Upsert(clone_state));
  clone_state->mutable_metadata()->CommitMutation();
  return clone_state;
}

Result<CloneStateInfoPtr> CloneStateManager::CreateCloneState(
    uint32_t seq_no, const NamespaceId& source_namespace_id,
    const TxnSnapshotId& source_snapshot_id, const TxnSnapshotId& target_snapshot_id,
    const HybridTime& restore_time, const ExternalTableSnapshotDataMap& table_snapshot_data) {
  CloneStateInfoPtr clone_state;
  {
    std::lock_guard lock(mutex_);
    auto it = source_clone_state_map_.find(source_namespace_id);
    if (it != source_clone_state_map_.end()) {
      auto state = it->second->LockForRead()->pb.aggregate_state();
      if (state != SysCloneStatePB::RESTORED) {
        return STATUS_FORMAT(
            AlreadyPresent, "Cannot create new clone state because there is already an ongoing "
            "clone for source namespace $0 in state $1", source_namespace_id, state);
      }
      // One day we might want to clean up the replaced clone state object here instead of at load
      // time.
    }
    clone_state = CloneStateInfoPtr(new CloneStateInfo(GenerateObjectId()));
    source_clone_state_map_[source_namespace_id] = clone_state;
  }

  clone_state->mutable_metadata()->StartMutation();
  auto* pb = &clone_state->mutable_metadata()->mutable_dirty()->pb;
  pb->set_clone_request_seq_no(seq_no);
  pb->set_source_snapshot_id(source_snapshot_id.data(), source_snapshot_id.size());
  pb->set_target_snapshot_id(target_snapshot_id.data(), target_snapshot_id.size());
  pb->set_source_namespace_id(source_namespace_id);
  pb->set_restore_time(restore_time.ToUint64());

  // Add data for each tablet in this table.
  std::unordered_map<TabletId, int> target_tablet_to_index;
  for (const auto& [_, table_snapshot_data] : table_snapshot_data) {
    for (auto& tablet : table_snapshot_data.table_meta->tablets_ids()) {
      auto* tablet_data = pb->add_tablet_data();
      tablet_data->set_source_tablet_id(tablet.old_id());
      tablet_data->set_target_tablet_id(tablet.new_id());
      target_tablet_to_index[tablet.new_id()] = pb->tablet_data_size() - 1;
    }
  }
  // If this namespace somehow has 0 tablets, create it as RESTORED.
  pb->set_aggregate_state(
      target_tablet_to_index.empty() ? SysCloneStatePB::RESTORED : SysCloneStatePB::CREATING);

  RETURN_NOT_OK(external_funcs_.Upsert(clone_state));
  clone_state->mutable_metadata()->CommitMutation();
  return clone_state;
}

Status CloneStateManager::UpdateCloneStateWithSnapshotInfo(
    CloneStateInfoPtr clone_state, const TxnSnapshotId& source_snapshot_id,
    const TxnSnapshotId& target_snapshot_id,
    const ExternalTableSnapshotDataMap& table_snapshot_data) {
  clone_state->mutable_metadata()->StartMutation();
  auto* pb = &clone_state->mutable_metadata()->mutable_dirty()->pb;
  pb->set_source_snapshot_id(source_snapshot_id.data(), source_snapshot_id.size());
  pb->set_target_snapshot_id(target_snapshot_id.data(), target_snapshot_id.size());

  // Add data for each tablet in this table.
  std::unordered_map<TabletId, int> target_tablet_to_index;
  for (const auto& [_, table_snapshot_data] : table_snapshot_data) {
    for (auto& tablet : table_snapshot_data.table_meta->tablets_ids()) {
      auto* tablet_data = pb->add_tablet_data();
      tablet_data->set_source_tablet_id(tablet.old_id());
      tablet_data->set_target_tablet_id(tablet.new_id());
      target_tablet_to_index[tablet.new_id()] = pb->tablet_data_size() - 1;
    }
  }
  RETURN_NOT_OK(external_funcs_.Upsert(clone_state));
  clone_state->mutable_metadata()->CommitMutation();
  return Status::OK();
}

Status CloneStateManager::ScheduleCloneOps(
    const CloneStateInfoPtr& clone_state, const LeaderEpoch& epoch) {
  auto lock = clone_state->LockForRead();
  auto& pb = lock->pb;
  for (auto& tablet_data : pb.tablet_data()) {
    auto source_tablet = VERIFY_RESULT(
        external_funcs_.GetTabletInfo(tablet_data.source_tablet_id()));
    auto target_tablet = VERIFY_RESULT(
        external_funcs_.GetTabletInfo(tablet_data.target_tablet_id()));
    auto target_table = target_tablet->table();
    auto target_table_lock = target_table->LockForRead();

    tablet::CloneTabletRequestPB req;
    req.set_tablet_id(tablet_data.source_tablet_id());
    req.set_target_tablet_id(tablet_data.target_tablet_id());
    req.set_source_snapshot_id(pb.source_snapshot_id().data(), pb.source_snapshot_id().size());
    req.set_target_snapshot_id(pb.target_snapshot_id().data(), pb.target_snapshot_id().size());
    req.set_target_table_id(target_table->id());
    req.set_target_namespace_name(target_table_lock->namespace_name());
    req.set_clone_request_seq_no(pb.clone_request_seq_no());
    req.set_target_pg_table_id(target_table_lock->pb.pg_table_id());
    if (target_table_lock->pb.has_index_info()) {
      *req.mutable_target_index_info() = target_table_lock->pb.index_info();
    }
    *req.mutable_target_schema() = target_table_lock->pb.schema();
    *req.mutable_target_partition_schema() = target_table_lock->pb.partition_schema();
    RETURN_NOT_OK(external_funcs_.ScheduleCloneTabletCall(source_tablet, epoch, std::move(req)));
  }
  return Status::OK();
}

Result<CloneStateInfoPtr> CloneStateManager::GetCloneStateFromSourceNamespace(
    const NamespaceId& namespace_id) {
  std::lock_guard lock(mutex_);
  return FIND_OR_RESULT(source_clone_state_map_, namespace_id);
}

AsyncClonePgSchema::ClonePgSchemaCallbackType CloneStateManager::MakeDoneClonePGSchemaCallback(
    CloneStateInfoPtr clone_state, const SnapshotScheduleId& snapshot_schedule_id,
    const HybridTime& restore_time, const std::string& target_namespace_name,
    CoarseTimePoint deadline, const LeaderEpoch& epoch) {
  return [this, clone_state, snapshot_schedule_id, restore_time, target_namespace_name, deadline,
          epoch](Status pg_schema_cloning_status) {
    return StartTabletsCloningYsql(
        clone_state, snapshot_schedule_id, restore_time, target_namespace_name, deadline, epoch,
        pg_schema_cloning_status);
  };
}

Status CloneStateManager::HandleCreatingState(const CloneStateInfoPtr& clone_state) {
  auto lock = clone_state->LockForWrite();

  SCHECK_EQ(lock->pb.aggregate_state(), SysCloneStatePB::CREATING, IllegalState,
      "Expected clone to be in creating state");

  bool all_tablets_running = true;
  auto& pb = lock.mutable_data()->pb;
  for (auto& tablet_data : *pb.mutable_tablet_data()) {
    // Check to see if the tablet is done cloning (i.e. it is RUNNING).
    auto tablet = VERIFY_RESULT(external_funcs_.GetTabletInfo(tablet_data.target_tablet_id()));
    if (!tablet->LockForRead()->is_running()) {
      all_tablets_running = false;
    }
  }

  if (!all_tablets_running) {
    return Status::OK();
  }

  LOG(INFO) << Format("All tablets for cloned namespace $0 with seq_no $1 are running. "
      "Marking clone operation as restoring.",
      pb.source_namespace_id(), pb.clone_request_seq_no());
  auto target_snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(pb.target_snapshot_id()));

  // Check for an existing restoration id. This might have happened if a restoration was submitted
  // but we crashed / failed over before we were able to persist the clone state as RESTORING.
  ListSnapshotRestorationsResponsePB resp;
  RETURN_NOT_OK(external_funcs_.ListRestorations(target_snapshot_id, &resp));
  if (resp.restorations().empty()) {
    RETURN_NOT_OK(external_funcs_.Restore(target_snapshot_id, HybridTime(pb.restore_time())));
  }
  pb.set_aggregate_state(SysCloneStatePB::RESTORING);

  RETURN_NOT_OK(external_funcs_.Upsert(clone_state));
  lock.Commit();
  return Status::OK();
}

Status CloneStateManager::HandleRestoringState(const CloneStateInfoPtr& clone_state) {
  auto lock = clone_state->LockForWrite();
  auto target_snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(lock->pb.target_snapshot_id()));

  ListSnapshotRestorationsResponsePB resp;
  RETURN_NOT_OK(external_funcs_.ListRestorations(target_snapshot_id, &resp));
  SCHECK_EQ(resp.restorations_size(), 1, IllegalState, "Unexpected number of restorations.");

  if (resp.restorations(0).entry().state() != SysSnapshotEntryPB::RESTORED) {
    return Status::OK();
  }

  lock.mutable_data()->pb.set_aggregate_state(SysCloneStatePB::RESTORED);
  RETURN_NOT_OK(external_funcs_.Upsert(clone_state));
  lock.Commit();
  return Status::OK();
}

Status CloneStateManager::Run() {
  // Copy is required to avoid deadlocking with the catalog manager mutex in
  // CatalogManager::RunLoaders, which calls CloneStateManager::ClearAndRunLoaders.
  CloneStateMap source_clone_state_map;
  {
    std::lock_guard lock(mutex_);
    source_clone_state_map = source_clone_state_map_;
  }
  for (auto& [source_namespace_id, clone_state] : source_clone_state_map) {
    Status s;
    switch (clone_state->LockForRead()->pb.aggregate_state()) {
      case SysCloneStatePB::CREATING:
        s = HandleCreatingState(clone_state);
        break;
      case SysCloneStatePB::RESTORING:
        s = HandleRestoringState(clone_state);
        break;
      case SysCloneStatePB::CLONE_SCHEMA_STARTED:
        FALLTHROUGH_INTENDED;
      case SysCloneStatePB::RESTORED:
        break;
    }
    WARN_NOT_OK(s,
        Format("Could not handle clone state for source namespace $0", source_namespace_id));
  }
  return Status::OK();
}

} // namespace master
} // namespace yb
