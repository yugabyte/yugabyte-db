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

#include "yb/master/xcluster/xcluster_inbound_replication_group_setup_task.h"

#include "yb/client/table_info.h"
#include "yb/client/table.h"
#include "yb/client/xcluster_client.h"

#include "yb/common/colocated_util.h"
#include "yb/common/common_net.pb.h"
#include "yb/common/xcluster_util.h"

#include "yb/gutil/bind.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/master_util.h"
#include "yb/master/master.h"
#include "yb/master/xcluster_consumer_registry_service.h"
#include "yb/master/xcluster/master_xcluster_util.h"
#include "yb/master/xcluster/xcluster_manager.h"
#include "yb/master/xcluster/xcluster_replication_group.h"

#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

// TODO: Bootstrap check sends multiple RPCs. Consider converting it to one RPC when enabling
// check_bootstrap_required flag by default.
DEFINE_RUNTIME_bool(check_bootstrap_required, false,
    "Is it necessary to check whether bootstrap is required for Universe Replication.");

DEFINE_test_flag(bool, allow_ycql_transactional_xcluster, false,
    "Determines if xCluster transactional replication on YCQL tables is allowed.");

DEFINE_test_flag(bool, fail_universe_replication_merge, false,
    "Causes MergeUniverseReplication to fail with an error.");

DEFINE_test_flag(bool, exit_unfinished_merging, false,
    "Whether to exit part way through the merging universe process.");

DECLARE_bool(enable_xcluster_auto_flag_validation);

using namespace std::placeholders;

