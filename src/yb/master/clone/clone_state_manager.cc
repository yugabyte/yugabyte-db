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

#include "yb/master/clone/clone_state_manager.h"

#include <mutex>

#include "yb/common/colocated_util.h"
#include "yb/common/common_flags.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/ref_counted.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/clone/clone_state_entity.h"
#include "yb/master/clone/external_functions.h"
#include "yb/master/master.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/master_types.pb.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/tablet_creation_limits.h"
#include "yb/master/ts_manager.h"

#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/util/flags/flag_tags.h"
#include "yb/util/monotime.h"
#include "yb/util/oid_generator.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

DEFINE_RUNTIME_bool(enable_db_clone, false, "Enable DB cloning.");
TAG_FLAG(enable_db_clone, advanced);

DECLARE_int32(ysql_clone_pg_schema_rpc_timeout_ms);
DEFINE_test_flag(bool, fail_clone_pg_schema, false, "Fail clone pg schema operation for testing");
DEFINE_test_flag(bool, fail_clone_tablets, false, "Fail ImportSnapshotAndStartTabletsCloning for "
    "testing");

namespace yb {
namespace master {

using std::string;
using namespace std::literals;
using namespace std::placeholders;

class CloneStateManagerExternalFunctions : public CloneStateManagerExternalFunctionsBase {
 public:
  CloneStateManagerExternalFunctions(
      CatalogManagerIf* catalog_manager, Master* master, SysCatalogTable* sys_catalog)
      : catalog_manager_(catalog_manager), master_(master), sys_catalog_(sys_catalog) {}

  ~CloneStateManagerExternalFunctions() {}

  Status ListSnapshotSchedules(ListSnapshotSchedulesResponsePB* resp) override {
    auto schedule_id = SnapshotScheduleId::Nil();
    return master_->snapshot_coordinator().ListSnapshotSchedules(schedule_id, resp);
  }

  Status DeleteSnapshot(const TxnSnapshotId& snapshot_id) override {
    return master_->snapshot_coordinator().Delete(
        snapshot_id, catalog_manager_->leader_ready_term(), CoarseMonoClock::Now() + 30s);
  }

  Result<TxnSnapshotRestorationId> Restore(
      const TxnSnapshotId& snapshot_id, HybridTime restore_at) override {
    return master_->snapshot_coordinator().Restore(
        snapshot_id, restore_at, catalog_manager_->leader_ready_term());
  }

  Status ListRestorations(
      const TxnSnapshotRestorationId& restoration_id, ListSnapshotRestorationsResponsePB* resp)
      override {
    return master_->snapshot_coordinator().ListRestorations(
        restoration_id, TxnSnapshotId::Nil(), resp);
  }

  // Catalog manager.
  Result<TabletInfoPtr> GetTabletInfo(const TabletId& tablet_id) override {
    return catalog_manager_->GetTabletInfo(tablet_id);
  }

  Result<NamespaceInfoPtr> FindNamespace(const NamespaceIdentifierPB& ns_identifier) override {
    return catalog_manager_->FindNamespace(ns_identifier);
  }

  Status ScheduleCloneTabletCall(
      const TabletInfoPtr& source_tablet, LeaderEpoch epoch, tablet::CloneTabletRequestPB req)
      override {
    auto call = std::make_shared<AsyncCloneTablet>(
        master_, catalog_manager_->AsyncTaskPool(), source_tablet, epoch, std::move(req));
    return catalog_manager_->ScheduleTask(call);
  }

  Status ScheduleClonePgSchemaTask(
      const TabletServerId& ts_uuid, const std::string& source_db_name,
      const std::string& target_db_name, const std::string& pg_source_owner,
      const std::string& pg_target_owner, HybridTime restore_ht,
      AsyncClonePgSchema::ClonePgSchemaCallbackType callback, MonoTime deadline) override {
    auto task = std::make_shared<AsyncClonePgSchema>(
        master_, catalog_manager_->AsyncTaskPool(), ts_uuid, source_db_name,
        target_db_name, restore_ht, pg_source_owner, pg_target_owner, std::move(callback),
        deadline);
    return catalog_manager_->ScheduleTask(task);
  }

  Status ScheduleClearMetaCacheTasks(
      const TSDescriptorVector& tservers, const std::string& namespace_id,
      AsyncClearMetacache::ClearMetacacheCallbackType callback) override {
    for (const auto& ts : tservers) {
      auto task = std::make_shared<AsyncClearMetacache>(
          master_, catalog_manager_->AsyncTaskPool(), ts->permanent_uuid(), namespace_id, callback);
      LOG(INFO) << Format(
          "Scheduling clear metacache entries task for namespace: $0 and tserver with UUID: $1",
          namespace_id, ts->permanent_uuid());
      RETURN_NOT_OK(catalog_manager_->ScheduleTask(task));
    }
    return Status::OK();
  }

