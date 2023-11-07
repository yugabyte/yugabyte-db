// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/tserver/tablet_validator.h"

#include <chrono>
#include <iterator>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/common/entity_ids_types.h"
#include "yb/common/index.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/master/master_ddl.pb.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/background_task.h"
#include "yb/util/compare_util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/tostring.h"

DEFINE_RUNTIME_uint32(tablet_validator_retain_delete_markers_validation_period_sec, 60,
    "The time period in seconds for validation of retain delete markers property for index tables. "
    "The value is not updated immediately, it is posponed till the nearest run of the periodic "
    "validation task. The value of 0 is used to disable verification. In this case, the validation "
    "task wakes up every minute to get a new flag value.");
TAG_FLAG(tablet_validator_retain_delete_markers_validation_period_sec, advanced);

DEFINE_test_flag(uint32, tablet_validator_cache_retention_interval_sec, 3600,
    "Used in tests to indicate the time period in seconds "
    "to retain tablet validator cache's entries.");

DEFINE_test_flag(uint32, tablet_validator_max_tables_number_per_rpc, 50,
    "Used in tests to indicate the maximum number of tables whose backfill status could be "
    "requested within a single GetBackfillStatus request. The response will contain the actual set "
    "of tables being passed via the corresponding request. It is the responsibility of a caller "
    "to issue next GetBackfillStatus request if necessary. Set to 0 to avoid maximum limit.");

DEFINE_test_flag(bool, tablet_validator_one_rpc_per_validation_period, false,
    "Used in tests to send exactly only one RPC per validation period.");

using namespace std::literals;

namespace yb::tserver {
namespace {

// The first purpose of IndexMap is to reduce the number of RPCs sent to the master. The status
// is requested not per index tablet, but per index table the corresponding tablets belong to.
// For example, if an index table have 3 tablets hosted on the tserver, there will be only one
// RPC which includes the id of that index table and the status will be propagate across all the
// index tablets during handling of the response. Note: one RPC may include several index tables.
// The second purporse of IndexMap is to reduce the number of locks (table requests) made during
// backfill status preparation at catalog manager side. All index tables are grouped by the
// corresponding indexed table as it's requrired to access the indexed table info to get the
// backfilling status and such grouping allows to make only one search of the indexed table --
// refer to CatalogManager::GetBackfillStatus() for the details.
struct IndexTableIdTag;
struct IndexTableInfo {
  TabletId index_tablet_id;
  TableId  index_table_id;
  TableId  group_id; // Generally contains indexed_table_id.

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(index_tablet_id, index_table_id, group_id);
  }
};
using IndexMap = boost::multi_index::multi_index_container<
    IndexTableInfo,
    boost::multi_index::indexed_by<
        // Grouped by group_id, index_table_id (not interested in index_tablet_id). Used to order
        // indexes by indexed table id at the first place to put indexes of the same table into the
        // same RPC, which lets us take less locks when retrieving backfilling status on the master.
        boost::multi_index::ordered_non_unique<boost::multi_index::composite_key<
            IndexTableInfo,
            BOOST_MULTI_INDEX_MEMBER(IndexTableInfo, TableId, group_id),
            BOOST_MULTI_INDEX_MEMBER(IndexTableInfo, TableId, index_table_id)>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<IndexTableIdTag>,
            BOOST_MULTI_INDEX_MEMBER(IndexTableInfo, TableId, index_table_id)>>>;

struct TableCacheEntry {
  CoarseTimePoint timestamp;
  master::IndexStatusPB index_status;

  const TableId& table_id() const { return index_status.index_table().table_id(); }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(timestamp, index_status);
  }
};

struct TableIdTag;
using TableCache = boost::multi_index::multi_index_container<
    TableCacheEntry,
    boost::multi_index::indexed_by<
        boost::multi_index::ordered_non_unique<
            BOOST_MULTI_INDEX_MEMBER(TableCacheEntry, CoarseTimePoint, timestamp)>,
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<TableIdTag>,
            BOOST_MULTI_INDEX_CONST_MEM_FUN(TableCacheEntry, const TableId&, table_id)>>>;

bool IsBackfillDone(const master::IndexStatusPB& index_status) {
  // Sanity check to make sure table_id is set - this is what is used for requesting the status.
  CHECK(index_status.has_index_table() && index_status.index_table().has_table_id());
  return index_status.has_backfill_status() &&
         index_status.backfill_status() == master::IndexStatusPB::BACKFILL_SUCCESS;
}