namespace yb::master {

Result<std::shared_ptr<XClusterInboundReplicationGroupSetupTaskIf>>
CreateSetupUniverseReplicationTask(
    Master& master, CatalogManager& catalog_manager, const SetupUniverseReplicationRequestPB* req,
    const LeaderEpoch& epoch) {
  SCHECK_PB_FIELDS_NOT_EMPTY(
      *req, replication_group_id, producer_master_addresses, producer_table_ids);

  std::vector<xrepl::StreamId> stream_ids;
  for (const auto& bootstrap_id : req->producer_bootstrap_ids()) {
    stream_ids.push_back(VERIFY_RESULT(xrepl::StreamId::FromString(bootstrap_id)));
  }

  std::vector<NamespaceId> source_namespace_ids, target_namespace_ids;
  for (const auto& source_ns_id : req->producer_namespaces()) {
    SCHECK(!source_ns_id.id().empty(), InvalidArgument, "Invalid Namespace Id");
    SCHECK(!source_ns_id.name().empty(), InvalidArgument, "Invalid Namespace name");
    SCHECK_EQ(
        source_ns_id.database_type(), YQLDatabase::YQL_DATABASE_PGSQL, InvalidArgument,
        "Invalid Namespace database_type");

    source_namespace_ids.push_back(source_ns_id.id());

    NamespaceIdentifierPB target_ns_id;
    target_ns_id.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    target_ns_id.set_name(source_ns_id.name());
    auto ns_info = VERIFY_RESULT(catalog_manager.FindNamespace(target_ns_id));
    target_namespace_ids.push_back(ns_info->id());
  }

  std::vector<TableId> source_table_ids;
  source_table_ids.insert(
      source_table_ids.begin(), req->producer_table_ids().begin(), req->producer_table_ids().end());

  auto setup_task = std::shared_ptr<XClusterInboundReplicationGroupSetupTask>(
      new XClusterInboundReplicationGroupSetupTask(
          master, catalog_manager, epoch, xcluster::ReplicationGroupId(req->replication_group_id()),
          req->producer_master_addresses(), std::move(source_table_ids), std::move(stream_ids),
          req->transactional(), std::move(source_namespace_ids), std::move(target_namespace_ids)));
  RETURN_NOT_OK(setup_task->ValidateInputArguments());

  return setup_task;
}

XClusterInboundReplicationGroupSetupTask::XClusterInboundReplicationGroupSetupTask(
    Master& master, CatalogManager& catalog_manager, const LeaderEpoch& epoch,
    xcluster::ReplicationGroupId&& replication_group_id,
    const google::protobuf::RepeatedPtrField<HostPortPB>& source_masters,
    std::vector<TableId>&& source_table_ids, std::vector<xrepl::StreamId>&& stream_ids,
    bool transactional, std::vector<NamespaceId>&& source_namespace_ids,
    std::vector<NamespaceId>&& target_namespace_ids)
    : MultiStepMonitoredTask(*catalog_manager.AsyncTaskPool(), *master.messenger()),
      master_(master),
      catalog_manager_(catalog_manager),
      sys_catalog_(*catalog_manager.sys_catalog()),
      xcluster_manager_(*catalog_manager.GetXClusterManagerImpl()),
      epoch_(epoch),
      replication_group_id_(std::move(replication_group_id)),
      source_masters_(source_masters),
      transactional_(transactional),
      source_table_ids_(std::move(source_table_ids)),
      stream_ids_(std::move(stream_ids)),
      source_namespace_ids_(std::move(source_namespace_ids)),
      target_namespace_ids_(std::move(target_namespace_ids)),
      is_alter_replication_(xcluster::IsAlterReplicationGroupId(replication_group_id_)),
      is_db_scoped_(!source_namespace_ids_.empty()),
      stream_ids_provided_(!stream_ids_.empty()) {
  log_prefix_ = Format(
      "xCluster InboundReplicationGroup [$0] $1: ", replication_group_id_,
      (is_alter_replication_ ? "Alter" : "Setup"));
}

IsOperationDoneResult XClusterInboundReplicationGroupSetupTask::DoneResult() const {
  SharedLock l(done_result_mutex_);
  return done_result_;
}

client::YBClient& XClusterInboundReplicationGroupSetupTask::GetYbClient() {
  return remote_client_->GetYbClient();
}

client::XClusterClient& XClusterInboundReplicationGroupSetupTask::GetXClusterClient() {
  return remote_client_->GetXClusterClient();
}

Status XClusterInboundReplicationGroupSetupTask::RegisterTask() {
  VLOG_WITH_PREFIX_AND_FUNC(1);
  return xcluster_manager_.RegisterMonitoredTask(shared_from(this));
}

void XClusterInboundReplicationGroupSetupTask::UnregisterTask() {
  VLOG_WITH_PREFIX_AND_FUNC(1);
  xcluster_manager_.UnRegisterMonitoredTask(shared_from(this));
}

Status XClusterInboundReplicationGroupSetupTask::ValidateRunnable() {
  if (DoneResult().done()) {
    return STATUS(Aborted, LogPrefix(), "Task already completed");
  }

  return Status::OK();
}

void XClusterInboundReplicationGroupSetupTask::TaskCompleted(const Status& status) {
  if (remote_client_) {
    // Stop any inflight RPCs on child tasks.
    remote_client_->Shutdown();
  }

  std::lock_guard l(done_result_mutex_);

  if (!status.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Failed: " << status.ToString();

    DCHECK(!done_result_) << "We cannot fail after we successfully completed";
    if (!done_result_.done()) {
      // Only record the first error.
      done_result_ = IsOperationDoneResult::Done(std::move(status));
    }
    return;
  }

  LOG_IF(DFATAL, !done_result_.done())
      << "done_result_ should be marked as done before successful task completion";
}

bool XClusterInboundReplicationGroupSetupTask::TryCancel() {
  static auto status = STATUS(Aborted, LogPrefix(), "Cancelled");
  {
    std::lock_guard l(done_result_mutex_);
    if (done_result_.done()) {
      return !done_result_.status().ok();
    }

    done_result_ = IsOperationDoneResult::Done(status);
  }

  AbortAndReturnPrevState(status);
  return true;
}

Status XClusterInboundReplicationGroupSetupTask::ValidateInputArguments() {
  SCHECK(!replication_group_id_.empty(), InvalidArgument, "Invalid Replication Group Id");
  SCHECK(!source_table_ids_.empty(), InvalidArgument, "No tables provided");

  for (const auto& source_table_id : source_table_ids_) {
    SCHECK(!source_table_id.empty(), InvalidArgument, "Invalid Table Id");
    SCHECK(
        !IsColocatedDbParentTableId(source_table_id), NotSupported,
        "Pre GA colocated databases are not supported with xCluster replication: $0",
        source_table_id);
  }

  if (stream_ids_provided_) {
    SCHECK_EQ(
        stream_ids_.size(), source_table_ids_.size(), InvalidArgument,
        "Number of bootstrap ids must be equal to number of tables");

    for (const auto& stream_id : stream_ids_) {
      SCHECK(stream_id, InvalidArgument, "Invalid Stream Id");
    }
  }

  {
    auto l = catalog_manager_.ClusterConfig()->LockForRead();
    SCHECK_NE(
        l->pb.cluster_uuid(), replication_group_id_, InvalidArgument,
        "Replication group Id cannot be the same as the cluster UUID");
  }

  RETURN_NOT_OK(ValidateMasterAddressesBelongToDifferentCluster(master_, source_masters_));

  SCHECK(
      source_namespace_ids_.empty() || transactional_, InvalidArgument,
      "Transactional flag must be set for Db scoped replication groups");

  SCHECK_EQ(
      source_namespace_ids_.size(), target_namespace_ids_.size(), InvalidArgument,
      "Source and target namespace ids must be of the same size");

  auto universe_replication = catalog_manager_.GetUniverseReplication(
      xcluster::GetOriginalReplicationGroupId(replication_group_id_));
  if (is_alter_replication_) {
    SCHECK(universe_replication, NotFound, "Replication group $0 not found", replication_group_id_);
  } else {
    SCHECK(
        !universe_replication, AlreadyPresent, "Replication group $0 already present",
        replication_group_id_);
  }

  RETURN_NOT_OK(ValidateNamespaceListForDbScoped());

  IF_DEBUG_MODE(argument_validation_done_ = true);

  return Status::OK();
}

Status XClusterInboundReplicationGroupSetupTask::FirstStep() {
  IF_DEBUG_MODE(CHECK(argument_validation_done_));

  {
    std::vector<HostPort> hp;
    HostPortsFromPBs(source_masters_, &hp);
    remote_client_ =
        VERIFY_RESULT(client::XClusterRemoteClientHolder::Create(replication_group_id_, hp));
  }

  if (FLAGS_enable_xcluster_auto_flag_validation && !is_alter_replication_) {
    // Sanity check AutoFlags compatibility before we start further work.
    RETURN_NOT_OK(GetAutoFlagConfigVersionIfCompatible());
  }

  LOG_WITH_PREFIX(INFO) << "Started schema validation for " << source_table_ids_.size()
                        << " table(s)";

  std::vector<std::shared_ptr<MultiStepMonitoredTask>> child_tasks;
  for (size_t i = 0; i < source_table_ids_.size(); i++) {
    const auto stream_id = stream_ids_provided_ ? stream_ids_[i] : xrepl::StreamId::Nil();
    auto table_setup_info = std::shared_ptr<XClusterTableSetupTask>(
        new XClusterTableSetupTask(shared_from(this), source_table_ids_[i], stream_id));

    table_setup_info->Start();
  }

  return Status::OK();
}

void XClusterInboundReplicationGroupSetupTask::TableTaskCompletionCallback(
    const TableId& source_table_id, const Result<XClusterTableSetupInfo>& table_setup_result) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << table_setup_result.ToString();

