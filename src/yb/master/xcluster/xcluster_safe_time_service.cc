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

#include <chrono>

#include "yb/client/async_initializer.h"
#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/session.h"
#include "yb/client/yb_op.h"
#include "yb/common/wire_protocol.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"
#include "yb/util/atomic.h"
#include "yb/util/flag_tags.h"
#include "yb/util/thread.h"
#include "yb/client/schema.h"
#include "yb/client/table_handle.h"

using namespace std::chrono_literals;

DEFINE_int32(xcluster_safe_time_table_num_tablets, 1,
    "Number of tablets to use when creating the xcluster safe time table. "
    "0 to use the same default num tablets as for regular tables.");
TAG_FLAG(xcluster_safe_time_table_num_tablets, advanced);

DECLARE_int32(xcluster_safe_time_update_interval_secs);

namespace yb {
using OK = Status::OK;

namespace master {

const client::YBTableName kSafeTimeTableName(
    YQL_DATABASE_CQL, kSystemNamespaceName, kXClusterSafeTimeTableName);

XClusterSafeTimeService::XClusterSafeTimeService(Master* master, CatalogManager* catalog_manager)
    : master_(master),
      catalog_manager_(catalog_manager),
      shutdown_(false),
      shutdown_cond_(&shutdown_cond_lock_),
      task_enqueued_(false),
      safe_time_table_ready_(false),
      cluster_config_version_(kInvalidClusterConfigVersion) {}

XClusterSafeTimeService::~XClusterSafeTimeService() { Shutdown(); }

Status XClusterSafeTimeService::Init() {
  auto thread_pool_builder = ThreadPoolBuilder("XClusterSafeTimeServiceTasks");
  thread_pool_builder.set_max_threads(1);

  RETURN_NOT_OK(thread_pool_builder.Build(&thread_pool_));
  thread_pool_token_ = thread_pool_->NewToken(ThreadPool::ExecutionMode::SERIAL);

  return OK();
}

void XClusterSafeTimeService::Shutdown() {
  shutdown_ = true;
  shutdown_cond_.Broadcast();

  if (thread_pool_token_) {
    thread_pool_token_->Shutdown();
  }

  if (thread_pool_) {
    thread_pool_->Shutdown();
  }
}

void XClusterSafeTimeService::ScheduleTaskIfNeeded() {
  if (shutdown_) {
    return;
  }

  std::lock_guard lock(task_enqueue_lock_);
  if (task_enqueued_) {
    return;
  }

  // It is ok to scheduled a new task even when we have a running task. The thread pool token uses
  // serial execution and the task will sleep before returning. So it is always guaranteed that we
  // only run one task and that it will wait the required amount before running again.
  task_enqueued_ = true;
  Status s = thread_pool_token_->SubmitFunc(
      std::bind(&XClusterSafeTimeService::ProcessTaskPeriodically, this));
  if (!s.IsOk()) {
    task_enqueued_ = false;
    LOG(ERROR) << "Failed to schedule XClusterSafeTime Task :" << s;
  }
}

void XClusterSafeTimeService::ProcessTaskPeriodically() {
  {
    std::lock_guard lock(task_enqueue_lock_);
    task_enqueued_ = false;
  }

  if (shutdown_) {
    return;
  }

  auto wait_time = GetAtomicFlag(&FLAGS_xcluster_safe_time_update_interval_secs);
  if (wait_time <= 0) {
    VLOG_WITH_FUNC(1) << "Going into idle mode due to xcluster_safe_time_update_interval_secs flag";
    return;
  }

  int64_t leader_term = -1;
  {
    SCOPED_LEADER_SHARED_LOCK(l, catalog_manager_);

    if (!l.IsInitializedAndIsLeader()) {
      VLOG_WITH_FUNC(1) << "Going into idle mode due to master leader change";
      return;
    }

    leader_term = l.GetLeaderReadyTerm();
  }

  bool further_computation_needed = true;
  auto result = ComputeSafeTime(leader_term);
  if (result.ok()) {
    further_computation_needed = result.get();
  } else {
    LOG(WARNING) << "Failure in XClusterSafeTime task: " << result;
  }

  if (!further_computation_needed) {
    VLOG_WITH_FUNC(1) << "Going into idle mode due to lack of work";
    return;
  }

  // Delay before before running the task again.
  {
    MutexLock lock(shutdown_cond_lock_);
    shutdown_cond_.TimedWait(wait_time * 1s);
  }

  ScheduleTaskIfNeeded();
}

Status XClusterSafeTimeService::CreateXClusterSafeTimeTableIfNotFound() {
  if (PREDICT_TRUE(VERIFY_RESULT(
          catalog_manager_->TableExists(kSystemNamespaceName, kXClusterSafeTimeTableName)))) {
    return OK();
  }

  // Set up a CreateTable request internally.
  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  req.set_name(kXClusterSafeTimeTableName);
  req.mutable_namespace_()->set_name(kSystemNamespaceName);
  req.set_table_type(TableType::YQL_TABLE_TYPE);

  // Schema:
  // universe_id string (HASH), tablet_id string (HASH), safe_time int64
  client::YBSchemaBuilder schema_builder;
  schema_builder.AddColumn(kXCUniverseId)->HashPrimaryKey()->Type(DataType::STRING);
  schema_builder.AddColumn(kXCProducerTabletId)->HashPrimaryKey()->Type(DataType::STRING);
  schema_builder.AddColumn(kXCSafeTime)->Type(DataType::INT64);

  client::YBSchema yb_schema;
  RETURN_NOT_OK(schema_builder.Build(&yb_schema));

  const auto& schema = yb::client::internal::GetSchema(yb_schema);
  SchemaToPB(schema, req.mutable_schema());

  // Explicitly set the number tablets if the corresponding flag is set, otherwise CreateTable
  // will use the same defaults as for regular tables.
  if (FLAGS_xcluster_safe_time_table_num_tablets > 0) {
    req.mutable_schema()->mutable_table_properties()->set_num_tablets(
        FLAGS_xcluster_safe_time_table_num_tablets);
  }

  Status status = catalog_manager_->CreateTable(&req, &resp, nullptr /*RpcContext*/);

  // We do not lock here so it is technically possible that the table was already created.
  // If so, there is nothing to do so we just ignore the "AlreadyPresent" error.
  if (!status.ok() && !status.IsAlreadyPresent()) {
    return status;
  }

  return OK();
}

namespace {
XClusterNamespaceToSafeTimeMap ComputeSafeTimeMap(
    const XClusterNamespaceToSafeTimeMap& previous_safe_time_map,
    const std::map<NamespaceId, HybridTime>& namespace_min_safe_time) {
  XClusterNamespaceToSafeTimeMap new_safe_time_map;

  for (const auto& new_safe_time : namespace_min_safe_time) {
    auto previous_safe_time = HybridTime(FindWithDefault(
        previous_safe_time_map, new_safe_time.first, HybridTime::kInvalid));

    if (!new_safe_time.second.is_special() &&
        (!previous_safe_time.is_valid() || new_safe_time.second > previous_safe_time)) {
      new_safe_time_map[new_safe_time.first] = new_safe_time.second;
    } else {
      new_safe_time_map[new_safe_time.first] = previous_safe_time;
    }
  }

  return new_safe_time_map;
}
}  // namespace

Result<bool> XClusterSafeTimeService::ComputeSafeTime(const int64_t leader_term) {
  std::lock_guard lock(mutex_);
  auto tablet_to_safe_time_map = VERIFY_RESULT(GetSafeTimeFromTable());

  // The tablet map has to be updated after we read the table, as consumer registry could have
  // changed and tservers may have already started populating new entires in it
  RETURN_NOT_OK(RefreshProducerTabletToNamespaceMap());

  std::map<NamespaceId, HybridTime> namespace_min_safe_time;
  std::vector<ProducerTabletInfo> table_entries_to_delete;

  for (const auto& entry : producer_tablet_namespace_map_) {
    namespace_min_safe_time[entry.second] = HybridTime::kMax;
    // Add Invalid values for missing tablets
    InsertIfNotPresent(&tablet_to_safe_time_map, entry.first, HybridTime::kInvalid);
  }

  for (const auto& entry : tablet_to_safe_time_map) {
    auto* namespace_id = FindOrNull(producer_tablet_namespace_map_, entry.first);
    if (!namespace_id) {
      // Mark dropped tablets for cleanup
      table_entries_to_delete.emplace_back(entry.first);
      continue;
    }

    // Ignore values like Invalid, Min, Max and only consider a valid clock time.
    if (entry.second.is_special()) {
      namespace_min_safe_time[*namespace_id] = HybridTime::kInvalid;
      continue;
    }

    auto& safe_time = FindOrDie(namespace_min_safe_time, *namespace_id);

    if (safe_time.is_valid()) {
      safe_time = min(safe_time, entry.second);
    }
  }

  const auto previous_safe_time_map = VERIFY_RESULT(GetXClusterNamespaceToSafeTimeMap());
  auto new_safe_time_map = ComputeSafeTimeMap(previous_safe_time_map, namespace_min_safe_time);

  // Use the leader term to ensure leader has not changed between the time we did our computation
  // and setting the new config. Its important to make sure that the config we persist is accurate
  // as only that protects the safe time from going backwards.
  RETURN_NOT_OK(SetXClusterSafeTime(leader_term, std::move(new_safe_time_map)));

  // There is no guarantee that we are still running on a leader. But this is ok as we are just
  // performing idempotent clean up of stale entries in the table.
  RETURN_NOT_OK(CleanupEntriesFromTable(table_entries_to_delete));

  // We can stop the task when there is no replication streams present. We have already cleaned up
  // the safe time map in sys catalog and the table.
  // Note: Some TServers may not have gotten the updated registry yet and may reinsert into the
  // table. This is not an issue, as we will clean these up when replication starts again.
  bool further_computation_needed = !producer_tablet_namespace_map_.empty();

  return further_computation_needed;
}

Result<std::map<XClusterSafeTimeService::ProducerTabletInfo, HybridTime>>
XClusterSafeTimeService::GetSafeTimeFromTable() {
  std::map<ProducerTabletInfo, HybridTime> tablet_safe_time;

  auto* yb_client = master_->cdc_state_client_initializer().client();
  if (!yb_client) {
    return STATUS(IllegalState, "Client not initialized or shutting down");
  }

  if (!safe_time_table_ready_) {
    if (!VERIFY_RESULT(yb_client->TableExists(kSafeTimeTableName))) {
      if (!VERIFY_RESULT(CreateTableRequired())) {
        // Return empty map if table does not exist and create is not needed
        return tablet_safe_time;
      }

      // Table is created when consumer registry is updated. But this is needed to handle upgrades
      // of old clusters that have an already existing replication stream
      RETURN_NOT_OK(CreateXClusterSafeTimeTableIfNotFound());
    }

    RETURN_NOT_OK(yb_client->WaitForCreateTableToFinish(kSafeTimeTableName));
    safe_time_table_ready_ = true;
  }

  if (!safe_time_table_) {
    auto table = std::make_unique<client::TableHandle>();
    RETURN_NOT_OK(table->Open(kSafeTimeTableName, yb_client));
    table.swap(safe_time_table_);
  }

  Status table_scan_status;
  client::TableIteratorOptions options;
  options.error_handler = [&table_scan_status](const Status& status) {
    table_scan_status = status;
  };

  for (const auto& row : client::TableRange(*safe_time_table_, options)) {
    auto universe_id = row.column(kXCUniverseIdIdx).string_value();
    auto tablet_id = row.column(kXCProducerTabletIdIdx).string_value();
    auto safe_time = row.column(kXCSafeTimeIdx).int64_value();
    HybridTime safe_ht;
    RETURN_NOT_OK_PREPEND(
        safe_ht.FromUint64(static_cast<uint64_t>(safe_time)),
        Format(
            "Invalid safe time set in $0 table. universe_uuid:$1, tablet_id:$2",
            kSafeTimeTableName.table_name(), universe_id, tablet_id));

    tablet_safe_time[{universe_id, tablet_id}] = safe_ht;
  }

  RETURN_NOT_OK_PREPEND(
      table_scan_status, Format(
                             "Scan of table $0 failed: $1. Could not compute xcluster safe time.",
                             kSafeTimeTableName.table_name(), table_scan_status));

  return tablet_safe_time;
}

Status XClusterSafeTimeService::RefreshProducerTabletToNamespaceMap() {
  auto latest_config_version = VERIFY_RESULT(catalog_manager_->GetClusterConfigVersion());

  if (latest_config_version != cluster_config_version_) {
    producer_tablet_namespace_map_.clear();

    auto consumer_registry = VERIFY_RESULT(catalog_manager_->GetConsumerRegistry());
    if (consumer_registry) {
      const auto& producer_map = consumer_registry->producer_map();
      for (const auto& cluster_entry : producer_map) {
        if (cluster_entry.second.disable_stream()) {
          continue;
        }

        const auto& cluster_uuid = cluster_entry.first;
        for (const auto& stream_entry : cluster_entry.second.stream_map()) {
          const auto& consumer_table_id = stream_entry.second.consumer_table_id();
          auto consumer_namespace =
              VERIFY_RESULT(catalog_manager_->GetTableNamespaceId(consumer_table_id));

          for (const auto& tablets_entry : stream_entry.second.consumer_producer_tablet_map()) {
            for (const auto& tablet_id : tablets_entry.second.tablets()) {
              producer_tablet_namespace_map_[{cluster_uuid, tablet_id}] = consumer_namespace;
            }
          }
        }
      }
    }

    // Its important to use the version we got before getting the registry, as it could have
    // changed again.
    cluster_config_version_ = latest_config_version;
  }

  return OK();
}

Result<bool> XClusterSafeTimeService::CreateTableRequired() {
  // Create the table only if we have some replication streams

  RETURN_NOT_OK(RefreshProducerTabletToNamespaceMap());
  return !producer_tablet_namespace_map_.empty();
}

Result<XClusterNamespaceToSafeTimeMap>
XClusterSafeTimeService::GetXClusterNamespaceToSafeTimeMap() {
  return catalog_manager_->GetXClusterNamespaceToSafeTimeMap();
}

Status XClusterSafeTimeService::SetXClusterSafeTime(
    const int64_t leader_term, XClusterNamespaceToSafeTimeMap new_safe_time_map) {
  if (VLOG_IS_ON(2)) {
    for (auto& entry : new_safe_time_map) {
      VLOG_WITH_FUNC(2) << "NamespaceId: " << entry.first
                        << ", SafeTime: " << HybridTime(entry.second).ToDebugString();
    }
  }

  return catalog_manager_->SetXClusterNamespaceToSafeTimeMap(
      leader_term, std::move(new_safe_time_map));
}

Status XClusterSafeTimeService::CleanupEntriesFromTable(
    const std::vector<ProducerTabletInfo>& entries_to_delete) {
  if (entries_to_delete.empty()) {
    return OK();
  }

  auto* ybclient = master_->cdc_state_client_initializer().client();
  if (!ybclient) {
    return STATUS(IllegalState, "Client not initialized or shutting down");
  }

  // We should have already scanned the table to get the list of entries to delete.
  DCHECK(safe_time_table_ready_);
  DCHECK(safe_time_table_);

  std::shared_ptr<client::YBSession> session = ybclient->NewSession();
  session->SetTimeout(ybclient->default_rpc_timeout());

  std::vector<client::YBOperationPtr> ops;
  ops.reserve(entries_to_delete.size());

  for (auto& tablet : entries_to_delete) {
    const auto op = safe_time_table_->NewWriteOp(QLWriteRequestPB::QL_STMT_DELETE);
    auto* const req = op->mutable_request();
    QLAddStringHashValue(req, tablet.cluster_uuid);
    QLAddStringHashValue(req, tablet.tablet_id);

    VLOG_WITH_FUNC(1) << "Cleaning up tablet from " << kSafeTimeTableName.table_name()
                      << ". cluster_uuid: " << tablet.cluster_uuid
                      << ", tablet_id: " << tablet.tablet_id;

    ops.push_back(std::move(op));
  }

  RETURN_NOT_OK_PREPEND(
      session->ApplyAndFlushSync(ops), "Failed to cleanup to XClusterSafeTime table");

  return OK();
}

}  // namespace master
}  // namespace yb
