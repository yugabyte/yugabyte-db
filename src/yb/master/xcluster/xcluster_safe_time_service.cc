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
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/common/schema_pbutil.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"

#include "yb/util/atomic.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/string_util.h"
#include "yb/util/thread.h"

using std::min;

using namespace std::chrono_literals;

DEFINE_NON_RUNTIME_int32(xcluster_safe_time_table_num_tablets, 1,
    "Number of tablets to use when creating the xcluster safe time table. "
    "0 to use the same default num tablets as for regular tables.");
TAG_FLAG(xcluster_safe_time_table_num_tablets, advanced);

DECLARE_int32(xcluster_safe_time_update_interval_secs);

DEFINE_RUNTIME_uint32(xcluster_safe_time_log_outliers_interval_secs, 600,
    "Frequency in seconds at which to log outlier tablets for xcluster safe time.");

DEFINE_RUNTIME_uint32(xcluster_safe_time_slow_tablet_delta_secs, 600,
    "Lag in seconds at which a tablet is considered an outlier for xcluster safe time.");

METRIC_DECLARE_entity(cluster);

namespace yb {
using OK = Status::OK;

namespace master {

const client::YBTableName kSafeTimeTableName(
    YQL_DATABASE_CQL, kSystemNamespaceName, kXClusterSafeTimeTableName);

XClusterSafeTimeService::XClusterSafeTimeService(
    Master* master, CatalogManager* catalog_manager, MetricRegistry* metric_registry)
    : master_(master),
      catalog_manager_(catalog_manager),
      shutdown_(false),
      shutdown_cond_(&shutdown_cond_lock_),
      task_enqueued_(false),
      safe_time_table_ready_(false),
      cluster_config_version_(kInvalidClusterConfigVersion),
      metric_registry_(metric_registry) {}

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
    // Can only happen in tests
    VLOG_WITH_FUNC(1) << "Going into idle mode due to xcluster_safe_time_update_interval_secs flag";
    EnterIdleMode("xcluster_safe_time_update_interval_secs flag");
    return;
  }

  auto leader_term_result = GetLeaderTermFromCatalogManager();
  if (!leader_term_result.ok()) {
    VLOG_WITH_FUNC(1) << "Going into idle mode due to master leader change";
    EnterIdleMode("master leader change");
    return;
  }
  int64_t leader_term = leader_term_result.get();

  // Compute safe time now and also update the metrics.
  bool further_computation_needed = true;
  auto result = ComputeSafeTime(leader_term, /* update_metrics */ true);
  if (result.ok()) {
    further_computation_needed = result.get();
  } else {
    LOG(WARNING) << "Failure in XClusterSafeTime task: " << result;
  }

  if (!further_computation_needed) {
    VLOG_WITH_FUNC(1) << "Going into idle mode due to lack of work";
    EnterIdleMode("no more work left");
    return;
  }

  // Delay before before running the task again.
  {
    MutexLock lock(shutdown_cond_lock_);
    shutdown_cond_.TimedWait(wait_time * 1s);
  }

  ScheduleTaskIfNeeded();
}