  if (!table_setup_result) {
    AbortAndReturnPrevState(table_setup_result.status());
    return;
  }

  {
    std::lock_guard l(mutex_);
    if (source_table_infos_.contains(source_table_id)) {
      LOG_WITH_PREFIX(DFATAL) << "TableTaskCompletionCallback called multiple times for table "
                              << source_table_id;
      return;
    }

    source_table_infos_[source_table_id] = std::move(*table_setup_result);

    VLOG_WITH_PREFIX(1) << "Processed " << source_table_infos_.size() << " tables out of "
                        << source_table_ids_.size();

    if (source_table_infos_.size() != source_table_ids_.size()) {
      // We still have uncompleted tasks. The final task will proceed with the setup.
      return;
    }
  }

  ScheduleNextStep(
      std::bind(
          &XClusterInboundReplicationGroupSetupTask::SetupReplicationAfterProcessingAllTables,
          shared_from(this)),
      "Process replication group after tables validated");
}

Status XClusterInboundReplicationGroupSetupTask::SetupReplicationAfterProcessingAllTables() {
  if (stream_ids_provided_) {
    RETURN_NOT_OK_PREPEND(
        UpdateSourceStreamOptions(), "Failed to update xCluster stream options on source universe");
  }

  LOG_WITH_PREFIX(INFO) << "Table validation and stream setup completed successfully for "
                        << source_table_ids_.size() << " table(s)";

  return SetupReplicationGroup();
}

Status XClusterInboundReplicationGroupSetupTask::UpdateSourceStreamOptions() {
  std::vector<xrepl::StreamId> update_bootstrap_ids;
  std::vector<SysCDCStreamEntryPB> update_entries;

  {
    std::lock_guard l(mutex_);
    for (const auto& [source_table_id, table_info] : source_table_infos_) {
      SysCDCStreamEntryPB new_entry;
      new_entry.add_table_id(source_table_id);
      new_entry.mutable_options()->Reserve(narrow_cast<int>(table_info.stream_options.size()));
      for (const auto& [key, value] : table_info.stream_options) {
        if (key == cdc::kStreamState) {
          // We will set state explicitly.
          continue;
        }
        auto new_option = new_entry.add_options();
        new_option->set_key(key);
        new_option->set_value(value);
      }
      new_entry.set_state(master::SysCDCStreamEntryPB::ACTIVE);
      new_entry.set_transactional(transactional_);

      update_bootstrap_ids.push_back(table_info.stream_id);
      update_entries.push_back(new_entry);
    }
  }

  RETURN_NOT_OK_PREPEND(
      remote_client_->GetYbClient().UpdateCDCStream(update_bootstrap_ids, update_entries),
      "Unable to update xCluster stream options on source universe");

  return Status::OK();
}

Status XClusterInboundReplicationGroupSetupTask::SetupReplicationGroup() {
  std::lock_guard l(mutex_);

  // Last minute validations.
  // TODO: Block DDLs and other xCluster setup/alter operations between this step till the end of
  // this function.
  RETURN_NOT_OK(ValidateNamespaceListForDbScoped());
  RETURN_NOT_OK(ValidateTableListForDbScoped());

  const auto original_id = xcluster::GetOriginalReplicationGroupId(replication_group_id_);

  auto universe = catalog_manager_.GetUniverseReplication(original_id);
  std::optional<UniverseReplicationInfo::WriteLock> universe_lock;
  if (is_alter_replication_) {
    SCHECK(universe != nullptr, NotFound, "Replication group $0 not found", original_id);
    universe_lock = universe->LockForWrite();
  } else {
    SCHECK(
        universe == nullptr, AlreadyPresent, "Replication group $0 already present",
        replication_group_id_);

    universe = VERIFY_RESULT(CreateNewUniverseReplicationInfo());
  }

  auto& universe_pb = universe->mutable_metadata()->mutable_dirty()->pb;
  PopulateUniverseReplication(universe_pb);

  auto cluster_config = catalog_manager_.ClusterConfig();
  auto cluster_config_l = cluster_config->LockForWrite();
  auto& cluster_config_producer_map =
      *cluster_config_l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();

  if (is_alter_replication_) {
    SCHECK_EQ(
        cluster_config_producer_map.count(original_id.ToString()), 1, InvalidArgument,
        Format("ClusterConfig producer_map missing for ReplicationGroup $0", original_id));

    LOG(INFO) << "Merging xCluster ReplicationGroup: " << replication_group_id_ << " into "
              << original_id;

    SCHECK(
        !FLAGS_TEST_fail_universe_replication_merge, IllegalState,
        "TEST_fail_universe_replication_merge");

    if (FLAGS_TEST_exit_unfinished_merging) {
      LOG_WITH_PREFIX(INFO) << "Completed successfully due to FLAGS_TEST_exit_unfinished_merging";
      {
        std::lock_guard done_l(done_result_mutex_);
        done_result_ = IsOperationDoneResult::Done();
      }
      Complete();
      return Status::OK();
    }
  } else {
    SCHECK_EQ(
        cluster_config_producer_map.count(original_id.ToString()), 0, InvalidArgument,
        Format("ReplicationGroup $0 already exists in ClusterConfig producer_map", original_id));

    cdc::ProducerEntryPB producer_entry;
    producer_entry.mutable_master_addrs()->CopyFrom(source_masters_);

    if (FLAGS_enable_xcluster_auto_flag_validation) {
      auto auto_flag_config_version = VERIFY_RESULT(GetAutoFlagConfigVersionIfCompatible());

      universe_pb.set_validated_local_auto_flags_config_version(auto_flag_config_version);
      producer_entry.set_compatible_auto_flag_config_version(auto_flag_config_version);
      producer_entry.set_validated_auto_flags_config_version(auto_flag_config_version);
    }

    cluster_config_producer_map[original_id.ToString()].Swap(&producer_entry);
  }

  auto& stream_map = *cluster_config_producer_map.at(original_id.ToString()).mutable_stream_map();
  for (auto& [source_table_id, table_info] : source_table_infos_) {
    stream_map[table_info.stream_id.ToString()].Swap(&table_info.stream_entry);
  }

  cluster_config_l.mutable_data()->pb.set_version(
      cluster_config_l.mutable_data()->pb.version() + 1);

  // Grab the done mutex, since we can no longer be cancelled.
  UniqueLock done_l(done_result_mutex_);
  if (done_result_.done()) {
    return STATUS(Aborted, LogPrefix(), "Task already completed");
  }

  RETURN_NOT_OK_PREPEND(
      sys_catalog_.Upsert(epoch_, universe, cluster_config), "Failed to wite to sys-catalog");

  LOG_WITH_PREFIX(INFO) << "Replication Map: "
                        << cluster_config_producer_map.at(original_id.ToString()).DebugString();

  // Update the in-memory states now that data is persistent in sys catalog.
  if (!is_alter_replication_) {
    catalog_manager_.InsertNewUniverseReplication(*universe);
  }

  for (const auto& [_, table_info] : source_table_infos_) {
    DCHECK(table_info.stream_id);
    xcluster_manager_.RecordTableConsumerStream(
        table_info.target_table_id, original_id, table_info.stream_id);
  }

  xcluster_manager_.SyncConsumerReplicationStatusMap(original_id, cluster_config_producer_map);
  cluster_config_l.Commit();

  if (universe_lock) {
    universe_lock->Commit();
  } else {
    universe->mutable_metadata()->CommitMutation();
  }

  xcluster_manager_.CreateXClusterSafeTimeTableAndStartService();

  done_result_ = IsOperationDoneResult::Done();
  done_l.unlock();

  Complete();

  return Status::OK();
}