using TabletTablesMap = std::unordered_map<TabletId, std::vector<TableId>>;

std::chrono::milliseconds SanitizeRetainDeleteMarkersValidationPeriod(const uint32_t value) {
  return MonoDelta::FromSeconds(value == 0 ? 60 : value).ToChronoMilliseconds();
}

} // namespace

class TabletMetadataValidator::Impl final {
 public:
  explicit Impl(const std::string& log_prefix, TSTabletManager* tablet_manager)
      : log_prefix_(log_prefix), tablet_manager_(*CHECK_NOTNULL(tablet_manager)) {
    LOG_WITH_PREFIX(INFO) << "Tablet validator created";
  }

  Status Init();
  void ScheduleValidation(const tablet::RaftGroupMetadata& metadata);
  void Shutdown();

 private:
  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  // Requesting backfill status from the master and re-issuing backfilling
  // done for a corresponding tablet peer if necessary.
  void DoValidate();

  // Parses response from the master and triggers meta data update for all applicables tablets.
  // Returns true if response has no master error.
  bool HandleMasterResponse(const master::GetBackfillStatusResponsePB& response);

  // Iterates through all the tablets registered for the validation and adds corresponding on-cached
  // tables into the synchronization queue. Additionally triggers metadata update (if applicable)
  // for all tablets, whose tables are cached, without requesting an index status from the master.
  // Returns a pair { num tablets to sync, num tablets updated in-place }.
  std::pair<size_t, size_t> HandleScheduledTablets() EXCLUDES(scheduled_index_tablets_mutex_);

  // Must run this job periodically to avoid memory leaks and remove old entries from the cache.
  void RemoveStaleEntriesFromTableCache();

  // Checks if a table is a candidate for the validation on base of its schema's table properties.
  bool ScheduleTabletPropertiesValidation(
      const tablet::RaftGroupMetadata& meta) EXCLUDES(scheduled_index_tablets_mutex_);

  // Background job for validation or/and cleanup work. If retain delete markers property is set
  // to true, tablet validator requests a backfilling status for the corresponing tablet from
  // the master. The property will be update to false if that index tablet backfilling has been
  // successfully completed.
  void Sync() EXCLUDES(scheduled_index_tablets_mutex_);

  // Requests index permissions for tables_to_sync_.
  Result<master::GetBackfillStatusResponsePB> SyncWithMaster();

  // Updates metadata for the requested indexes. Currently, may trigger OnBackfillDone() only.
  void TriggerMetadataUpdate(const TabletTablesMap& indexes_for_update);

 private:
  std::string log_prefix_;
  TSTabletManager& tablet_manager_;
  std::atomic<bool> shutting_down_{false};

  std::unique_ptr<BackgroundTask> validation_task_;

  // Table cache is used to reduce the number of RPC sent to the master. If we've already requested
  // the index table state it will be perisisted in the cache for the configured retention period.
  TableCache index_tables_cache_; // Accessed from the master sync job only.

  // The map is used to optimize the number of RPCs from a tserver to the master and the number of
  // table locks on the master side. Refer to the extended comment for IndexMap.
  IndexMap index_tablets_to_sync_; // Accessed from the master sync job only.

  std::mutex scheduled_index_tablets_mutex_;
  std::vector<IndexTableInfo> scheduled_index_tablets_ GUARDED_BY(scheduled_index_tablets_mutex_);
};

void TabletMetadataValidator::Impl::DoValidate() {
  bool request_next_batch = true;
  while (request_next_batch) {
    const auto [num_tablets_to_sync, num_tablets_updated] = HandleScheduledTablets();

    if (num_tablets_to_sync == 0) {
      request_next_batch = num_tablets_updated;
      continue;
    }

    auto sync_result = SyncWithMaster();
    if (!sync_result.ok()) {
      LOG_WITH_PREFIX(ERROR) << "Failed to sync with the master, status: " << sync_result.status();
      break;
    }

    request_next_batch = HandleMasterResponse(*sync_result) &&
        !FLAGS_TEST_tablet_validator_one_rpc_per_validation_period;
  }
}