Status XClusterSafeTimeService::GetXClusterSafeTimeInfoFromMap(
    const LeaderEpoch& epoch, GetXClusterSafeTimeResponsePB* resp) {
  // Recompute safe times again before fetching maps.
  const auto& current_safe_time_map =
      VERIFY_RESULT(RefreshAndGetXClusterNamespaceToSafeTimeMap(epoch));
  XClusterNamespaceToSafeTimeMap max_safe_time_map;
  {
    std::lock_guard lock(mutex_);
    max_safe_time_map = GetMaxNamespaceSafeTimeFromMap(VERIFY_RESULT(GetSafeTimeFromTable()));
  }
  const auto cur_time_micros = GetCurrentTimeMicros();

  for (const auto& [namespace_id, safe_time] : current_safe_time_map) {
    // First set all the current safe time values.
    auto entry = resp->add_namespace_safe_times();
    entry->set_namespace_id(namespace_id);
    entry->set_safe_time_ht(safe_time.ToUint64());
    // Safe time lag is calculated as (current time - current safe time).
    entry->set_safe_time_lag(
        std::max(cur_time_micros - safe_time.GetPhysicalValueMicros(), (uint64_t)0));

    // Then find and set the skew.
    // Safe time skew is calculated as (safe time of most caught up tablet - safe time of
    // laggiest tablet).
    const auto it = max_safe_time_map.find(namespace_id);
    if (safe_time.is_special() || it == max_safe_time_map.end() || it->second.is_special()) {
      // Missing a valid safe time, so return an invalid value.
      entry->set_safe_time_skew(UINT64_MAX);
      continue;
    }

    const auto& max_safe_time = it->second;
    if (max_safe_time < safe_time) {
      // Very rare case that could happen since clocks are not synced.
      entry->set_safe_time_skew(0);
    } else {
      entry->set_safe_time_skew(max_safe_time.PhysicalDiff(safe_time));
    }
  }

  return Status::OK();
}

Result<std::unordered_map<NamespaceId, uint64_t>>
XClusterSafeTimeService::GetEstimatedDataLossMicroSec(const LeaderEpoch& epoch) {
  // Recompute safe times again before fetching maps.
  const auto& current_safe_time_map =
      VERIFY_RESULT(RefreshAndGetXClusterNamespaceToSafeTimeMap(epoch));
  XClusterNamespaceToSafeTimeMap max_safe_time_map;
  {
    std::lock_guard lock(mutex_);
    max_safe_time_map = GetMaxNamespaceSafeTimeFromMap(VERIFY_RESULT(GetSafeTimeFromTable()));
  }

  std::unordered_map<NamespaceId, uint64_t> safe_time_diff_map;
  // current_safe_time_map is the source of truth, so loop over it to construct the final mapping.
  for (const auto& [namespace_id, safe_time] : current_safe_time_map) {
    const auto it = max_safe_time_map.find(namespace_id);
    if (safe_time.is_special() || it == max_safe_time_map.end() || it->second.is_special()) {
      // Missing a valid safe time, so return an invalid value.
      safe_time_diff_map[namespace_id] = UINT64_MAX;
      continue;
    }

    const auto& max_safe_time = it->second;
    if (max_safe_time < safe_time) {
      // Very rare case that could happen since clocks are not synced.
      safe_time_diff_map[namespace_id] = 0;
    } else {
      safe_time_diff_map[namespace_id] = max_safe_time.PhysicalDiff(safe_time);
    }
  }

  return safe_time_diff_map;
}

Result<XClusterNamespaceToSafeTimeMap>
XClusterSafeTimeService::RefreshAndGetXClusterNamespaceToSafeTimeMap(const LeaderEpoch& epoch) {
  RETURN_NOT_OK(ComputeSafeTime(epoch.leader_term));
  return GetXClusterNamespaceToSafeTimeMap();
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
  // replication_group_id string (HASH), tablet_id string (HASH), safe_time int64
  client::YBSchemaBuilder schema_builder;
  schema_builder.AddColumn(kXCReplicationGroupId)->HashPrimaryKey()->Type(DataType::STRING);
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

  Status status = catalog_manager_->CreateTable(
      &req, &resp, nullptr /*RpcContext*/, catalog_manager_->GetLeaderEpochInternal());

  // We do not lock here so it is technically possible that the table was already created.
  // If so, there is nothing to do so we just ignore the "AlreadyPresent" error.
  if (!status.ok() && !status.IsAlreadyPresent()) {
    return status;
  }

  return OK();
}