Result<uint32> XClusterInboundReplicationGroupSetupTask::GetAutoFlagConfigVersionIfCompatible() {
  DCHECK(!is_alter_replication_);

  auto local_config = master_.GetAutoFlagsConfig();
  VLOG_WITH_FUNC(2) << "Validating AutoFlags config for replication group: "
                    << replication_group_id_
                    << " with target config version: " << local_config.config_version();

  auto validate_result =
      VERIFY_RESULT(master::ValidateAutoFlagsConfig(*remote_client_, local_config));

  if (!validate_result) {
    VLOG_WITH_FUNC(2)
        << "Source universe of replication group " << replication_group_id_
        << " is running a version that does not support the AutoFlags compatibility check yet";
    return kInvalidAutoFlagsConfigVersion;
  }

  auto& [is_valid, source_version] = *validate_result;

  SCHECK(
      is_valid, IllegalState,
      "AutoFlags between the universes are not compatible. Upgrade the target universe to a "
      "version higher than or equal to the source universe");

  return source_version;
}

Status XClusterInboundReplicationGroupSetupTask::ValidateNamespaceListForDbScoped() {
  if (!is_db_scoped_) {
    return Status::OK();
  }

  for (const auto& universe : catalog_manager_.GetAllUniverseReplications()) {
    for (const auto& target_namespace_id : target_namespace_ids_) {
      SCHECK_FORMAT(
          !IncludesConsumerNamespace(*universe, target_namespace_id), AlreadyPresent,
          "Namespace $0 already included in replication group $1", target_namespace_id,
          universe->ReplicationGroupId());
    }
  }

  return Status::OK();
}

Status XClusterInboundReplicationGroupSetupTask::ValidateTableListForDbScoped() {
  if (!is_db_scoped_) {
    return Status::OK();
  }

  std::set<TableId> target_table_ids;
  for (const auto& [source_table_id, table_info] : source_table_infos_) {
    target_table_ids.insert(table_info.target_table_id);
  }

  std::set<TableId> validated_tables;
  for (const auto& namespace_id : target_namespace_ids_) {
    auto table_infos =
        VERIFY_RESULT(GetTablesEligibleForXClusterReplication(catalog_manager_, namespace_id));

    std::vector<TableId> missing_tables;

    for (const auto& table_info : table_infos) {
      const auto& table_id = table_info->id();
      if (target_table_ids.contains(table_id)) {
        validated_tables.insert(table_id);
      } else {
        missing_tables.push_back(table_id);
      }
    }

    SCHECK_FORMAT(
        missing_tables.empty(), IllegalState,
        "Namespace $0 has additional tables that were not added to xCluster DB Scoped replication "
        "group $1: $2",
        namespace_id, replication_group_id_, yb::ToString(missing_tables));
  }

  auto diff = STLSetSymmetricDifference(target_table_ids, validated_tables);
  SCHECK_FORMAT(
      diff.empty(), IllegalState,
      "xCluster DB Scoped replication group $0 contains tables $1 that do not belong to replicated "
      "namespaces $2",
      replication_group_id_, yb::ToString(diff), yb::ToString(target_namespace_ids_));

  return Status::OK();
}

Result<scoped_refptr<UniverseReplicationInfo>>
XClusterInboundReplicationGroupSetupTask::CreateNewUniverseReplicationInfo() {
  scoped_refptr<UniverseReplicationInfo> ri = new UniverseReplicationInfo(replication_group_id_);
  ri->mutable_metadata()->StartMutation();
  SysUniverseReplicationEntryPB* metadata = &ri->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_replication_group_id(replication_group_id_.ToString());
  metadata->mutable_producer_master_addresses()->CopyFrom(source_masters_);

  metadata->set_state(SysUniverseReplicationEntryPB::ACTIVE);
  metadata->set_transactional(transactional_);

  return ri;
}