  Status ScheduleEnableDbConnectionsTask(
      const TabletServerId& ts_uuid, const std::string& target_db_name,
      AsyncEnableDbConns::EnableDbConnsCallbackType callback) override {
    auto task = std::make_shared<AsyncEnableDbConns>(
        master_, catalog_manager_->AsyncTaskPool(), ts_uuid, target_db_name, callback);
    return catalog_manager_->ScheduleTask(task);
  }

  Status DoCreateSnapshot(
      const CreateSnapshotRequestPB* req, CreateSnapshotResponsePB* resp,
      CoarseTimePoint deadline, const LeaderEpoch& epoch) override {
    return catalog_manager_->DoCreateSnapshot(req, resp, deadline, epoch);
  }

  Result<CatalogManagerIf::CloneSnapshotInfo>
  GenerateSnapshotInfoFromScheduleForClone(
      const SnapshotScheduleId& snapshot_schedule_id, HybridTime export_time,
      CoarseTimePoint deadline) override {
    return catalog_manager_->GenerateSnapshotInfoFromScheduleForClone(
        snapshot_schedule_id, export_time, deadline);
  }

  Status DoImportSnapshotMeta(
      const SnapshotInfoPB& snapshot_pb, const LeaderEpoch& epoch,
      const std::optional<std::string>& clone_target_namespace_name, NamespaceMap* namespace_map,
      UDTypeMap* type_map, ExternalTableSnapshotDataMap* tables_data,
      CoarseTimePoint deadline) override {
    return catalog_manager_->DoImportSnapshotMeta(
        snapshot_pb, epoch, clone_target_namespace_name, namespace_map, type_map, tables_data,
        deadline);
  }

  // Pick tserver to execute PG operations.
  Result<TSDescriptorPtr> GetClosestLiveTserver() override {
    return catalog_manager_->GetClosestLiveTserver();
  }

  TSDescriptorVector GetTservers() override {
    return catalog_manager_->GetAllLiveNotBlacklistedTServers();
  }

  Result<BlacklistSet> GetBlacklist() override {
    return catalog_manager_->BlacklistSetFromPB();
  }

  // Sys catalog.
  Status Upsert(int64_t leader_term, const CloneStateInfoPtr& clone_state) override {
    return sys_catalog_->Upsert(leader_term, clone_state);
  }

  Status Upsert(
      int64_t leader_term, const CloneStateInfoPtr& clone_state,
      const NamespaceInfoPtr& source_namespace) override {
    return sys_catalog_->Upsert(leader_term, clone_state, source_namespace);
  }

  Status Load(
      const std::string& type,
      std::function<Status(const std::string&, const SysCloneStatePB&)> inserter) override {
    return sys_catalog_->Load<CloneStateLoader, SysCloneStatePB>(type, inserter);
  }