namespace {
HybridTime GetNewSafeTime(
    const XClusterNamespaceToSafeTimeMap& previous_safe_time_map, const NamespaceId& namespace_id,
    const HybridTime& safe_time) {
  auto previous_safe_time =
      HybridTime(FindWithDefault(previous_safe_time_map, namespace_id, HybridTime::kInvalid));

  if (!safe_time.is_special() &&
      (!previous_safe_time.is_valid() || safe_time > previous_safe_time)) {
    return safe_time;
  }

  return previous_safe_time;
}

XClusterNamespaceToSafeTimeMap ComputeSafeTimeMap(
    const XClusterNamespaceToSafeTimeMap& previous_safe_time_map,
    const std::unordered_map<NamespaceId, HybridTime>& namespace_safe_time) {
  XClusterNamespaceToSafeTimeMap new_safe_time_map;

  // System tables like 'transactions' table affect the safe time of every user namespace. Compute
  // that first and use it as min in every other namespace.
  HybridTime sys_safe_time = HybridTime::kInvalid;
  auto sys_namespace_it = FindOrNull(namespace_safe_time, kSystemNamespaceId);
  if (sys_namespace_it) {
    sys_safe_time = new_safe_time_map[kSystemNamespaceId] =
        GetNewSafeTime(previous_safe_time_map, kSystemNamespaceId, *sys_namespace_it);
  }

  for (auto[namespace_id, safe_time] : namespace_safe_time) {
    if (namespace_id == kSystemNamespaceId) {
      continue;
    }

    if (!safe_time.is_special() &&
        sys_namespace_it &&
        (sys_safe_time.is_special() || sys_safe_time < safe_time)) {
      // Set the safe time of the user namespace when the following 3 conditions are true:
      // 1. The user namespace safe time is valid. If it's invalid, it means that not all tablets in
      // the safe time table have valid values and we want to let GetNewSafeTime figure out the safe
      // time.
      // 2. The system namespace is in the safe time map.
      // 3. The system namespace is either invalid or its safe time is less than than the user
      // namespace safe time, and we always want to use the min of the two.
      safe_time = sys_safe_time;
    }

    new_safe_time_map[namespace_id] =
        GetNewSafeTime(previous_safe_time_map, namespace_id, safe_time);
  }

  return new_safe_time_map;
}

// Similar to YB_LOG_EVERY_N_SECS, but doesn't return ShouldLog true until interval has passed since
// instance creation. (YB_LOG_EVERY_N_SECS returns ShouldLog true the first time it is called)
class LogThrottle {
 public:
  LogThrottle() { last_timestamp_ = GetMonoTimeMicros(); }

  bool ShouldLog(const MonoDelta& interval) {
    MicrosecondsInt64 current_timestamp = GetMonoTimeMicros();
    if (current_timestamp - last_timestamp_ > interval.ToMicroseconds()) {
      last_timestamp_ = current_timestamp;
      return true;
    }
    return false;
  }

 private:
  MicrosecondsInt64 last_timestamp_;
};
}  // namespace