void XClusterInboundReplicationGroupSetupTask::PopulateUniverseReplication(
    SysUniverseReplicationEntryPB& universe_pb) {
  if (!source_namespace_ids_.empty()) {
    auto* db_scoped_info = universe_pb.mutable_db_scoped_info();
    for (size_t i = 0; i < source_namespace_ids_.size(); i++) {
      auto* ns_info = db_scoped_info->mutable_namespace_infos()->Add();
      ns_info->set_producer_namespace_id(source_namespace_ids_[i]);
      ns_info->set_consumer_namespace_id(target_namespace_ids_[i]);
    }
  }

  // We need to preserve the input table order, so loop on source_table_ids_ list instead of the
  // source_table_infos_ map. This has been the behavior since the beginning, and tests rely on
  // this.
  for (const auto& source_table_id : source_table_ids_) {
    const auto& table_info = source_table_infos_[source_table_id];

    universe_pb.add_tables(source_table_id);
    (*universe_pb.mutable_table_streams())[source_table_id] = table_info.stream_id.ToString();
    (*universe_pb.mutable_validated_tables())[source_table_id] = table_info.target_table_id;
  }
}

XClusterTableSetupTask::XClusterTableSetupTask(
    std::shared_ptr<XClusterInboundReplicationGroupSetupTask> parent_task,
    const TableId& source_table_id, const xrepl::StreamId& stream_id)
    : MultiStepMonitoredTask(
          *parent_task->catalog_manager_.AsyncTaskPool(), *parent_task->master_.messenger()),
      parent_task_(parent_task),
      source_table_id_(source_table_id) {
  table_setup_info_.stream_id = stream_id;
  log_prefix_ = Format(
      "xCluster InboundReplicationGroup [$0] Source Table [$1]: ",
      parent_task_->replication_group_id_, source_table_id_);
}

Status XClusterTableSetupTask::RegisterTask() {
  VLOG_WITH_PREFIX_AND_FUNC(1);
  return parent_task_->xcluster_manager_.RegisterMonitoredTask(shared_from(this));
}

void XClusterTableSetupTask::UnregisterTask() {
  VLOG_WITH_PREFIX_AND_FUNC(1);
  parent_task_->xcluster_manager_.UnRegisterMonitoredTask(shared_from(this));
}

Status XClusterTableSetupTask::ValidateRunnable() { return parent_task_->ValidateRunnable(); }

void XClusterTableSetupTask::TaskCompleted(const Status& status) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << status;

  if (!status.ok()) {
    parent_task_->TableTaskCompletionCallback(
        source_table_id_,
        status.CloneAndPrepend(Format("Error processing source table $0", source_table_id_)));
    return;
  }

  parent_task_->TableTaskCompletionCallback(source_table_id_, std::move(table_setup_info_));
}

Status XClusterTableSetupTask::FirstStep() {
  if (IsTablegroupParentTableId(source_table_id_)) {
    auto source_tablegroup_id = GetTablegroupIdFromParentTableId(source_table_id_);
    auto tables_info = std::make_shared<std::vector<client::YBTableInfo>>();
    return parent_task_->GetYbClient().GetTablegroupSchemaById(
        source_tablegroup_id, tables_info,
        Bind(&XClusterTableSetupTask::GetTablegroupSchemaCallback, shared_from(this), tables_info));
  }

  auto table_info = std::make_shared<client::YBTableInfo>();
  return parent_task_->GetYbClient().GetTableSchemaById(
      source_table_id_, table_info,
      Bind(&XClusterTableSetupTask::GetTableSchemaCallback, shared_from(this), table_info));
}

void XClusterTableSetupTask::GetTableSchemaCallback(
    std::shared_ptr<XClusterTableSetupTask> shared_this,
    const std::shared_ptr<client::YBTableInfo>& source_table_info, const Status& s) {
  shared_this->ScheduleNextStep(
      std::bind(&XClusterTableSetupTask::ProcessTable, shared_this, source_table_info, s),
      "Processing table");
}

Status XClusterTableSetupTask::ProcessTable(
    const std::shared_ptr<client::YBTableInfo>& source_info, const Status& s) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << s;
  RETURN_NOT_OK_PREPEND(s, "Error from source universe");
  SCHECK(source_info != nullptr, InvalidArgument, "Received null table info from source universe");

  SCHECK_NE(
      source_info->table_name.namespace_name(), master::kSystemNamespaceName, NotSupported,
      "Cannot replicate system tables");

  auto target_schema = VERIFY_RESULT(ValidateSourceSchemaAndGetTargetSchema(*source_info));

  const auto& target_table_id = target_schema.identifier().table_id();
  RSTATUS_DCHECK_NE(
      source_info->schema.version(), cdc::kInvalidSchemaVersion, IllegalState,
      Format("Invalid source schema version for target table $0", target_table_id));

  auto* schema_versions = table_setup_info_.stream_entry.mutable_schema_versions();
  schema_versions->set_current_producer_schema_version(source_info->schema.version());
  schema_versions->set_current_consumer_schema_version(target_schema.version());

  RETURN_NOT_OK(
      PopulateTableStreamEntry(target_schema.identifier().table_id(), target_schema.version()));

  return ValidateBootstrapAndSetupStreams();
}

void XClusterTableSetupTask::GetTablegroupSchemaCallback(
    std::shared_ptr<XClusterTableSetupTask> shared_this,
    const std::shared_ptr<std::vector<client::YBTableInfo>>& source_table_infos, const Status& s) {
  shared_this->ScheduleNextStep(
      std::bind(&XClusterTableSetupTask::ProcessTablegroup, shared_this, source_table_infos, s),
      "Processing tablegroup");
}