 private:
  CatalogManagerIf* catalog_manager_;
  Master* master_;
  SysCatalogTable* sys_catalog_;
};

std::unique_ptr<CloneStateManager> CloneStateManager::Create(
    CatalogManagerIf* catalog_manager, Master* master, SysCatalogTable* sys_catalog) {
  auto external_funcs = std::make_unique<CloneStateManagerExternalFunctions>(
      catalog_manager, master, sys_catalog);
  return std::unique_ptr<CloneStateManager>(new CloneStateManager(std::move(external_funcs)));
}

CloneStateManager::CloneStateManager(
    std::unique_ptr<CloneStateManagerExternalFunctionsBase> external_funcs):
    external_funcs_(std::move(external_funcs)) {}

Status CloneStateManager::ListClones(const ListClonesRequestPB* req, ListClonesResponsePB* resp) {
  // Get matching clone states.
  std::vector<CloneStateInfoPtr> clone_states;
  {
    std::lock_guard l(mutex_);
    if (req->has_source_namespace_id()) {
      auto it = source_clone_state_map_.find(req->source_namespace_id());
      if (it != source_clone_state_map_.end()) {
        std::copy_if(
            it->second.begin(), it->second.end(), std::back_inserter(clone_states),
            [req](const auto& clone) {
              return !req->has_seq_no() ||
                     req->seq_no() == clone->LockForRead()->pb.clone_request_seq_no();
            });
      }
    } else {
      for (const auto& source_clones : source_clone_state_map_) {
        std::copy(
            source_clones.second.begin(), source_clones.second.end(),
            std::back_inserter(clone_states));
      }
    }
  }

  // Populate the response.
  for (const auto& clone_state : clone_states) {
    SysCloneStatePB* list_clone_entry = resp->add_entries();
    list_clone_entry->CopyFrom(clone_state->LockForRead()->pb);
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
      req->source_namespace(),
      restore_time,
      req->target_namespace_name(),
      req->pg_source_owner(),
      req->pg_target_owner(),
      rpc->GetClientDeadline(),
      epoch));
  resp->set_source_namespace_id(source_namespace_id);
  resp->set_seq_no(seq_no);
  LOG_WITH_FUNC(INFO) << "source_namespace_id: " << source_namespace_id << ", seq_no: " << seq_no;
  return Status::OK();
}

Result<std::pair<NamespaceId, uint32_t>> CloneStateManager::CloneNamespace(
    const NamespaceIdentifierPB& source_namespace_identifier,
    const HybridTime& restore_time,
    const std::string& target_namespace_name,
    const std::string& pg_source_owner,
    const std::string& pg_target_owner,
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
  auto source_namespace = VERIFY_RESULT(
      external_funcs_->FindNamespace(source_namespace_identifier));

  ListSnapshotSchedulesResponsePB resp;
  RETURN_NOT_OK(external_funcs_->ListSnapshotSchedules(&resp));
  auto snapshot_schedule_id = SnapshotScheduleId::Nil();
  for (const auto& schedule : resp.schedules()) {
    auto& tables = schedule.options().filter().tables().tables();
    if (!tables.empty() && tables[0].namespace_().id() == source_namespace->id()) {
      snapshot_schedule_id = VERIFY_RESULT(FullyDecodeSnapshotScheduleId(schedule.id()));
      break;
    }
  }
  if (snapshot_schedule_id.IsNil()) {
    return STATUS_FORMAT(
        InvalidArgument, "Could not find snapshot schedule for namespace $0",
        source_namespace_identifier.name());
  }

  // Set up clone state.
  // Past this point, we should abort the clone state if we get a non-OK status from any step.
  auto clone_state = VERIFY_RESULT(CreateCloneState(
      epoch, source_namespace, source_namespace_identifier.database_type(),
      target_namespace_name, restore_time));

  auto status = TryCloneNamespace(clone_state, snapshot_schedule_id, deadline, source_namespace,
      target_namespace_name, pg_source_owner, pg_target_owner);
  if (!status.ok()) {
    WARN_NOT_OK(MarkCloneAborted(clone_state, status.ToString()), "Failed to mark clone aborted");
  }
  return make_pair(source_namespace->id(), clone_state->LockForRead()->pb.clone_request_seq_no());
}

Status CloneStateManager::TryCloneNamespace(
    CloneStateInfoPtr clone_state,
    const SnapshotScheduleId& snapshot_schedule_id,
    CoarseTimePoint deadline,
    const NamespaceInfoPtr& source_namespace,
    const std::string& target_namespace_name,
    const std::string& pg_source_owner,
    const std::string& pg_target_owner) {
  // Export snapshot info.
  HybridTime restore_ht(clone_state->LockForRead()->pb.restore_time());
  auto clone_snapshot_info = VERIFY_RESULT(
      external_funcs_->GenerateSnapshotInfoFromScheduleForClone(
          snapshot_schedule_id, restore_ht, deadline));
  VLOG(2) << Format(
      "The generated SnapshotInfoPB as of time: $0, snapshot_info: $1 ",
      HybridTime(clone_state->LockForRead()->pb.restore_time()), clone_snapshot_info.snapshot_info);

  RETURN_NOT_OK(CheckTabletLimits(clone_snapshot_info.replication_info_and_num_tablets));

  // Clone PG Schema objects first in case of PGSQL databases. Tablets cloning is initiated in the
  // callback of ClonePgSchemaObjects async task.
  Status status;
  if (source_namespace->database_type() == YQL_DATABASE_PGSQL) {
    status = ClonePgSchemaObjects(
        clone_state, source_namespace->name(), target_namespace_name, pg_source_owner,
        pg_target_owner, snapshot_schedule_id, std::move(clone_snapshot_info));
  } else {
    // For YCQL, start tablets cloning directly.
    status = ImportSnapshotAndStartTabletsCloning(
        clone_state, snapshot_schedule_id, target_namespace_name, std::move(clone_snapshot_info),
        deadline);
  }
  return status;
}

Status CloneStateManager::ImportSnapshotAndStartTabletsCloning(
    CloneStateInfoPtr clone_state,
    const SnapshotScheduleId& snapshot_schedule_id,
    const std::string& target_namespace_name,
    CatalogManagerIf::CloneSnapshotInfo clone_snapshot_info,
    CoarseTimePoint deadline) {
  if (FLAGS_TEST_fail_clone_tablets) {
    return STATUS_FORMAT(RuntimeError, "Failing clone due to test flag fail_clone_tablets");
  }

  // Import snapshot info.
  NamespaceMap namespace_map;
  UDTypeMap type_map;
  ExternalTableSnapshotDataMap tables_data;
  RETURN_NOT_OK(external_funcs_->DoImportSnapshotMeta(
      clone_snapshot_info.snapshot_info, clone_state->Epoch(), target_namespace_name,
      &namespace_map, &type_map, &tables_data, deadline));
  if (namespace_map.size() != 1) {
    return STATUS_FORMAT(IllegalState, "Expected 1 namespace, got $0", namespace_map.size());
  }

  auto lock = clone_state->LockForWrite();
  auto target_namespace_id = namespace_map[lock.data().pb.source_namespace_id()].new_namespace_id;
  lock.mutable_data()->pb.set_target_namespace_id(target_namespace_id);
  RETURN_NOT_OK(external_funcs_->Upsert(clone_state->Epoch().leader_term, clone_state));
  lock.Commit();

  // Generate a new snapshot.
  // It is safe to trigger the clone op immediately after this since imported snapshots are created
  // synchronously.
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
  // All indexes already are in the request. Do not add them twice.
  create_snapshot_req.set_add_indexes(false);
  create_snapshot_req.set_imported(true);
  RETURN_NOT_OK(external_funcs_->DoCreateSnapshot(
      &create_snapshot_req, &create_snapshot_resp, deadline, clone_state->Epoch()));
  if (create_snapshot_resp.has_error()) {
    return StatusFromPB(create_snapshot_resp.error().status());
  }
  auto target_snapshot_id =
      VERIFY_RESULT(FullyDecodeTxnSnapshotId(create_snapshot_resp.snapshot_id()));

  // Set up the rest of the clone state fields.
  auto source_snapshot_id = VERIFY_RESULT(
      FullyDecodeTxnSnapshotId(clone_snapshot_info.snapshot_info.id()));
  RETURN_NOT_OK(UpdateCloneStateWithSnapshotInfo(
      clone_state, source_snapshot_id, target_snapshot_id, tables_data));

  RETURN_NOT_OK(ScheduleCloneOps(clone_state, clone_snapshot_info.not_snapshotted_tablets));
  return Status::OK();
}

Status CloneStateManager::CheckTabletLimits(
    const std::vector<std::pair<ReplicationInfoPB, int>>& replication_info_and_num_tablets) {
  RETURN_NOT_OK(CanCreateTabletReplicas(
      replication_info_and_num_tablets, external_funcs_->GetTservers(),
      VERIFY_RESULT(external_funcs_->GetBlacklist())));
  return Status::OK();
}

Status CloneStateManager::ClonePgSchemaObjects(
    CloneStateInfoPtr clone_state,
    const std::string& source_db_name,
    const std::string& target_db_name,
    const std::string& pg_source_owner,
    const std::string& pg_target_owner,
    const SnapshotScheduleId& snapshot_schedule_id,
    CatalogManagerIf::CloneSnapshotInfo clone_snapshot_info) {
  if (FLAGS_TEST_fail_clone_pg_schema) {
    return STATUS_FORMAT(RuntimeError, "Failing clone due to test flag fail_clone_pg_schema");
  }

  // Pick one of the live tservers to send ysql_dump and ysqlsh requests to.
  auto ts = VERIFY_RESULT(external_funcs_->GetClosestLiveTserver());
  // Deadline passed to the ClonePgSchemaTask (including rpc time and callback execution deadline)
  auto deadline = MonoTime::Now() + FLAGS_ysql_clone_pg_schema_rpc_timeout_ms * 1ms;
  RETURN_NOT_OK(external_funcs_->ScheduleClonePgSchemaTask(
      ts->permanent_uuid(), source_db_name, target_db_name, pg_source_owner, pg_target_owner,
      HybridTime(clone_state->LockForRead()->pb.restore_time()),
      MakeDoneClonePgSchemaCallback(
          clone_state, snapshot_schedule_id, target_db_name, std::move(clone_snapshot_info),
          ToCoarse(deadline)),
      deadline));
  return Status::OK();
}

Status CloneStateManager::ClearAndRunLoaders(const LeaderEpoch& epoch) {
  {
    std::lock_guard l(mutex_);
    source_clone_state_map_.clear();
  }
  RETURN_NOT_OK(external_funcs_->Load(
      "Clone states",
      std::function<Status(const std::string&, const SysCloneStatePB&)>(
          std::bind(&CloneStateManager::LoadCloneState, this, epoch, _1, _2))));

  return Status::OK();
}

Status CloneStateManager::LoadCloneState(
    const LeaderEpoch& epoch, const std::string& id, const SysCloneStatePB& metadata) {
  auto clone_state = std::make_shared<CloneStateInfo>(id);
  clone_state->Load(metadata);

  // Abort the clone if it was not in a terminal state.
  if (!CloneStateInfoHelpers::IsDone(metadata)) {
    RETURN_NOT_OK(MarkCloneAborted(clone_state, "aborted by master failover", epoch.leader_term));
  }

  {
    std::lock_guard lock(mutex_);
    auto [_, inserted] =
      source_clone_state_map_[metadata.source_namespace_id()].insert(clone_state);
    if (!inserted) {
      LOG(WARNING) << Format("Duplicate clone state found for source namespace $0 with seq_no $1",
                             metadata.source_namespace_id(), metadata.clone_request_seq_no());
    }
  }
  return Status::OK();
}

Result<CloneStateInfoPtr> CloneStateManager::CreateCloneState(
    const LeaderEpoch& epoch,
    const NamespaceInfoPtr& source_namespace,
    YQLDatabase database_type,
    const std::string& target_namespace_name,
    const HybridTime& restore_time) {
  // Check if there is an ongoing clone for the source namespace.
  std::lock_guard lock(mutex_);
  auto it = source_clone_state_map_.find(source_namespace->id());
  if (it != source_clone_state_map_.end()) {
    auto latest_clone_it = it->second.rbegin();
    if (latest_clone_it != it->second.rend()) {
      auto lock = (*latest_clone_it)->LockForRead();
      if (!CloneStateInfoHelpers::IsDone(lock->pb)) {
        return STATUS_FORMAT(
            AlreadyPresent, "Cannot create new clone state because there is already an ongoing "
            "clone for source namespace $0 in state $1", source_namespace->id(),
            lock->pb.aggregate_state());
      }
    }
  }

  auto clone_state = std::make_shared<CloneStateInfo>(GenerateObjectId());
  auto clone_state_lock = clone_state->LockForWrite();
  clone_state->SetEpoch(epoch);

  auto namespace_lock = source_namespace->LockForWrite();
  auto seq_no = namespace_lock->pb.clone_request_seq_no() + 1;
  namespace_lock.mutable_data()->pb.set_clone_request_seq_no(seq_no);

  auto& pb = clone_state_lock.mutable_data()->pb;
  pb.set_aggregate_state(SysCloneStatePB::CLONE_SCHEMA_STARTED);
  pb.set_clone_request_seq_no(seq_no);
  pb.set_source_namespace_id(source_namespace->id());
  pb.set_source_namespace_name(source_namespace->name());
  pb.set_restore_time(restore_time.ToUint64());
  pb.set_target_namespace_name(target_namespace_name);
  pb.set_database_type(database_type);
  RETURN_NOT_OK(external_funcs_->Upsert(
      clone_state->Epoch().leader_term, clone_state, source_namespace));
  namespace_lock.Commit();
  clone_state_lock.Commit();

  // Add to the in-memory map.
  source_clone_state_map_[source_namespace->id()].insert(clone_state);

  return clone_state;
}

Status CloneStateManager::UpdateCloneStateWithSnapshotInfo(
    const CloneStateInfoPtr& clone_state,
    const TxnSnapshotId& source_snapshot_id,
    const TxnSnapshotId& target_snapshot_id,
    const ExternalTableSnapshotDataMap& table_snapshot_data) {
  clone_state->SetSourceSnapshotId(source_snapshot_id);
  clone_state->SetTargetSnapshotId(target_snapshot_id);
  using ColocatedTables = std::vector<CloneStateInfo::ColocatedTableData>;

  // In case of colocated database, create the vector of colocated tables' schemas to send along the
  // clone tablet request of the parent tablet
  std::unordered_map<TabletId, ColocatedTables> colocated_tables_data;
  for (const auto& [_, table_data] : table_snapshot_data) {
    LOG_WITH_FUNC(INFO)
        << "new id: " << table_data.new_table_id
        << ", old id: " << table_data.old_table_id
        << ", entry pb: " << AsString(table_data.table_entry_pb);
    if (!table_data.table_entry_pb.colocated() ||
        IsColocationParentTableId(table_data.new_table_id)) {
      continue;
    }
    auto& colocated_tables = colocated_tables_data[table_data.table_entry_pb.parent_table_id()];
    colocated_tables.push_back(CloneStateInfo::ColocatedTableData {
      .new_table_id = table_data.new_table_id,
      .old_table_id = table_data.old_table_id,
      .table_entry_pb = table_data.table_entry_pb,
      .new_schema_version = *table_data.new_table_schema_version,
    });
    auto& added_table = colocated_tables.back();
    if (added_table.table_entry_pb.has_index_info()) {
      auto& index_info = *added_table.table_entry_pb.mutable_index_info();
      DCHECK_EQ(index_info.table_id(), added_table.old_table_id);
      index_info.set_table_id(added_table.new_table_id);
      auto it = table_snapshot_data.find(index_info.indexed_table_id());
      if (it == table_snapshot_data.end()) {
        return STATUS_FORMAT(
            NotFound, "Did not find indexed table $0", index_info.indexed_table_id());
      }
      index_info.set_indexed_table_id(it->second.new_table_id);
    }
  }

  for (const auto& [_, table_data] : table_snapshot_data) {
    // Add colocated tables' schemas for the parent tablet only.
    const ColocatedTables* colocated_tables = nullptr;
    auto it = colocated_tables_data.find(table_data.old_table_id);
    if (it != colocated_tables_data.end()) {
      colocated_tables = &it->second;
    }
    for (auto& tablet : table_data.table_meta->tablets_ids()) {
      clone_state->AddTabletData(CloneStateInfo::TabletData {
          .source_tablet_id = tablet.old_id(),
          .target_tablet_id = tablet.new_id(),
          .colocated_tables_data = colocated_tables ? *colocated_tables : ColocatedTables{},
      });
    }
  }
  return Status::OK();
}

Status CloneStateManager::ScheduleCloneOps(
    const CloneStateInfoPtr& clone_state,
    const std::unordered_set<TabletId>& not_snapshotted_tablets) {
  for (auto& tablet_data : clone_state->GetTabletData()) {
    auto source_tablet = VERIFY_RESULT(
        external_funcs_->GetTabletInfo(tablet_data.source_tablet_id));
    auto target_tablet = VERIFY_RESULT(
        external_funcs_->GetTabletInfo(tablet_data.target_tablet_id));
    auto source_table = source_tablet->table();
    auto target_table = target_tablet->table();

    // Don't need to worry about ordering here because these are both read locks.
    auto source_table_lock = source_table->LockForRead();
    auto target_table_lock = target_table->LockForRead();

    const auto& clone_pb_lock = clone_state->LockForRead();
    tablet::CloneTabletRequestPB req;
    if (not_snapshotted_tablets.contains(tablet_data.source_tablet_id)) {
      auto lock = source_tablet->LockForRead();
      RSTATUS_DCHECK(lock->is_hidden() || lock->pb.split_tablet_ids_size() != 0, IllegalState,
          Format("Expected not snapshotted tablet to be hidden or split state. Actual state: $0",
              source_table_lock->state_name()));
      VLOG(1) << Format(
          "Cloning tablet $0 from active rocksdb since it was deleted or split before snapshot",
          tablet_data.source_tablet_id);
      req.set_clone_from_active_rocksdb(true);
    }
    req.set_tablet_id(tablet_data.source_tablet_id);
    req.set_target_tablet_id(tablet_data.target_tablet_id);
    req.set_source_snapshot_id(
        clone_state->SourceSnapshotId().data(), clone_state->SourceSnapshotId().size());
    req.set_target_snapshot_id(
        clone_state->TargetSnapshotId().data(), clone_state->TargetSnapshotId().size());
    req.set_target_table_id(target_table->id());
    req.set_target_namespace_name(target_table_lock->namespace_name());
    req.set_clone_request_seq_no(clone_pb_lock->pb.clone_request_seq_no());
    req.set_target_pg_table_id(target_table_lock->pb.pg_table_id());
    if (target_table_lock->pb.has_index_info()) {
      *req.mutable_target_index_info() = target_table_lock->pb.index_info();
    }
    *req.mutable_target_schema() = target_table_lock->pb.schema();
    *req.mutable_target_partition_schema() = target_table_lock->pb.partition_schema();
    for (const auto& colocated_table_data : tablet_data.colocated_tables_data) {
      const auto& source_pb = colocated_table_data.table_entry_pb;
      auto& pb = *req.add_colocated_tables();
      CatalogManagerUtil::FillTableInfoPB(
          colocated_table_data.new_table_id, source_pb.name(),
          TableType::PGSQL_TABLE_TYPE, source_pb.schema(),
          /* schema_version */ colocated_table_data.new_schema_version,
          source_pb.partition_schema(), &pb);
      if (source_pb.has_index_info()) {
        *pb.mutable_index_info() = source_pb.index_info();
      }
    }
    RETURN_NOT_OK(external_funcs_->ScheduleCloneTabletCall(
        source_tablet, clone_state->Epoch(), std::move(req)));
  }

  auto lock = clone_state->LockForWrite();
  auto& pb = lock.mutable_data()->pb;
  pb.set_aggregate_state(SysCloneStatePB::CREATING);
  RETURN_NOT_OK(external_funcs_->Upsert(clone_state->Epoch().leader_term, clone_state));
  lock.Commit();

  return Status::OK();
}

AsyncClonePgSchema::ClonePgSchemaCallbackType CloneStateManager::MakeDoneClonePgSchemaCallback(
    CloneStateInfoPtr clone_state, const SnapshotScheduleId& snapshot_schedule_id,
    const std::string& target_namespace_name,
    CatalogManagerIf::CloneSnapshotInfo clone_snapshot_info,
    CoarseTimePoint deadline) {
  return [this, clone_state, snapshot_schedule_id, target_namespace_name, deadline,
          clone_snapshot_info = std::move(clone_snapshot_info)]
      (const Status& pg_schema_cloning_status) mutable -> Status {
    auto status = pg_schema_cloning_status;
    LOG(INFO) << "Done ClonePgSchema: " << status;
    if (status.ok()) {
      status = ImportSnapshotAndStartTabletsCloning(
          clone_state, snapshot_schedule_id, target_namespace_name, std::move(clone_snapshot_info),
          deadline);
    }
    if (!status.ok()) {
      RETURN_NOT_OK(MarkCloneAborted(clone_state, status.ToString()));
    }
    return Status::OK();
  };
}

Status CloneStateManager::HandleCreatingState(const CloneStateInfoPtr& clone_state) {
  bool all_tablets_running = true;
  for (auto& tablet_data : clone_state->GetTabletData()) {
    // Check to see if the tablet is done cloning (i.e. it is RUNNING).
    auto tablet = VERIFY_RESULT(external_funcs_->GetTabletInfo(tablet_data.target_tablet_id));
    if (!tablet->LockForRead()->is_running()) {
      all_tablets_running = false;
    }
  }

  if (!all_tablets_running) {
    return Status::OK();
  }

  auto lock = clone_state->LockForWrite();
  auto& pb = lock.mutable_data()->pb;
  SCHECK_EQ(lock->pb.aggregate_state(), SysCloneStatePB::CREATING, IllegalState,
      "Expected clone to be in creating state");

  LOG(INFO) << Format("All tablets for cloned namespace $0 with seq_no $1 are running. "
      "Triggering restore.",
      pb.source_namespace_id(), pb.clone_request_seq_no());
  auto restoration_id = VERIFY_RESULT(external_funcs_->Restore(
      clone_state->TargetSnapshotId(), HybridTime(lock->pb.restore_time())));
  clone_state->SetRestorationId(restoration_id);
  pb.set_aggregate_state(SysCloneStatePB::RESTORING);

  RETURN_NOT_OK(external_funcs_->Upsert(clone_state->Epoch().leader_term, clone_state));
  lock.Commit();
  return Status::OK();
}

Status CloneStateManager::ClearMetaCaches(const CloneStateInfoPtr& clone_state) {
  auto callback = [this, clone_state]() -> Status {
    auto num_tservers_with_stale_metacache = clone_state->NumTserversWithStaleMetacache();
    num_tservers_with_stale_metacache->CountDown();
    if (num_tservers_with_stale_metacache->count() == 0) {
      RETURN_NOT_OK(EnableDbConnections(clone_state));
    }
    return Status::OK();
  };
  NamespaceIdentifierPB target_namespace_identifier;
  target_namespace_identifier.set_name(clone_state->LockForRead()->pb.target_namespace_name());
  target_namespace_identifier.set_database_type(YQL_DATABASE_PGSQL);
  auto target_namespace_id =
      VERIFY_RESULT(external_funcs_->FindNamespace(target_namespace_identifier))->id();

  TSDescriptorVector running_tservers = external_funcs_->GetTservers();
  clone_state->SetNumTserversWithStaleMetacache(running_tservers.size());
  RETURN_NOT_OK(external_funcs_->ScheduleClearMetaCacheTasks(
      running_tservers, target_namespace_id, callback));
  return Status::OK();
}

Status CloneStateManager::EnableDbConnections(const CloneStateInfoPtr& clone_state) {
  auto callback = [this, clone_state](const Status& enable_db_conns_status) -> Status {

    auto status = enable_db_conns_status;
    if (status.ok()) {
      auto lock = clone_state->LockForWrite();
      SCHECK_EQ(lock->pb.aggregate_state(), SysCloneStatePB::RESTORED, IllegalState,
          "Expected clone to be in restored state");
      LOG(INFO) << Format("Marking clone as complete for source namespace $0 with seq_no $1",
                          lock->pb.source_namespace_id(), lock->pb.clone_request_seq_no());
      lock.mutable_data()->pb.set_aggregate_state(SysCloneStatePB::COMPLETE);
      auto status = external_funcs_->Upsert(clone_state->Epoch().leader_term, clone_state);
      if (status.ok()) {
        lock.Commit();
      }
    }
    if (!status.ok()) {
      RETURN_NOT_OK(MarkCloneAborted(clone_state, status.ToString()));
    }
    return Status::OK();
  };
  auto ts = VERIFY_RESULT(external_funcs_->GetClosestLiveTserver());
  LOG(INFO) << Format(
      "Scheduling enable DB Connections Task for database:$0 ",
      clone_state->LockForRead()->pb.target_namespace_name());
  RETURN_NOT_OK(external_funcs_->ScheduleEnableDbConnectionsTask(
      ts->permanent_uuid(), clone_state->LockForRead()->pb.target_namespace_name(), callback));
  return Status::OK();
}

Status CloneStateManager::HandleRestoringState(const CloneStateInfoPtr& clone_state) {
  auto lock = clone_state->LockForWrite();
  SCHECK_EQ(lock->pb.aggregate_state(), SysCloneStatePB::RESTORING, IllegalState,
      "Expected clone to be in restoring state");

  ListSnapshotRestorationsResponsePB resp;

  RETURN_NOT_OK(external_funcs_->ListRestorations(clone_state->RestorationId(), &resp));
  SCHECK_EQ(resp.restorations_size(), 1, IllegalState, "Unexpected number of restorations.");

  if (resp.restorations(0).entry().state() != SysSnapshotEntryPB::RESTORED) {
    return Status::OK();
  }

  if (lock->pb.database_type() == YQL_DATABASE_PGSQL) {
    lock.mutable_data()->pb.set_aggregate_state(SysCloneStatePB::RESTORED);
    RETURN_NOT_OK(external_funcs_->Upsert(clone_state->Epoch().leader_term, clone_state));
    lock.Commit();
    return ClearMetaCaches(clone_state);
  } else {
    lock.mutable_data()->pb.set_aggregate_state(SysCloneStatePB::COMPLETE);
    RETURN_NOT_OK(external_funcs_->Upsert(clone_state->Epoch().leader_term, clone_state));
    lock.Commit();
    return Status::OK();
  }
}

Status CloneStateManager::MarkCloneAborted(
    const CloneStateInfoPtr& clone_state, const std::string& abort_reason) {
  return MarkCloneAborted(clone_state, abort_reason, clone_state->Epoch().leader_term);
}

Status CloneStateManager::MarkCloneAborted(
    const CloneStateInfoPtr& clone_state, const std::string& abort_reason, int64_t leader_term) {
  auto lock = clone_state->LockForWrite();
  LOG(INFO) << Format(
      "Aborted clone for source namespace $0 because: $1.\n"
      "seq_no: $2, target namespace: $3, restore time: $4",
      lock->pb.source_namespace_id(), abort_reason, lock->pb.clone_request_seq_no(),
      lock->pb.target_namespace_name(), lock->pb.restore_time());
  lock.mutable_data()->pb.set_abort_message(abort_reason);
  lock.mutable_data()->pb.set_aggregate_state(SysCloneStatePB::ABORTED);
  RETURN_NOT_OK(external_funcs_->Upsert(leader_term, clone_state));
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
  for (auto& [source_namespace_id, clone_states] : source_clone_state_map) {
    Status s;
    auto& latest_clone_state = *clone_states.rbegin();
    switch (latest_clone_state->LockForRead()->pb.aggregate_state()) {
      case SysCloneStatePB::CREATING:
        s = HandleCreatingState(latest_clone_state);
        break;
      case SysCloneStatePB::RESTORING:
        s = HandleRestoringState(latest_clone_state);
        break;
      case SysCloneStatePB::CLONE_SCHEMA_STARTED: FALLTHROUGH_INTENDED;
      case SysCloneStatePB::RESTORED: FALLTHROUGH_INTENDED;
      case SysCloneStatePB::COMPLETE: FALLTHROUGH_INTENDED;
      case SysCloneStatePB::ABORTED:
        break;
    }
    if (!s.ok()) {
      RETURN_NOT_OK(MarkCloneAborted(latest_clone_state, s.ToString()));
    }
  }
  return Status::OK();
}

} // namespace master
} // namespace yb