Result<bool> XClusterSafeTimeService::ComputeSafeTime(
    const int64_t leader_term, bool update_metrics) {
  std::lock_guard lock(mutex_);
  auto tablet_to_safe_time_map = VERIFY_RESULT(GetSafeTimeFromTable());

  // The tablet map has to be updated after we read the table, as consumer registry could have
  // changed and tservers may have already started populating new entries in it.
  RETURN_NOT_OK(RefreshProducerTabletToNamespaceMap());

  static LogThrottle log_throttle;
  const bool should_log_outlier_tablets =
      log_throttle.ShouldLog(1s * FLAGS_xcluster_safe_time_log_outliers_interval_secs);

  std::unordered_map<NamespaceId, HybridTime> namespace_safe_time_map;
  std::vector<ProducerTabletInfo> table_entries_to_delete;

  // Track tablets that are missing from the safe time, or slow. This is for reporting only.
  std::unordered_map<NamespaceId, std::vector<TabletId>> tablets_missing_safe_time_map;
  std::unordered_map<NamespaceId, std::vector<TabletId>> slow_tablets_map;
  std::unordered_map<NamespaceId, HybridTime> namespace_max_safe_time;
  std::unordered_map<NamespaceId, HybridTime> namespace_min_safe_time;

  for (const auto& [tablet_info, namespace_id] : producer_tablet_namespace_map_) {
    namespace_safe_time_map[namespace_id] = HybridTime::kMax;
    // Add Invalid values for missing tablets
    InsertIfNotPresent(&tablet_to_safe_time_map, tablet_info, HybridTime::kInvalid);
    if (should_log_outlier_tablets) {
      const auto& tablet_safe_time = tablet_to_safe_time_map[tablet_info];
      if (tablet_safe_time.is_special()) {
        tablets_missing_safe_time_map[namespace_id].emplace_back(tablet_info.tablet_id);
      } else {
        namespace_max_safe_time[namespace_id].MakeAtLeast(tablet_safe_time);
      }
    }
  }

  if (should_log_outlier_tablets) {
    for (const auto& [namespace_id, tablet_ids] : tablets_missing_safe_time_map) {
      LOG(WARNING) << "Missing xcluster safe time for producer tablet(s) "
                   << JoinStringsLimitCount(tablet_ids, ",", 20) << " in namespace "
                   << namespace_id;
    }
  }

  for (const auto& [tablet_info, tablet_safe_time] : tablet_to_safe_time_map) {
    auto* namespace_id = FindOrNull(producer_tablet_namespace_map_, tablet_info);
    if (!namespace_id) {
      // Mark dropped tablets for cleanup
      table_entries_to_delete.emplace_back(tablet_info);
      continue;
    }

    // Ignore values like Invalid, Min, Max and only consider a valid clock time.
    if (tablet_safe_time.is_special()) {
      namespace_safe_time_map[*namespace_id] = HybridTime::kInvalid;
      continue;
    }

    if (should_log_outlier_tablets) {
      if (tablet_safe_time.AddDelta(1s * FLAGS_xcluster_safe_time_slow_tablet_delta_secs) <
          namespace_max_safe_time[*namespace_id]) {
        namespace_min_safe_time[*namespace_id].MakeAtMost(tablet_safe_time);
        slow_tablets_map[*namespace_id].emplace_back(tablet_info.tablet_id);
      }
    }

    auto& namespace_safe_time = FindOrDie(namespace_safe_time_map, *namespace_id);

    // Ignore if it has been marked as invalid.
    if (namespace_safe_time.is_valid()) {
      namespace_safe_time.MakeAtMost(tablet_safe_time);
    }
  }

  if (should_log_outlier_tablets) {
    for (const auto& [namespace_id, tablet_ids] : slow_tablets_map) {
      LOG(WARNING) << "xcluster safe time for namespace " << namespace_id << " is held up by "
                   << namespace_max_safe_time[namespace_id].PhysicalDiff(
                          namespace_min_safe_time[namespace_id]) /
                          MonoTime::kMicrosecondsPerSecond
                   << "s due to producer tablet(s) " << JoinStringsLimitCount(tablet_ids, ",", 20);
    }
  }

  const auto previous_safe_time_map = VERIFY_RESULT(GetXClusterNamespaceToSafeTimeMap());
  auto new_safe_time_map = ComputeSafeTimeMap(previous_safe_time_map, namespace_safe_time_map);

  // Use the leader term to ensure leader has not changed between the time we did our computation
  // and setting the new config. Its important to make sure that the config we persist is accurate
  // as only that protects the safe time from going backwards.
  RETURN_NOT_OK(SetXClusterSafeTime(leader_term, new_safe_time_map));

  if (update_metrics) {
    // Update the metrics using the newly computed maps.
    UpdateMetrics(tablet_to_safe_time_map, new_safe_time_map);
  }

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

Result<XClusterSafeTimeService::ProducerTabletToSafeTimeMap>
XClusterSafeTimeService::GetSafeTimeFromTable() {
  ProducerTabletToSafeTimeMap tablet_safe_time;

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
    auto replication_group_id = row.column(kXCReplicationGroupIdIdx).string_value();
    auto tablet_id = row.column(kXCProducerTabletIdIdx).string_value();
    auto safe_time = row.column(kXCSafeTimeIdx).int64_value();
    HybridTime safe_ht;
    RETURN_NOT_OK_PREPEND(
        safe_ht.FromUint64(static_cast<uint64_t>(safe_time)),
        Format(
            "Invalid safe time set in $0 table. universe_uuid:$1, tablet_id:$2",
            kSafeTimeTableName.table_name(), replication_group_id, tablet_id));

    tablet_safe_time[{replication_group_id, tablet_id}] = safe_ht;
  }

  RETURN_NOT_OK_PREPEND(
      table_scan_status, Format(
                             "Scan of table $0 failed: $1. Could not compute xcluster safe time.",
                             kSafeTimeTableName.table_name(), table_scan_status));

  return tablet_safe_time;
}