Status XClusterTableSetupTask::ProcessTablegroup(
    const std::shared_ptr<std::vector<client::YBTableInfo>>& source_table_infos, const Status& s) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << s;

  RETURN_NOT_OK_PREPEND(s, "Error from source universe");

  SCHECK(
      source_table_infos != nullptr, InvalidArgument,
      "Received null table info from source universe");
  SCHECK(
      !source_table_infos->empty(), IllegalState,
      "Received empty list of tables to validate from source universe");

  const auto source_tablegroup_id = GetTablegroupIdFromParentTableId(source_table_id_);
  std::unordered_set<TableId> validated_target_tables;
  for (const auto& source_table_info : *source_table_infos) {
    SCHECK(
        source_table_info.colocated, InvalidArgument,
        "Received non-colocated table $0 from source universe", source_table_info.table_id);

    auto target_schema = VERIFY_RESULT(ValidateSourceSchemaAndGetTargetSchema(source_table_info));

    const auto& colocation_id = target_schema.schema().colocated_table_id().colocation_id();
    auto& schema_versions =
        (*table_setup_info_.stream_entry.mutable_colocated_schema_versions())[colocation_id];
    schema_versions.set_current_producer_schema_version(source_table_info.schema.version());
    schema_versions.set_current_consumer_schema_version(target_schema.version());

    validated_target_tables.insert(target_schema.identifier().table_id());
  }

  // Make sure the list of tables in our tablegroup matches the source.
  auto target_tablegroup_id = VERIFY_RESULT(
      parent_task_->catalog_manager_.GetTablegroupId(*validated_target_tables.begin()));

  std::unordered_set<TableId> tables_in_consumer_tablegroup;
  {
    GetTablegroupSchemaRequestPB req;
    GetTablegroupSchemaResponsePB resp;
    req.mutable_tablegroup()->set_id(target_tablegroup_id);
    auto status = parent_task_->catalog_manager_.GetTablegroupSchema(&req, &resp);
    if (status.ok() && resp.has_error()) {
      status = StatusFromPB(resp.error().status());
    }
    RETURN_NOT_OK_PREPEND(
        status, Format("Error when getting target tablegroup schema: $0", target_tablegroup_id));

    for (const auto& info : resp.get_table_schema_response_pbs()) {
      tables_in_consumer_tablegroup.insert(info.identifier().table_id());
    }
  }

  SCHECK_EQ(
      validated_target_tables, tables_in_consumer_tablegroup, IllegalState,
      Format(
          "Mismatch between tables associated with source tablegroup $0 and "
          "tables in target tablegroup $1: ($2) vs ($3).",
          source_tablegroup_id, target_tablegroup_id, AsString(validated_target_tables),
          AsString(tables_in_consumer_tablegroup)));

  const auto target_parent_table_id = IsColocatedDbTablegroupParentTableId(source_table_id_)
                                          ? GetColocationParentTableId(target_tablegroup_id)
                                          : GetTablegroupParentTableId(target_tablegroup_id);

  auto target_schema_version =
      VERIFY_RESULT(parent_task_->catalog_manager_.GetTableSchemaVersion(target_parent_table_id));

  RETURN_NOT_OK(PopulateTableStreamEntry(target_parent_table_id, target_schema_version));

  return ValidateBootstrapAndSetupStreams();
}