bool TabletMetadataValidator::Impl::HandleMasterResponse(
    const master::GetBackfillStatusResponsePB& response) {
  VLOG_WITH_PREFIX_AND_FUNC(2) << "response: " << response.ShortDebugString();

  if (response.has_error()) {
    LOG_WITH_PREFIX(ERROR) << "Failed to get backfilling status from the master, "
                           << "error: " << response.error().ShortDebugString();
    return false; // Will try during next period.
  }

  // Check there's something to handle.
  if (response.index_status_size() == 0) {
    return true; // Returning true due to no master error for the response.
  }

  TabletTablesMap indexes_for_update;
  indexes_for_update.reserve(response.index_status_size());

  // We are going to update table cache with received indexes and their status.
  auto& cached_tables = index_tables_cache_.get<TableIdTag>();
  const auto now = CoarseMonoClock::Now();

  // Extend set of backfilled tables with the backfilled tables from the response.
  for (const auto& index_status : response.index_status()) {
    // Sanity check, we're requesting tables by table_id and are expecting to have
    // the table_id set in the response.
    if (!index_status.has_index_table() || !index_status.index_table().has_table_id()) {
      LOG_WITH_PREFIX(DFATAL)
          << "Backfilling status is expected to have table_id be specified, received table: "
          << (index_status.has_index_table() ? index_status.index_table().ShortDebugString() : "-");
      continue;
    }
    const auto& index_table_id = index_status.index_table().table_id();

    // First step, add table to the cache.
    // Sanity check, we expect table cache does not contain received table.
    if (cached_tables.contains(index_table_id)) {
      LOG_WITH_PREFIX(DFATAL) << "Table " << index_table_id << " must not be in the cache";
      cached_tables.erase(index_table_id);
    }
    cached_tables.insert({now, index_status});

    // Second step, check table error and handle backfill status in case of success.
    if (index_status.has_error()) {
      LOG(INFO) << "Failed to get index status for table " << index_table_id << ", "
                << "error: [" << StatusFromPB(index_status.error()) << "]. "
                << "It is expected to get some errors from time to time.";
    } else if (IsBackfillDone(index_status)) {
      auto [index_tablets_begin, index_tablets_end] =
          index_tablets_to_sync_.get<IndexTableIdTag>().equal_range(index_table_id);
      for (auto it = index_tablets_begin; it != index_tablets_end; ++it) {
        indexes_for_update[it->index_tablet_id].emplace_back(index_table_id);
      }
    }

    // Third step, clean index tables from future synchronization.
    index_tablets_to_sync_.get<IndexTableIdTag>().erase(index_table_id);
  }

  // The final step, update metadata.
  if (indexes_for_update.empty()) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "No index tablets for metadata update";
  } else {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Updating metadata: " << yb::ToString(indexes_for_update);
    TriggerMetadataUpdate(indexes_for_update);
  }

  return true;
}

std::pair<size_t, size_t> TabletMetadataValidator::Impl::HandleScheduledTablets() {
  // Get all tablets which require validation.
  decltype(scheduled_index_tablets_) scheduled;
  {
    std::lock_guard lock(scheduled_index_tablets_mutex_);
    scheduled.swap(scheduled_index_tablets_);
  }
  VLOG_WITH_PREFIX(3) << "Scheduled: " << yb::ToString(scheduled);

  // Exclude tables from synchronization if their state has already been received and cached.
  TabletTablesMap indexes_from_cache;
  auto& cached_tables = index_tables_cache_.get<TableIdTag>();
  for (std::move_iterator it{scheduled.begin()}, end{scheduled.end()}; it != end; ++it) {
    auto cache_it = cached_tables.find(it->index_table_id);
    if (cache_it == cached_tables.end()) {
      index_tablets_to_sync_.emplace(*it);
      continue;
    }

    // An index table is in the cache, no need to request its status from the master. Let's just
    // check if its peers' metadata should be updated due to the table backfilling is done.
    VLOG_WITH_PREFIX(2) << "Index status for table " << cache_it->table_id()
                        << " was found in the cache: sync with master is not required";
    if (IsBackfillDone(cache_it->index_status)) {
      indexes_from_cache[(*it).index_tablet_id].emplace_back((*it).index_table_id);
    }
  }
  VLOG_WITH_PREFIX(2) << "To sync: " << yb::ToString(index_tablets_to_sync_);

  if (!indexes_from_cache.empty()) {
    VLOG_WITH_PREFIX_AND_FUNC(2)
        << "Updating metadata for cached indexes: " << yb::ToString(indexes_from_cache);
    TriggerMetadataUpdate(indexes_from_cache);
  }

  return { index_tablets_to_sync_.size(), indexes_from_cache.size() };
}