XClusterNamespaceToSafeTimeMap XClusterSafeTimeService::GetMaxNamespaceSafeTimeFromMap(
    const ProducerTabletToSafeTimeMap& tablet_to_safe_time_map) {
  XClusterNamespaceToSafeTimeMap max_safe_time_map;
  for (const auto& [prod_tablet_info, safe_time] : tablet_to_safe_time_map) {
    const auto* namespace_id = FindOrNull(producer_tablet_namespace_map_, prod_tablet_info);
    if (!namespace_id) {
      // Stale entry in the table, can skip this namespace.
      continue;
    }
    if (!safe_time.is_special()) {
      auto it = max_safe_time_map.find(*namespace_id);
      if (it == max_safe_time_map.end() || (!it->second.is_special() && it->second < safe_time)) {
        max_safe_time_map[*namespace_id] = safe_time;
      }
    } else {
      max_safe_time_map[*namespace_id] = HybridTime::kInvalid;
    }
  }
  return max_safe_time_map;
}

Status XClusterSafeTimeService::RefreshProducerTabletToNamespaceMap() {
  auto latest_config_version = VERIFY_RESULT(catalog_manager_->GetClusterConfigVersion());

  if (latest_config_version != cluster_config_version_) {
    producer_tablet_namespace_map_.clear();

    auto consumer_registry = VERIFY_RESULT(catalog_manager_->GetConsumerRegistry());
    if (consumer_registry && consumer_registry->role() != cdc::XClusterRole::ACTIVE) {
      const auto& producer_map = consumer_registry->producer_map();
      for (const auto& cluster_entry : producer_map) {
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
  return master_->xcluster_manager()->GetXClusterNamespaceToSafeTimeMap();
}

Status XClusterSafeTimeService::SetXClusterSafeTime(
    const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& new_safe_time_map) {
  if (VLOG_IS_ON(2)) {
    for (auto& [namespace_id, safe_time] : new_safe_time_map) {
      VLOG_WITH_FUNC(2) << "NamespaceId: " << namespace_id
                        << ", SafeTime: " << HybridTime(safe_time).ToDebugString();
    }
  }

  return master_->xcluster_manager()->SetXClusterNamespaceToSafeTimeMap(
      leader_term, new_safe_time_map);
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

  auto session = ybclient->NewSession(ybclient->default_rpc_timeout());

  for (auto& tablet : entries_to_delete) {
    const auto op = safe_time_table_->NewWriteOp(QLWriteRequestPB::QL_STMT_DELETE);
    auto* const req = op->mutable_request();
    QLAddStringHashValue(req, tablet.cluster_uuid);
    QLAddStringHashValue(req, tablet.tablet_id);

    VLOG_WITH_FUNC(1) << "Cleaning up tablet from " << kSafeTimeTableName.table_name()
                      << ". cluster_uuid: " << tablet.cluster_uuid
                      << ", tablet_id: " << tablet.tablet_id;

    session->Apply(std::move(op));
  }

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_PREPEND(session->TEST_Flush(), "Failed to cleanup to XClusterSafeTime table");

  return OK();
}

Result<int64_t> XClusterSafeTimeService::GetLeaderTermFromCatalogManager() {
  SCOPED_LEADER_SHARED_LOCK(l, catalog_manager_);

  if (!l.IsInitializedAndIsLeader()) {
    return l.first_failed_status();
  }

  return l.GetLeaderReadyTerm();
}

void XClusterSafeTimeService::UpdateMetrics(
    const ProducerTabletToSafeTimeMap& safe_time_map,
    const XClusterNamespaceToSafeTimeMap& current_safe_time_map) {
  const auto max_safe_time_map = GetMaxNamespaceSafeTimeFromMap(safe_time_map);
  const auto cur_time_micros = GetCurrentTimeMicros();

  // current_safe_time_map is the source of truth, so loop over it to construct the final mapping.
  for (const auto& [namespace_id, safe_time] : current_safe_time_map) {
    // Check if the metric exists or not.
    auto metrics_it = cluster_metrics_per_namespace_.find(namespace_id);
    if (metrics_it == cluster_metrics_per_namespace_.end()) {
      // Instantiate the metric.
      MetricEntity::AttributeMap attrs;
      attrs["namespace_id"] = namespace_id;

      scoped_refptr<yb::MetricEntity> entity;
      entity = METRIC_ENTITY_cluster.Instantiate(metric_registry_, namespace_id, attrs);

      metrics_it =
          cluster_metrics_per_namespace_
              .emplace(
                  namespace_id, std::make_unique<xcluster::XClusterConsumerClusterMetrics>(entity))
              .first;
    }

    // In the case that we cannot get valid safe times yet, set the metrics to 0.
    uint64_t consumer_safe_time_skew_ms = 0;
    uint64_t consumer_safe_time_lag_ms = 0;

    if (!safe_time.is_special()) {
      // Fetch the max safe time if it is valid.
      const auto it = max_safe_time_map.find(namespace_id);
      if (it != max_safe_time_map.end() && !it->second.is_special()) {
        DCHECK_GE(it->second, safe_time);
        const auto& max_safe_time = std::max(it->second, safe_time);

        // Compute the metrics, note conversion to milliseconds.
        consumer_safe_time_skew_ms = max_safe_time.PhysicalDiff(safe_time) /
            MonoTime::kMicrosecondsPerMillisecond;
        consumer_safe_time_lag_ms = (cur_time_micros - safe_time.GetPhysicalValueMicros()) /
            MonoTime::kMicrosecondsPerMillisecond;
      }
    }

    // Set the metric values.
    metrics_it->second->consumer_safe_time_skew->set_value(consumer_safe_time_skew_ms);
    metrics_it->second->consumer_safe_time_lag->set_value(consumer_safe_time_lag_ms);
  }

  // Delete any non-existant namespaces leftover in the metrics.
  for (auto it = cluster_metrics_per_namespace_.begin();
       it != cluster_metrics_per_namespace_.end();) {
    const auto& namespace_id = it->first;
    if (!current_safe_time_map.contains(namespace_id)) {
      it = cluster_metrics_per_namespace_.erase(it);
    } else {
      ++it;
    }
  }
}

void XClusterSafeTimeService::EnterIdleMode(const std::string& reason) {
  VLOG(1) << "XClusterSafeTimeService entering idle mode due to: " << reason;
  std::lock_guard lock(mutex_);
  cluster_metrics_per_namespace_.clear();
  return;
}

xcluster::XClusterConsumerClusterMetrics* XClusterSafeTimeService::TEST_GetMetricsForNamespace(
    const NamespaceId& namespace_id) {
  std::lock_guard lock(mutex_);
  return cluster_metrics_per_namespace_[namespace_id].get();
}

}  // namespace master
}  // namespace yb