Result<GetTableSchemaResponsePB> XClusterTableSetupTask::ValidateSourceSchemaAndGetTargetSchema(
    const client::YBTableInfo& source_table_info) {
  bool is_ysql_table = source_table_info.table_type == client::YBTableType::PGSQL_TABLE_TYPE;
  if (parent_task_->transactional_ &&
      !GetAtomicFlag(&FLAGS_TEST_allow_ycql_transactional_xcluster) && !is_ysql_table) {
    return STATUS_FORMAT(
        NotSupported, "Transactional replication is not supported for non-YSQL tables: $0",
        source_table_info.table_name.ToString());
  }

  // Get corresponding table schema on local universe.
  GetTableSchemaRequestPB table_schema_req;
  GetTableSchemaResponsePB table_schema_resp;

  auto* table = table_schema_req.mutable_table();
  table->set_table_name(source_table_info.table_name.table_name());
  table->mutable_namespace_()->set_name(source_table_info.table_name.namespace_name());
  table->mutable_namespace_()->set_database_type(
      GetDatabaseTypeForTable(client::ClientToPBTableType(source_table_info.table_type)));

  // Since YSQL tables are not present in table map, we first need to list tables to get the table
  // ID and then get table schema.
  // Remove this once table maps are fixed for YSQL.
  ListTablesRequestPB list_req;
  ListTablesResponsePB list_resp;

  list_req.set_name_filter(source_table_info.table_name.table_name());
  Status status = parent_task_->catalog_manager_.ListTables(&list_req, &list_resp);
  SCHECK(
      status.ok() && !list_resp.has_error(), NotFound,
      Format("Error while listing table: $0", status.ToString()));

  const auto& source_schema = client::internal::GetSchema(source_table_info.schema);
  for (const auto& t : list_resp.tables()) {
    // Check that table name and namespace both match.
    if (t.name() != source_table_info.table_name.table_name() ||
        t.namespace_().name() != source_table_info.table_name.namespace_name()) {
      continue;
    }

    // Check that schema name matches for YSQL tables, if the field is empty, fill in that
    // information during GetTableSchema call later.
    bool has_valid_pgschema_name = !t.pgschema_name().empty();
    if (is_ysql_table && has_valid_pgschema_name &&
        t.pgschema_name() != source_schema.SchemaName()) {
      continue;
    }

    // Get the table schema.
    table->set_table_id(t.id());
    status = parent_task_->catalog_manager_.GetTableSchema(&table_schema_req, &table_schema_resp);
    SCHECK(
        status.ok() && !table_schema_resp.has_error(), NotFound,
        Format("Error while getting table schema: $0", status.ToString()));

    // Double-check schema name here if the previous check was skipped.
    if (is_ysql_table && !has_valid_pgschema_name) {
      std::string target_schema_name = table_schema_resp.schema().pgschema_name();
      if (target_schema_name != source_schema.SchemaName()) {
        table->clear_table_id();
        continue;
      }
    }

    // Verify that the table on the target side supports replication.
    if (is_ysql_table && t.has_relation_type() && t.relation_type() == MATVIEW_TABLE_RELATION) {
      return STATUS_FORMAT(
          NotSupported, "Replication is not supported for materialized view: $0",
          source_table_info.table_name.ToString());
    }

    Schema target_schema;
    RETURN_NOT_OK(SchemaFromPB(table_schema_resp.schema(), &target_schema));

    // We now have a table match. Validate the schema.
    SCHECK(
        target_schema.EquivalentForDataCopy(source_schema), IllegalState,
        Format(
            "Source and target schemas don't match: "
            "Source: $0, Target: $1, Source schema: $2, Target schema: $3",
            source_table_info.table_id, table_schema_resp.identifier().table_id(),
            source_table_info.schema.ToString(), table_schema_resp.schema().DebugString()));
    break;
  }

  SCHECK(
      table->has_table_id(), NotFound,
      Format(
          "Could not find matching table for $0$1", source_table_info.table_name.ToString(),
          (is_ysql_table ? " pgschema_name: " + source_schema.SchemaName() : "")));

  if (source_table_info.colocated) {
    // We require that colocated tables have the same colocation ID.
    //
    // Backward compatibility: tables created prior to #7378 use YSQL table OID as a colocation
    // ID.
    SCHECK_FORMAT(
        source_table_info.schema.has_colocation_id(), NotFound,
        "Missing colocation ID for source table $0", source_table_info.table_id);
    SCHECK_FORMAT(
        table_schema_resp.schema().has_colocated_table_id() &&
            table_schema_resp.schema().colocated_table_id().has_colocation_id(),
        NotFound, "Missing colocation ID for target table $0",
        table_schema_resp.identifier().table_id());

    auto source_clc_id = source_table_info.schema.colocation_id();
    auto target_clc_id = table_schema_resp.schema().colocated_table_id().colocation_id();
    SCHECK_EQ(
        source_clc_id, target_clc_id, IllegalState,
        Format(
            "Source and target colocation IDs don't match for colocated table: "
            "Source: $0, Target: $1, Source colocation ID: $2, Target colocation ID: $3",
            source_table_info.table_id, table_schema_resp.identifier().table_id(), source_clc_id,
            target_clc_id));
  }

  return table_schema_resp;
}

Status XClusterTableSetupTask::PopulateTableStreamEntry(
    const TableId& target_table_id, const SchemaVersion& target_schema_version) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << YB_STRUCT_TO_STRING(target_table_id, target_schema_version);

  SCHECK(
      !parent_task_->xcluster_manager_.IsTableReplicationConsumer(target_table_id), IllegalState,
      "N:1 replication topology not supported");

  table_setup_info_.target_table_id = target_table_id;

  auto& stream_entry = table_setup_info_.stream_entry;
  stream_entry.set_consumer_table_id(target_table_id);
  stream_entry.set_producer_table_id(source_table_id_);

  RSTATUS_DCHECK_NE(
      target_schema_version, cdc::kInvalidSchemaVersion, IllegalState,
      Format(
          "Invalid target schema version for source table $0, target table $1", source_table_id_,
          target_table_id));

  stream_entry.mutable_producer_schema()->set_last_compatible_consumer_schema_version(
      target_schema_version);

  // Mark this stream as special if it is for the ddl_queue table.
  auto yb_table_info = parent_task_->catalog_manager_.GetTableInfo(target_table_id);
  stream_entry.set_is_ddl_queue_table(
      yb_table_info->GetTableType() == PGSQL_TABLE_TYPE &&
      yb_table_info->name() == xcluster::kDDLQueueTableName &&
      yb_table_info->pgschema_name() == xcluster::kDDLQueuePgSchemaName);

  return Status::OK();
}

Status XClusterTableSetupTask::ValidateBootstrapAndSetupStreams() {
  if (FLAGS_check_bootstrap_required) {
    RETURN_NOT_OK_PREPEND(
        ValidateBootstrapNotRequired(), "Error checking if bootstrap is required");
  }

  SetupStreams();

  return Status::OK();
}

Status XClusterTableSetupTask::ValidateBootstrapNotRequired() {
  boost::optional<xrepl::StreamId> bootstrap_id;

  if (table_setup_info_.stream_id) {
    bootstrap_id = table_setup_info_.stream_id;
  }

  // TODO: When FLAGS_check_bootstrap_required is enabled by default we need to convert this to a
  // async rpc call.
  if (VERIFY_RESULT(
          parent_task_->GetYbClient().IsBootstrapRequired({source_table_id_}, bootstrap_id))) {
    return STATUS(
        IllegalState, LogPrefix(),
        Format(
            "Bootstrap is required for Table $0$1", source_table_id_,
            (bootstrap_id ? Format(", stream $0", bootstrap_id->ToString()) : "")));
  }
  return Status::OK();
}

void XClusterTableSetupTask::SetupStreams() {
  if (!table_setup_info_.stream_id) {
    // Streams are used as soon as they are created so set state to active.
    parent_task_->GetXClusterClient().CreateXClusterStreamAsync(
        source_table_id_, /*active=*/true,
        cdc::StreamModeTransactional(parent_task_->transactional_),
        std::bind(&XClusterTableSetupTask::CreateXClusterStreamCallback, shared_from(this), _1));
    return;
  }

  auto received_table_id = std::make_shared<TableId>();
  auto stream_options = std::make_shared<std::unordered_map<std::string, std::string>>();
  parent_task_->GetYbClient().GetCDCStream(
      table_setup_info_.stream_id, received_table_id, stream_options,
      std::bind(
          &XClusterTableSetupTask::GetStreamCallback, shared_from(this), received_table_id,
          stream_options, _1));
}