Status TabletMetadataValidator::Impl::Init() {
  const auto validation_period = SanitizeRetainDeleteMarkersValidationPeriod(
      FLAGS_tablet_validator_retain_delete_markers_validation_period_sec);
  validation_task_ = std::make_unique<BackgroundTask>(
      [this]() { Sync(); }, "tablet manager", "bg tablet validator master sync", validation_period);
  return validation_task_->Init();
}

void TabletMetadataValidator::Impl::ScheduleValidation(const tablet::RaftGroupMetadata& metadata) {
  if (!FLAGS_tablet_validator_retain_delete_markers_validation_period_sec) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Retain delete markers validation is turned off";
    return;
  }

  // Used to recover from https://github.com/yugabyte/yugabyte-db/issues/19544.
  if (!ScheduleTabletPropertiesValidation(metadata)) {
    VLOG_WITH_PREFIX(2) << "Tablet " << metadata.raft_group_id() << ": nothing to validate";
  }
}

void TabletMetadataValidator::Impl::RemoveStaleEntriesFromTableCache() {
  if (index_tables_cache_.empty()) {
    return;
  }

  // Clean cache by time.
  const auto now = CoarseMonoClock::Now();
  const auto retention = FLAGS_TEST_tablet_validator_cache_retention_interval_sec * 1s;

  // Sanity check, we expect cache latest timestamp is always less than now.
  auto cache_it = index_tables_cache_.begin();
  if (now < cache_it->timestamp) {
    LOG_WITH_PREFIX(DFATAL) << Format(
        "Now cannot be less than the timestamp of the oldest cache entry, "
        "now: $0, the oldest timestamp: $1", now, cache_it->timestamp);
    return;
  }

  // Remove all old enough entries from the cache.
  while (cache_it != index_tables_cache_.end() && (now - cache_it->timestamp) > retention) {
    VLOG_WITH_PREFIX(2) << "Removing from cache: " << cache_it->ToString();
    cache_it = index_tables_cache_.erase(cache_it);
  }
}

bool TabletMetadataValidator::Impl::ScheduleTabletPropertiesValidation(
    const tablet::RaftGroupMetadata& meta) {
  VLOG_WITH_PREFIX(2) << "Checking tablet " << meta.raft_group_id();

  // We are not interested in syscatalog tables.
  if (meta.table_id() == master::kSysCatalogTableId) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Skipping syscatalog table " << meta.table_id();
    return false;
  }

  // Pick all index tables with retain_delete_markers set to true.
  std::vector<IndexTableInfo> candidates;
  candidates.reserve(meta.GetColocatedTablesCount()); // Let's try to predict the size.
  meta.IterateColocatedTables([this, &meta, &candidates](const yb::tablet::TableInfo& info){
    VLOG_WITH_PREFIX(4) << "Candidate: " << info.ToString();
    if (info.schema().table_properties().retain_delete_markers()) {
      // For colocation tables info.index_info may not be set and thus it's not possible
      // to get indexed_table_id to be able to prepare more optimal request. Let's use
      // index table id instead of indexed table id to reuse the same structures.
      auto group_id = info.index_info ? info.index_info->indexed_table_id() : info.table_id;
      candidates.emplace_back(IndexTableInfo{
          meta.raft_group_id(), info.table_id, std::move(group_id) });
    }
  });
  if (candidates.empty()) {
    return false;
  }

  LOG_WITH_PREFIX(INFO) << "Tablet " << meta.raft_group_id() << ": found index table(s) with "
                        << "retain delete markers set: " << yb::ToString(candidates);
  {
    std::lock_guard lock(scheduled_index_tablets_mutex_);
    scheduled_index_tablets_.insert(
        scheduled_index_tablets_.end(),
        std::move_iterator(candidates.begin()),
        std::move_iterator(candidates.end()));
  }
  return true;
}

void TabletMetadataValidator::Impl::Shutdown() {
  bool expected = false;
  if (!shutting_down_.compare_exchange_strong(expected, true)) {
    return;
  }

  if (validation_task_) {
    validation_task_->Shutdown();
  }
}

void TabletMetadataValidator::Impl::Sync() {
  VLOG_WITH_PREFIX_AND_FUNC(1) << "begin";

  if (shutting_down_) {
    VLOG_WITH_PREFIX_AND_FUNC(1) << "shutting down";
    return;
  }

  const auto validation_period = FLAGS_tablet_validator_retain_delete_markers_validation_period_sec;
  if (validation_period) {
    DoValidate();
  } else {
    // Due to runtime nature of the flag it's required to make sure nothing is kept in
    // the collections to avoid memory leaks.
    index_tablets_to_sync_.clear();
    decltype(scheduled_index_tablets_) to_validate;
    {
      std::lock_guard lock(scheduled_index_tablets_mutex_);
      to_validate.swap(scheduled_index_tablets_);
    }
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Retain delete markers validation is turned off";
  }

  RemoveStaleEntriesFromTableCache();

  // Update validation period in case if the flag has been dynamically changed.
  validation_task_->SetInterval(SanitizeRetainDeleteMarkersValidationPeriod(validation_period));

  VLOG_WITH_PREFIX_AND_FUNC(1) << "end";
}