void XClusterTableSetupTask::GetStreamCallback(
    std::shared_ptr<TableId> received_table_id,
    std::shared_ptr<std::unordered_map<std::string, std::string>> options, const Status& s) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << YB_STRUCT_TO_STRING(received_table_id, options, s);
  ScheduleNextStep(
      std::bind(
          &XClusterTableSetupTask::ProcessStreamOptions, shared_from(this), received_table_id,
          options, s),
      "Processing stream options");
}

Status XClusterTableSetupTask::ProcessStreamOptions(
    std::shared_ptr<TableId> received_table_id,
    std::shared_ptr<std::unordered_map<std::string, std::string>> options, const Status& s) {
  RETURN_NOT_OK_PREPEND(s, "Error from source universe");
  SCHECK(
      received_table_id != nullptr, InvalidArgument, "Received null table id from source universe");

  SCHECK_EQ(
      *received_table_id, source_table_id_, InvalidArgument,
      Format(
          "Invalid xCluster Stream id for table $0. Stream id $1 belongs to table $2",
          source_table_id_, table_setup_info_.stream_id, *received_table_id));

  // Store the stream options for later use. XClusterInboundReplicationGroupSetupTask::
  // UpdateSourceStreamOptions will process options of tables in one batch.
  table_setup_info_.stream_options.swap(*options);

  PopulateTabletMapping();

  return Status::OK();
}

void XClusterTableSetupTask::CreateXClusterStreamCallback(
    const Result<xrepl::StreamId>& stream_id) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << stream_id.ToString();
  ScheduleNextStep(
      std::bind(&XClusterTableSetupTask::ProcessNewStream, shared_from(this), std::move(stream_id)),
      "Processing new stream");
}

Status XClusterTableSetupTask::ProcessNewStream(const Result<xrepl::StreamId>& stream_id) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << stream_id;

  RETURN_NOT_OK_PREPEND(stream_id, "Error creating xCluster Stream on source universe");
  SCHECK(*stream_id, InvalidArgument, "Received invalid xCluster Stream id from source universe");

  table_setup_info_.stream_id = *stream_id;

  PopulateTabletMapping();

  return Status::OK();
}

void XClusterTableSetupTask::PopulateTabletMapping() {
  VLOG_WITH_PREFIX_AND_FUNC(1);

  parent_task_->GetYbClient().GetTableLocations(
      source_table_id_, /* max_tablets = */ std::numeric_limits<int32_t>::max(),
      RequireTabletsRunning::kTrue, PartitionsOnly::kTrue,
      std::bind(&XClusterTableSetupTask::PopulateTabletMappingCallback, shared_from(this), _1),
      IncludeInactive::kFalse);
}

void XClusterTableSetupTask::PopulateTabletMappingCallback(
    const Result<master::GetTableLocationsResponsePB*>& result) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << result.ToString();

  auto schedule_func = [this](Result<master::GetTableLocationsResponsePB>&& result) {
    ScheduleNextStep(
        std::bind(
            &XClusterTableSetupTask::ProcessTabletMapping, shared_from(this), std::move(result)),
        "Processing new stream");
  };

  if (!result.ok()) {
    schedule_func(result.status());
    return;
  }

  master::GetTableLocationsResponsePB resp;
  resp.Swap(*result);
  schedule_func(std::move(resp));
}

Status XClusterTableSetupTask::ProcessTabletMapping(
    const Result<master::GetTableLocationsResponsePB>& result) {
  RETURN_NOT_OK_PREPEND(result, "Error from source universe");
  const auto& resp = *result;
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status()).CloneAndPrepend("Error from source universe");
  }

  auto& tablets = resp.tablet_locations();

  auto target_tablet_keys = VERIFY_RESULT(
      parent_task_->catalog_manager_.GetTableKeyRanges(table_setup_info_.target_table_id));

  RETURN_NOT_OK(PopulateXClusterStreamEntryTabletMapping(
      source_table_id_, table_setup_info_.target_table_id, target_tablet_keys,
      &table_setup_info_.stream_entry, tablets));

  Complete();

  return Status::OK();
}

Status ValidateMasterAddressesBelongToDifferentCluster(
    Master& master, const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses) {
  std::vector<ServerEntryPB> cluster_master_addresses;
  RETURN_NOT_OK(master.ListMasters(&cluster_master_addresses));
  std::unordered_set<HostPort, HostPortHash> cluster_master_hps;

  for (const auto& cluster_elem : cluster_master_addresses) {
    if (cluster_elem.has_registration()) {
      auto p_rpc_addresses = cluster_elem.registration().private_rpc_addresses();
      for (const auto& p_rpc_elem : p_rpc_addresses) {
        cluster_master_hps.insert(HostPort::FromPB(p_rpc_elem));
      }

      auto broadcast_addresses = cluster_elem.registration().broadcast_addresses();
      for (const auto& bc_elem : broadcast_addresses) {
        cluster_master_hps.insert(HostPort::FromPB(bc_elem));
      }
    }

    for (const auto& master_address : master_addresses) {
      auto master_hp = HostPort::FromPB(master_address);
      SCHECK(
          !cluster_master_hps.contains(master_hp), InvalidArgument,
          "Master address $0 belongs to the target universe", master_hp);
    }
  }

  return Status::OK();
}

}  // namespace yb::master