Result<master::GetBackfillStatusResponsePB> TabletMetadataValidator::Impl::SyncWithMaster() {
  // Extract indexes for synchronization and take into account max batch size.
  const size_t max_batch_size = FLAGS_TEST_tablet_validator_max_tables_number_per_rpc;
  const auto batch_size = max_batch_size == 0 ?
      index_tablets_to_sync_.size() : std::min(index_tablets_to_sync_.size(), max_batch_size);
  std::unordered_set<std::string_view> to_sync;
  for (auto it = index_tablets_to_sync_.begin();
       to_sync.size() < batch_size && it != index_tablets_to_sync_.end(); ++it) {
    to_sync.emplace(it->index_table_id);
  }

  // Only backfilling status is being synced with the master at the moment.
  VLOG_WITH_PREFIX(1) << "Requesting backfill status for " << yb::ToString(to_sync);
  return tablet_manager_.client().GetBackfillStatus({to_sync.begin(), to_sync.end()});
}

void TabletMetadataValidator::Impl::TriggerMetadataUpdate(
    const TabletTablesMap& indexes_for_update) {
  if (indexes_for_update.empty()) {
    VLOG_WITH_PREFIX(2) << "No index tablets to trigger BackfillDone()";
    return;
  }

  for (const auto& [index_tablet_id, index_tables] : indexes_for_update) {
    auto result = tablet_manager_.GetTablet(index_tablet_id);
    if (!result.ok()) {
      // Maybe tablet was gone while we were waiting for the request result.
      VLOG_WITH_PREFIX_AND_FUNC(2) << "Failed to locate tablet " << index_tablet_id << ", "
                                   << "status: " << result.status();
      continue;
    }

    const auto tablet_meta = (*result)->tablet_metadata(); // Want a copy here.
    if (!tablet_meta) {
      VLOG_WITH_PREFIX_AND_FUNC(2) << "Failed to get tablet " << index_tablet_id << " metadata";
      continue;
    }

    // Iterate through all tables of the current tablet and trigger backfill done.
    for (const auto& index_table_id : index_tables) {
      LOG_WITH_PREFIX(INFO) << "Tablet " << index_tablet_id << " index table "
                            << index_table_id << ": resetting backfill as done";
      auto status = tablet_meta->OnBackfillDone(index_table_id);
      // Sanity check. In accordace with the method description, NotFound is the only possible
      // error. We are OK to not handle the error as the peer is not serving the table anymore.
      if (!status.ok() && !status.IsNotFound()) {
        LOG_WITH_PREFIX(DFATAL) << "Tablet " << index_tablet_id << " table " << index_table_id
                                << " backfill done failed, status: " << status;
      }
    }

    // Flush tablet metadata, cannot use tablet::OnlyIfDirty::kTrue as it requires an OpId
    // has been changed from the last flush (some operation has been applied), but this cannot be
    // guaranteed as no raft opeation is used for retain_delete_markers recovery.
    auto status = tablet_meta->Flush();
    LOG_IF_WITH_PREFIX(ERROR, !status.ok())
        << "Tablet " << index_tablet_id << " metadata flush failed: " << status;

    if (status.ok()) {
      VLOG_WITH_PREFIX(2) << "Tablet " << index_tablet_id << " metadata updated and flushed";
    }
  }
}

TabletMetadataValidator::~TabletMetadataValidator() = default;

TabletMetadataValidator::TabletMetadataValidator(
    const std::string& log_prefix, TSTabletManager* tablet_manager)
    : impl_(std::make_unique<Impl>(log_prefix, tablet_manager)) {
}

Status TabletMetadataValidator::Init() {
  return impl_->Init();
}

void TabletMetadataValidator::ScheduleValidation(const tablet::RaftGroupMetadata& metadata) {
  impl_->ScheduleValidation(metadata);
}

void TabletMetadataValidator::Shutdown() {
  impl_->Shutdown();
}

} // namespace yb::tserver
