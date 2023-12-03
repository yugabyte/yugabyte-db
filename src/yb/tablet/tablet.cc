// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/tablet/tablet.h"

#include <utility>

#include <boost/container/static_vector.hpp>

#include "yb/client/auto_flags_manager.h"
#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/meta_data_cache.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/yb_op.h"

#include "yb/qlexpr/index_column.h"
#include "yb/common/pgsql_error.h"
#include "yb/qlexpr/ql_rowblock.h"
#include "yb/common/row_mark.h"
#include "yb/common/schema.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction_error.h"
#include "yb/common/schema_pbutil.h"

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/opid_util.h"

#include "yb/docdb/compaction_file_filter.h"
#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/cql_operation.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_compaction_filter_intents.h"
#include "yb/docdb/docdb_debug.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_statistics.h"
#include "yb/docdb/pgsql_operation.h"
#include "yb/docdb/ql_rocksdb_storage.h"
#include "yb/docdb/redis_operation.h"
#include "yb/docdb/rocksdb_writer.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"

#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/utilities/checkpoint.h"

#include "yb/rocksutil/yb_rocksdb.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/operations/operation.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/operations/split_operation.h"
#include "yb/tablet/operations/truncate_operation.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/tablet/read_result.h"
#include "yb/tablet/snapshot_coordinator.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_retention_policy.h"
#include "yb/tablet/tablet_snapshots.h"
#include "yb/tablet/transaction_coordinator.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/write_query.h"

#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_error.h"

#include "yb/util/debug-util.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/net/net_util.h"
#include "yb/util/pg_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/trace.h"
#include "yb/util/yb_pg_errcodes.h"

#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DEFINE_UNKNOWN_bool(tablet_do_dup_key_checks, true,
            "Whether to check primary keys for duplicate on insertion. "
            "Use at your own risk!");
TAG_FLAG(tablet_do_dup_key_checks, unsafe);

DEFINE_UNKNOWN_bool(tablet_do_compaction_cleanup_for_intents, true,
            "Whether to clean up intents for aborted transactions in compaction.");

DEPRECATE_FLAG(int32, tablet_bloom_block_size, "06_2023");
DEPRECATE_FLAG(double, tablet_bloom_target_fp_rate, "06_2023");

METRIC_DEFINE_entity(table);
METRIC_DEFINE_entity(tablet);

DEPRECATE_FLAG(int32, tablet_rocksdb_ops_quiet_down_timeout_ms, "04_2023");

DEFINE_UNKNOWN_int32(intents_flush_max_delay_ms, 2000,
             "Max time to wait for regular db to flush during flush of intents. "
             "After this time flush of regular db will be forced.");

DEFINE_UNKNOWN_int32(num_raft_ops_to_force_idle_intents_db_to_flush, 1000,
             "When writes to intents RocksDB are stopped and the number of Raft operations after "
             "the last write to the intents RocksDB "
             "is greater than this value, the intents RocksDB would be requested to flush.");

DEFINE_UNKNOWN_bool(delete_intents_sst_files, true,
            "Delete whole intents .SST files when possible.");

DEFINE_RUNTIME_uint64(backfill_index_write_batch_size, 128,
    "The batch size for backfilling the index.");
TAG_FLAG(backfill_index_write_batch_size, advanced);

DEFINE_RUNTIME_int32(backfill_index_rate_rows_per_sec, 0,
    "Rate of at which the indexed table's entries are populated into the index table during index "
    "backfill. This is a per-tablet flag, i.e. a tserver responsible for "
    "multiple tablets could be processing more than this.");
TAG_FLAG(backfill_index_rate_rows_per_sec, advanced);

DEFINE_RUNTIME_uint64(verify_index_read_batch_size, 128, "The batch size for reading the index.");
TAG_FLAG(verify_index_read_batch_size, advanced);

DEFINE_RUNTIME_int32(verify_index_rate_rows_per_sec, 0,
    "Rate of at which the indexed table's entries are read during index consistency checks."
    "This is a per-tablet flag, i.e. a tserver responsible for multiple tablets could be "
    "processing more than this.");
TAG_FLAG(verify_index_rate_rows_per_sec, advanced);

DEFINE_RUNTIME_int32(backfill_index_timeout_grace_margin_ms, -1,
             "The time we give the backfill process to wrap up the current set "
             "of writes and return successfully the RPC with the information about "
             "how far we have processed the rows.");
TAG_FLAG(backfill_index_timeout_grace_margin_ms, advanced);

DEFINE_RUNTIME_bool(yql_allow_compatible_schema_versions, true,
            "Allow YCQL requests to be accepted even if they originate from a client who is ahead "
            "of the server's schema, but is determined to be compatible with the current version.");
TAG_FLAG(yql_allow_compatible_schema_versions, advanced);

DEFINE_RUNTIME_bool(disable_alter_vs_write_mutual_exclusion, false,
    "A safety switch to disable the changes from D8710 which makes a schema "
    "operation take an exclusive lock making all write operations wait for it.");
TAG_FLAG(disable_alter_vs_write_mutual_exclusion, advanced);

DEFINE_RUNTIME_bool(dump_metrics_to_trace, false,
    "Whether to dump changed metrics in tracing.");

DEFINE_RUNTIME_bool(ysql_analyze_dump_metrics, true,
    "Whether to return changed metrics for YSQL queries in RPC response.");

DEFINE_UNKNOWN_bool(cleanup_intents_sst_files, true,
    "Cleanup intents files that are no more relevant to any running transaction.");

DEFINE_UNKNOWN_int32(ysql_transaction_abort_timeout_ms, 15 * 60 * 1000,  // 15 minutes
             "Max amount of time we can wait for active transactions to abort on a tablet "
             "after DDL (eg. DROP TABLE) is executed. This deadline is same as "
             "unresponsive_ts_rpc_timeout_ms");

DEFINE_test_flag(int32, backfill_sabotage_frequency, 0,
    "If set to value greater than 0, every nth row will be corrupted in the backfill process "
    "to create an inconsistency between the index and the indexed tables where n is the "
    "input parameter given.");

DEFINE_test_flag(int32, backfill_drop_frequency, 0,
    "If set to value greater than 0, every nth row will be dropped in the backfill process "
    "to create an inconsistency between the index and the indexed tables where n is the "
    "input parameter given.");

DEFINE_UNKNOWN_bool(tablet_enable_ttl_file_filter, false,
            "Enables compaction to directly delete files that have expired based on TTL, "
            "rather than removing them via the normal compaction process.");

DEFINE_UNKNOWN_bool(enable_schema_packing_gc, true, "Whether schema packing GC is enabled.");

DEFINE_RUNTIME_bool(batch_tablet_metrics_update, true,
                    "Batch update of rocksdb and tablet metrics to once per request rather than "
                    "updating immediately.");

DEFINE_test_flag(int32, slowdown_backfill_by_ms, 0,
                 "If set > 0, slows down the backfill process by this amount.");

DEFINE_test_flag(uint64, backfill_paging_size, 0,
                 "If set > 0, returns early after processing this number of rows.");

DEFINE_test_flag(bool, tablet_verify_flushed_frontier_after_modifying, false,
                 "After modifying the flushed frontier in RocksDB, verify that the restored value "
                 "of it is as expected. Used for testing.");

DEFINE_test_flag(bool, docdb_log_write_batches, false,
                 "Dump write batches being written to RocksDB");

DEFINE_NON_RUNTIME_bool(export_intentdb_metrics, true,
                    "Dump intentsdb statistics to prometheus metrics");

DEFINE_test_flag(bool, pause_before_full_compaction, false,
                 "Pause before triggering full compaction.");

DEFINE_test_flag(bool, disable_adding_user_frontier_to_sst, false,
                 "Prevents adding the UserFrontier to SST file in order to mimic older files.");

DEFINE_test_flag(bool, skip_post_split_compaction, false,
                 "Skip processing post split compaction.");

DEFINE_RUNTIME_uint64(post_split_compaction_input_size_threshold_bytes, 256_MB,
             "Max size of a files to be compacted within one iteration. "
             "Set to 0 to compact all files at once during post split compaction.");

DEFINE_test_flag(bool, pause_before_getting_safe_time, false,
                 "Pause before doing Tablet::DoGetSafeTime");

DEFINE_RUNTIME_bool(tablet_exclusive_post_split_compaction, false,
       "Enables exclusive mode for post-split compaction for a tablet: all scheduled and "
       "unscheduled compactions are run before post-split compaction and no other compaction "
       "will get scheduled during post-split compaction.");

DEFINE_RUNTIME_bool(tablet_exclusive_full_compaction, false,
       "Enables exclusive mode for any non-post-split full compaction for a tablet: all "
       "scheduled and unscheduled compactions are run before the full compaction and no other "
       "compactions will get scheduled during a full compaction.");

// FLAGS_TEST_disable_getting_user_frontier_from_mem_table is used in conjunction with
// FLAGS_TEST_disable_adding_user_frontier_to_sst.  Two flags are needed for the case in which
// we're writing a mixture of SST files with and without UserFrontiers, to ensure that we're
// not attempting to read the UserFrontier from the MemTable in either case.
DEFINE_test_flag(bool, disable_getting_user_frontier_from_mem_table, false,
                 "Prevents checking the MemTable for a UserFrontier for test cases where we are "
                 "generating SST files without UserFrontiers.");

DEFINE_test_flag(bool, disable_adding_last_compaction_to_tablet_metadata, false,
                 "Prevents adding the last full compaction time to tablet metadata upon "
                 "full compaction completion.");

DECLARE_int32(client_read_write_timeout_ms);
DECLARE_bool(consistent_restore);
DECLARE_int64(db_block_size_bytes);
DECLARE_int32(rocksdb_level0_slowdown_writes_trigger);
DECLARE_int32(rocksdb_level0_stop_writes_trigger);
DECLARE_uint64(rocksdb_max_file_size_for_compaction);
DECLARE_int64(apply_intents_task_injected_delay_ms);
DECLARE_string(regular_tablets_data_block_key_value_encoding);
DECLARE_int64(cdc_intent_retention_ms);

DEFINE_test_flag(uint64, inject_sleep_before_applying_write_batch_ms, 0,
                 "Sleep before applying write batches");
DEFINE_test_flag(uint64, inject_sleep_before_applying_intents_ms, 0,
                 "Sleep before applying intents to docdb after transaction commit");

DECLARE_bool(TEST_invalidate_last_change_metadata_op);

using namespace std::placeholders;

using std::shared_ptr;
using std::string;
using std::unordered_set;
using std::vector;
using std::unique_ptr;
using namespace std::literals;  // NOLINT

using rocksdb::SequenceNumber;

namespace yb {
namespace tablet {

using strings::Substitute;

using client::YBSession;
using client::YBTablePtr;

using dockv::DocKey;
using docdb::DocRowwiseIterator;
using docdb::TableInfoProvider;
using dockv::SubDocKey;
using docdb::StorageDbType;
using qlexpr::IndexInfo;
using qlexpr::QLTableRow;

const std::hash<std::string> hash_for_data_root_dir;

// Returns true if the given transaction ids are sorted when compared in encoded string notation.
bool IsSortedAscendingEncoded(
    const std::pair<TransactionId, SubtxnSet>& id1,
    const std::pair<TransactionId, SubtxnSet>& id2) {
  return id1.first.ToString() <= id2.first.ToString();
}

////////////////////////////////////////////////////////////
// Tablet
////////////////////////////////////////////////////////////

namespace {

thread_local docdb::DocDBStatistics scoped_docdb_statistics;
thread_local ScopedTabletMetrics scoped_tablet_metrics;

void TraceScopedMetrics() {
  std::stringstream ss;
  ss << "Metric changes:\n";
  size_t changes = scoped_docdb_statistics.Dump(&ss);
  changes += scoped_tablet_metrics.Dump(&ss);
  if (changes > 0) {
    TRACE(ss.str());
  }
}

std::string MakeTabletLogPrefix(
    const TabletId& tablet_id, const std::string& log_prefix_suffix) {
  return Format("T $0$1: ", tablet_id, log_prefix_suffix);
}

// When write is caused by transaction apply, we have 2 hybrid times.
// log_ht - apply raft operation hybrid time.
// commit_ht - transaction commit hybrid time.
// So frontiers should cover range of those times.
docdb::ConsensusFrontiers* InitFrontiers(
    const OpId op_id,
    const HybridTime log_ht,
    const HybridTime commit_ht,
    docdb::ConsensusFrontiers* frontiers) {
  if (FLAGS_TEST_disable_adding_user_frontier_to_sst) {
    return nullptr;
  }
  set_op_id(op_id, frontiers);
  HybridTime min_ht = log_ht;
  HybridTime max_ht = log_ht;
  if (commit_ht) {
    min_ht = std::min(min_ht, commit_ht);
    max_ht = std::max(max_ht, commit_ht);
  }
  frontiers->Smallest().set_hybrid_time(min_ht);
  frontiers->Largest().set_hybrid_time(max_ht);
  return frontiers;
}

docdb::ConsensusFrontiers* InitFrontiers(
    const TransactionApplyData& data, docdb::ConsensusFrontiers* frontiers) {
  return InitFrontiers(data.op_id, data.log_ht, data.commit_ht, frontiers);
}

docdb::ConsensusFrontiers* InitFrontiers(
    const RemoveIntentsData& data, docdb::ConsensusFrontiers* frontiers) {
  return InitFrontiers(data.op_id, data.log_ht, HybridTime::kInvalid, frontiers);
}

rocksdb::UserFrontierPtr GetMutableMemTableFrontierFromDb(
    rocksdb::DB* db,
    rocksdb::UpdateUserValueType type) {
  if (FLAGS_TEST_disable_getting_user_frontier_from_mem_table) {
    return nullptr;
  }
  return db->GetMutableMemTableFrontier(type);
}


Result<HybridTime> CheckSafeTime(HybridTime time, HybridTime min_allowed) {
  if (time) {
    return time;
  }
  return STATUS_FORMAT(TimedOut, "Timed out waiting for safe time $0", min_allowed);
}

} // namespace

class Tablet::RegularRocksDbListener : public rocksdb::EventListener {
 public:
  RegularRocksDbListener(Tablet* tablet, const std::string& log_prefix)
      : tablet_(*CHECK_NOTNULL(tablet)),
        log_prefix_(log_prefix) {}

  void OnCompactionCompleted(rocksdb::DB* db, const rocksdb::CompactionJobInfo& ci) override {
    auto& metadata = *CHECK_NOTNULL(tablet_.metadata());
    if (ci.is_full_compaction) {
      if (PREDICT_TRUE(!FLAGS_TEST_disable_adding_last_compaction_to_tablet_metadata)) {
        metadata.set_last_full_compaction_time(tablet_.clock()->Now().ToUint64());
      }
      metadata.OnPostSplitCompactionDone();
      ERROR_NOT_OK(metadata.Flush(), log_prefix_);
    } else if (ci.is_no_op_compaction &&
               ci.compaction_reason == rocksdb::CompactionReason::kPostSplitCompaction) {
      // Trivial post split compaction completion generally means that there are no more files
      // inherited from a parent tablet available for compaction (literally all the files have
      // been already compacted due to some reason), or there are no files at all (not expected).
      if (metadata.OnPostSplitCompactionDone()) {
        ERROR_NOT_OK(metadata.Flush(), log_prefix_);
      }
    }

    if (FLAGS_enable_schema_packing_gc) {
      OldSchemaGC();
    }
  }

 private:
  using MinSchemaVersionMap = std::unordered_map<Uuid, SchemaVersion, UuidHash>;

  void OldSchemaGC() {
    MinSchemaVersionMap table_id_to_min_schema_version;
    {
      auto scoped_read_operation = tablet_.CreateScopedRWOperationNotBlockingRocksDbShutdownStart();
      if (!scoped_read_operation.ok()) {
        VLOG_WITH_FUNC(4) << "Skip";
        return;
      }

      // Collect min schema version from all DB entries. I.e. stored in memory and flushed to disk.
      FillMinSchemaVersion(tablet_.regular_db_.get(), &table_id_to_min_schema_version);
      FillMinSchemaVersion(tablet_.intents_db_.get(), &table_id_to_min_schema_version);
    }

    FillMinXClusterSchemaVersion(&table_id_to_min_schema_version);
    ERROR_NOT_OK(tablet_.metadata()->OldSchemaGC(table_id_to_min_schema_version), log_prefix_);
  }

  // Checks XCluster config to determine the min schema version at which to expect rows.
  void FillMinXClusterSchemaVersion(MinSchemaVersionMap* table_id_to_min_schema_version) {
    if (nullptr == tablet_.get_min_xcluster_schema_version_) {
      return;
    }

    auto& min_schema_versions = *table_id_to_min_schema_version;
    auto primary_table_id = tablet_.metadata()->table_id();
    if(!tablet_.metadata()->colocated()) {
      auto schema_version = tablet_.get_min_xcluster_schema_version_(primary_table_id,
          kColocationIdNotSet);
      VLOG_WITH_FUNC(4) <<
          Format("MinNonXClusterSchemaVersion, MinXClusterSchemaVersion for $0:$1,$2",
              primary_table_id, min_schema_versions[Uuid::Nil()], schema_version);
      if (schema_version < min_schema_versions[Uuid::Nil()]) {
        min_schema_versions[Uuid::Nil()] = schema_version;
      }
      return;
    }

    auto colocated_tables = tablet_.metadata()->GetAllColocatedTablesWithColocationId();
    for(auto& [table_id, schema_version] : min_schema_versions) {
      ColocationId colocation_id = colocated_tables[table_id.ToHexString()];
      auto xcluster_min_schema_version = tablet_.get_min_xcluster_schema_version_(primary_table_id,
          colocation_id);
      VLOG_WITH_FUNC(4) <<
          Format("MinNonXClusterSchemaVersion, MinXClusterSchemaVersion for $0,$1:$2,$3",
              primary_table_id, colocation_id, min_schema_versions[table_id],
              xcluster_min_schema_version);
      if (xcluster_min_schema_version < min_schema_versions[table_id]) {
        min_schema_versions[table_id] = xcluster_min_schema_version;
      }
    }
  }

  void FillMinSchemaVersion(rocksdb::DB* db, MinSchemaVersionMap* table_id_to_min_schema_version) {
    if (!db) {
      return;
    }
    {
      auto smallest = db->CalcMemTableFrontier(rocksdb::UpdateUserValueType::kSmallest);
      if (smallest) {
        down_cast<docdb::ConsensusFrontier&>(*smallest).MakeExternalSchemaVersionsAtMost(
            table_id_to_min_schema_version);
      }
    }
    for (const auto& file : db->GetLiveFilesMetaData()) {
      if (!file.smallest.user_frontier) {
        continue;
      }
      auto& smallest = down_cast<docdb::ConsensusFrontier&>(*file.smallest.user_frontier);
      smallest.MakeExternalSchemaVersionsAtMost(table_id_to_min_schema_version);
    }
  }

  Tablet& tablet_;
  const std::string log_prefix_;
};

Tablet::Tablet(const TabletInitData& data)
    : key_schema_(std::make_unique<Schema>(data.metadata->schema()->CreateKeyProjection())),
      metadata_(data.metadata),
      table_type_(data.metadata->table_type()),
      log_anchor_registry_(data.log_anchor_registry),
      mem_tracker_(MemTracker::FindOrCreateTracker(
          Format("tablet-$0", tablet_id()), /* metric_name */ "PerTablet", data.parent_mem_tracker,
              AddToParent::kTrue, CreateMetrics::kFalse)),
      block_based_table_mem_tracker_(data.block_based_table_mem_tracker),
      clock_(data.clock),
      mvcc_(
          MakeTabletLogPrefix(data.metadata->raft_group_id(), data.log_prefix_suffix), data.clock),
      tablet_options_(data.tablet_options),
      pending_op_counter_blocking_rocksdb_shutdown_start_(Format(
          "T $0 Read/write operations blocking start of RocksDB shutdown",
          metadata_->raft_group_id())),
      pending_op_counter_not_blocking_rocksdb_shutdown_start_(Format(
          "T $0 Read/write operations not blocking start of RocksDB shutdown",
          metadata_->raft_group_id())),
      write_ops_being_submitted_counter_("Tablet schema"),
      client_future_(data.client_future),
      transaction_manager_provider_(data.transaction_manager_provider),
      metadata_cache_(data.metadata_cache),
      local_tablet_filter_(data.local_tablet_filter),
      log_prefix_suffix_(data.log_prefix_suffix),
      is_sys_catalog_(data.is_sys_catalog),
      txns_enabled_(data.txns_enabled),
      retention_policy_(std::make_shared<TabletRetentionPolicy>(
          clock_, data.allowed_history_cutoff_provider, metadata_.get())),
      full_compaction_pool_(data.full_compaction_pool),
      admin_triggered_compaction_pool_(data.admin_triggered_compaction_pool),
      ts_post_split_compaction_added_(std::move(data.post_split_compaction_added)),
      get_min_xcluster_schema_version_(std::move(data.get_min_xcluster_schema_version)) {
  CHECK(schema()->has_column_ids());
  LOG_WITH_PREFIX(INFO) << "Schema version for " << metadata_->table_name() << " is "
                        << metadata_->schema_version();

  if (data.metric_registry) {
    MetricEntity::AttributeMap attrs;
    // TODO(KUDU-745): table_id is apparently not set in the metadata.
    attrs["table_id"] = metadata_->table_id();
    attrs["table_name"] = metadata_->table_name();
    attrs["table_type"] = TableType_Name(metadata_->table_type());
    attrs["namespace_name"] = metadata_->namespace_name();
    metric_mem_tracker_ = MemTracker::CreateTracker(
        "Metrics", mem_tracker_, AddToParent::kTrue, CreateMetrics::kFalse);
    table_metrics_entity_ =
        METRIC_ENTITY_table.Instantiate(data.metric_registry, metadata_->table_id(), attrs);
    tablet_metrics_entity_ = METRIC_ENTITY_tablet.Instantiate(
            data.metric_registry, tablet_id(), attrs, metric_mem_tracker_);
    // If we are creating a KV table create the metrics callback.
    regulardb_statistics_ =
        rocksdb::CreateDBStatistics(table_metrics_entity_, tablet_metrics_entity_);
    intentsdb_statistics_ =
        (GetAtomicFlag(&FLAGS_export_intentdb_metrics)
             ? rocksdb::CreateDBStatistics(table_metrics_entity_, tablet_metrics_entity_, true)
             : rocksdb::CreateDBStatistics(nullptr, nullptr, true));

    metrics_.reset(CreateTabletMetrics(table_metrics_entity_, tablet_metrics_entity_).release());

    mem_tracker_->SetMetricEntity(tablet_metrics_entity_);
  }

  auto table_info = metadata_->primary_table_info();
  bool has_index = !table_info->index_map->empty();
  bool transactional = data.metadata->schema()->table_properties().is_transactional();
  if (transactional) {
    server::HybridClock::EnableClockSkewControl();
  }
  if (txns_enabled_ &&
      data.transaction_participant_context &&
      (is_sys_catalog_ || transactional)) {
    transaction_participant_ = std::make_unique<TransactionParticipant>(
        data.transaction_participant_context, this, DCHECK_NOTNULL(tablet_metrics_entity_),
        data.parent_mem_tracker);
    if (data.waiting_txn_registry) {
      transaction_participant_->SetWaitQueue(std::make_unique<docdb::WaitQueue>(
        transaction_participant_.get(), metadata_->fs_manager()->uuid(), data.waiting_txn_registry,
        client_future_, clock(), DCHECK_NOTNULL(tablet_metrics_entity_),
        DCHECK_NOTNULL(data.wait_queue_pool)->NewToken(ThreadPool::ExecutionMode::SERIAL)));
    }
  }

  LOG_IF(FATAL, has_index && !metadata_cache_)
      << "Metadata cache is missing when indexes are present";

  if (data.transaction_coordinator_context &&
      table_info->table_type == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    transaction_coordinator_ = std::make_unique<TransactionCoordinator>(
        metadata_->fs_manager()->uuid(),
        data.transaction_coordinator_context,
        metrics_.get(),
        DCHECK_NOTNULL(tablet_metrics_entity_));
  }

  snapshots_ = std::make_unique<TabletSnapshots>(this);

  snapshot_coordinator_ = data.snapshot_coordinator;

  if (metadata_->tablet_data_state() == TabletDataState::TABLET_DATA_SPLIT_COMPLETED) {
    SplitDone();
  }
  auto restoration_hybrid_time = metadata_->restoration_hybrid_time();
  if (restoration_hybrid_time && transaction_participant_ && FLAGS_consistent_restore) {
    transaction_participant_->IgnoreAllTransactionsStartedBefore(restoration_hybrid_time);
  }
  SyncRestoringOperationFilter(ResetSplit::kFalse);

  if (is_sys_catalog_) {
    auto_flags_manager_ = data.auto_flags_manager;
  }
}

Tablet::~Tablet() {
  if (StartShutdown()) {
    CompleteShutdown(DisableFlushOnShutdown::kFalse, AbortOps::kFalse);
  } else {
    auto state = state_;
    if (state != kShutdown) {
      LOG_WITH_PREFIX(DFATAL) << "Destroying Tablet that did not complete shutdown: " << state;
      // Still try to complete shutdown in release builds, but disable flush in this state to
      // minimize risk of flushing potentially corrupted data.
      CompleteShutdown(DisableFlushOnShutdown::kTrue, AbortOps::kFalse);
    }
  }
  if (regulardb_mem_tracker_) {
    regulardb_mem_tracker_->UnregisterFromParent();
  }
  if (intentdb_mem_tracker_) {
    intentdb_mem_tracker_->UnregisterFromParent();
  }
  if (metric_mem_tracker_) {
    metric_mem_tracker_->UnregisterFromParent();
  }
  mem_tracker_->UnregisterFromParent();
}

Status Tablet::Open() {
  TRACE_EVENT0("tablet", "Tablet::Open");
  std::lock_guard lock(component_lock_);
  CHECK_EQ(state_, kInitialized) << "already open";
  CHECK(schema()->has_column_ids());

  switch (table_type_) {
    case TableType::PGSQL_TABLE_TYPE: FALLTHROUGH_INTENDED;
    case TableType::YQL_TABLE_TYPE: FALLTHROUGH_INTENDED;
    case TableType::REDIS_TABLE_TYPE:
      RETURN_NOT_OK(OpenKeyValueTablet());
      state_ = kBootstrapping;
      return Status::OK();
    case TableType::TRANSACTION_STATUS_TABLE_TYPE:
      state_ = kBootstrapping;
      return Status::OK();
  }
  FATAL_INVALID_ENUM_VALUE(TableType, table_type_);

  return Status::OK();
}

Status Tablet::CreateTabletDirectories(const string& db_dir, FsManager* fs) {
  LOG_WITH_PREFIX(INFO) << "Creating RocksDB database in dir " << db_dir;

  // Create the directory table-uuid first.
  RETURN_NOT_OK_PREPEND(fs->CreateDirIfMissingAndSync(DirName(db_dir)),
                        Format("Failed to create RocksDB table directory $0", DirName(db_dir)));

  RETURN_NOT_OK_PREPEND(fs->CreateDirIfMissingAndSync(db_dir),
                        Format("Failed to create RocksDB tablet directory $0", db_dir));

  RETURN_NOT_OK_PREPEND(fs->CreateDirIfMissingAndSync(db_dir + kIntentsDBSuffix),
                        Format("Failed to create RocksDB tablet intents directory $0", db_dir));

  RETURN_NOT_OK(snapshots_->CreateDirectories(db_dir, fs));

  return Status::OK();
}

client::YBMetaDataCache* Tablet::YBMetaDataCache() {
  return metadata_cache_;
}

void Tablet::ResetYBMetaDataCache() {
  if (metadata_cache_) {
    metadata_cache_->Reset();
  }
}

template <class F>
auto MakeMemTableFlushFilterFactory(const F& f) {
  // Trick to get type of mem_table_flush_filter_factory field.
  typedef typename decltype(
      static_cast<rocksdb::Options*>(nullptr)->mem_table_flush_filter_factory)::element_type
      MemTableFlushFilterFactoryType;
  return std::make_shared<MemTableFlushFilterFactoryType>(f);
}

template <class F>
auto MakeMaxFileSizeWithTableTTLFunction(const F& f) {
  // Trick to get type of max_file_size_for_compaction field.
  typedef typename decltype(
      static_cast<rocksdb::Options*>(nullptr)->max_file_size_for_compaction)::element_type
      MaxFileSizeWithTableTTLFunction;
  return std::make_shared<MaxFileSizeWithTableTTLFunction>(f);
}

Result<bool> Tablet::IntentsDbFlushFilter(const rocksdb::MemTable& memtable, bool write_blocked) {
  VLOG_WITH_PREFIX(4) << __func__;

  auto frontiers = memtable.Frontiers();
  if (frontiers) {
    const auto& intents_largest =
        down_cast<const docdb::ConsensusFrontier&>(frontiers->Largest());

    // We allow to flush intents DB only after regular DB.
    // Otherwise we could lose applied intents when corresponding regular records were not
    // flushed.
    auto regular_flushed_frontier = regular_db_->GetFlushedFrontier();
    if (regular_flushed_frontier) {
      const auto& regular_flushed_largest =
          static_cast<const docdb::ConsensusFrontier&>(*regular_flushed_frontier);
      if (regular_flushed_largest.op_id().index >= intents_largest.op_id().index) {
        VLOG_WITH_PREFIX(4) << __func__ << ", regular already flushed";
        return true;
      }
    }
  } else {
    VLOG_WITH_PREFIX(4) << __func__ << ", no frontiers";
  }

  // If regular db does not have anything to flush, it means that we have just added intents,
  // without apply, so it is OK to flush the intents RocksDB.
  auto flush_intention = regular_db_->GetFlushAbility();
  if (flush_intention == rocksdb::FlushAbility::kNoNewData) {
    VLOG_WITH_PREFIX(4) << __func__ << ", no new data";
    return true;
  }

  // Force flush of regular DB if we were not able to flush for too long.
  auto timeout = std::chrono::milliseconds(FLAGS_intents_flush_max_delay_ms);
  if (flush_intention != rocksdb::FlushAbility::kAlreadyFlushing &&
      (shutdown_requested_.load(std::memory_order_acquire) || write_blocked ||
       std::chrono::steady_clock::now() > memtable.FlushStartTime() + timeout)) {
    VLOG_WITH_PREFIX(2) << __func__ << ", force flush";

    rocksdb::FlushOptions options;
    options.wait = false;
    RETURN_NOT_OK(regular_db_->Flush(options));
  }

  return false;
}

std::string Tablet::LogPrefix() const {
  return MakeTabletLogPrefix(tablet_id(), log_prefix_suffix_);
}

namespace {

std::string LogDbTypePrefix(docdb::StorageDbType db_type) {
  switch (db_type) {
    case docdb::StorageDbType::kRegular:
      return "R";
    case docdb::StorageDbType::kIntents:
      return "I";
  }
  FATAL_INVALID_ENUM_VALUE(docdb::StorageDbType, db_type);
}

std::string MakeTabletLogPrefix(
    const TabletId& tablet_id, const std::string& log_prefix_suffix, docdb::StorageDbType db_type) {
  return MakeTabletLogPrefix(
      tablet_id, Format("$0 [$1]", log_prefix_suffix, LogDbTypePrefix(db_type)));
}

} // namespace

std::string Tablet::LogPrefix(docdb::StorageDbType db_type) const {
  return MakeTabletLogPrefix(tablet_id(), log_prefix_suffix_, db_type);
}

Status Tablet::OpenKeyValueTablet() {
  static const std::string kRegularDB = "RegularDB"s;
  static const std::string kIntentsDB = "IntentsDB"s;

  rocksdb::BlockBasedTableOptions table_options;
  if (!metadata()->primary_table_info()->index_info || metadata()->colocated()) {
    // This tablet is not dedicated to the index table, so it should be effective to use
    // advanced key-value encoding algorithm optimized for docdb keys structure.
    table_options.use_delta_encoding = true;
    table_options.data_block_key_value_encoding_format =
        VERIFY_RESULT(docdb::GetConfiguredKeyValueEncodingFormat(
            FLAGS_regular_tablets_data_block_key_value_encoding));
  }
  rocksdb::Options rocksdb_options;
  InitRocksDBOptions(
      &rocksdb_options, LogPrefix(docdb::StorageDbType::kRegular), std::move(table_options));
  rocksdb_options.mem_tracker = MemTracker::FindOrCreateTracker(kRegularDB, mem_tracker_);
  regulardb_mem_tracker_ = MemTracker::FindOrCreateTracker(
      Format("$0-$1", kRegularDB, tablet_id()), /* metric_name */ kRegularDB,
          block_based_table_mem_tracker_, AddToParent::kTrue, CreateMetrics::kFalse);
  rocksdb_options.block_based_table_mem_tracker = regulardb_mem_tracker_;

  // We may not have a metrics_entity_ instantiated in tests.
  if (tablet_metrics_entity_) {
    rocksdb_options.block_based_table_mem_tracker->SetMetricEntity(tablet_metrics_entity_);
  }

  key_bounds_ = docdb::KeyBounds(metadata()->lower_bound_key(), metadata()->upper_bound_key());

  // Install the history cleanup handler. Note that TabletRetentionPolicy is going to hold a raw ptr
  // to this tablet. So, we ensure that rocksdb_ is reset before this tablet gets destroyed.
  rocksdb_options.compaction_context_factory = docdb::CreateCompactionContextFactory(
      retention_policy_, &key_bounds_,
      std::bind(&Tablet::DeleteMarkerRetentionTime, this, _1),
      metadata_.get());

  rocksdb_options.mem_table_flush_filter_factory = MakeMemTableFlushFilterFactory([this] {
    {
      std::lock_guard lock(flush_filter_mutex_);
      if (mem_table_flush_filter_factory_) {
        return mem_table_flush_filter_factory_();
      }
    }
    return rocksdb::MemTableFilter();
  });
  if (FLAGS_tablet_enable_ttl_file_filter) {
    rocksdb_options.compaction_file_filter_factory =
        std::make_shared<docdb::DocDBCompactionFileFilterFactory>(retention_policy_, clock());
  }

  // Use a function that checks the table TTL before returning a value for max file size
  // for compactions.
  rocksdb_options.max_file_size_for_compaction = MakeMaxFileSizeWithTableTTLFunction([this] {
    if (HasActiveTTLFileExpiration()) {
      return FLAGS_rocksdb_max_file_size_for_compaction;
    }
    return std::numeric_limits<uint64_t>::max();
  });

  rocksdb_options.disable_auto_compactions = true;
  rocksdb_options.level0_slowdown_writes_trigger = std::numeric_limits<int>::max();
  rocksdb_options.level0_stop_writes_trigger = std::numeric_limits<int>::max();

  rocksdb::Options regular_rocksdb_options(rocksdb_options);
  regular_rocksdb_options.listeners.push_back(
      std::make_shared<RegularRocksDbListener>(this, regular_rocksdb_options.log_prefix));

  const string db_dir = metadata()->rocksdb_dir();
  RETURN_NOT_OK(CreateTabletDirectories(db_dir, metadata()->fs_manager()));

  LOG(INFO) << "Opening RocksDB at: " << db_dir;
  rocksdb::DB* db = nullptr;
  rocksdb::Status rocksdb_open_status = rocksdb::DB::Open(regular_rocksdb_options, db_dir, &db);
  if (!rocksdb_open_status.ok()) {
    LOG_WITH_PREFIX(ERROR) << "Failed to open a RocksDB database in directory " << db_dir << ": "
                           << rocksdb_open_status;
    if (db != nullptr) {
      delete db;
    }
    return STATUS(IllegalState, rocksdb_open_status.ToString());
  }
  regular_db_.reset(db);
  regular_db_->ListenFilesChanged(std::bind(&Tablet::RegularDbFilesChanged, this));

  if (transaction_participant_) {
    LOG_WITH_PREFIX(INFO) << "Opening intents DB at: " << db_dir + kIntentsDBSuffix;
    rocksdb::Options intents_rocksdb_options(rocksdb_options);
    intents_rocksdb_options.compaction_context_factory = {};
    docdb::SetLogPrefix(&intents_rocksdb_options, LogPrefix(docdb::StorageDbType::kIntents));

    intents_rocksdb_options.mem_table_flush_filter_factory = MakeMemTableFlushFilterFactory([this] {
      return std::bind(&Tablet::IntentsDbFlushFilter, this, _1, _2);
    });

    intents_rocksdb_options.compaction_filter_factory =
        FLAGS_tablet_do_compaction_cleanup_for_intents ?
        std::make_shared<docdb::DocDBIntentsCompactionFilterFactory>(this, &key_bounds_) : nullptr;

    intents_rocksdb_options.mem_tracker = MemTracker::FindOrCreateTracker(kIntentsDB, mem_tracker_);
    intentdb_mem_tracker_ = MemTracker::FindOrCreateTracker(
        Format("$0-$1", kIntentsDB, tablet_id()), /* metric_name */ kIntentsDB,
            block_based_table_mem_tracker_, AddToParent::kTrue, CreateMetrics::kFalse);
    intents_rocksdb_options.block_based_table_mem_tracker = intentdb_mem_tracker_;
    // We may not have a metrics_entity_ instantiated in tests.
    if (tablet_metrics_entity_) {
      intents_rocksdb_options.block_based_table_mem_tracker->SetMetricEntity(
          tablet_metrics_entity_);
    }
    intents_rocksdb_options.statistics = intentsdb_statistics_;

    rocksdb::DB* intents_db = nullptr;
    RETURN_NOT_OK(
        rocksdb::DB::Open(intents_rocksdb_options, db_dir + kIntentsDBSuffix, &intents_db));
    intents_db_.reset(intents_db);
    intents_db_->ListenFilesChanged(std::bind(&Tablet::CleanupIntentFiles, this));
  }

  ql_storage_.reset(new docdb::QLRocksDBStorage(doc_db()));
  if (transaction_participant_) {
    // We need to set the "cdc_sdk_min_checkpoint_op_id" so that intents don't get
    // garbage collected after transactions are loaded.
    transaction_participant_->SetIntentRetainOpIdAndTime(
        metadata_->cdc_sdk_min_checkpoint_op_id(),
        MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_cdc_intent_retention_ms)));
    RETURN_NOT_OK(transaction_participant_->SetDB(
        doc_db(), &key_bounds_, &pending_op_counter_blocking_rocksdb_shutdown_start_));
  }

  // Don't allow reads at timestamps lower than the highest history cutoff of a past compaction.
  auto regular_flushed_frontier = regular_db_->GetFlushedFrontier();
  if (regular_flushed_frontier) {
    retention_policy_->UpdateCommittedHistoryCutoff(
        static_cast<const docdb::ConsensusFrontier&>(*regular_flushed_frontier).
            history_cutoff());
  }

  LOG_WITH_PREFIX(INFO) << "Successfully opened a RocksDB database at " << db_dir
                        << ", obj: " << db;

  return Status::OK();
}

void Tablet::RegularDbFilesChanged() {
  std::lock_guard lock(num_sst_files_changed_listener_mutex_);
  if (num_sst_files_changed_listener_) {
    num_sst_files_changed_listener_();
  }
}

void Tablet::SetCleanupPool(ThreadPool* thread_pool) {
  if (!transaction_participant_) {
    return;
  }

  cleanup_intent_files_token_ = thread_pool->NewToken(ThreadPool::ExecutionMode::SERIAL);

  CleanupIntentFiles();
}

void Tablet::CleanupIntentFiles() {
  auto scoped_read_operation = CreateScopedRWOperationNotBlockingRocksDbShutdownStart();
  if (!scoped_read_operation.ok() || state_ != State::kOpen || !FLAGS_delete_intents_sst_files ||
      !cleanup_intent_files_token_) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Skip";
    return;
  }

  WARN_NOT_OK(
      cleanup_intent_files_token_->SubmitFunc(std::bind(&Tablet::DoCleanupIntentFiles, this)),
      "Submit cleanup intent files failed");
}

void Tablet::DoCleanupIntentFiles() {
  if (metadata_->IsUnderXClusterReplication()) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Exit because of xCluster replication";
    return;
  }
  HybridTime best_file_max_ht = HybridTime::kMax;
  std::vector<rocksdb::LiveFileMetaData> files;
  // Stops when there are no more files to delete.
  uint64_t previous_name_id = std::numeric_limits<uint64_t>::max();
  // If intents SST file deletion was blocked by running transactions we want to wait for running
  // transactions to have time larger than best_file_max_ht by calling
  // transaction_participant_->WaitMinRunningHybridTime outside of ScopedReadOperation.
  bool has_deletions_blocked_by_running_transations = false;
  while (GetAtomicFlag(&FLAGS_cleanup_intents_sst_files)) {
    auto scoped_read_operation = CreateScopedRWOperationNotBlockingRocksDbShutdownStart();
    if (!scoped_read_operation.ok()) {
      VLOG_WITH_PREFIX_AND_FUNC(4) << "Failed to acquire scoped read operation";
      break;
    }

    best_file_max_ht = HybridTime::kMax;
    const rocksdb::LiveFileMetaData* best_file = nullptr;
    files.clear();
    intents_db_->GetLiveFilesMetaData(&files);
    auto min_largest_seq_no = std::numeric_limits<rocksdb::SequenceNumber>::max();

    VLOG_WITH_PREFIX_AND_FUNC(5) << "Files: " << AsString(files);

    for (const auto& file : files) {
      if (file.largest.seqno < min_largest_seq_no) {
        min_largest_seq_no = file.largest.seqno;
        if (file.largest.user_frontier) {
          auto& frontier = down_cast<docdb::ConsensusFrontier&>(*file.largest.user_frontier);
          best_file_max_ht = frontier.hybrid_time();
        } else {
          best_file_max_ht = HybridTime::kMax;
        }
        best_file = &file;
      }
    }

    auto min_running_start_ht = transaction_participant_->MinRunningHybridTime();
    if (!min_running_start_ht.is_valid() || min_running_start_ht <= best_file_max_ht) {
      has_deletions_blocked_by_running_transations = true;
      VLOG_WITH_PREFIX_AND_FUNC(4)
          << "Cannot delete because of running transactions: " << min_running_start_ht
          << ", best file max ht: " << best_file_max_ht;
      break;
    }
    if (best_file->name_id == previous_name_id) {
      LOG_WITH_PREFIX_AND_FUNC(INFO)
          << "Attempt to delete same file: " << previous_name_id << ", stopping cleanup";
      break;
    }
    previous_name_id = best_file->name_id;

    LOG_WITH_PREFIX_AND_FUNC(INFO)
        << "Intents SST file will be deleted: " << best_file->ToString()
        << ", max ht: " << best_file_max_ht << ", min running transaction start ht: "
        << min_running_start_ht;
    auto flush_status = regular_db_->Flush(rocksdb::FlushOptions());
    if (!flush_status.ok()) {
      LOG_WITH_PREFIX_AND_FUNC(WARNING) << "Failed to flush regular db: " << flush_status;
      break;
    }
    auto delete_status = intents_db_->DeleteFile(best_file->Name());
    if (!delete_status.ok()) {
      LOG_WITH_PREFIX_AND_FUNC(WARNING)
          << "Failed to delete " << best_file->ToString() << ", all files " << AsString(files)
          << ": " << delete_status;
      break;
    }
  }

  if (best_file_max_ht != HybridTime::kMax && has_deletions_blocked_by_running_transations) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Wait min running hybrid time: " << best_file_max_ht;
    transaction_participant_->WaitMinRunningHybridTime(best_file_max_ht);
  }
}

Status Tablet::EnableCompactions(
    ScopedRWOperationPause* blocking_rocksdb_shutdown_start_ops_pause) {
  if (state_ != kOpen) {
    LOG_WITH_PREFIX(INFO) << Format(
        "Cannot enable compaction for the tablet in state other than kOpen, current state is $0",
        state_);
    return Status::OK();
  }
  if (!blocking_rocksdb_shutdown_start_ops_pause) {
    auto operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
    RETURN_NOT_OK(operation);
    return DoEnableCompactions();
  }

  return DoEnableCompactions();
}

Status Tablet::DoEnableCompactions() {
  Status regular_db_status;
  std::unordered_map<std::string, std::string> new_options = {
      { "level0_slowdown_writes_trigger"s,
        std::to_string(max_if_negative(FLAGS_rocksdb_level0_slowdown_writes_trigger))},
      { "level0_stop_writes_trigger"s,
        std::to_string(max_if_negative(FLAGS_rocksdb_level0_stop_writes_trigger))},
  };
  if (regular_db_) {
    WARN_WITH_PREFIX_NOT_OK(
        regular_db_->SetOptions(new_options, /* dump_options= */ false),
        "Failed to set options on regular DB");
    regular_db_status =
        regular_db_->EnableAutoCompaction({regular_db_->DefaultColumnFamily()});
    if (!regular_db_status.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Failed to enable compactions on regular DB: "
                               << regular_db_status;
    }
  }
  if (intents_db_) {
    WARN_WITH_PREFIX_NOT_OK(
        intents_db_->SetOptions(new_options, /* dump_options= */ false),
        "Failed to set options on provisional records DB");
    Status intents_db_status =
        intents_db_->EnableAutoCompaction({intents_db_->DefaultColumnFamily()});
    if (!intents_db_status.ok()) {
      LOG_WITH_PREFIX(WARNING)
          << "Failed to enable compactions on provisional records DB: " << intents_db_status;
      return intents_db_status;
    }
  }
  return regular_db_status;
}

void Tablet::MarkFinishedBootstrapping() {
  CHECK_EQ(state_, kBootstrapping);
  state_ = kOpen;
}

bool Tablet::StartShutdown() {
  LOG_WITH_PREFIX(INFO) << __func__;

  bool expected = false;
  if (!shutdown_requested_.compare_exchange_strong(expected, true)) {
    return false;
  }

  if (transaction_participant_) {
    transaction_participant_->StartShutdown();
  }

  return true;
}

void Tablet::CompleteShutdown(
    const DisableFlushOnShutdown disable_flush_on_shutdown, const AbortOps abort_ops) {
  LOG_WITH_PREFIX(INFO) << __func__;

  StartShutdown();

  auto op_pauses = StartShutdownRocksDBs(disable_flush_on_shutdown, abort_ops, Stop::kTrue);

  cleanup_intent_files_token_.reset();

  if (transaction_coordinator_) {
    transaction_coordinator_->Shutdown();
  }

  if (transaction_participant_) {
    transaction_participant_->CompleteShutdown();
  }

  {
    std::lock_guard lock(operation_filters_mutex_);

    if (completed_split_log_anchor_) {
      WARN_NOT_OK(log_anchor_registry_->Unregister(completed_split_log_anchor_.get()),
                  "Unregister split anchor");
    }

    if (completed_split_operation_filter_) {
      UnregisterOperationFilterUnlocked(completed_split_operation_filter_.get());
    }

    if (restoring_operation_filter_) {
      UnregisterOperationFilterUnlocked(restoring_operation_filter_.get());
    }
  }

  std::lock_guard lock(component_lock_);

  // Shutdown the RocksDB instance for this tablet, if present.
  // Destruct intents DB and regular DB in-memory objects in reverse order to their creation.
  // Also it makes sure that regular DB is alive during flush filter of intents db.
  CompleteShutdownRocksDBs(op_pauses);

  {
    std::lock_guard lock(full_compaction_token_mutex_);
    if (full_compaction_task_pool_token_) {
      full_compaction_task_pool_token_->Shutdown();
    }
  }

  state_ = kShutdown;

  for (auto* op_pause : op_pauses.AsArray()) {
    // Release the mutex that prevents snapshot restore / truncate operations from running. Such
    // operations are no longer possible because the tablet has shut down. When we start the
    // "read/write operation pause", we incremented the "exclusive operation" counter. This will
    // prevent us from decrementing that counter back, disabling read/write operations permanently.
    op_pause->ReleaseMutexButKeepDisabled();
    // Ensure that op_pause stays in scope throughout this function.
    LOG_IF(DFATAL, !op_pause->status().ok()) << op_pause->status();
  }
}

TabletScopedRWOperationPauses Tablet::StartShutdownRocksDBs(
    DisableFlushOnShutdown disable_flush_on_shutdown, AbortOps abort_ops, Stop stop) {
  TabletScopedRWOperationPauses op_pauses;

  auto pause = [this,
                stop](const BlockingRocksDbShutdownStart is_blocking) -> ScopedRWOperationPause {
    auto op_pause = PauseReadWriteOperations(is_blocking, stop);
    if (!op_pause.ok()) {
      LOG(FATAL) << "Failed to stop read/write operations: " << op_pause.status();
    }
    return op_pause;
  };

  op_pauses.blocking_rocksdb_shutdown_start = pause(BlockingRocksDbShutdownStart::kTrue);

  bool expected = false;
  // If shutdown has been already requested, we still might need to wait for all pending read/write
  // operations to complete here, because caller is not holding ScopedRWOperationPause.
  if (rocksdb_shutdown_requested_.compare_exchange_strong(expected, true)) {
    for (auto* db : {regular_db_.get(), intents_db_.get()}) {
      if (db) {
        db->SetDisableFlushOnShutdown(disable_flush_on_shutdown);
        db->StartShutdown();
      }
    }
  }

  if (abort_ops) {
    abort_pending_op_status_holder_.SetError(
        STATUS_FORMAT(Aborted, "$0aborted pending operations", LogPrefix()));
  }

  op_pauses.not_blocking_rocksdb_shutdown_start = pause(BlockingRocksDbShutdownStart::kFalse);

  if (abort_ops) {
    // Clean aborted status after all pending operations have been completed.
    // This is necessary for the cases when we want to start rocksdb again for the tablet, for
    // example after truncate or restore.
    abort_pending_op_status_holder_.Reset();
  }

  return op_pauses;
}

std::vector<std::string> Tablet::CompleteShutdownRocksDBs(
    const TabletScopedRWOperationPauses& ops_pauses) {
  // We need ops_pauses just to guarantee that PauseReadWriteOperations has been called.

  if (intents_db_) {
    intents_db_->ListenFilesChanged(nullptr);
  }

  rocksdb::Options rocksdb_options;

  std::vector<std::string> db_paths;
  for (auto* db_uniq_ptr : {&intents_db_, &regular_db_}) {
    if (*db_uniq_ptr) {
      db_paths.push_back((*db_uniq_ptr)->GetName());
      db_uniq_ptr->reset();
    }
  }

  key_bounds_ = docdb::KeyBounds();
  // Reset rocksdb_shutdown_requested_ to the initial state like RocksDBs were never opened,
  // so we don't have to reset it on RocksDB open (we potentially can have several places in the
  // code doing opening RocksDB while RocksDB shutdown is always going through
  // Tablet::ShutdownRocksDBs).
  rocksdb_shutdown_requested_ = false;

  return db_paths;
}

Status Tablet::DeleteRocksDBs(const std::vector<std::string>& db_paths) {
  rocksdb::Options rocksdb_options;
  InitRocksDBOptions(&rocksdb_options, LogPrefix());

  Status status;
  for (const auto& db_path : db_paths) {
    // Attempt to delete each RocksDB and return the first error encountered.
    const auto s = rocksdb::DestroyDB(db_path, rocksdb_options);
    ERROR_NOT_OK(s, "Failed to delete rocksdb:");
    if (status.ok()) {
      status = s;
    }
  }
  return status;
}

Result<std::unique_ptr<docdb::DocRowwiseIterator>> Tablet::NewUninitializedDocRowIterator(
    const dockv::ReaderProjection& projection,
    const ReadHybridTime& read_hybrid_time,
    const TableId& table_id,
    CoarseTimePoint deadline,
    AllowBootstrappingState allow_bootstrapping_state) const {
  if (state_ != kOpen && (!allow_bootstrapping_state || state_ != kBootstrapping)) {
    return STATUS_FORMAT(IllegalState, "Tablet in wrong state: $0", state_);
  }

  if (table_type_ != TableType::YQL_TABLE_TYPE && table_type_ != TableType::PGSQL_TABLE_TYPE) {
    return STATUS_FORMAT(NotSupported, "Invalid table type: $0", table_type_);
  }

  auto scoped_read_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_read_operation);

  VLOG_WITH_PREFIX(2) << "Created new Iterator reading at " << read_hybrid_time.ToString();

  const auto table_info = VERIFY_RESULT(metadata_->GetTableInfo(table_id));

  auto txn_op_ctx = VERIFY_RESULT(CreateTransactionOperationContext(
      /* transaction_id */ boost::none,
      table_info->schema().table_properties().is_ysql_catalog_table()));
  docdb::ReadOperationData read_operation_data = {
    .deadline = deadline,
    .read_time = read_hybrid_time
        ? read_hybrid_time
        : ReadHybridTime::SingleTime(VERIFY_RESULT(SafeTime(RequireLease::kFalse))),
  };
  return std::make_unique<DocRowwiseIterator>(
      projection, table_info->doc_read_context, txn_op_ctx, doc_db(), read_operation_data,
      std::move(scoped_read_operation));
}

Result<std::unique_ptr<docdb::YQLRowwiseIteratorIf>> Tablet::NewRowIterator(
    const dockv::ReaderProjection& projection,
    const ReadHybridTime& read_hybrid_time,
    const TableId& table_id,
    CoarseTimePoint deadline,
    docdb::SkipSeek skip_seek) const {
  auto iter = VERIFY_RESULT(NewUninitializedDocRowIterator(
      projection, read_hybrid_time, table_id, deadline, AllowBootstrappingState::kFalse));
  iter->InitForTableType(table_type_, Slice(), skip_seek);
  return std::move(iter);
}

Result<std::unique_ptr<docdb::YQLRowwiseIteratorIf>> Tablet::NewRowIterator(
    const TableId& table_id) const {
  const std::shared_ptr<tablet::TableInfo> table_info =
      VERIFY_RESULT(metadata_->GetTableInfo(table_id));
  CHECK(false);
  dockv::ReaderProjection projection(table_info->schema());
  return NewRowIterator(projection, {}, table_id);
}

Status Tablet::ApplyRowOperations(
    WriteOperation* operation, AlreadyAppliedToRegularDB already_applied_to_regular_db) {
  AtomicFlagSleepMs(&FLAGS_TEST_inject_sleep_before_applying_write_batch_ms);
  const auto& write_request =
      operation->consensus_round() && operation->consensus_round()->replicate_msg()
          // Online case.
          ? operation->consensus_round()->replicate_msg()->write()
          // Bootstrap case.
          : *operation->request();
  const auto& put_batch = write_request.write_batch();
  if (metrics()) {
    VLOG(3) << "Applying write batch (write_pairs=" << put_batch.write_pairs().size() << "): "
            << put_batch.ShortDebugString();
    metrics()->IncrementBy(TabletCounters::kRowsInserted, put_batch.write_pairs().size());
  }

  return ApplyOperation(
      *operation, write_request.batch_idx(), put_batch, already_applied_to_regular_db);
}

Status Tablet::ApplyOperation(
    const Operation& operation, int64_t batch_idx,
    const docdb::LWKeyValueWriteBatchPB& write_batch,
    AlreadyAppliedToRegularDB already_applied_to_regular_db) {
  // The write_hybrid_time is MVCC timestamp to be applied to the records in the batch. This will be
  // different from batch_hybrid_time in cases like xcluster and index backfill.
  auto write_hybrid_time = operation.WriteHybridTime();
  auto batch_hybrid_time = operation.hybrid_time();

  docdb::ConsensusFrontiers frontiers;
  // Even if we have an external hybrid time, use the local commit hybrid time in the consensus
  // frontier.
  auto frontiers_ptr = InitFrontiers(
      operation.op_id(), batch_hybrid_time,
      /* commit_ht= */ HybridTime::kInvalid, &frontiers);
  if (frontiers_ptr) {
    auto ttl = write_batch.has_ttl()
        ? MonoDelta::FromNanoseconds(write_batch.ttl())
        : dockv::ValueControlFields::kMaxTtl;
    frontiers_ptr->Largest().set_max_value_level_ttl_expiration_time(
        dockv::FileExpirationFromValueTTL(batch_hybrid_time, ttl));
    for (const auto& p : write_batch.table_schema_version()) {
      // Since new frontiers does not contain schema version just add it there.
      auto table_id = p.table_id().empty()
          ? Uuid::Nil() : VERIFY_RESULT(Uuid::FromSlice(p.table_id()));
      frontiers_ptr->Smallest().AddSchemaVersion(table_id, p.schema_version());
      frontiers_ptr->Largest().AddSchemaVersion(table_id, p.schema_version());
    }
  }
  return ApplyKeyValueRowOperations(
      batch_idx, write_batch, frontiers_ptr, write_hybrid_time, batch_hybrid_time,
      already_applied_to_regular_db);
}

Status Tablet::WriteTransactionalBatch(
    int64_t batch_idx,
    const docdb::LWKeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    const rocksdb::UserFrontiers* frontiers) {
  auto transaction_id = CHECK_RESULT(
      FullyDecodeTransactionId(put_batch.transaction().transaction_id()));

  bool store_metadata = false;
  if (put_batch.transaction().has_isolation()) {
    // Store transaction metadata (status tablet, isolation level etc.)
    auto metadata = VERIFY_RESULT(TransactionMetadata::FromPB(put_batch.transaction()));
    auto add_result = transaction_participant()->Add(metadata);
    if (!add_result.ok()) {
      return add_result.status();
    }
    store_metadata = add_result.get();
  }
  boost::container::small_vector<uint8_t, 16> encoded_replicated_batch_idx_set;
  auto prepare_batch_data = VERIFY_RESULT(transaction_participant()->PrepareBatchData(
      transaction_id, batch_idx, &encoded_replicated_batch_idx_set));
  if (!prepare_batch_data) {
    // If metadata is missing it could be caused by aborted and removed transaction.
    // In this case we should not add new intents for it.
    return STATUS(TryAgain,
                  Format("Transaction metadata missing: $0, looks like it was just aborted",
                         transaction_id), Slice(),
                         PgsqlError(YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE));
  }

  auto isolation_level = prepare_batch_data->first;
  auto& last_batch_data = prepare_batch_data->second;

  docdb::TransactionalWriter writer(
      put_batch, hybrid_time, transaction_id, isolation_level,
      dockv::PartialRangeKeyIntents(metadata_->UsePartialRangeKeyIntents()),
      Slice(encoded_replicated_batch_idx_set.data(), encoded_replicated_batch_idx_set.size()),
      last_batch_data.next_write_id);
  if (store_metadata) {
    writer.SetMetadataToStore(&put_batch.transaction());
  }
  rocksdb::WriteBatch write_batch;
  write_batch.SetDirectWriter(&writer);
  RequestScope request_scope = VERIFY_RESULT(CreateRequestScope());

  WriteToRocksDB(frontiers, &write_batch, StorageDbType::kIntents);

  last_batch_data.hybrid_time = hybrid_time;
  last_batch_data.next_write_id = writer.intra_txn_write_id();
  transaction_participant()->BatchReplicated(transaction_id, last_batch_data);

  return Status::OK();
}

Status Tablet::ApplyKeyValueRowOperations(
    int64_t batch_idx, const docdb::LWKeyValueWriteBatchPB& put_batch,
    docdb::ConsensusFrontiers* frontiers, HybridTime write_hybrid_time,
    HybridTime batch_hybrid_time, AlreadyAppliedToRegularDB already_applied_to_regular_db) {
  if (put_batch.write_pairs().empty() && put_batch.read_pairs().empty() &&
      put_batch.apply_external_transactions().empty()) {
    return Status::OK();
  }

  // Could return failure only for cases where it is safe to skip applying operations to DB.
  // For instance where aborted transaction intents are written.
  // In all other cases we should crash instead of skipping apply.

  if (put_batch.has_transaction()) {
    RETURN_NOT_OK(WriteTransactionalBatch(batch_idx, put_batch, write_hybrid_time, frontiers));
  } else {
    // See comments for PrepareExternalWriteBatch.

    // This should be only set when we are replicating the transaction status table (since this is
    // only used for UPDATE_TRANSACTION_OP).
    DCHECK(!already_applied_to_regular_db);

    rocksdb::WriteBatch intents_write_batch;
    docdb::ExternalIntentsBatchWriter batcher(
        put_batch, write_hybrid_time, batch_hybrid_time, intents_db_.get(), &intents_write_batch,
        &GetSchemaPackingProvider());
    batcher.SetFrontiers(frontiers);

    rocksdb::WriteBatch regular_write_batch;
    regular_write_batch.SetDirectWriter(&batcher);
    WriteToRocksDB(frontiers, &regular_write_batch, StorageDbType::kRegular);

    if (intents_write_batch.Count() != 0) {
      if (!metadata_->IsUnderXClusterReplication()) {
        RETURN_NOT_OK(metadata_->SetIsUnderXClusterReplicationAndFlush(true));
      }
      WriteToRocksDB(frontiers, &intents_write_batch, StorageDbType::kIntents);
    }

    if (snapshot_coordinator_) {
      for (const auto& pair : put_batch.write_pairs()) {
        WARN_NOT_OK(snapshot_coordinator_->ApplyWritePair(pair.key(), pair.value()),
                    "ApplyWritePair failed");
      }
    }
  }

  return Status::OK();
}

void Tablet::WriteToRocksDB(
    const rocksdb::UserFrontiers* frontiers,
    rocksdb::WriteBatch* write_batch,
    docdb::StorageDbType storage_db_type) {
  rocksdb::DB* dest_db = nullptr;
  switch (storage_db_type) {
    case StorageDbType::kRegular: dest_db = regular_db_.get(); break;
    case StorageDbType::kIntents: dest_db = intents_db_.get(); break;
  }

  // Frontiers can be null for deferred apply operations.
  if (frontiers) {
    write_batch->SetFrontiers(frontiers);
  }

  // We are using Raft replication index for the RocksDB sequence number for
  // all members of this write batch.
  rocksdb::WriteOptions write_options;
  InitRocksDBWriteOptions(&write_options);

  std::optional<docdb::DocWriteBatchFormatter> formatter;
  if (FLAGS_TEST_docdb_log_write_batches) {
    formatter.emplace(
        storage_db_type, BinaryOutputFormat::kEscapedAndHex, WriteBatchOutputFormat::kArrow,
        "  " + LogPrefix(storage_db_type), &GetSchemaPackingProvider());
    write_batch->SetHandlerForLogging(&*formatter);
  }

  auto rocksdb_write_status = dest_db->Write(write_options, write_batch);
  if (!rocksdb_write_status.ok()) {
    LOG_WITH_PREFIX(FATAL) << "Failed to write a batch with " << write_batch->Count()
                           << " operations into RocksDB: " << rocksdb_write_status;
  }

  if (FLAGS_TEST_docdb_log_write_batches) {
    LOG_WITH_PREFIX(INFO)
        << "Wrote " << formatter->Count()
        << " key/value pairs to " << storage_db_type
        << " RocksDB:\n" << formatter->str();
  }
}

//--------------------------------------------------------------------------------------------------
// Redis Request Processing.
Status Tablet::HandleRedisReadRequest(const docdb::ReadOperationData& read_operation_data,
                                      const RedisReadRequestPB& redis_read_request,
                                      RedisResponsePB* response) {
  // TODO: move this locking to the top-level read request handler in TabletService.
  auto scoped_read_operation = CreateScopedRWOperationNotBlockingRocksDbShutdownStart(
      read_operation_data.deadline);
  RETURN_NOT_OK(scoped_read_operation);

  ScopedTabletMetricsLatencyTracker metrics_tracker(
      metrics_.get(), TabletEventStats::kQlReadLatency);

  docdb::RedisReadOperation doc_op(redis_read_request, doc_db(), read_operation_data);
  RETURN_NOT_OK(doc_op.Execute());
  *response = std::move(doc_op.response());
  return Status::OK();
}

bool IsSchemaVersionCompatible(
    SchemaVersion current_version, SchemaVersion request_version,
    bool compatible_with_previous_version) {
  if (request_version == current_version) {
    return true;
  }

  if (compatible_with_previous_version && request_version == current_version + 1) {
    DVLOG(1) << (FLAGS_yql_allow_compatible_schema_versions ? "A" : "Not a")
             << "ccepting request that is ahead of us by 1 version";
    return FLAGS_yql_allow_compatible_schema_versions;
  }

  return false;
}

//--------------------------------------------------------------------------------------------------
// CQL Request Processing.
Status Tablet::HandleQLReadRequest(
    const docdb::ReadOperationData& read_operation_data,
    const QLReadRequestPB& ql_read_request,
    const TransactionMetadataPB& transaction_metadata,
    QLReadRequestResult* result,
    WriteBuffer* rows_data) {
  auto scoped_read_operation = CreateScopedRWOperationNotBlockingRocksDbShutdownStart(
      read_operation_data.deadline);
  RETURN_NOT_OK(scoped_read_operation);
  ScopedTabletMetricsLatencyTracker metrics_tracker(
      metrics_.get(), TabletEventStats::kQlReadLatency);

  bool schema_version_compatible = IsSchemaVersionCompatible(
      metadata()->schema_version(), ql_read_request.schema_version(),
      ql_read_request.is_compatible_with_previous_version());

  Status status;
  if (schema_version_compatible) {
    Result<TransactionOperationContext> txn_op_ctx =
        CreateTransactionOperationContext(transaction_metadata, /* is_ysql_catalog_table */ false);
    RETURN_NOT_OK(txn_op_ctx);
    status = AbstractTablet::HandleQLReadRequest(
        read_operation_data, ql_read_request, *txn_op_ctx, *ql_storage_, scoped_read_operation,
        result, rows_data);

    schema_version_compatible = IsSchemaVersionCompatible(
        metadata()->schema_version(), ql_read_request.schema_version(),
        ql_read_request.is_compatible_with_previous_version());
  }

  if (!schema_version_compatible) {
    DVLOG(1) << "Setting status for read as YQL_STATUS_SCHEMA_VERSION_MISMATCH";
    result->response.Clear();
    result->response.set_status(QLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH);
    result->response.set_error_message(Format(
        "schema version mismatch for table $0: expected $1, got $2 (compt with prev: $3)",
        metadata()->table_id(),
        metadata()->schema_version(),
        ql_read_request.schema_version(),
        ql_read_request.is_compatible_with_previous_version()));
    return Status::OK();
  }

  return status;
}

Status Tablet::CreatePagingStateForRead(const QLReadRequestPB& ql_read_request,
                                        const size_t row_count,
                                        QLResponsePB* response) const {

  // If the response does not have a next partition key, it means we are done reading the current
  // tablet. But, if the request does not have the hash columns set, this must be a table-scan,
  // so we need to decide if we are done or if we need to move to the next tablet.
  // If we did not reach the:
  //   1. max number of results (LIMIT clause -- if set)
  //   2. end of the table (this was the last tablet)
  //   3. max partition key (upper bound condition using 'token' -- if set)
  // we set the paging state to point to the exclusive end partition key of this tablet, which is
  // the start key of the next tablet).
  if (ql_read_request.hashed_column_values().empty() &&
      !response->paging_state().has_next_partition_key()) {
    // Check we did not reach the results limit.
    // If return_paging_state is set, it means the request limit is actually just the page size.
    if (!ql_read_request.has_limit() ||
        row_count < ql_read_request.limit() ||
        ql_read_request.return_paging_state()) {

      // Check we did not reach the last tablet.
      const string& next_partition_key = metadata_->partition()->partition_key_end();
      if (!next_partition_key.empty()) {
        uint16_t next_hash_code = dockv::PartitionSchema::DecodeMultiColumnHashValue(
            next_partition_key);

        // Check we did not reach the max partition key.
        if (!ql_read_request.has_max_hash_code() ||
            next_hash_code <= ql_read_request.max_hash_code()) {
          response->mutable_paging_state()->set_next_partition_key(next_partition_key);
        }
      }
    }
  }

  // If there is a paging state, update the total number of rows read so far.
  if (response->has_paging_state()) {
    response->mutable_paging_state()->set_total_num_rows_read(
        ql_read_request.paging_state().total_num_rows_read() + row_count);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// PGSQL Request Processing.
//--------------------------------------------------------------------------------------------------
Status Tablet::HandlePgsqlReadRequest(
    const docdb::ReadOperationData& read_operation_data,
    bool is_explicit_request_read_time,
    const PgsqlReadRequestPB& pgsql_read_request,
    const TransactionMetadataPB& transaction_metadata,
    const SubTransactionMetadataPB& subtransaction_metadata,
    PgsqlReadRequestResult* result) {

  TRACE(LogPrefix());
  auto scoped_read_operation = CreateScopedRWOperationNotBlockingRocksDbShutdownStart(
      read_operation_data.deadline);
  RETURN_NOT_OK(scoped_read_operation);

  docdb::DocDBStatistics* statistics = nullptr;
  TabletMetrics* metrics = metrics_.get();

  if (GetAtomicFlag(&FLAGS_batch_tablet_metrics_update)) {
    statistics = &scoped_docdb_statistics;
    metrics = &scoped_tablet_metrics;
  }

  auto status = DoHandlePgsqlReadRequest(
      &scoped_read_operation, statistics, metrics, read_operation_data,
      is_explicit_request_read_time, pgsql_read_request, transaction_metadata,
      subtransaction_metadata, result);

  if (statistics) {
    if (GetAtomicFlag(&FLAGS_dump_metrics_to_trace)) {
      TraceScopedMetrics();
    }
    auto metrics_capture = pgsql_read_request.metrics_capture();
    if (GetAtomicFlag(&FLAGS_ysql_analyze_dump_metrics) &&
        metrics_capture == PgsqlMetricsCaptureType::PGSQL_METRICS_CAPTURE_ALL) {
      statistics->CopyToPgsqlResponse(&result->response);
      scoped_tablet_metrics.CopyToPgsqlResponse(&result->response);
    }
    scoped_tablet_metrics.MergeAndClear(metrics_.get());
    statistics->MergeAndClear(regulardb_statistics_.get(), intentsdb_statistics_.get());
  }

  return status;
}

Status Tablet::DoHandlePgsqlReadRequest(
    ScopedRWOperation* scoped_read_operation,
    docdb::DocDBStatistics* statistics,
    TabletMetrics* metrics,
    const docdb::ReadOperationData& read_operation_data,
    bool is_explicit_request_read_time,
    const PgsqlReadRequestPB& pgsql_read_request,
    const TransactionMetadataPB& transaction_metadata,
    const SubTransactionMetadataPB& subtransaction_metadata,
    PgsqlReadRequestResult* result) {
  ScopedTabletMetricsLatencyTracker metrics_tracker(
      metrics, TabletEventStats::kQlReadLatency);

  docdb::QLRocksDBStorage storage{doc_db(metrics)};

  const shared_ptr<tablet::TableInfo> table_info =
      VERIFY_RESULT(metadata_->GetTableInfo(pgsql_read_request.table_id()));

  Status status;
  if (pgsql_read_request.has_get_tablet_key_ranges_request()) {
    status = ProcessPgsqlGetTableKeyRangesRequest(pgsql_read_request, result);
  } else {
    Result<TransactionOperationContext> txn_op_ctx =
        CreateTransactionOperationContext(
            transaction_metadata,
            table_info->schema().table_properties().is_ysql_catalog_table(),
            &subtransaction_metadata);
    RETURN_NOT_OK(txn_op_ctx);
    status = ProcessPgsqlReadRequest(
        read_operation_data, is_explicit_request_read_time, pgsql_read_request, table_info,
        *txn_op_ctx, storage, statistics, *scoped_read_operation, result);
  }

  // Assert the table is a Postgres table.
  DCHECK_EQ(table_info->table_type, TableType::PGSQL_TABLE_TYPE);
  if (table_info->schema_version != pgsql_read_request.schema_version()) {
    result->response.Clear();
    result->response.set_status(PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH);
    result->response.set_error_message(
        Format("schema version mismatch for table $0: expected $1, got $2",
               table_info->table_id,
               table_info->schema_version,
               pgsql_read_request.schema_version()));
    return Status::OK();
  }

  return status;
}

// Returns true if the query can be satisfied by rows present in current tablet.
// Returns false if query requires other tablets to also be scanned. Examples of this include:
//   (1) full table scan queries
//   (2) queries that whose key conditions are such that the query will require a multi tablet
//       scan.
//
// Requests that are of the form batched index lookups of ybctids are sent only to a single tablet.
// However there can arise situations where tablets splitting occurs after such requests are being
// prepared by the pggate layer (specifically pg_doc_op.cc). Under such circumstances, if tablets
// are split into two sub-tablets, then such batched index lookups of ybctid requests should be sent
// to multiple tablets (the two sub-tablets). Hence, the request ends up not being a single tablet
// request.
Result<bool> Tablet::IsQueryOnlyForTablet(
    const PgsqlReadRequestPB& pgsql_read_request, size_t row_count) const {
  // For cases when neither partition_column_values nor range_column_values are set,
  // https://github.com/yugabyte/yugabyte-db/commit/57bee5a77d0df959c7fe0f000b65be97de9f90a0 removed
  // routing to exact tablet containing requested key and ybctid is simply set to lower/upper bound
  // key in order to have unified approach on setting partition_key.

  // If ybctid is specified without batch_arguments we don't treat it as a single tablet request
  // automatically, it can be continued until we hit upper bound (scan tablet with upper bound
  // matching request upper bound).
  if ((!pgsql_read_request.ybctid_column_value().value().binary_value().empty() &&
       std::cmp_equal(std::max(pgsql_read_request.batch_arguments_size(), 1), row_count)) ||
      !pgsql_read_request.partition_column_values().empty()) {
    return true;
  }

  std::shared_ptr<const Schema> schema = metadata_->schema();
  if (schema->has_cotable_id() || schema->has_colocation_id())  {
    // This is a colocated table.
    return true;
  }

  if (schema->num_hash_key_columns() == 0 &&
      schema->num_range_key_columns() ==
          implicit_cast<size_t>(pgsql_read_request.range_column_values_size())) {
    // PK is contained within this tablet.
    return true;
  }
  return false;
}

Result<bool> Tablet::HasScanReachedMaxPartitionKey(
    const PgsqlReadRequestPB& pgsql_read_request,
    const string& partition_key,
    size_t row_count) const {
  auto schema = metadata_->schema();

  if (schema->num_hash_key_columns() > 0) {
    uint16_t next_hash_code = dockv::PartitionSchema::DecodeMultiColumnHashValue(partition_key);
    // For batched index lookup of ybctids, check if the current partition hash is lesser than
    // upper bound. If it is, we can then avoid paging. Paging of batched index lookup of ybctids
    // occur when tablets split after request is prepared.
    if (implicit_cast<size_t>(pgsql_read_request.batch_arguments_size()) > row_count) {
      if (!pgsql_read_request.upper_bound().has_key()) {
          return false;
      }
      uint16_t upper_bound_hash = dockv::PartitionSchema::DecodeMultiColumnHashValue(
          pgsql_read_request.upper_bound().key());
      uint16_t partition_hash =
          dockv::PartitionSchema::DecodeMultiColumnHashValue(partition_key);
      return pgsql_read_request.upper_bound().is_inclusive() ?
          partition_hash > upper_bound_hash :
          partition_hash >= upper_bound_hash;
    }
    if (pgsql_read_request.has_max_hash_code() &&
        next_hash_code > pgsql_read_request.max_hash_code()) {
      return true;
    }
  } else if (pgsql_read_request.has_upper_bound()) {
    dockv::DocKey partition_doc_key(*schema);
    VERIFY_RESULT(partition_doc_key.DecodeFrom(
        partition_key, dockv::DocKeyPart::kWholeDocKey, dockv::AllowSpecial::kTrue));
    dockv::DocKey max_partition_doc_key(*schema);
    VERIFY_RESULT(max_partition_doc_key.DecodeFrom(
        pgsql_read_request.upper_bound().key(), dockv::DocKeyPart::kWholeDocKey,
        dockv::AllowSpecial::kTrue));

    auto cmp = partition_doc_key.CompareTo(max_partition_doc_key);
    return pgsql_read_request.upper_bound().is_inclusive() ? cmp > 0 : cmp >= 0;
  }

  return false;
}

namespace {

void SetBackfillSpecForYsqlBackfill(
    const PgsqlReadRequestPB& pgsql_read_request,
    const size_t& row_count,
    PgsqlResponsePB* response) {
  PgsqlBackfillSpecPB in_spec;
  in_spec.ParseFromString(a2b_hex(pgsql_read_request.backfill_spec()));

  auto limit = in_spec.limit();
  PgsqlBackfillSpecPB out_spec;
  out_spec.set_limit(limit);
  out_spec.set_count(in_spec.count() + row_count);
  response->set_is_backfill_batch_done(!response->has_paging_state());
  if (limit >= 0 && out_spec.count() >= limit) {
    // Hint postgres to stop scanning now. And set up the
    // next_row_key based on the paging state.
    if (response->has_paging_state()) {
      out_spec.set_next_row_key(response->paging_state().next_row_key());
    }
    response->set_is_backfill_batch_done(true);
  }

  VLOG(2) << "Got input spec " << yb::ToString(in_spec)
          << " set output spec " << yb::ToString(out_spec)
          << " batch_done=" << response->is_backfill_batch_done();
  string serialized_pb;
  out_spec.SerializeToString(&serialized_pb);
  response->set_backfill_spec(b2a_hex(serialized_pb));
}

}  // namespace

Status Tablet::CreatePagingStateForRead(const PgsqlReadRequestPB& pgsql_read_request,
                                        const size_t row_count,
                                        PgsqlResponsePB* response) const {
  // If there is no hash column in the read request, this is a full-table query. And if there is no
  // paging state in the response, we are done reading from the current tablet. In this case, we
  // should return the exclusive end partition key of this tablet if not empty which is the start
  // key of the next tablet. Do so only if the request has no row count limit, or there is and we
  // haven't hit it, or we are asked to return paging state even when we have hit the limit.
  // Otherwise, leave the paging state empty which means we are completely done reading for the
  // whole SELECT statement.
  const bool single_tablet_query =
      VERIFY_RESULT(IsQueryOnlyForTablet(pgsql_read_request, row_count));
  if (!single_tablet_query &&
      !response->has_paging_state() &&
      (!pgsql_read_request.has_limit() || row_count < pgsql_read_request.limit() ||
       pgsql_read_request.return_paging_state())) {
    // For backward scans partition_key_start must be used as next_partition_key.
    // Client level logic will check it and route next request to the preceding tablet.
    const auto& next_partition_key =
        pgsql_read_request.has_hash_code() ||
        pgsql_read_request.is_forward_scan()
            ? metadata_->partition()->partition_key_end()
            : metadata_->partition()->partition_key_start();
    // Check we did not reach the last tablet.
    const bool end_scan = next_partition_key.empty() ||
        VERIFY_RESULT(HasScanReachedMaxPartitionKey(
            pgsql_read_request, next_partition_key, row_count));
    if (!end_scan) {
      response->mutable_paging_state()->set_next_partition_key(next_partition_key);
    }
  }

  // If there is a paging state, update the total number of rows read so far.
  if (response->has_paging_state()) {
    response->mutable_paging_state()->set_total_num_rows_read(
        pgsql_read_request.paging_state().total_num_rows_read() + row_count);
  }

  if (pgsql_read_request.is_for_backfill()) {
    // BackfillSpec is used to implement "paging" across multiple BackfillIndex
    // rpcs from the master.
    SetBackfillSpecForYsqlBackfill(pgsql_read_request, row_count, response);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

void Tablet::AcquireLocksAndPerformDocOperations(std::unique_ptr<WriteQuery> query) {
  TRACE(__func__);
  if (table_type_ == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    query->Cancel(
        STATUS(NotSupported, "Transaction status table does not support write"));
    return;
  }

  if (!GetAtomicFlag(&FLAGS_disable_alter_vs_write_mutual_exclusion)) {
    auto write_permit = GetPermitToWrite(query->deadline());
    if (!write_permit.ok()) {
      TRACE("Could not get the write permit.");
      WriteQuery::StartSynchronization(std::move(query), MoveStatus(write_permit));
      return;
    }
    // Save the write permit to be released after the operation is submitted
    // to Raft queue.
    query->UseSubmitToken(std::move(write_permit));
  }

  WriteQuery::Execute(std::move(query));
}

Status Tablet::Flush(FlushMode mode, FlushFlags flags, int64_t ignore_if_flushed_after_tick) {
  TRACE_EVENT0("tablet", "Tablet::Flush");

  ScopedRWOperation pending_op;
  if (!HasFlags(flags, FlushFlags::kNoScopedOperation)) {
    pending_op = CreateScopedRWOperationBlockingRocksDbShutdownStart();
    LOG_IF(DFATAL, !pending_op.ok())
        << "CreateScopedRWOperationBlockingRocksDbShutdownStart failed";
    RETURN_NOT_OK(pending_op);
  }

  rocksdb::FlushOptions options;
  options.ignore_if_flushed_after_tick = ignore_if_flushed_after_tick;
  bool flush_intents = intents_db_ && HasFlags(flags, FlushFlags::kIntents);
  if (flush_intents) {
    options.wait = false;
    WARN_NOT_OK(intents_db_->Flush(options), "Flush intents DB");
  }

  if (HasFlags(flags, FlushFlags::kRegular) && regular_db_) {
    options.wait = mode == FlushMode::kSync;
    WARN_NOT_OK(regular_db_->Flush(options), "Flush regular DB");
  }

  if (flush_intents && mode == FlushMode::kSync) {
    RETURN_NOT_OK(intents_db_->WaitForFlush());
  }

  return Status::OK();
}

Status Tablet::WaitForFlush() {
  TRACE_EVENT0("tablet", "Tablet::WaitForFlush");

  if (regular_db_) {
    RETURN_NOT_OK(regular_db_->WaitForFlush());
  }
  if (intents_db_) {
    RETURN_NOT_OK(intents_db_->WaitForFlush());
  }

  return Status::OK();
}

Status Tablet::FlushSuperblock(OnlyIfDirty only_if_dirty) {
  return metadata_->Flush(only_if_dirty);
}

Status Tablet::ImportData(const std::string& source_dir) {
  // We import only regular records, so don't have to deal with intents here.
  return regular_db_->Import(source_dir);
}

// We apply intents by iterating over whole transaction reverse index.
// Using value of reverse index record we find original intent record and apply it.
// After that we delete both intent record and reverse index record.
Result<docdb::ApplyTransactionState> Tablet::ApplyIntents(const TransactionApplyData& data) {
  VLOG_WITH_PREFIX(4) << __func__ << ": " << data.transaction_id;

  // This flag enables tests to induce a situation where a transaction has committed but its intents
  // haven't yet moved to regular db for a sufficiently long period. For example, it can help a test
  // to reliably assert that conflict resolution/ concurrency control with a conflicting committed
  // transaction is done properly in the rare situation where the committed transaction's intents
  // are still in intents db and not yet in regular db.
  AtomicFlagSleepMs(&FLAGS_TEST_inject_sleep_before_applying_intents_ms);
  docdb::ApplyIntentsContext context(
      data.transaction_id, data.apply_state, data.aborted, data.commit_ht, data.log_ht,
      &key_bounds_, metadata_.get(), intents_db_.get());
  docdb::IntentsWriter intents_writer(
      data.apply_state ? data.apply_state->key : Slice(), intents_db_.get(), &context);
  rocksdb::WriteBatch regular_write_batch;
  regular_write_batch.SetDirectWriter(&intents_writer);
  // data.hybrid_time contains transaction commit time.
  // We don't set transaction field of put_batch, otherwise we would write another bunch of intents.
  docdb::ConsensusFrontiers frontiers;
  auto frontiers_ptr = data.op_id.empty() ? nullptr : InitFrontiers(data, &frontiers);
  context.SetFrontiers(frontiers_ptr);
  WriteToRocksDB(frontiers_ptr, &regular_write_batch, StorageDbType::kRegular);
  return context.apply_state();
}

template <class Ids>
Status Tablet::RemoveIntentsImpl(
    const RemoveIntentsData& data, RemoveReason reason, const Ids& ids) {
  auto scoped_read_operation = CreateScopedRWOperationNotBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_read_operation);

  rocksdb::WriteBatch intents_write_batch;
  for (const auto& id : ids) {
    boost::optional<docdb::ApplyTransactionState> apply_state;
    for (;;) {
      docdb::RemoveIntentsContext context(id, static_cast<uint8_t>(reason));
      docdb::IntentsWriter writer(
          apply_state ? apply_state->key : Slice(), intents_db_.get(), &context);
      intents_write_batch.SetDirectWriter(&writer);
      docdb::ConsensusFrontiers frontiers;
      auto frontiers_ptr = InitFrontiers(data, &frontiers);
      WriteToRocksDB(frontiers_ptr, &intents_write_batch, StorageDbType::kIntents);

      if (!context.apply_state().active()) {
        break;
      }

      apply_state = std::move(context.apply_state());
      intents_write_batch.Clear();

      AtomicFlagSleepMs(&FLAGS_apply_intents_task_injected_delay_ms);
    }
  }

  return Status::OK();
}


Status Tablet::RemoveIntents(
    const RemoveIntentsData& data, RemoveReason reason, const TransactionId& id) {
  return RemoveIntentsImpl(data, reason, std::initializer_list<TransactionId>{id});
}

Status Tablet::RemoveIntents(
    const RemoveIntentsData& data, RemoveReason reason, const TransactionIdSet& transactions) {
  return RemoveIntentsImpl(data, reason, transactions);
}

// We batch this as some tx could be very large and may not fit in one batch
Status Tablet::GetIntents(
    const TransactionId& id, std::vector<docdb::IntentKeyValueForCDC>* key_value_intents,
    docdb::ApplyTransactionState* stream_state) {
  auto scoped_read_operation = CreateScopedRWOperationNotBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_read_operation);

  docdb::ApplyTransactionState new_stream_state;

  new_stream_state = VERIFY_RESULT(
      docdb::GetIntentsBatch(id, &key_bounds_, stream_state, intents_db_.get(), key_value_intents));
  stream_state->key = new_stream_state.key;
  stream_state->write_id = new_stream_state.write_id;

  return Status::OK();
}

HybridTime Tablet::ApplierSafeTime(HybridTime min_allowed, CoarseTimePoint deadline) {
  // We could not use mvcc_ directly, because correct lease should be passed to it.
  return mvcc_.SafeTimeForFollower(min_allowed, deadline);
}

Result<std::unique_ptr<docdb::YQLRowwiseIteratorIf>> Tablet::CreateCDCSnapshotIterator(
    const dockv::ReaderProjection& projection, const ReadHybridTime& time, const string& next_key,
    const TableId& table_id) {
  VLOG_WITH_PREFIX(2) << "The nextKey is " << next_key;

  dockv::KeyBytes encoded_next_key;
  if (!next_key.empty()) {
    dockv::SubDocKey start_sub_doc_key;
    dockv::KeyBytes start_key_bytes(next_key);
    RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));
    encoded_next_key = start_sub_doc_key.doc_key().Encode();
    VLOG_WITH_PREFIX(2) << "The nextKey doc is " << encoded_next_key;
  }
  auto iter = VERIFY_RESULT(NewUninitializedDocRowIterator(
      projection, time, table_id, CoarseTimePoint::max(), AllowBootstrappingState::kFalse));
  iter->InitForTableType(table_type_, encoded_next_key);
  return std::move(iter);
}

// This is called From ChangeMetadaOperation::Apply during the
// creation of the stream and also possibly during Tablet Bootstrap.
//
// One thing to note here is that the retention barrier on the consensus layer
// is not set in both these situations. This is no different from the
// current behaviour during Tablet Bootstrap. This is alright since
// that is only for the Log Cache's eviction policy. The Cache will reload
// from the Log segments if there is a cache miss.
//
// Here, a stream is setting its initial retention barrier on the tablet.
// It will be conservative. That is, it will reset the barrier only if
// the current barrier is ahead of its requirement. If there is already
// another slower consumer with a stricter barrier requirement, that
// will be left alone and the barrier will not be reset.
Status Tablet::SetAllInitialCDCSDKRetentionBarriers(
  OpId cdc_sdk_op_id, MonoDelta cdc_sdk_op_id_expiration, HybridTime cdc_sdk_history_cutoff,
  bool require_history_cutoff) {

  // WAL, History, Intents Retention
  if (VERIFY_RESULT(metadata_->SetAllInitialCDCSDKRetentionBarriers(cdc_sdk_op_id,
                                                                    cdc_sdk_history_cutoff,
                                                                    require_history_cutoff))) {
    // Intents Retention setting on txn_participant
    // 1. cdc_sdk_op_id - opid beyond which GC will not happen
    // 2. cdc_sdk_op_id_expiration - time limit upto which intents barrier setting holds
    auto txn_participant = transaction_participant();
    if (txn_participant) {
      txn_participant->SetIntentRetainOpIdAndTime(cdc_sdk_op_id, cdc_sdk_op_id_expiration);
    }
  }

  return Status::OK();
}

Status Tablet::CreatePreparedChangeMetadata(
    ChangeMetadataOperation *operation, const Schema* schema, IsLeaderSide is_leader_side) {
  if (schema) {
    // On follower, the previous op for adding table may not finish applying.
    // GetKeySchema might fail in this case.
    if (is_leader_side) {
      auto key_schema = GetKeySchema(
          operation->has_table_id() ? operation->table_id().ToBuffer() : "");
      if (!key_schema.KeyEquals(*schema)) {
        return STATUS_FORMAT(
            InvalidArgument,
            "Schema keys cannot be altered. New schema key: $0. Existing schema key: $1",
            schema->CreateKeyProjection(),
            key_schema);
      }
    }

    if (!schema->has_column_ids()) {
      // this probably means that the request is not from the Master
      return STATUS(InvalidArgument, "Missing Column IDs");
    }
  }

  operation->set_schema(schema);
  return Status::OK();
}

Status Tablet::AddTableInMemory(const TableInfoPB& table_info, const OpId& op_id) {
  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(table_info.schema(), &schema));

  dockv::PartitionSchema partition_schema;
  RETURN_NOT_OK(dockv::PartitionSchema::FromPB(
      table_info.partition_schema(), schema, &partition_schema));

  metadata_->AddTable(
      table_info.table_id(), table_info.namespace_name(), table_info.table_name(),
      table_info.table_type(), schema, qlexpr::IndexMap(), partition_schema, boost::none,
      table_info.schema_version(), op_id);

  return Status::OK();
}

Status Tablet::AddTable(const TableInfoPB& table_info, const OpId& op_id) {
  RETURN_NOT_OK(AddTableInMemory(table_info, op_id));
  if (!metadata_->IsLazySuperblockFlushEnabled()) {
    RETURN_NOT_OK(metadata_->Flush());
  } else {
    VLOG_WITH_PREFIX(1) << "Skipping superblock flush on " << table_info.table_name()
                        << " table creation, will be done lazily";
  }
  return Status::OK();
}

// TODO(lazy_sb_flush): Lazily flush the superblock here when extending the feature to cotables.
Status Tablet::AddMultipleTables(
    const google::protobuf::RepeatedPtrField<TableInfoPB>& table_infos, const OpId& op_id) {
  // If nothing has changed then return.
  RSTATUS_DCHECK_GT(table_infos.size(), 0, Ok, "No table to add to metadata");
  for (const auto& table_info : table_infos) {
    RETURN_NOT_OK(AddTableInMemory(table_info, op_id));
  }
  return metadata_->Flush();
}

Status Tablet::RemoveTable(const std::string& table_id, const OpId& op_id) {
  metadata_->RemoveTable(table_id, op_id);
  RETURN_NOT_OK(metadata_->Flush());
  return Status::OK();
}

Status Tablet::MarkBackfillDone(const OpId& op_id, const TableId& table_id) {
  LOG_WITH_PREFIX(INFO) << "Setting backfill as done.";
  metadata_->OnBackfillDone(op_id, table_id);
  return metadata_->Flush();
}

Status Tablet::AlterSchema(ChangeMetadataOperation *operation) {
  auto current_table_info = VERIFY_RESULT(metadata_->GetTableInfo(
        operation->request()->has_alter_table_id() ?
        operation->request()->alter_table_id().ToBuffer() : ""));
  auto key_schema = current_table_info->schema().CreateKeyProjection();

  RSTATUS_DCHECK_NE(operation->schema(), static_cast<void*>(nullptr), InvalidArgument,
                    "Schema could not be null");
  RSTATUS_DCHECK(key_schema.KeyEquals(*DCHECK_NOTNULL(operation->schema())), InvalidArgument,
                 "Schema keys cannot be altered");

  // If the current version >= new version, there is nothing to do.
  if (current_table_info->schema_version >= operation->schema_version()) {
    LOG_WITH_PREFIX(INFO)
        << "Already running schema version " << current_table_info->schema_version
        << " got alter request for version " << operation->schema_version();
    return Status::OK();
  }

  LOG_WITH_PREFIX(INFO) << "Alter schema from " << current_table_info->schema().ToString()
                        << " version " << current_table_info->schema_version
                        << " to " << operation->schema()->ToString()
                        << " version " << operation->schema_version();

  // Find out which columns have been deleted in this schema change, and add them to metadata.
  vector<DeletedColumn> deleted_cols;
  for (const auto& col : current_table_info->schema().column_ids()) {
    if (operation->schema()->find_column_by_id(col) == Schema::kColumnNotFound) {
      deleted_cols.emplace_back(col, clock_->Now());
      LOG_WITH_PREFIX(INFO) << "Column " << col << " recorded as deleted.";
    }
  }

  if (operation->has_new_table_name()) {
    metadata_->SetSchemaAndTableName(
        *operation->schema(), operation->index_map(), deleted_cols,
        operation->schema_version(), current_table_info->namespace_name,
        operation->new_table_name().ToBuffer(),
        operation->op_id(),
        current_table_info->table_id);
    // We shouldn't update the attributes of the colocation parent table's metrics when we alter
    // a colocated table.
    if (!metadata_->colocated()) {
      if (table_metrics_entity_) {
        table_metrics_entity_->SetAttribute("table_name", operation->new_table_name().ToBuffer());
        table_metrics_entity_->SetAttribute("namespace_name", current_table_info->namespace_name);
      }
      if (tablet_metrics_entity_) {
        tablet_metrics_entity_->SetAttribute("table_name", operation->new_table_name().ToBuffer());
        tablet_metrics_entity_->SetAttribute("namespace_name", current_table_info->namespace_name);
      }
    }
  } else {
    metadata_->SetSchema(
        *operation->schema(), operation->index_map(), deleted_cols,
        operation->schema_version(),
        operation->op_id(),
        current_table_info->table_id);
  }

  // Cleanup the index from cache which are deleted or updated.
  if (current_table_info->index_map && !current_table_info->index_map->empty()) {
    for (const auto& index : *current_table_info->index_map) {
      const auto it = operation->index_map().find(index.first);
      // Remove entry if it's not found in updated map or it's version has changed.
      if (it == operation->index_map().end()) {
        YBMetaDataCache()->RemoveCachedTable(index.second.table_id());
      } else if (it->second.schema_version() != index.second.schema_version()) {
        YBMetaDataCache()->RemoveCachedTable(index.second.table_id(), it->second.schema_version());
      }
    }
  }

  // Flush the updated schema metadata to disk.
  return metadata_->Flush();
}

Status Tablet::AlterWalRetentionSecs(ChangeMetadataOperation* operation) {
  if (operation->has_wal_retention_secs()) {
    LOG_WITH_PREFIX(INFO) << "Altering metadata wal_retention_secs from "
                          << metadata_->wal_retention_secs()
                          << " to " << operation->wal_retention_secs();
    metadata_->set_wal_retention_secs(operation->wal_retention_secs());
    // Update OpId of the last change metadata operation.
    // It can happen that we change the wal_retention without modifying
    // the schema in which case the alter table will return early
    // without modifying the RaftGroupMetadata but we
    // still need to persist the op id corresponding to this operation
    // so as to avoid replaying during tablet bootstrap.
    // We don't update if the passed op id is invalid. One case when this will happen:
    // If we are replaying during tablet bootstrap and last_flushed_change_metadata_op_id
    // was invalid - For e.g. after upgrade when new code reads old data.
    // In such a case, we ensure that we are no worse than old code's behavior
    // which essentially implies that we mute this new logic for the entire
    // duration of the bootstrap. However, once bootstrap finishes we set this
    // field so that subsequent restarts can start leveraging this feature.
    metadata_->OnChangeMetadataOperationApplied(operation->op_id());
    // Flush the updated schema metadata to disk.
    return metadata_->Flush();
  }
  return STATUS_SUBSTITUTE(InvalidArgument, "Invalid ChangeMetadataOperation: $0",
                           operation->ToString());
}

namespace {

Result<pgwrapper::PGConn> ConnectToPostgres(
    const HostPort& pgsql_proxy_bind_address,
    const std::string& database_name,
    uint64_t postgres_auth_key,
    const CoarseTimePoint& deadline) {
  // Note that the plain password in the connection string will be sent over the wire, but since
  // it only goes over a unix-domain socket, there should be no eavesdropping/tampering issues.
  //
  // By default, connect_timeout is 0, meaning infinite. 1 is automatically converted to 2, so set
  // it to at least 2 in the first place. See connectDBComplete.
  auto conn_res = pgwrapper::PGConnBuilder({
    .host = PgDeriveSocketDir(pgsql_proxy_bind_address),
    .port = pgsql_proxy_bind_address.port(),
    .dbname = database_name,
    .user = "postgres",
    .password = UInt64ToString(postgres_auth_key),
    .connect_timeout = static_cast<size_t>(std::max(
        2, static_cast<int>(ToSeconds(deadline - CoarseMonoClock::Now()))))
  }).Connect();
  if (!conn_res) {
    auto libpq_error_message = AuxilaryMessage(conn_res.status()).value();
    if (libpq_error_message.empty()) {
      return STATUS(IllegalState, "backfill failed to connect to DB");
    }
    return STATUS_FORMAT(IllegalState, "backfill connection to DB failed: $0", libpq_error_message);
  }
  return conn_res;
}

string GenerateSerializedBackfillSpec(size_t batch_size, const string& next_row_to_backfill) {
  PgsqlBackfillSpecPB backfill_spec;
  std::string serialized_backfill_spec;
  // Note that although we set the desired batch_size as the limit, postgres
  // has its own internal paging size of 1024 (controlled by --ysql_prefetch_limit). So the actual
  // rows processed could be larger than the limit set here; unless it happens
  // to be a multiple of FLAGS_ysql_prefetch_limit
  backfill_spec.set_limit(batch_size);
  backfill_spec.set_next_row_key(next_row_to_backfill);
  backfill_spec.SerializeToString(&serialized_backfill_spec);
  VLOG(2) << "Generating backfill_spec " << yb::ToString(backfill_spec)
          << (VLOG_IS_ON(3) ? Format(" encoded as $0 a string of length $1",
                                     b2a_hex(serialized_backfill_spec),
                                     serialized_backfill_spec.length())
                            : "");
  return serialized_backfill_spec;
}

Result<PgsqlBackfillSpecPB> QueryPostgresToDoBackfill(
    pgwrapper::PGConn* conn, const string& query) {
  auto result = conn->Fetch(query);
  if (!result.ok()) {
    const auto libpq_error_msg = AuxilaryMessage(result.status()).value();
    LOG(WARNING) << "libpq query \"" << query << "\" returned "
                 << result.status() << ": " << libpq_error_msg;
    return STATUS(IllegalState, libpq_error_msg);
  }
  auto& res = result.get();
  CHECK_EQ(PQntuples(res.get()), 1);
  CHECK_EQ(PQnfields(res.get()), 1);
  const auto returned_spec = CHECK_RESULT(pgwrapper::GetValue<std::string>(res.get(), 0, 0));
  VLOG(3) << "Got back " << returned_spec << " of length " << returned_spec.length();

  PgsqlBackfillSpecPB spec;
  spec.ParseFromString(a2b_hex(returned_spec));
  return spec;
}

struct BackfillParams {
  explicit BackfillParams(const CoarseTimePoint deadline)
      : start_time(CoarseMonoClock::Now()),
        deadline(deadline),
        rate_per_sec(GetAtomicFlag(&FLAGS_backfill_index_rate_rows_per_sec)),
        batch_size(GetAtomicFlag(&FLAGS_backfill_index_write_batch_size)) {
    auto grace_margin_ms = GetAtomicFlag(&FLAGS_backfill_index_timeout_grace_margin_ms);
    if (grace_margin_ms < 0) {
      // We need: grace_margin_ms >= 1000 * batch_size / rate_per_sec;
      // By default, we will set it to twice the minimum value + 1s.
      grace_margin_ms = (rate_per_sec > 0 ? 1000 * (1 + 2.0 * batch_size / rate_per_sec) : 1000);
      YB_LOG_EVERY_N_SECS(INFO, 10)
          << "Using grace margin of " << grace_margin_ms << "ms, original deadline: "
          << MonoDelta(deadline - start_time);
    }
    modified_deadline = deadline - grace_margin_ms * 1ms;
  }

  CoarseTimePoint start_time;
  CoarseTimePoint deadline;
  size_t rate_per_sec;
  size_t batch_size;
  CoarseTimePoint modified_deadline;
};

// Slow down before the next batch to throttle the rate of processing.
void MaybeSleepToThrottleBackfill(
    const CoarseTimePoint& start_time,
    size_t number_of_rows_processed) {
  if (FLAGS_backfill_index_rate_rows_per_sec <= 0) {
    return;
  }

  auto now = CoarseMonoClock::Now();
  auto duration_for_rows_processed = MonoDelta(now - start_time);
  auto expected_time_for_processing_rows = MonoDelta::FromMilliseconds(
      number_of_rows_processed * 1000 / FLAGS_backfill_index_rate_rows_per_sec);
  DVLOG(3) << "Duration since last batch " << duration_for_rows_processed << " expected duration "
           << expected_time_for_processing_rows << " extra time to sleep: "
           << expected_time_for_processing_rows - duration_for_rows_processed;
  if (duration_for_rows_processed < expected_time_for_processing_rows) {
    SleepFor(expected_time_for_processing_rows - duration_for_rows_processed);
  }
}

bool CanProceedToBackfillMoreRows(
    const BackfillParams& backfill_params,
    size_t number_of_rows_processed) {
  auto now = CoarseMonoClock::Now();
  if (now > backfill_params.modified_deadline ||
      (FLAGS_TEST_backfill_paging_size > 0 &&
       number_of_rows_processed >= FLAGS_TEST_backfill_paging_size)) {
    // We are done if we are out of time.
    // Or, if for testing purposes we have a bound on the size of batches to process.
    return false;
  }
  return true;
}

bool CanProceedToBackfillMoreRows(
    const BackfillParams& backfill_params,
    const string& backfilled_until,
    size_t number_of_rows_processed) {
  if (backfilled_until.empty()) {
    // The backfill is done for this tablet. No need to do another batch.
    return false;
  }

  return CanProceedToBackfillMoreRows(backfill_params, number_of_rows_processed);
}

}  // namespace

// Assume that we are already in the Backfilling mode.
Status Tablet::BackfillIndexesForYsql(
    const std::vector<IndexInfo>& indexes,
    const std::string& backfill_from,
    const CoarseTimePoint deadline,
    const HybridTime read_time,
    const HostPort& pgsql_proxy_bind_address,
    const std::string& database_name,
    const uint64_t postgres_auth_key,
    size_t* number_of_rows_processed,
    std::string* backfilled_until) {
  if (PREDICT_FALSE(FLAGS_TEST_slowdown_backfill_by_ms > 0)) {
    TRACE("Sleeping for $0 ms", FLAGS_TEST_slowdown_backfill_by_ms);
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_backfill_by_ms));
  }
  LOG(INFO) << "Begin " << __func__ << " at " << read_time << " from "
            << (backfill_from.empty() ? "<start-of-the-tablet>" : strings::b2a_hex(backfill_from))
            << " for " << AsString(indexes);
  *backfilled_until = backfill_from;
  BackfillParams backfill_params(deadline);
  auto conn = VERIFY_RESULT(ConnectToPostgres(
    pgsql_proxy_bind_address, database_name, postgres_auth_key, backfill_params.modified_deadline));

  // Construct query string.
  std::string index_oids;
  {
    std::stringstream ss;
    for (auto& index : indexes) {
      // Cannot use Oid type because for large OID such as 2147500041, it overflows Postgres
      // lexer <ival> type. Use int to output as -2147467255 that is accepted by <ival>.
      int index_oid = VERIFY_RESULT(GetPgsqlTableOid(index.table_id()));
      ss << index_oid << ",";
    }
    index_oids = ss.str();
    index_oids.pop_back();
  }
  std::string partition_key = metadata_->partition()->partition_key_start();

  *number_of_rows_processed = 0;
  do {
    std::string serialized_backfill_spec =
        GenerateSerializedBackfillSpec(backfill_params.batch_size, *backfilled_until);

    // This should be safe from injection attacks because the parameters only consist of characters
    // [-,0-9a-f].
    std::string query_str = Format(
        "BACKFILL INDEX $0 WITH x'$1' READ TIME $2 PARTITION x'$3';",
        index_oids,
        b2a_hex(serialized_backfill_spec),
        read_time.ToUint64(),
        b2a_hex(partition_key));
    VLOG(1) << __func__ << ": libpq query string: " << query_str;

    const auto spec = VERIFY_RESULT(QueryPostgresToDoBackfill(&conn, query_str));
    *number_of_rows_processed += spec.count();
    *backfilled_until = spec.next_row_key();

    VLOG(2) << "Backfilled " << *number_of_rows_processed << " rows. "
            << "Setting backfilled_until to "
            << (backfilled_until->empty() ? "(empty)" : b2a_hex(*backfilled_until)) << " of length "
            << backfilled_until->length();

    MaybeSleepToThrottleBackfill(backfill_params.start_time, *number_of_rows_processed);
  } while (CanProceedToBackfillMoreRows(
      backfill_params, *backfilled_until, *number_of_rows_processed));

  VLOG(1) << "Backfilled " << *number_of_rows_processed << " rows. "
          << "Set backfilled_until to "
          << (backfilled_until->empty() ? "(empty)" : b2a_hex(*backfilled_until));
  return Status::OK();
}

std::vector<ColumnId> Tablet::GetColumnSchemasForIndex(
    const std::vector<IndexInfo>& indexes) {
  std::unordered_set<ColumnId> col_ids_set;
  std::vector<ColumnId> columns;

  for (auto id : schema()->column_ids()) {
    if (schema()->is_key_column(id)) {
      col_ids_set.insert(id);
      columns.push_back(id);
    }
  }
  for (const IndexInfo& idx : indexes) {
    for (const auto& idx_col : idx.columns()) {
      if (col_ids_set.insert(idx_col.indexed_column_id).second) {
        columns.push_back(idx_col.indexed_column_id);
      }
    }
    if (idx.where_predicate_spec()) {
      for (const auto col_in_pred : idx.where_predicate_spec()->column_ids()) {
        ColumnId col_id_in_pred(col_in_pred);
        if (col_ids_set.insert(col_id_in_pred).second) {
          columns.push_back(col_id_in_pred);
        }
      }
    }
  }
  return columns;
}

namespace {

std::vector<TableId> GetIndexIds(const std::vector<IndexInfo>& indexes) {
  std::vector<TableId> index_ids;
  for (const IndexInfo& idx : indexes) {
    index_ids.push_back(idx.table_id());
  }
  return index_ids;
}

template <typename SomeVector>
void SleepToThrottleRate(
    SomeVector* index_requests, int32 row_access_rate_per_sec, CoarseTimePoint* last_flushed_at) {
  auto now = CoarseMonoClock::Now();
  if (row_access_rate_per_sec > 0) {
    auto duration_since_last_batch = MonoDelta(now - *last_flushed_at);
    auto expected_duration_ms =
        MonoDelta::FromMilliseconds(index_requests->size() * 1000 / row_access_rate_per_sec);
    DVLOG(3) << "Duration since last batch " << duration_since_last_batch << " expected duration "
             << expected_duration_ms
             << " extra time so sleep: " << expected_duration_ms - duration_since_last_batch;
    if (duration_since_last_batch < expected_duration_ms) {
      SleepFor(expected_duration_ms - duration_since_last_batch);
    }
  }
}

}  // namespace

Result<RequestScope> Tablet::CreateRequestScope() {
  RequestScope scope;
  if (transaction_participant_) {
    return RequestScope::Create(transaction_participant_.get());
  }
  return scope;
}

// Should backfill the index with the information contained in this tablet.
// Assume that we are already in the Backfilling mode.
Status Tablet::BackfillIndexes(
    const std::vector<IndexInfo>& indexes,
    const std::string& backfill_from,
    const CoarseTimePoint deadline,
    const HybridTime read_time,
    size_t* number_of_rows_processed,
    std::string* backfilled_until,
    std::unordered_set<TableId>* failed_indexes) {
  TRACE(__func__);
  if (PREDICT_FALSE(FLAGS_TEST_slowdown_backfill_by_ms > 0)) {
    TRACE("Sleeping for $0 ms", FLAGS_TEST_slowdown_backfill_by_ms);
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_backfill_by_ms));
  }
  VLOG(2) << "Begin BackfillIndexes at " << read_time << " for " << AsString(indexes);

  std::vector<TableId> index_ids = GetIndexIds(indexes);
  auto columns = GetColumnSchemasForIndex(indexes);

  dockv::ReaderProjection projection(*schema(), columns);
  // We must hold this RequestScope for the lifetime of this iterator to ensure backfill has a
  // consistent snapshot of the tablet w.r.t. transaction state.
  RequestScope scope = VERIFY_RESULT(CreateRequestScope());
  auto iter = VERIFY_RESULT(NewRowIterator(
      projection, ReadHybridTime::SingleTime(read_time), "" /* table_id */, deadline,
      docdb::SkipSeek(!backfill_from.empty())));
  QLTableRow row;
  docdb::IndexRequests index_requests;

  BackfillParams backfill_params{deadline};
  constexpr auto kProgressInterval = 1000;

  if (!backfill_from.empty()) {
    VLOG(1) << "Resuming backfill from " << b2a_hex(backfill_from);
    *backfilled_until = backfill_from;
    iter->SeekTuple(Slice(backfill_from));
  }

  string resume_backfill_from;
  *number_of_rows_processed = 0;
  int TEST_number_rows_corrupted = 0;
  int TEST_number_rows_dropped = 0;

  while (VERIFY_RESULT(iter->FetchNext(&row))) {
    if (index_requests.empty()) {
      *backfilled_until = iter->GetTupleId().ToBuffer();
      MaybeSleepToThrottleBackfill(backfill_params.start_time, *number_of_rows_processed);
    }

    if (!CanProceedToBackfillMoreRows(backfill_params, *number_of_rows_processed)) {
      resume_backfill_from = iter->GetTupleId().ToBuffer();
      break;
    }

    if (FLAGS_TEST_backfill_sabotage_frequency > 0 &&
        *number_of_rows_processed % FLAGS_TEST_backfill_sabotage_frequency == 0) {
      VLOG(1) << "Corrupting fetched row: " << row.ToString();
      // Corrupt first key column, since index should not be built on primary key
      row.MarkTombstoned(schema()->column_id(0));
      TEST_number_rows_corrupted++;
    }

    if (FLAGS_TEST_backfill_drop_frequency > 0 &&
        *number_of_rows_processed % FLAGS_TEST_backfill_drop_frequency == 0) {
      (*number_of_rows_processed)++;
      VLOG(1) << "Dropping fetched row: " << row.ToString();
      TEST_number_rows_dropped++;
      continue;
    }

    DVLOG(2) << "Building index for fetched row: " << row.ToString();
    RETURN_NOT_OK(UpdateIndexInBatches(
        row, indexes, read_time, backfill_params.deadline, &index_requests,
        failed_indexes));

    if (++(*number_of_rows_processed) % kProgressInterval == 0) {
      VLOG(1) << "Processed " << *number_of_rows_processed << " rows";
    }
  }
  // Destruct RequestScope once iterator is no longer used to ensure transaction participant can
  // clean-up old transactions.
  scope = RequestScope();

  if (FLAGS_TEST_backfill_sabotage_frequency > 0) {
    LOG(INFO) << "In total, " << TEST_number_rows_corrupted
              << " rows were corrupted in index backfill.";
  }

  if (FLAGS_TEST_backfill_drop_frequency > 0) {
    LOG(INFO) << "In total, " << TEST_number_rows_dropped
              << " rows were dropped in index backfill.";
  }

  VLOG(1) << "Processed " << *number_of_rows_processed << " rows";
  RETURN_NOT_OK(FlushWriteIndexBatch(
      read_time, backfill_params.deadline, &index_requests, failed_indexes));
  MaybeSleepToThrottleBackfill(backfill_params.start_time, *number_of_rows_processed);
  *backfilled_until = resume_backfill_from;
  LOG(INFO) << "Done BackfillIndexes at " << read_time << " for " << AsString(index_ids)
            << " until "
            << (backfilled_until->empty() ? "<end of the tablet>" : b2a_hex(*backfilled_until));
  return Status::OK();
}

Status Tablet::UpdateIndexInBatches(
    const QLTableRow& row,
    const std::vector<IndexInfo>& indexes,
    const HybridTime write_time,
    const CoarseTimePoint deadline,
    docdb::IndexRequests* index_requests,
    std::unordered_set<TableId>* failed_indexes) {
  const QLTableRow& kEmptyRow = QLTableRow::empty_row();
  qlexpr::QLExprExecutor expr_executor;

  for (const IndexInfo& index : indexes) {
    QLWriteRequestPB* const index_request = VERIFY_RESULT(
        docdb::CreateAndSetupIndexInsertRequest(
            &expr_executor, /* index_has_write_permission */ true,
            kEmptyRow, row, &index, index_requests));
    if (index_request)
      index_request->set_is_backfill(true);
  }

  // Update the index write op.
  return FlushWriteIndexBatchIfRequired(write_time, deadline, index_requests, failed_indexes);
}

Result<std::shared_ptr<YBSession>> Tablet::GetSessionForVerifyOrBackfill(
    const CoarseTimePoint deadline) {
  if (!client_future_.valid()) {
    return STATUS_FORMAT(IllegalState, "Client future is not set up for $0", tablet_id());
  }

  auto client = client_future_.get();
  return client->NewSession(deadline);
}

Status Tablet::FlushWriteIndexBatchIfRequired(
    const HybridTime write_time,
    const CoarseTimePoint deadline,
    docdb::IndexRequests* index_requests,
    std::unordered_set<TableId>* failed_indexes) {
  if (index_requests->size() < FLAGS_backfill_index_write_batch_size) {
    return Status::OK();
  }
  return FlushWriteIndexBatch(write_time, deadline, index_requests, failed_indexes);
}

Status Tablet::FlushWriteIndexBatch(
    const HybridTime write_time,
    const CoarseTimePoint deadline,
    docdb::IndexRequests* index_requests,
    std::unordered_set<TableId>* failed_indexes) {
  if (!client_future_.valid()) {
    return STATUS_FORMAT(IllegalState, "Client future is not set up for $0", tablet_id());
  }
  std::shared_ptr<YBSession> session = VERIFY_RESULT(GetSessionForVerifyOrBackfill(deadline));

  std::unordered_set<
      client::YBqlWriteOpPtr, client::YBqlWritePrimaryKeyComparator,
      client::YBqlWritePrimaryKeyComparator>
      ops_by_primary_key;
  std::vector<shared_ptr<client::YBqlWriteOp>> write_ops;

  constexpr int kMaxNumRetries = 10;
  auto metadata_cache = YBMetaDataCache();
  SCHECK(metadata_cache, IllegalState, "Table metadata cache is not present for index update");

  for (auto& pair : *index_requests) {
    auto index_table = VERIFY_RESULT(metadata_cache->GetTable(pair.first->table_id()));

    shared_ptr<client::YBqlWriteOp> index_op(index_table->NewQLWrite());
    index_op->set_write_time_for_backfill(write_time);
    index_op->mutable_request()->Swap(&pair.second);
    if (index_table->IsUniqueIndex()) {
      if (ops_by_primary_key.count(index_op) > 0) {
        VLOG(2) << "Splitting the batch of writes because " << index_op->ToString()
                << " collides with an existing update in this batch.";
        VLOG(1) << "Flushing " << ops_by_primary_key.size() << " ops to the index";
        RETURN_NOT_OK(FlushWithRetries(session, write_ops, kMaxNumRetries, failed_indexes));
        VLOG(3) << "Done flushing ops to the index";
        ops_by_primary_key.clear();
      }
      ops_by_primary_key.insert(index_op);
    }
    session->Apply(index_op);
    write_ops.push_back(index_op);
  }

  VLOG(1) << Format("Flushing $0 ops to the index",
                    (!ops_by_primary_key.empty() ? ops_by_primary_key.size()
                                                 : write_ops.size()));
  RETURN_NOT_OK(FlushWithRetries(session, write_ops, kMaxNumRetries, failed_indexes));
  index_requests->clear();

  return Status::OK();
}

template <typename SomeYBqlOp>
Status Tablet::FlushWithRetries(
    shared_ptr<YBSession> session,
    const std::vector<shared_ptr<SomeYBqlOp>>& index_ops,
    int num_retries,
    std::unordered_set<TableId>* failed_indexes) {
  auto retries_left = num_retries;
  std::vector<std::shared_ptr<SomeYBqlOp>> pending_ops = index_ops;
  std::unordered_map<string, int32_t> error_msg_cnts;
  do {
    std::vector<std::shared_ptr<SomeYBqlOp>> failed_ops;
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    RETURN_NOT_OK_PREPEND(session->TEST_Flush(), "Flush failed.");
    VLOG(3) << "Done flushing ops to the index";
    for (auto index_op : pending_ops) {
      if (index_op->response().status() == QLResponsePB::YQL_STATUS_OK) {
        continue;
      }

      VLOG(2) << "Got response " << AsString(index_op->response()) << " for "
              << AsString(index_op->request());
      if (index_op->response().status() != QLResponsePB::YQL_STATUS_RESTART_REQUIRED_ERROR) {
        failed_indexes->insert(index_op->table()->id());
        const string& error_message = index_op->response().error_message();
        error_msg_cnts[error_message]++;
        VLOG_WITH_PREFIX(3) << "Failing index " << index_op->table()->id()
                            << " due to non-retryable errors " << error_message;
        continue;
      }

      failed_ops.push_back(index_op);
      session->Apply(index_op);
    }

    if (!failed_ops.empty()) {
      VLOG(1) << Format("Flushing $0 failed ops again to the index", failed_ops.size());
    }
    pending_ops = std::move(failed_ops);
  } while (!pending_ops.empty() && --retries_left > 0);

  if (!failed_indexes->empty()) {
    VLOG_WITH_PREFIX(1) << "Failed due to non-retryable errors " << AsString(*failed_indexes);
  }
  if (!pending_ops.empty()) {
    for (auto index_op : pending_ops) {
      failed_indexes->insert(index_op->table()->id());
      const string& error_message = index_op->response().error_message();
      error_msg_cnts[error_message]++;
    }
    VLOG_WITH_PREFIX(1) << "Failed indexes including retryable and non-retryable errors are "
                        << AsString(*failed_indexes);
  }
  return (
      failed_indexes->empty()
          ? Status::OK()
          : STATUS_SUBSTITUTE(
                IllegalState, "Index op failed for $0 requests after $1 retries with errors: $2",
                pending_ops.size(), num_retries, AsString(error_msg_cnts)));
}

Status Tablet::VerifyIndexTableConsistencyForCQL(
    const std::vector<IndexInfo>& indexes,
    const std::string& start_key,
    const int num_rows,
    const CoarseTimePoint deadline,
    const HybridTime read_time,
    std::unordered_map<TableId, uint64>* consistency_stats,
    std::string* verified_until) {
  std::vector<TableId> index_ids = GetIndexIds(indexes);
  auto columns = GetColumnSchemasForIndex(indexes);
  return VerifyTableConsistencyForCQL(
      index_ids, columns, start_key, num_rows, deadline, read_time, false, consistency_stats,
      verified_until);
}

Status Tablet::VerifyMainTableConsistencyForCQL(
    const TableId& main_table_id,
    const std::string& start_key,
    const int num_rows,
    const CoarseTimePoint deadline,
    const HybridTime read_time,
    std::unordered_map<TableId, uint64>* consistency_stats,
    std::string* verified_until) {
  const auto& columns = schema()->column_ids();
  const std::vector<TableId>& table_ids = {main_table_id};
  return VerifyTableConsistencyForCQL(
      table_ids, columns, start_key, num_rows, deadline, read_time, true, consistency_stats,
      verified_until);
}

Status Tablet::VerifyTableConsistencyForCQL(
    const std::vector<TableId>& table_ids,
    const std::vector<ColumnId>& columns,
    const std::string& start_key,
    const int num_rows,
    const CoarseTimePoint deadline,
    const HybridTime read_time,
    const bool is_main_table,
    std::unordered_map<TableId, uint64>* consistency_stats,
    std::string* verified_until) {
  dockv::ReaderProjection projection(*schema(), columns);
  // We must hold this RequestScope for the lifetime of this iterator to ensure verification has a
  // consistent snapshot of the tablet w.r.t. transaction state.
  RequestScope scope = VERIFY_RESULT(CreateRequestScope());
  auto iter = VERIFY_RESULT(NewRowIterator(
      projection, ReadHybridTime::SingleTime(read_time), "" /* table_id */, deadline));

  if (!start_key.empty()) {
    VLOG(2) << "Starting verify index from " << b2a_hex(start_key);
    iter->SeekTuple(Slice(start_key));
  }

  constexpr int kProgressInterval = 1000;
  CoarseTimePoint last_flushed_at;

  QLTableRow row;
  std::vector<std::pair<const TableId, QLReadRequestPB>> requests;
  std::unordered_set<TableId> failed_indexes;
  std::string resume_verified_from;

  int rows_verified = 0;
  while (VERIFY_RESULT(iter->FetchNext(&row)) && rows_verified < num_rows &&
         CoarseMonoClock::Now() < deadline) {
    resume_verified_from = iter->GetTupleId().ToBuffer();
    VLOG(1) << "Verifying index for main table row: " << row.ToString();

    RETURN_NOT_OK(VerifyTableInBatches(
        row, table_ids, read_time, deadline, is_main_table, &requests, &last_flushed_at,
        &failed_indexes, consistency_stats));
    if (++rows_verified % kProgressInterval == 0) {
      VLOG(1) << "Verified " << rows_verified << " rows";
    }
    *verified_until = resume_verified_from;
  }
  // Destruct RequestScope once iterator is no longer used to ensure transaction participant can
  // clean-up old transactions.
  scope = RequestScope();
  return FlushVerifyBatch(
      read_time, deadline, &requests, &last_flushed_at, &failed_indexes, consistency_stats);
}

namespace {

QLConditionPB* InitWhereOp(QLReadRequestPB* req) {
  // Add the hash column values
  DCHECK(req->hashed_column_values().empty());

  // Add the range column values to the where clause
  QLConditionPB* where_pb = req->mutable_where_expr()->mutable_condition();
  if (!where_pb->has_op()) {
    where_pb->set_op(QL_OP_AND);
  }
  DCHECK_EQ(where_pb->op(), QL_OP_AND);
  return where_pb;
}

void SetSelectedExprToTrue(QLReadRequestPB* req) {
  // Set TRUE as selected exprs helps reduce
  // the need for row retrieval in the index read request
  req->add_selected_exprs()->mutable_value()->set_bool_value(true);
  QLRSRowDescPB* rsrow_desc = req->mutable_rsrow_desc();
  QLRSColDescPB* rscol_desc = rsrow_desc->add_rscol_descs();
  rscol_desc->set_name("1");
  rscol_desc->mutable_ql_type()->set_main(PersistentDataType::BOOL);
}

Status WhereMainTableToPB(
    const QLTableRow& key,
    const IndexInfo& index_info,
    const Schema& main_table_schema,
    QLReadRequestPB* req) {
  std::unordered_map<ColumnId, ColumnId> column_id_map;
  for (const auto& col : index_info.columns()) {
    column_id_map.insert({col.indexed_column_id, col.column_id});
  }

  auto column_refs = req->mutable_column_refs();
  QLConditionPB* where_pb = InitWhereOp(req);

  for (const auto& col_id : main_table_schema.column_ids()) {
    if (main_table_schema.is_hash_key_column(col_id)) {
      *req->add_hashed_column_values()->mutable_value() = *key.GetValue(column_id_map[col_id]);
      column_refs->add_ids(col_id);
    } else {
      auto it = column_id_map.find(col_id);
      if (it != column_id_map.end()) {
        QLConditionPB* col_cond_pb = where_pb->add_operands()->mutable_condition();
        col_cond_pb->set_op(QL_OP_EQUAL);
        col_cond_pb->add_operands()->set_column_id(col_id);
        *col_cond_pb->add_operands()->mutable_value() = *key.GetValue(it->second);
        column_refs->add_ids(col_id);
      }
    }
  }

  SetSelectedExprToTrue(req);
  return Status::OK();
}

// Schema is index schema while key is row from main table
Status WhereIndexToPB(
    const QLTableRow& key,
    const IndexInfo& index_info,
    const Schema& schema,
    QLReadRequestPB* req) {
  QLConditionPB* where_pb = InitWhereOp(req);
  auto column_refs = req->mutable_column_refs();

  for (size_t idx = 0; idx < index_info.columns().size(); idx++) {
    const ColumnId& column_id = index_info.column(idx).column_id;
    const ColumnId& indexed_column_id = index_info.column(idx).indexed_column_id;
    if (schema.is_hash_key_column(column_id)) {
      *req->add_hashed_column_values()->mutable_value() = *key.GetValue(indexed_column_id);
    } else {
      QLConditionPB* col_cond_pb = where_pb->add_operands()->mutable_condition();
      col_cond_pb->set_op(QL_OP_EQUAL);
      col_cond_pb->add_operands()->set_column_id(column_id);
      *col_cond_pb->add_operands()->mutable_value() = *key.GetValue(indexed_column_id);
    }
    column_refs->add_ids(column_id);
  }

  SetSelectedExprToTrue(req);
  return Status::OK();
}

}  // namespace

Status Tablet::VerifyTableInBatches(
    const QLTableRow& row,
    const std::vector<TableId>& table_ids,
    const HybridTime read_time,
    const CoarseTimePoint deadline,
    const bool is_main_table,
    std::vector<std::pair<const TableId, QLReadRequestPB>>* requests,
    CoarseTimePoint* last_flushed_at,
    std::unordered_set<TableId>* failed_indexes,
    std::unordered_map<TableId, uint64>* consistency_stats) {
  auto client = client_future_.get();
  auto local_index_info = metadata_->primary_table_info()->index_info.get();
  for (const TableId& table_id : table_ids) {
    std::shared_ptr<client::YBTable> table;
    RETURN_NOT_OK(client->OpenTable(table_id, &table));
    std::shared_ptr<client::YBqlReadOp> read_op(table->NewQLSelect());

    QLReadRequestPB* req = read_op->mutable_request();
    if (is_main_table) {
      RETURN_NOT_OK(WhereMainTableToPB(row, *local_index_info, table->InternalSchema(), req));
    } else {
      RETURN_NOT_OK(WhereIndexToPB(row, table->index_info(), table->InternalSchema(), req));
    }

    requests->emplace_back(table_id, *req);
  }

  return FlushVerifyBatchIfRequired(
      read_time, deadline, requests, last_flushed_at, failed_indexes, consistency_stats);
}

Status Tablet::FlushVerifyBatchIfRequired(
    const HybridTime read_time,
    const CoarseTimePoint deadline,
    std::vector<std::pair<const TableId, QLReadRequestPB>>* requests,
    CoarseTimePoint* last_flushed_at,
    std::unordered_set<TableId>* failed_indexes,
    std::unordered_map<TableId, uint64>* consistency_stats) {
  if (requests->size() < FLAGS_verify_index_read_batch_size) {
    return Status::OK();
  }
  return FlushVerifyBatch(
      read_time, deadline, requests, last_flushed_at, failed_indexes, consistency_stats);
}

Status Tablet::FlushVerifyBatch(
    const HybridTime read_time,
    const CoarseTimePoint deadline,
    std::vector<std::pair<const TableId, QLReadRequestPB>>* requests,
    CoarseTimePoint* last_flushed_at,
    std::unordered_set<TableId>* failed_indexes,
    std::unordered_map<TableId, uint64>* consistency_stats) {
  std::vector<client::YBqlReadOpPtr> read_ops;
  std::shared_ptr<YBSession> session = VERIFY_RESULT(GetSessionForVerifyOrBackfill(deadline));

  auto client = client_future_.get();
  for (auto& pair : *requests) {
    client::YBTablePtr table;
    RETURN_NOT_OK(client->OpenTable(pair.first, &table));

    client::YBqlReadOpPtr read_op(table->NewQLRead());
    read_op->mutable_request()->Swap(&pair.second);
    read_op->SetReadTime(ReadHybridTime::SingleTime(read_time));

    session->Apply(read_op);

    // Note: always emplace at tail because row keys must
    // correspond sequentially with the read_ops in the vector
    read_ops.push_back(read_op);
  }

  RETURN_NOT_OK(FlushWithRetries(session, read_ops, 0, failed_indexes));

  for (size_t idx = 0; idx < requests->size(); idx++) {
    const client::YBqlReadOpPtr& read_op = read_ops[idx];
    auto row_block = read_op->MakeRowBlock();
    if (row_block && row_block->row_count() == 1) continue;
    (*consistency_stats)[read_op->table()->id()]++;
  }

  SleepToThrottleRate(requests, FLAGS_verify_index_rate_rows_per_sec, last_flushed_at);
  *last_flushed_at = CoarseMonoClock::Now();
  requests->clear();

  return Status::OK();
}

ScopedRWOperationPause Tablet::PauseReadWriteOperations(
    BlockingRocksDbShutdownStart blocking_rocksdb_shutdown_start, const Stop stop) {
  VTRACE(1, LogPrefix());
  LOG_SLOW_EXECUTION(WARNING, 1000,
                     Substitute("$0Waiting for pending ops to complete", LogPrefix())) {
    return ScopedRWOperationPause(
        blocking_rocksdb_shutdown_start ? &pending_op_counter_blocking_rocksdb_shutdown_start_
                                        : &pending_op_counter_not_blocking_rocksdb_shutdown_start_,
        CoarseTimePoint::max(), stop);
  }
  FATAL_ERROR("Unreachable code -- the previous block must always return");
}

ScopedRWOperation Tablet::CreateScopedRWOperationNotBlockingRocksDbShutdownStart(
    const CoarseTimePoint deadline) const {
  return ScopedRWOperation(
      &pending_op_counter_not_blocking_rocksdb_shutdown_start_, abort_pending_op_status_holder_,
      deadline);
}

ScopedRWOperation Tablet::CreateScopedRWOperationBlockingRocksDbShutdownStart(
    const CoarseTimePoint deadline) const {
  return ScopedRWOperation(&pending_op_counter_blocking_rocksdb_shutdown_start_, deadline);
}

Status Tablet::ModifyFlushedFrontier(
    const docdb::ConsensusFrontier& frontier,
    rocksdb::FrontierModificationMode mode,
    FlushFlags flags) {
  const Status s = regular_db_->ModifyFlushedFrontier(frontier.Clone(), mode);
  if (PREDICT_FALSE(!s.ok())) {
    auto status = STATUS(IllegalState, "Failed to set flushed frontier", s.ToString());
    LOG_WITH_PREFIX(WARNING) << status;
    return status;
  }
  {
    auto flushed_frontier = regular_db_->GetFlushedFrontier();
    const auto& consensus_flushed_frontier = *down_cast<docdb::ConsensusFrontier*>(
        flushed_frontier.get());
    DCHECK_EQ(frontier.op_id(), consensus_flushed_frontier.op_id());
    DCHECK_EQ(frontier.hybrid_time(), consensus_flushed_frontier.hybrid_time());
  }

  if (FLAGS_TEST_tablet_verify_flushed_frontier_after_modifying &&
      mode == rocksdb::FrontierModificationMode::kForce) {
    LOG(INFO) << "Verifying that flushed frontier was force-set successfully";
    string test_data_dir = VERIFY_RESULT(Env::Default()->GetTestDirectory());
    const string checkpoint_dir_for_test = Format(
        "$0/test_checkpoint_$1_$2", test_data_dir, tablet_id(), MonoTime::Now().ToUint64());
    RETURN_NOT_OK(
        rocksdb::checkpoint::CreateCheckpoint(regular_db_.get(), checkpoint_dir_for_test));
    auto se = ScopeExit([checkpoint_dir_for_test] {
      CHECK_OK(Env::Default()->DeleteRecursively(checkpoint_dir_for_test));
    });
    rocksdb::Options rocksdb_options;
    docdb::InitRocksDBOptions(
        &rocksdb_options, LogPrefix(), /* statistics */ nullptr, tablet_options_,
        rocksdb::BlockBasedTableOptions(), hash_for_data_root_dir(metadata_->data_root_dir()));
    rocksdb_options.create_if_missing = false;
    LOG_WITH_PREFIX(INFO) << "Opening the test RocksDB at " << checkpoint_dir_for_test
        << ", expecting to see flushed frontier of " << frontier.ToString();
    std::unique_ptr<rocksdb::DB> test_db = VERIFY_RESULT(
        rocksdb::DB::Open(rocksdb_options, checkpoint_dir_for_test));
    LOG_WITH_PREFIX(INFO) << "Getting flushed frontier from test RocksDB at "
                          << checkpoint_dir_for_test;
    auto restored_flushed_frontier = test_db->GetFlushedFrontier();
    if (!restored_flushed_frontier) {
      LOG_WITH_PREFIX(FATAL) << LogPrefix() << "Restored flushed frontier not present";
    }
    CHECK_EQ(
        frontier,
        down_cast<docdb::ConsensusFrontier&>(*restored_flushed_frontier));
    LOG_WITH_PREFIX(INFO) << "Successfully verified persistently stored flushed frontier: "
        << frontier.ToString();
  }

  if (intents_db_) {
    // It is OK to flush intents even if the regular DB is not yet flushed,
    // because it would wait for flush of regular DB if we have unflushed intents.
    // Otherwise it does not matter which flushed op id is stored.
    RETURN_NOT_OK(intents_db_->ModifyFlushedFrontier(frontier.Clone(), mode));
  }

  return Flush(FlushMode::kAsync, flags);
}

Status Tablet::Truncate(TruncateOperation* operation) {
  if (metadata_->table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    // We use only Raft log for transaction status table.
    return Status::OK();
  }

  auto op_pauses = StartShutdownRocksDBs(DisableFlushOnShutdown::kTrue, AbortOps::kTrue);

  // Check if tablet is in shutdown mode.
  if (IsShutdownRequested()) {
    return STATUS(IllegalState, "Tablet was shut down");
  }

  const rocksdb::SequenceNumber sequence_number = regular_db_->GetLatestSequenceNumber();
  const string db_dir = regular_db_->GetName();

  auto s = DeleteRocksDBs(CompleteShutdownRocksDBs(op_pauses));
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Failed to clean up db dir " << db_dir << ": " << s;
    return STATUS(IllegalState, "Failed to clean up db dir", s.ToString());
  }

  // Create a new database.
  // Note: db_dir == metadata()->rocksdb_dir() is still valid db dir.
  s = OpenKeyValueTablet();
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Failed to create a new db: " << s;
    return s;
  }

  docdb::ConsensusFrontier frontier;
  frontier.set_op_id(operation->op_id());
  frontier.set_hybrid_time(operation->hybrid_time());
  // We use the kUpdate mode here, because unlike the case of restoring a snapshot to a completely
  // different tablet in an arbitrary Raft group, here there is no possibility of the flushed
  // frontier needing to go backwards.
  RETURN_NOT_OK(ModifyFlushedFrontier(
      frontier, rocksdb::FrontierModificationMode::kUpdate,
      FlushFlags::kAllDbs | FlushFlags::kNoScopedOperation));

  LOG_WITH_PREFIX(INFO) << "Created new db for truncated tablet";
  LOG_WITH_PREFIX(INFO) << "Sequence numbers: old=" << sequence_number
                        << ", new=" << regular_db_->GetLatestSequenceNumber();

  // Ensure that op_pauses stays in scope throughout this function.
  for (auto* op_pause : op_pauses.AsArray()) {
    LOG_IF(FATAL, !op_pause->ok()) << op_pause->status();
  }
  return DoEnableCompactions();
}

void Tablet::UpdateMonotonicCounter(int64_t value) {
  int64_t counter = monotonic_counter_;
  while (true) {
    if (counter >= value) {
      break;
    }
    if (monotonic_counter_.compare_exchange_weak(counter, value)) {
      break;
    }
  }
}

////////////////////////////////////////////////////////////
// Tablet
////////////////////////////////////////////////////////////

Result<bool> Tablet::HasSSTables() const {
  if (!regular_db_) {
    return false;
  }

  auto scoped_read_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_read_operation);

  std::vector<rocksdb::LiveFileMetaData> live_files_metadata;
  regular_db_->GetLiveFilesMetaData(&live_files_metadata);
  return !live_files_metadata.empty();
}

yb::OpId MaxPersistentOpIdForDb(rocksdb::DB* db, bool invalid_if_no_new_data) {
  // A possible race condition could happen, when data is written between this query and
  // actual log gc. But it is not a problem as long as we are reading committed op id
  // before MaxPersistentOpId, since we always keep last committed entry in the log during garbage
  // collection.
  // See TabletPeer::GetEarliestNeededLogIndex
  if (db == nullptr ||
      (invalid_if_no_new_data &&
       db->GetFlushAbility() == rocksdb::FlushAbility::kNoNewData)) {
    return yb::OpId::Invalid();
  }

  rocksdb::UserFrontierPtr frontier = db->GetFlushedFrontier();
  if (!frontier) {
    return yb::OpId();
  }

  return down_cast<docdb::ConsensusFrontier*>(frontier.get())->op_id();
}

Result<DocDbOpIds> Tablet::MaxPersistentOpId(bool invalid_if_no_new_data) const {
  auto scoped_read_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_read_operation);

  return DocDbOpIds{
      MaxPersistentOpIdForDb(regular_db_.get(), invalid_if_no_new_data),
      MaxPersistentOpIdForDb(intents_db_.get(), invalid_if_no_new_data)
  };
}

void Tablet::FlushIntentsDbIfNecessary(const yb::OpId& lastest_log_entry_op_id) {
  auto scoped_read_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  if (!scoped_read_operation.ok()) {
    return;
  }

  auto intents_frontier = intents_db_
                              ? GetMutableMemTableFrontierFromDb(
                                    intents_db_.get(), rocksdb::UpdateUserValueType::kLargest)
                              : nullptr;
  if (intents_frontier) {
    auto index_delta =
        lastest_log_entry_op_id.index -
        down_cast<docdb::ConsensusFrontier*>(intents_frontier.get())->op_id().index;
    if (index_delta > FLAGS_num_raft_ops_to_force_idle_intents_db_to_flush) {
      auto intents_flush_ability = intents_db_->GetFlushAbility();
      if (intents_flush_ability == rocksdb::FlushAbility::kHasNewData) {
        LOG_WITH_PREFIX(INFO)
            << "Force flushing intents DB since it was not flushed for " << index_delta
            << " operations, while only "
            << FLAGS_num_raft_ops_to_force_idle_intents_db_to_flush << " is allowed";
        rocksdb::FlushOptions options;
        options.wait = false;
        WARN_NOT_OK(intents_db_->Flush(options), "Flush intents db failed");
      }
    }
  }
}

bool Tablet::IsTransactionalRequest(bool is_ysql_request) const {
  // We consider all YSQL tables within the sys catalog transactional.
  return txns_enabled_ && (
      schema()->table_properties().is_transactional() ||
          (is_sys_catalog_ && is_ysql_request));
}

Result<HybridTime> Tablet::MaxPersistentHybridTime() const {
  auto scoped_read_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_read_operation);

  if (!regular_db_) {
    return HybridTime::kMin;
  }

  HybridTime result = HybridTime::kMin;
  auto temp = regular_db_->GetFlushedFrontier();
  if (temp) {
    result.MakeAtLeast(down_cast<docdb::ConsensusFrontier*>(temp.get())->hybrid_time());
  }
  if (intents_db_) {
    temp = intents_db_->GetFlushedFrontier();
    if (temp) {
      result.MakeAtLeast(down_cast<docdb::ConsensusFrontier*>(temp.get())->hybrid_time());
    }
  }
  return result;
}

Result<HybridTime> Tablet::OldestMutableMemtableWriteHybridTime() const {
  auto scoped_read_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_read_operation);

  HybridTime result = HybridTime::kMax;
  for (auto* db : { regular_db_.get(), intents_db_.get() }) {
    if (db) {
      auto mem_frontier =
          GetMutableMemTableFrontierFromDb(db, rocksdb::UpdateUserValueType::kSmallest);
      if (mem_frontier) {
        const auto hybrid_time =
            static_cast<const docdb::ConsensusFrontier&>(*mem_frontier).hybrid_time();
        result = std::min(result, hybrid_time);
      }
    }
  }
  return result;
}

const yb::SchemaPtr Tablet::schema() const {
  return metadata_->schema();
}

Status Tablet::DebugDump(vector<string> *lines) {
  switch (table_type_) {
    case TableType::PGSQL_TABLE_TYPE: FALLTHROUGH_INTENDED;
    case TableType::YQL_TABLE_TYPE: FALLTHROUGH_INTENDED;
    case TableType::REDIS_TABLE_TYPE:
      DocDBDebugDump(lines);
      return Status::OK();
    case TableType::TRANSACTION_STATUS_TABLE_TYPE:
      return Status::OK();
  }
  FATAL_INVALID_ENUM_VALUE(TableType, table_type_);
}

void Tablet::DocDBDebugDump(vector<string> *lines) {
  LOG_STRING(INFO, lines) << "Dumping tablet:";
  LOG_STRING(INFO, lines) << "---------------------------";
  docdb::DocDBDebugDump(
      regular_db_.get(), LOG_STRING(INFO, lines), &GetSchemaPackingProvider(),
      docdb::StorageDbType::kRegular);
}

Status Tablet::TEST_SwitchMemtable() {
  auto scoped_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_operation);

  if (regular_db_) {
    regular_db_->TEST_SwitchMemtable();
  } else {
    LOG_WITH_PREFIX(INFO) << "Ignoring TEST_SwitchMemtable: no regular RocksDB";
  }
  return Status::OK();
}

Result<HybridTime> Tablet::DoGetSafeTime(
    RequireLease require_lease, HybridTime min_allowed, CoarseTimePoint deadline) const {
  TEST_PAUSE_IF_FLAG(TEST_pause_before_getting_safe_time);
  if (require_lease == RequireLease::kFalse) {
    return CheckSafeTime(mvcc_.SafeTimeForFollower(min_allowed, deadline), min_allowed);
  }
  FixedHybridTimeLease ht_lease;
  if (ht_lease_provider_) {
    // This will block until a leader lease reaches the given value or a timeout occurs.
    auto ht_lease_result = ht_lease_provider_(min_allowed, deadline);
    if (!ht_lease_result.ok()) {
      if (require_lease == RequireLease::kFallbackToFollower &&
          ht_lease_result.status().IsIllegalState()) {
        return CheckSafeTime(mvcc_.SafeTimeForFollower(min_allowed, deadline), min_allowed);
      }
      return ht_lease_result.status();
    }
    ht_lease = *ht_lease_result;
    if (min_allowed > ht_lease.time) {
      return STATUS_FORMAT(
          InternalError, "Read request hybrid time after current time: $0, lease: $1",
          min_allowed, ht_lease);
    }
  } else if (min_allowed) {
    RETURN_NOT_OK(WaitUntil(clock_.get(), min_allowed, deadline));
  }
  if (min_allowed > ht_lease.lease) {
    return STATUS_FORMAT(
        InternalError, "Read request hybrid time after leader lease: $0, lease: $1",
        min_allowed, ht_lease);
  }
  return CheckSafeTime(mvcc_.SafeTime(min_allowed, deadline, ht_lease), min_allowed);
}

ScopedRWOperationPause Tablet::PauseWritePermits(CoarseTimePoint deadline) {
  TRACE("Blocking write permit(s)");
  auto se = ScopeExit([] { TRACE("Blocking write permit(s) done"); });
  // Prevent new write ops from being submitted.
  return ScopedRWOperationPause(&write_ops_being_submitted_counter_, deadline, Stop::kFalse);
}

ScopedRWOperation Tablet::GetPermitToWrite(CoarseTimePoint deadline) {
  TRACE("Acquiring write permit");
  auto se = ScopeExit([] { TRACE("Acquiring write permit done"); });
  return ScopedRWOperation(&write_ops_being_submitted_counter_, deadline);
}

Result<bool> Tablet::StillHasOrphanedPostSplitData() {
  auto scoped_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_operation);
  return StillHasOrphanedPostSplitDataAbortable();
}

bool Tablet::StillHasOrphanedPostSplitDataAbortable() {
  return key_bounds().IsInitialized() && !metadata()->parent_data_compacted();
}

bool Tablet::MayHaveOrphanedPostSplitData() {
  auto res = StillHasOrphanedPostSplitData();
  if (!res.ok()) {
    LOG(WARNING) << "Failed to call StillHasOrphanedPostSplitData: " << res.ToString();
    return true;
  }
  return res.get();
}

bool Tablet::ShouldDisableLbMove() {
  auto still_has_parent_data_result = StillHasOrphanedPostSplitData();
  if (still_has_parent_data_result.ok()) {
    return still_has_parent_data_result.get();
  }
  // If this call failed, one of three things may be true:
  // 1. We are in the middle of a tablet shutdown.
  //
  // In this case, what we report is not of much consequence, as the load balancer shouldn't try to
  // move us anyways. We choose to return false.
  //
  // 2. We are in the middle of a TRUNCATE.
  //
  // In this case, any concurrent attempted LB move should fail before trying to move data,
  // since the RocksDB instances are destroyed. On top of that, we do want to allow the LB to move
  // this tablet after the TRUNCATE completes, so we should return false.
  //
  // 3. We are in the middle of an AlterSchema operation. This is only true for tablets belonging to
  //    colocated tables.
  //
  // In this case, we want to disable tablet moves. We conservatively return true for any failure
  // if the tablet is part of a colocated table.
  return metadata_->schema()->has_colocation_id();
}

Status Tablet::ForceManualRocksDBCompact(docdb::SkipFlush skip_flush) {
  return ForceRocksDBCompact(rocksdb::CompactionReason::kManualCompaction, skip_flush);
}

Status Tablet::ForceRocksDBCompact(
    rocksdb::CompactionReason compaction_reason, docdb::SkipFlush skip_flush) {
  rocksdb::CompactRangeOptions options;
  options.skip_flush = skip_flush;
  options.compaction_reason = compaction_reason;
  if (compaction_reason != rocksdb::CompactionReason::kPostSplitCompaction) {
    options.exclusive_manual_compaction = FLAGS_tablet_exclusive_full_compaction;
    return ForceRocksDBCompact(options, options);
  }

  // Specific handling for post split compaction.
  options.exclusive_manual_compaction = FLAGS_tablet_exclusive_post_split_compaction;

  // We may want to modify options for regular db compaction, let's do a copy.
  auto regular_options = options;
  if (regular_db_) {
    // Our expectations at this point:
    // 1) If parent_data_compacted then we don't want to compact again.
    // 2) If file_number_upper_bound is not set, then we don't setup limited compaction.
    // 3) If file_number_upper_bound is 0 and !parent_data_compacted then is looks like a bug,
    //    but we still want to compact to have parent_data_compacted.
    // (1) and (3) are possible due to async nature of triggereing ForceRocksDBCompact()
    // via TriggerPostSplitCompactionIfNeeded().
    const auto parent_data_compacted = metadata()->parent_data_compacted();
    if (parent_data_compacted) {
      LOG_WITH_PREFIX(WARNING) << "Ignoring post split compaction as "
                               << "parent data have already been compacted.";
      return Status::OK();
    }

    const auto file_number_upper_bound =
        metadata()->post_split_compaction_file_number_upper_bound();
    LOG_IF(DFATAL, file_number_upper_bound == 0) <<
          "It is unexpected to have file_number_upper_bound == 0 when parent data has not been "
          "compacted yet, it is an inconsistent state. Please check the logic. Ignoring "
          "file_number_upper_bound as if it is not specified.";

    const auto input_size_threshold = FLAGS_post_split_compaction_input_size_threshold_bytes;
    if (file_number_upper_bound > 0 && input_size_threshold > 0) {
      regular_options.file_number_upper_bound = *file_number_upper_bound;
      regular_options.input_size_limit_per_job = input_size_threshold;
    }
  }

  return ForceRocksDBCompact(regular_options, options);
}

Status Tablet::ForceRocksDBCompact(
    const rocksdb::CompactRangeOptions& regular_options,
    const rocksdb::CompactRangeOptions& intents_options) {
  auto scoped_operation = CreateScopedRWOperationNotBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_operation);
  if (regular_db_) {
    RETURN_NOT_OK(docdb::ForceRocksDBCompact(regular_db_.get(), regular_options));
  }
  if (intents_db_) {
    if (!intents_options.skip_flush) {
      RETURN_NOT_OK_PREPEND(
          intents_db_->Flush(rocksdb::FlushOptions()), "Pre-compaction flush of intents db failed");
    }
    RETURN_NOT_OK(docdb::ForceRocksDBCompact(intents_db_.get(), intents_options));
  }
  return Status::OK();
}

std::string Tablet::TEST_DocDBDumpStr(IncludeIntents include_intents) {
  if (!regular_db_) return "";

  if (!include_intents) {
    return docdb::DocDBDebugDumpToStr(doc_db().WithoutIntents(),
        &GetSchemaPackingProvider());
  }

  return docdb::DocDBDebugDumpToStr(doc_db(), &GetSchemaPackingProvider());
}

void Tablet::TEST_DocDBDumpToContainer(
    IncludeIntents include_intents, std::unordered_set<std::string>* out) {
  if (!regular_db_) return;

  if (!include_intents) {
    return docdb::DocDBDebugDumpToContainer(doc_db().WithoutIntents(),
        &GetSchemaPackingProvider(), out);
  }

  return docdb::DocDBDebugDumpToContainer(doc_db(), &GetSchemaPackingProvider(), out);
}

void Tablet::TEST_DocDBDumpToLog(IncludeIntents include_intents) {
  if (!regular_db_) {
    LOG_WITH_PREFIX(INFO) << "No RocksDB to dump";
    return;
  }

  docdb::DumpRocksDBToLog(
      regular_db_.get(), &GetSchemaPackingProvider(), StorageDbType::kRegular, LogPrefix());

  if (include_intents && intents_db_) {
    docdb::DumpRocksDBToLog(
        intents_db_.get(), &GetSchemaPackingProvider(), StorageDbType::kIntents, LogPrefix());
  }
}

Result<size_t> Tablet::TEST_CountRegularDBRecords() {
  if (!regular_db_) return 0;
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  docdb::BoundedRocksDbIterator iter(regular_db_.get(), read_opts, &key_bounds_);

  size_t result = 0;
  for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
    ++result;
  }
  RETURN_NOT_OK(iter.status());
  return result;
}

template <class F>
auto Tablet::GetRegularDbStat(const F& func, const decltype(func())& default_value) const {
  auto scoped_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  std::lock_guard lock(component_lock_);

  // In order to get actual stats we would have to wait.
  // This would give us correct stats but would make this request slower.
  if (!scoped_operation.ok() || !regular_db_) {
    return default_value;
  }
  return func();
}

uint64_t Tablet::GetCurrentVersionSstFilesSize() const {
  return GetRegularDbStat([this] {
    return regular_db_->GetCurrentVersionSstFilesSize();
  }, 0);
}

uint64_t Tablet::GetCurrentVersionSstFilesUncompressedSize() const {
  return GetRegularDbStat([this] {
    return regular_db_->GetCurrentVersionSstFilesUncompressedSize();
  }, 0);
}

std::pair<uint64_t, uint64_t> Tablet::GetCurrentVersionSstFilesAllSizes() const {
  return GetRegularDbStat([this] {
    return regular_db_->GetCurrentVersionSstFilesAllSizes();
  }, std::pair<uint64_t, uint64_t>(0, 0));
}

uint64_t Tablet::GetCurrentVersionNumSSTFiles() const {
  return GetRegularDbStat([this] {
    return regular_db_->GetCurrentVersionNumSSTFiles();
  }, 0);
}

std::pair<int, int> Tablet::GetNumMemtables() const {
  int intents_num_memtables = 0;
  int regular_num_memtables = 0;

  {
    auto scoped_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
    if (!scoped_operation.ok()) {
      return std::make_pair(0, 0);
    }
    std::lock_guard lock(component_lock_);
    if (intents_db_) {
      // NOTE: 1 is added on behalf of cfd->mem().
      intents_num_memtables = 1 + intents_db_->GetCfdImmNumNotFlushed();
    }
    if (regular_db_) {
      // NOTE: 1 is added on behalf of cfd->mem().
      regular_num_memtables = 1 + regular_db_->GetCfdImmNumNotFlushed();
    }
  }

  return std::make_pair(intents_num_memtables, regular_num_memtables);
}

// ------------------------------------------------------------------------------------------------

Result<TransactionOperationContext> Tablet::CreateTransactionOperationContext(
    const TransactionMetadataPB& transaction_metadata,
    bool is_ysql_catalog_table,
    const SubTransactionMetadataPB* subtransaction_metadata) const {
  if (!txns_enabled_) {
    return TransactionOperationContext();
  }

  if (transaction_metadata.has_transaction_id()) {
    auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(
        transaction_metadata.transaction_id()));
    return CreateTransactionOperationContext(
        boost::make_optional(txn_id), is_ysql_catalog_table, subtransaction_metadata);
  } else {
    return CreateTransactionOperationContext(
        /* transaction_id */ boost::none, is_ysql_catalog_table, subtransaction_metadata);
  }
}

Result<TransactionOperationContext> Tablet::CreateTransactionOperationContext(
    const boost::optional<TransactionId>& transaction_id,
    bool is_ysql_catalog_table,
    const SubTransactionMetadataPB* subtransaction_metadata) const {
  if (!txns_enabled_) {
    return TransactionOperationContext();
  }

  const TransactionId* txn_id = nullptr;

  if (transaction_id.is_initialized()) {
    txn_id = transaction_id.get_ptr();
  } else if (metadata_->schema()->table_properties().is_transactional() || is_ysql_catalog_table) {
    // deadbeef-dead-beef-dead-beef00000075
    static const TransactionId kArbitraryTxnIdForNonTxnReads(
        17275436393656397278ULL, 8430738506459819486ULL);
    // We still need context with transaction participant in order to resolve intents during
    // possible reads.
    txn_id = &kArbitraryTxnIdForNonTxnReads;
  } else {
    return TransactionOperationContext();
  }

  if (!transaction_participant_) {
    return STATUS(IllegalState, "Transactional operation for non transactional tablet");
  }

  if (!subtransaction_metadata) {
    return TransactionOperationContext(*txn_id, transaction_participant());
  }

  auto subtxn = VERIFY_RESULT(SubTransactionMetadata::FromPB(*subtransaction_metadata));
  return TransactionOperationContext(*txn_id, std::move(subtxn), transaction_participant());
}

Status Tablet::CreateReadIntents(
    IsolationLevel level,
    const TransactionMetadataPB& transaction_metadata,
    const SubTransactionMetadataPB& subtransaction_metadata,
    const google::protobuf::RepeatedPtrField<QLReadRequestPB>& ql_batch,
    const google::protobuf::RepeatedPtrField<PgsqlReadRequestPB>& pgsql_batch,
    docdb::LWKeyValueWriteBatchPB* write_batch) {
  auto txn_op_ctx = VERIFY_RESULT(CreateTransactionOperationContext(
      transaction_metadata,
      /* is_ysql_catalog_table */ pgsql_batch.size() > 0 && is_sys_catalog_,
      &subtransaction_metadata));

  auto table_info = metadata_->primary_table_info();
  for (const auto& ql_read : ql_batch) {
    docdb::QLReadOperation doc_op(ql_read, txn_op_ctx);
    RETURN_NOT_OK(doc_op.GetIntents(table_info->schema(), write_batch));
  }

  for (const auto& pgsql_read : pgsql_batch) {
    if (table_info == nullptr || table_info->table_id != pgsql_read.table_id()) {
      table_info = VERIFY_RESULT(metadata_->GetTableInfo(pgsql_read.table_id()));
    }
    docdb::PgsqlReadOperation doc_op(pgsql_read, txn_op_ctx);
    RETURN_NOT_OK(doc_op.GetIntents(table_info->schema(), level, write_batch));
  }

  return Status::OK();
}

bool Tablet::ShouldApplyWrite() {
  auto scoped_read_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  if (!scoped_read_operation.ok()) {
    return false;
  }

  return !regular_db_->NeedsDelay();
}

Result<IsolationLevel> Tablet::GetIsolationLevel(const TransactionMetadataPB& transaction) {
  return DoGetIsolationLevel(transaction);
}

Result<IsolationLevel> Tablet::GetIsolationLevel(const LWTransactionMetadataPB& transaction) {
  return DoGetIsolationLevel(transaction);
}

template <class PB>
Result<IsolationLevel> Tablet::DoGetIsolationLevel(const PB& transaction) {
  if (transaction.has_isolation()) {
    return transaction.isolation();
  }
  return VERIFY_RESULT(transaction_participant_->PrepareMetadata(transaction)).isolation;
}

Result<RaftGroupMetadataPtr> Tablet::CreateSubtablet(
    const TabletId& tablet_id, const dockv::Partition& partition,
    const docdb::KeyBounds& key_bounds, const OpId& split_op_id,
    const HybridTime& split_op_hybrid_time) {
  SCHECK(key_bounds.IsInitialized(), IllegalState, "Key bounds must be set");

  auto scoped_read_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_read_operation);

  RETURN_NOT_OK(Flush(FlushMode::kSync));

  auto metadata = VERIFY_RESULT(metadata_->CreateSubtabletMetadata(
      tablet_id, partition, key_bounds.lower.ToStringBuffer(), key_bounds.upper.ToStringBuffer()));

  RETURN_NOT_OK(snapshots_->CreateCheckpoint(
      metadata->rocksdb_dir(), CreateIntentsCheckpointIn::kSubDir));

  // We want flushed frontier to cover split_op_id, so during bootstrap of after-split tablets
  // we don't replay split operation.
  docdb::ConsensusFrontier frontier;
  frontier.set_op_id(split_op_id);
  frontier.set_hybrid_time(split_op_hybrid_time);

  struct RocksDbDirWithType {
    std::string db_dir;
    docdb::StorageDbType db_type;
  };
  boost::container::static_vector<RocksDbDirWithType, 2> subtablet_rocksdbs(
      {{ metadata->rocksdb_dir(), docdb::StorageDbType::kRegular }});
  if (intents_db_) {
    subtablet_rocksdbs.push_back(
        { metadata->intents_rocksdb_dir(), docdb::StorageDbType::kIntents });
  }
  for (const auto& db_info : subtablet_rocksdbs) {
    rocksdb::Options rocksdb_options;
    docdb::InitRocksDBOptions(
        &rocksdb_options, MakeTabletLogPrefix(tablet_id, log_prefix_suffix_, db_info.db_type),
        /* statistics */ nullptr, tablet_options_, rocksdb::BlockBasedTableOptions(),
        hash_for_data_root_dir(metadata->data_root_dir()));
    rocksdb_options.create_if_missing = false;
    // Disable background compactions, we only need to update flushed frontier.
    rocksdb_options.compaction_style = rocksdb::CompactionStyle::kCompactionStyleNone;
    std::unique_ptr<rocksdb::DB> db =
        VERIFY_RESULT(rocksdb::DB::Open(rocksdb_options, db_info.db_dir));
    RETURN_NOT_OK(
        db->ModifyFlushedFrontier(frontier.Clone(), rocksdb::FrontierModificationMode::kUpdate));

    // Update meta with file number upper bound for post split compaction purpose.
    if (db_info.db_type == docdb::StorageDbType::kRegular) {
      const auto file_number_upper_bound = db->GetNextFileNumber();
      metadata->set_post_split_compaction_file_number_upper_bound(file_number_upper_bound);
      RETURN_NOT_OK(metadata->Flush());
    }
  }
  return metadata;
}

Result<int64_t> Tablet::CountIntents() {
  auto pending_op = CreateScopedRWOperationNotBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(pending_op);

  if (!intents_db_) {
    return 0;
  }
  rocksdb::ReadOptions read_options;
  auto intent_iter = std::unique_ptr<rocksdb::Iterator>(
      intents_db_->NewIterator(read_options));
  int64_t num_intents = 0;
  intent_iter->SeekToFirst();
  while (intent_iter->Valid()) {
    num_intents++;
    intent_iter->Next();
  }
  RETURN_NOT_OK(intent_iter->status());
  return num_intents;
}

Status Tablet::ReadIntents(std::vector<std::string>* intents) {
  auto pending_op = CreateScopedRWOperationNotBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(pending_op);

  if (!intents_db_) {
    return Status::OK();
  }

  rocksdb::ReadOptions read_options;
  auto intent_iter = std::unique_ptr<rocksdb::Iterator>(
      intents_db_->NewIterator(read_options));
  intent_iter->SeekToFirst();
  auto* schema_packing_provider = &GetSchemaPackingProvider();

  for (; intent_iter->Valid(); intent_iter->Next()) {
    auto item = EntryToString(
        intent_iter->key(), intent_iter->value(), schema_packing_provider,
        docdb::StorageDbType::kIntents);
    intents->push_back(item);
  }

  return intent_iter->status();
}

void Tablet::ListenNumSSTFilesChanged(std::function<void()> listener) {
  std::lock_guard lock(num_sst_files_changed_listener_mutex_);
  bool has_new_listener = listener != nullptr;
  bool has_old_listener = num_sst_files_changed_listener_ != nullptr;
  LOG_IF_WITH_PREFIX(DFATAL, has_new_listener == has_old_listener)
      << __func__ << " in wrong state, has_old_listener: " << has_old_listener;
  num_sst_files_changed_listener_ = std::move(listener);
}

void Tablet::InitRocksDBOptions(
    rocksdb::Options* options, const std::string& log_prefix,
    rocksdb::BlockBasedTableOptions table_options) {
  docdb::InitRocksDBOptions(
      options, log_prefix, regulardb_statistics_, tablet_options_, std::move(table_options),
      hash_for_data_root_dir(metadata_->data_root_dir()));
}

rocksdb::Env& Tablet::rocksdb_env() const {
  return *tablet_options_.rocksdb_env;
}

const std::string& Tablet::tablet_id() const {
  return metadata_->raft_group_id();
}

Result<std::string> Tablet::GetEncodedMiddleSplitKey(std::string *partition_split_key) const {
  auto error_prefix = [this]() {
    return Format(
        "Failed to detect middle key for tablet $0 (key_bounds: \"$1\" - \"$2\")",
        tablet_id(),
        Slice(key_bounds_.lower).ToDebugHexString(),
        Slice(key_bounds_.upper).ToDebugHexString());
  };

  // TODO(tsplit): should take key_bounds_ into account.
  auto middle_key = VERIFY_RESULT(regular_db_->GetMiddleKey());

  // In some rare cases middle key can point to a special internal record which is not visible
  // for a user, but tablet splitting routines expect the specific structure for partition keys
  // that does not match the struct of the internally used records. Moreover, it is expected
  // to have two child tablets with alive user records after the splitting, but the split
  // by the internal record will lead to a case when one tablet will consist of internal records
  // only and these records will be compacted out at some point making an empty tablet.
  if (PREDICT_FALSE(dockv::IsInternalRecordKeyType(dockv::DecodeKeyEntryType(middle_key[0])))) {
    return STATUS_FORMAT(
        IllegalState, "$0: got internal record \"$1\"",
        error_prefix(), Slice(middle_key).ToDebugHexString());
  }

  const auto key_part = metadata()->partition_schema()->IsHashPartitioning()
                            ? dockv::DocKeyPart::kUpToHashCode
                            : dockv::DocKeyPart::kWholeDocKey;
  const auto split_key_size = VERIFY_RESULT(dockv::DocKey::EncodedSize(middle_key, key_part));
  if (PREDICT_FALSE(split_key_size == 0)) {
    // Using this verification just to have a more sensible message. The below verification will
    // not pass with split_key_size == 0 also, but its message is not accurate enough. This failure
    // may happen when a key cannot be decoded with key_part inside DocKey::EncodedSize and the key
    // still valid for any reason (e.g. gettining non-hash key for hash partitioning).
    return STATUS_FORMAT(
        IllegalState, "$0: got unexpected key \"$1\"",
        error_prefix(), Slice(middle_key).ToDebugHexString());
  }

  middle_key.resize(split_key_size);
  const Slice middle_key_slice(middle_key);
  if (middle_key_slice.compare(key_bounds_.lower) <= 0 ||
      (!key_bounds_.upper.empty() && middle_key_slice.compare(key_bounds_.upper) >= 0)) {
    // This error occurs if there is no key strictly between the tablet lower and upper bound. It
    // causes the tablet split manager to temporarily delay splitting for this tablet.
    // The error can occur if:
    // 1. There are only one or two keys in the tablet (e.g. when indexing a large tablet by a low
    //    cardinality column), in which case we do not want to keep retrying splits.
    // 2. A post-split tablet wasn't fully compacted after it split. In this case, delaying splits
    //    will prevent splits after the compaction completes, but we should not be trying to split
    //    an uncompacted tablet anyways.
    return STATUS_EC_FORMAT(IllegalState,
        tserver::TabletServerError(tserver::TabletServerErrorPB::TABLET_SPLIT_KEY_RANGE_TOO_SMALL),
        "$0: got \"$1\".", error_prefix(), middle_key_slice.ToDebugHexString());
  }

  // Check middle_key fits tablet's partition bounds
  const Slice partition_start(metadata()->partition()->partition_key_start());
  const Slice partition_end(metadata()->partition()->partition_key_end());
  std::string middle_hash_key;
  if (metadata()->partition_schema()->IsHashPartitioning()) {
    const auto doc_key_hash = VERIFY_RESULT(dockv::DecodeDocKeyHash(middle_key));
    if (doc_key_hash.has_value()) {
      middle_hash_key = dockv::PartitionSchema::EncodeMultiColumnHashValue(doc_key_hash.value());
      if (partition_split_key) {
        *partition_split_key = middle_hash_key;
      }
    }
  }
  const Slice partition_middle_key(middle_hash_key.size() ? middle_hash_key : middle_key);
  if (partition_middle_key.compare(partition_start) <= 0 ||
      (!partition_end.empty() && partition_middle_key.compare(partition_end) >= 0)) {
    // This error occurs when middle key is not strictly between partition bounds.
    return STATUS_EC_FORMAT(IllegalState,
        tserver::TabletServerError(tserver::TabletServerErrorPB::TABLET_SPLIT_KEY_RANGE_TOO_SMALL),
        "$0 with partition bounds (\"$1\" - \"$2\"): got \"$3\".",
        error_prefix(), partition_start.ToDebugHexString(), partition_end.ToDebugHexString(),
        middle_key_slice.ToDebugHexString());
  }

  return middle_key;
}

bool Tablet::HasActiveFullCompaction() {
  std::lock_guard lock(full_compaction_token_mutex_);
  return HasActiveFullCompactionUnlocked();
}

void Tablet::TriggerPostSplitCompactionIfNeeded() {
  if (PREDICT_FALSE(FLAGS_TEST_skip_post_split_compaction)) {
    LOG(INFO) << "Skipping post split compaction due to FLAGS_TEST_skip_post_split_compaction";
    return;
  }
  if (!StillHasOrphanedPostSplitDataAbortable()) {
    return;
  }
  auto status = TriggerManualCompactionIfNeeded(rocksdb::CompactionReason::kPostSplitCompaction);
  if (status.ok()) {
    ts_post_split_compaction_added_->Increment();
  } else if (!status.IsServiceUnavailable()) {
    LOG_WITH_PREFIX(WARNING) << "Failed to submit compaction for post-split tablet: "
                             << status.ToString();
  }
}

Status Tablet::TriggerManualCompactionIfNeeded(rocksdb::CompactionReason compaction_reason) {
  if (!full_compaction_pool_ || state_ != State::kOpen) {
    return STATUS(ServiceUnavailable, "Full compaction thread pool unavailable.");
  }

  std::lock_guard lock(full_compaction_token_mutex_);
  if (HasActiveFullCompactionUnlocked()) {
    return STATUS(
        ServiceUnavailable, "Full compaction already running on this tablet.");
  }

  if (!full_compaction_task_pool_token_) {
    full_compaction_task_pool_token_ =
        full_compaction_pool_->NewToken(ThreadPool::ExecutionMode::SERIAL);
  }

  return full_compaction_task_pool_token_->SubmitFunc(
      std::bind(&Tablet::TriggerManualCompactionSync, this, compaction_reason));
}

Status Tablet::TriggerAdminFullCompactionIfNeededHelper(
    std::function<void()> on_compaction_completion) {
  if (!admin_triggered_compaction_pool_ || state_ != State::kOpen) {
    return STATUS(ServiceUnavailable, "Admin triggered compaction thread pool unavailable.");
  }

  std::lock_guard lock(full_compaction_token_mutex_);
  if (!admin_full_compaction_task_pool_token_) {
    admin_full_compaction_task_pool_token_ =
        admin_triggered_compaction_pool_->NewToken(ThreadPool::ExecutionMode::SERIAL);
  }

  return admin_full_compaction_task_pool_token_->SubmitFunc([this, on_compaction_completion]() {
    TriggerManualCompactionSync(rocksdb::CompactionReason::kAdminCompaction);
    on_compaction_completion();
  });
}

Status Tablet::TriggerAdminFullCompactionIfNeeded() {
  return TriggerAdminFullCompactionIfNeededHelper();
}

Status Tablet::TriggerAdminFullCompactionWithCallbackIfNeeded(
    std::function<void()> on_compaction_completion) {
  return TriggerAdminFullCompactionIfNeededHelper(on_compaction_completion);
}

void Tablet::TriggerManualCompactionSync(rocksdb::CompactionReason reason) {
  TEST_PAUSE_IF_FLAG(TEST_pause_before_full_compaction);
  WARN_WITH_PREFIX_NOT_OK(
      ForceRocksDBCompact(reason),
      Format("$0: Failed tablet full compaction ($1)", log_prefix_suffix_, ToString(reason)));
}

bool Tablet::HasActiveTTLFileExpiration() {
  return FLAGS_rocksdb_max_file_size_for_compaction > 0
      && retention_policy_->GetRetentionDirective().table_ttl !=
            dockv::ValueControlFields::kMaxTtl;
}

bool Tablet::IsEligibleForFullCompaction() {
  return !HasActiveFullCompaction()
      && !HasActiveTTLFileExpiration()
      && GetCurrentVersionNumSSTFiles() != 0;
}

Status Tablet::VerifyDataIntegrity() {
  LOG_WITH_PREFIX(INFO) << "Beginning data integrity checks on this tablet";

  // Verify regular db.
  if (regular_db_) {
    const auto& db_dir = metadata()->rocksdb_dir();
    RETURN_NOT_OK(OpenDbAndCheckIntegrity(db_dir));
  }

  // Verify intents db.
  if (intents_db_) {
    const auto& db_dir = metadata()->intents_rocksdb_dir();
    RETURN_NOT_OK(OpenDbAndCheckIntegrity(db_dir));
  }

  return Status::OK();
}

Status Tablet::OpenDbAndCheckIntegrity(const std::string& db_dir) {
  // Similar to ldb's CheckConsistency, we open db as read-only with paranoid checks on.
  // If any corruption is detected then the open will fail with a Corruption status.
  rocksdb::Options db_opts;
  InitRocksDBOptions(&db_opts, LogPrefix());
  db_opts.paranoid_checks = true;

  std::unique_ptr<rocksdb::DB> db;
  rocksdb::DB* db_raw = nullptr;
  rocksdb::Status st = rocksdb::DB::OpenForReadOnly(db_opts, db_dir, &db_raw);
  if (db_raw != nullptr) {
    db.reset(db_raw);
  }
  if (!st.ok()) {
    if (st.IsCorruption()) {
      LOG_WITH_PREFIX(WARNING) << "Detected rocksdb data corruption: " << st;
      // TODO: should we bump metric here or in top-level validation or both?
      metrics()->Increment(TabletCounters::kTabletDataCorruptions);
      return st;
    }

    LOG_WITH_PREFIX(WARNING) << "Failed to open read-only RocksDB in directory " << db_dir
                             << ": " << st;
    return Status::OK();
  }

  // TODO: we can add more checks here to verify block contents/checksums

  return Status::OK();
}

void Tablet::SplitDone() {
  {
    std::lock_guard lock(operation_filters_mutex_);
    if (completed_split_operation_filter_) {
      LOG_WITH_PREFIX(DFATAL) << "Already have split operation filter";
      return;
    }

    completed_split_operation_filter_ = MakeFunctorOperationFilter(
        [this](const OpId& op_id, consensus::OperationType op_type) -> Status {
          if (SplitOperation::ShouldAllowOpAfterSplitTablet(op_type)) {
            return Status::OK();
          }

          auto children = metadata_->split_child_tablet_ids();
          return SplitOperation::RejectionStatus(OpId(), op_id, op_type, children[0], children[1]);
        });
    operation_filters_.push_back(*completed_split_operation_filter_);

    completed_split_log_anchor_ = std::make_unique<log::LogAnchor>();

    log_anchor_registry_->Register(
        metadata_->split_op_id().index, "Splitted tablet", completed_split_log_anchor_.get());
  }
}

void Tablet::SyncRestoringOperationFilter(ResetSplit reset_split) {
  std::lock_guard lock(operation_filters_mutex_);

  if (reset_split) {
    if (completed_split_log_anchor_) {
      WARN_NOT_OK(log_anchor_registry_->Unregister(completed_split_log_anchor_.get()),
                  "Unregister split anchor");
      completed_split_log_anchor_ = nullptr;
    }

    if (completed_split_operation_filter_) {
      UnregisterOperationFilterUnlocked(completed_split_operation_filter_.get());
      completed_split_operation_filter_ = nullptr;
    }
  }

  if (metadata_->has_active_restoration()) {
    if (restoring_operation_filter_) {
      return;
    }
    restoring_operation_filter_ = MakeFunctorOperationFilter(
        [](const OpId& op_id, consensus::OperationType op_type) -> Status {
      if (SnapshotOperation::ShouldAllowOpDuringRestore(op_type)) {
        return Status::OK();
      }

      return SnapshotOperation::RejectionStatus(op_id, op_type);
    });
    operation_filters_.push_back(*restoring_operation_filter_);
  } else {
    if (!restoring_operation_filter_) {
      return;
    }

    UnregisterOperationFilterUnlocked(restoring_operation_filter_.get());
    restoring_operation_filter_ = nullptr;
  }
}

Status Tablet::RestoreStarted(const TxnSnapshotRestorationId& restoration_id) {
  metadata_->RegisterRestoration(restoration_id);
  RETURN_NOT_OK(metadata_->Flush());

  SyncRestoringOperationFilter(ResetSplit::kTrue);

  return Status::OK();
}

Status Tablet::RestoreFinished(
    const TxnSnapshotRestorationId& restoration_id, HybridTime restoration_hybrid_time) {
  metadata_->UnregisterRestoration(restoration_id);
  if (restoration_hybrid_time) {
    metadata_->SetRestorationHybridTime(restoration_hybrid_time);
    if (transaction_participant_ && FLAGS_consistent_restore) {
      transaction_participant_->IgnoreAllTransactionsStartedBefore(restoration_hybrid_time);
    }
  }
  RETURN_NOT_OK(metadata_->Flush());

  SyncRestoringOperationFilter(ResetSplit::kFalse);

  return Status::OK();
}

Status Tablet::CheckRestorations(const RestorationCompleteTimeMap& restoration_complete_time) {
  auto restoration_hybrid_time = metadata_->CheckCompleteRestorations(restoration_complete_time);
  if (restoration_hybrid_time != HybridTime::kMin
      && transaction_participant_
      && FLAGS_consistent_restore) {
    transaction_participant_->IgnoreAllTransactionsStartedBefore(restoration_hybrid_time);
  }

  // We cannot do it in a single shot, because should update transaction participant before
  // removing active transactions.
  if (!metadata_->CleanupRestorations(restoration_complete_time)) {
    return Status::OK();
  }

  RETURN_NOT_OK(metadata_->Flush());
  SyncRestoringOperationFilter(ResetSplit::kFalse);

  return Status::OK();
}

Status Tablet::CheckOperationAllowed(const OpId& op_id, consensus::OperationType op_type) {
  std::lock_guard lock(operation_filters_mutex_);
  for (const auto& filter : operation_filters_) {
    RETURN_NOT_OK(filter.CheckOperationAllowed(op_id, op_type));
  }

  return Status::OK();
}

void Tablet::RegisterOperationFilter(OperationFilter* filter) {
  std::lock_guard lock(operation_filters_mutex_);
  operation_filters_.push_back(*filter);
}

docdb::SchemaPackingProvider& Tablet::GetSchemaPackingProvider() {
  return *metadata_.get();
}

void Tablet::UnregisterOperationFilter(OperationFilter* filter) {
  std::lock_guard lock(operation_filters_mutex_);
  UnregisterOperationFilterUnlocked(filter);
}

void Tablet::UnregisterOperationFilterUnlocked(OperationFilter* filter) {
  operation_filters_.erase(operation_filters_.iterator_to(*filter));
}

docdb::DocReadContextPtr Tablet::GetDocReadContext() const {
  return metadata_->primary_table_info()->doc_read_context;
}

Result<docdb::DocReadContextPtr> Tablet::GetDocReadContext(const std::string& table_id) const {
  if (table_id.empty()) {
    return GetDocReadContext();
  }
  return VERIFY_RESULT(metadata_->GetTableInfo(table_id))->doc_read_context;
}

Schema Tablet::GetKeySchema(const std::string& table_id) const {
  if (table_id.empty()) {
    return *key_schema_;
  }
  auto table_info = CHECK_RESULT(metadata_->GetTableInfo(table_id));
  return table_info->schema().CreateKeyProjection();
}

HybridTime Tablet::DeleteMarkerRetentionTime(const std::vector<rocksdb::FileMetaData*>& inputs) {
  auto scoped_read_operation = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  if (!scoped_read_operation.ok()) {
    // Prevent markers from being deleted when we cannot calculate retention time during shutdown.
    return HybridTime::kMin;
  }

  // Query order is important. Since it is not atomic, we should be sure that write would not sneak
  // our queries. So we follow write record travel order.

  HybridTime result = transaction_participant_
      ? transaction_participant_->MinRunningHybridTime()
      : HybridTime::kMax;

  auto smallest = regular_db_->CalcMemTableFrontier(rocksdb::UpdateUserValueType::kSmallest);
  if (smallest) {
    result = std::min(
        result, down_cast<const docdb::ConsensusFrontier&>(*smallest).hybrid_time());
  }

  std::unordered_set<uint64_t> input_names;
  for (const auto& input : inputs) {
    input_names.insert(input->fd.GetNumber());
  }
  auto files = regular_db_->GetLiveFilesMetaData();

  for (const auto& file : files) {
    if (input_names.count(file.name_id) || !file.smallest.user_frontier) {
      continue;
    }
    result = std::min(
        result, down_cast<docdb::ConsensusFrontier&>(*file.smallest.user_frontier).hybrid_time());
  }

  return result;
}

Status Tablet::ProcessAutoFlagsConfigOperation(const AutoFlagsConfigPB& config) {
  if (!is_sys_catalog()) {
    LOG_WITH_PREFIX_AND_FUNC(DFATAL) << "AutoFlags config change ignored on non-sys_catalog tablet";
    return Status::OK();
  }

  if (!auto_flags_manager_) {
    LOG_WITH_PREFIX_AND_FUNC(DFATAL) << "AutoFlags manager not found";
    return STATUS(InternalError, "AutoFlags manager not found");
  }

  return auto_flags_manager_->ProcessAutoFlagsConfigOperation(config);
}

Status PopulateLockInfoFromIntent(
    Slice key, Slice val, const TableInfoProvider& table_info_provider,
    const std::map<TransactionId, SubtxnSet>& aborted_subtxn_info,
    TransactionLockInfoManager* lock_info_manager) {
  auto parsed_intent = VERIFY_RESULT(docdb::ParseIntentKey(key, val));
  auto decoded_value = VERIFY_RESULT(dockv::DecodeIntentValue(
      val, nullptr /* verify_transaction_id_slice */, HasStrong(parsed_intent.types)));

  if (!aborted_subtxn_info.empty()) {
    auto aborted_subtxn_set_it = aborted_subtxn_info.find(decoded_value.transaction_id);
    RSTATUS_DCHECK(
        aborted_subtxn_set_it != aborted_subtxn_info.end(), IllegalState,
        "Processing intent for unexpected transaction");
    if (aborted_subtxn_set_it->second.Test(decoded_value.subtransaction_id)) {
      return Status::OK();
    }
  }

  auto& lock_entry = *lock_info_manager->GetOrAddTransactionLockInfo(decoded_value.transaction_id);
  return docdb::PopulateLockInfoFromParsedIntent(
      parsed_intent, decoded_value, table_info_provider, lock_entry.add_granted_locks());
}

Status Tablet::GetLockStatus(const std::map<TransactionId, SubtxnSet>& transactions,
                             TabletLockInfoPB* tablet_lock_info) const {
  if (metadata_->table_type() != PGSQL_TABLE_TYPE) {
    return STATUS_FORMAT(
        InvalidArgument, "Cannot get lock status for non YSQL table $0", metadata_->table_id());
  }
  if (!metadata_->colocated()) {
    // For colocated table, we don't populate table_id field of TabletLockInfoPB message. Instead,
    // it is populated in LockInfoPB by downstream code.
    tablet_lock_info->set_table_id(metadata_->table_id());
  }
  tablet_lock_info->set_tablet_id(tablet_id());

  auto pending_op = CreateScopedRWOperationBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(pending_op);

  if (!intents_db_) {
    return Status::OK();
  }

  TransactionLockInfoManager lock_info_manager(tablet_lock_info);

  rocksdb::ReadOptions read_options;
  auto intent_iter = std::unique_ptr<rocksdb::Iterator>(intents_db_->NewIterator(read_options));
  intent_iter->SeekToFirst();

  if (transactions.empty()) {
    intent_iter->Seek(key_bounds_.lower);
    while (intent_iter->Valid() &&
           (key_bounds_.upper.empty() || intent_iter->key().compare(key_bounds_.upper) < 0)) {
      auto key = intent_iter->key();

      if (key[0] == dockv::KeyEntryTypeAsChar::kTransactionId) {
        static const std::array<char, 1> kAfterTransactionId{
            dockv::KeyEntryTypeAsChar::kTransactionId + 1};
        static const Slice kAfterTxnRegion(kAfterTransactionId);
        intent_iter->Seek(kAfterTxnRegion);
        continue;
      }

      RETURN_NOT_OK(PopulateLockInfoFromIntent(
          key, intent_iter->value(), *this, transactions, &lock_info_manager));

      intent_iter->Next();
    }

    RETURN_NOT_OK(intent_iter->status());
  } else {
    DCHECK(!transactions.empty());
    // While fetching intents of transactions below, we assume that the 'transactions' map is sorted
    // following the encoded string notation order of TransactionId. This assumption helps us avoid
    // repeated SeekToFirst calls on the rocksdb iterator so we can fetch relevant intents in one
    // forward pass.
    DCHECK(std::is_sorted(
        transactions.begin(), transactions.end(), IsSortedAscendingEncoded));

    std::vector<dockv::KeyBytes> txn_intent_keys;
    static constexpr size_t kReverseKeySize = 1 + sizeof(TransactionId);
    char reverse_key_data[kReverseKeySize];
    reverse_key_data[0] = dockv::KeyEntryTypeAsChar::kTransactionId;
    for (auto& txn : transactions) {
      auto& txn_id = txn.first;
      memcpy(&reverse_key_data[1], txn_id.data(), sizeof(TransactionId));
      auto reverse_key = Slice(reverse_key_data, kReverseKeySize);
      intent_iter->Seek(reverse_key);

      // Skip the transaction metadata entry.
      if (intent_iter->Valid() && intent_iter->key().compare(reverse_key) == 0) {
        intent_iter->Next();
      }

      // Scan the transaction's corresponding reverse index section.
      while (intent_iter->Valid() && intent_iter->key().compare_prefix(reverse_key) == 0) {
        DCHECK_EQ(intent_iter->key()[0], dockv::KeyEntryTypeAsChar::kTransactionId);
        // We should only consider intents whose value is within the tablet's key bounds.
        // Else, we would observe duplicate results in case of tablet split.
        if (key_bounds_.IsWithinBounds(intent_iter->value())) {
          txn_intent_keys.emplace_back(intent_iter->value());
        }
        intent_iter->Next();
      }
      RETURN_NOT_OK(intent_iter->status());
    }

    std::sort(txn_intent_keys.begin(), txn_intent_keys.end());
    intent_iter->SeekToFirst();
    RETURN_NOT_OK(intent_iter->status());

    for (const auto& intent_key : txn_intent_keys) {
      intent_iter->Seek(intent_key);
      RETURN_NOT_OK(intent_iter->status());

      auto key = intent_iter->key();
      DCHECK_EQ(intent_iter->key(), key);

      auto val = intent_iter->value();
      RETURN_NOT_OK(PopulateLockInfoFromIntent(key, val, *this, transactions, &lock_info_manager));
    }
  }

  const auto& wait_queue = transaction_participant()->wait_queue();
  if (wait_queue) {
    RETURN_NOT_OK(wait_queue->GetLockStatus(transactions, *this, &lock_info_manager));
  }

  return Status::OK();
}

Status Tablet::ProcessPgsqlGetTableKeyRangesRequest(
      const PgsqlReadRequestPB& req, PgsqlReadRequestResult* result) const {
  const auto& get_key_ranges_req = req.get_tablet_key_ranges_request();
  result->num_rows_read = 0;
  const auto status = GetTabletKeyRanges(
      get_key_ranges_req.lower_bound_key(), get_key_ranges_req.upper_bound_key(), req.limit(),
      get_key_ranges_req.range_size_bytes(), IsForward(get_key_ranges_req.is_forward()),
      get_key_ranges_req.max_key_length(), [result](Slice key) {
        pggate::WriteBinaryColumn(key, result->rows_data);
        ++result->num_rows_read;
      }, req.table_id());
  if (!status.ok()) {
    result->response.set_status(PgsqlResponsePB::PGSQL_STATUS_RUNTIME_ERROR);
    StatusToPB(status, result->response.add_error_status());
    return Status::OK();
  }
  RETURN_NOT_OK(CreatePagingStateForRead(req, result->num_rows_read, &result->response));
  result->response.set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

Result<TableInfoPtr> Tablet::GetTableInfo(ColocationId colocation_id) const {
  return metadata_->GetTableInfo(colocation_id);
}

namespace {

// Skips `num_keys_to_skip` keys using `skip_key_func` until `is_reached_end_key_func` return true.
// Stops at first key that satisfies `is_reached_end_key_func`.
// Can skip more keys if `has_key_changed_func` returns false.
template<typename IsReachedEndKeyFunc, typename SkipKeyFunc, typename HasKeyChangedFunc>
bool SkipKeys(
    const size_t num_keys_to_skip, IsReachedEndKeyFunc is_reached_end_key_func,
    SkipKeyFunc skip_key_func, HasKeyChangedFunc has_key_changed_func) {
  bool reached_end_key = is_reached_end_key_func();
  for (size_t i = 0; i < num_keys_to_skip && !reached_end_key; ++i) {
    skip_key_func();
    reached_end_key = is_reached_end_key_func();
  }
  while (!has_key_changed_func() && !reached_end_key) {
    skip_key_func();
    reached_end_key = is_reached_end_key_func();
  }
  return reached_end_key;
}

std::string IncrementedCopy(Slice key) {
  std::string key_str = key.ToBuffer();
  key = Slice(key_str);
  uint8_t* p = key.mutable_data() + key.size() - 1;
  while (p >= key.data()) {
    if (++(*p) != 0) {
      // No overflow - return.
      return key_str;
    }
    --p;
    // Overflow - try to increment previous byte.
  }
  // The whole key was 0xFF..FF, return max possible key (empty key).
  return "";
}

} // namespace

Status Tablet::GetTabletKeyRanges(
    const Slice lower_bound_key, const Slice upper_bound_key, const uint64_t max_num_ranges,
    const uint64_t range_size_bytes, const IsForward is_forward, const uint32_t max_key_length,
    WriteBuffer* keys_buffer, const TableId& colocated_table_id) const {
  if (table_type_ != PGSQL_TABLE_TYPE) {
    return STATUS_FORMAT(
        NotSupported, "GetTabletKeyRanges is only supported for YSQL, tablet_id: ", tablet_id());
  }
  return GetTabletKeyRanges(
      lower_bound_key, upper_bound_key, max_num_ranges, range_size_bytes, is_forward,
      max_key_length,
      [&keys_buffer](Slice key) {
        pggate::WriteBinaryColumn(key, keys_buffer);
      }, colocated_table_id);
}

Status Tablet::GetTabletKeyRanges(
    Slice lower_bound_key, Slice upper_bound_key, uint64_t max_num_ranges,
    const uint64_t range_size_bytes, const IsForward is_forward, uint32_t max_key_length,
    std::function<void(Slice key)> callback, const TableId& colocated_table_id) const {
  VLOG_WITH_FUNC(2) << "lower_bound_key: " << lower_bound_key.ToDebugHexString()
                    << " upper_bound_key: " << upper_bound_key.ToDebugHexString()
                    << " max_num_ranges: " << max_num_ranges
                    << " range_size_bytes: " << range_size_bytes
                    << " max_key_length: " << max_key_length;
  if (max_num_ranges == 0) {
    max_num_ranges = std::numeric_limits<decltype(max_num_ranges)>::max();
  }
  if (max_key_length == 0) {
    max_key_length = std::numeric_limits<decltype(max_key_length)>::max();
  }

  auto pending_op = CreateScopedRWOperationNotBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(pending_op);

  const auto num_blocks_to_skip = std::max<size_t>(range_size_bytes / FLAGS_db_block_size_bytes, 1);

  rocksdb::ReadOptions read_options;
  // An index block contains one entry per data block, where the key is a string >= last key in that
  // data block and < the first key in the successive data block. The value is the BlockHandle
  // (file offset and length) for the data block.
  // So, we need to skip last key in each SST index because there are no data keys after the last
  // index key.
  auto index_iter = regular_db_->NewIndexIterator(read_options, rocksdb::SkipLastEntry::kTrue);

  // TODO(get_table_key_ranges): consider reusing index_iter and BlockHandle to avoid double SST
  // index seek.
  auto data_iter = std::unique_ptr<rocksdb::Iterator>(regular_db_->NewIterator(read_options));

  std::string encoded_partition_key_start;
  std::string encoded_partition_key_end;
  Slice partition_lower_bound_key;
  Slice partition_upper_bound_key;

  const bool is_colocated = metadata()->colocated();
  if (is_colocated) {
    const auto table_info = VERIFY_RESULT(metadata_->GetTableInfo(colocated_table_id));
    const auto table_key_prefix = table_info->doc_read_context->table_key_prefix();
    partition_lower_bound_key = table_key_prefix;
    encoded_partition_key_end = IncrementedCopy(table_key_prefix);
    partition_upper_bound_key = encoded_partition_key_end;
  } else if (key_bounds_.IsInitialized()) {
    partition_lower_bound_key = key_bounds_.lower;
    partition_upper_bound_key = key_bounds_.upper;
  } else {
    const auto& partition_schema = metadata_->partition_schema();
    const auto& partition = metadata_->partition();
    encoded_partition_key_start =
        VERIFY_RESULT(partition_schema->GetEncodedPartitionKey(partition->partition_key_start()));
    encoded_partition_key_end =
        VERIFY_RESULT(partition_schema->GetEncodedPartitionKey(partition->partition_key_end()));
    partition_lower_bound_key = encoded_partition_key_start;
    partition_upper_bound_key = encoded_partition_key_end;
  }
  // Whether it make sense to continue fetching ranges for the caller if we reach end of
  // tablet(table) or upper/lower bound.
  bool use_empty_as_end_key;

  if (partition_lower_bound_key > lower_bound_key) {
    lower_bound_key = partition_lower_bound_key;
  }
  if (partition_upper_bound_key.empty() ||
      (!upper_bound_key.empty() && partition_upper_bound_key >= upper_bound_key)) {
    // Reaching partition upper bound means we can't have more keys in next tablet.
    use_empty_as_end_key = true;
  } else {
    // Partition upper bound is less than upper_bound_key, we can have more keys in the next
    // tablet (unless colocated).
    use_empty_as_end_key = is_colocated;
    upper_bound_key = partition_upper_bound_key;
  }

  return is_forward ? GetTabletKeyRangesForward(
                          index_iter.get(), data_iter.get(), lower_bound_key, upper_bound_key,
                          max_num_ranges, num_blocks_to_skip, max_key_length, std::move(callback),
                          use_empty_as_end_key)
                    : GetTabletKeyRangesBackward(
                          index_iter.get(), lower_bound_key, upper_bound_key, max_num_ranges,
                          num_blocks_to_skip, max_key_length, std::move(callback));
}

Status Tablet::GetTabletKeyRangesForward(
    rocksdb::Iterator* index_iter, rocksdb::Iterator* data_iter, Slice lower_bound_key,
    Slice upper_bound_key, const uint64_t max_num_ranges, const uint64_t num_blocks_to_skip,
    const uint32_t max_key_length, std::function<void(Slice key)> callback,
    const bool use_empty_as_end_key) const {
  // TODO(get_table_key_ranges): As of 2023-09 we get full encoded doc keys and skip keys longer
  // than max_key_length. Consider reworking that to allow using key prefixes as lower/upper bound
  // for scan, currently it is not accepted by QLRocksDBStorage::GetIterator.

  if (lower_bound_key.empty()) {
    index_iter->SeekToFirst();
  } else {
    index_iter->Seek(lower_bound_key);
  }

  // First index key is the last key in first data block, so we treat it as we've already
  // skipped one data block.
  bool treat_block_as_skipped = index_iter->Valid() && index_iter->key() != lower_bound_key;

  std::string last_key = lower_bound_key.ToBuffer();

  uint64_t num_keys = 1;

  auto has_iter_key_changed_func = [&index_iter, &last_key]() {
    if (!index_iter->Valid()) {
      return true;
    }
    return index_iter->key() > last_key;
  };

  while (num_keys <= max_num_ranges) {
    const auto reached_end_key = SkipKeys(
        num_blocks_to_skip - treat_block_as_skipped,
        [&index_iter, upper_bound_key]() {
          return !index_iter->Valid() ||
                 (!upper_bound_key.empty() && index_iter->key().GreaterOrEqual(upper_bound_key));
        },
        [&index_iter]() { index_iter->Next(); },
        has_iter_key_changed_func);
    treat_block_as_skipped = false;

    if (reached_end_key) {
      if (use_empty_as_end_key) {
        callback("");
      } else {
        callback(upper_bound_key);
      }
      break;
    }

    auto data_entry = data_iter->Seek(index_iter->key());
    RSTATUS_DCHECK(
        data_entry.Valid(), InternalError,
        Format(
            "Invalid data iterator after seeking to key from SST index: $0",
            index_iter->key().ToDebugHexString()));

    for (; data_entry.Valid(); data_entry = data_iter->Next()) {
      // Use encoded doc key to avoid breaking data related to the same row into halves.
      const auto doc_key_size_result =
          dockv::DocKey::EncodedSize(data_entry.key, dockv::DocKeyPart::kWholeDocKey);

      if (!doc_key_size_result.ok()) {
        YB_LOG_EVERY_N_SECS(WARNING, 60)
            << "Failed to get encoded size of key: " << data_entry.key.ToDebugHexString() << "."
            << doc_key_size_result.status();
        continue;
      }
      if (doc_key_size_result.get() > max_key_length) {
        // Skip keys longer than max_key_length.
        continue;
      }
      const auto key = data_entry.key.Prefix(doc_key_size_result.get());
      callback(key);
      last_key = key.ToBuffer();
      ++num_keys;
      break;
    }
  }

  return Status::OK();
}

Status Tablet::GetTabletKeyRangesBackward(
    rocksdb::Iterator* index_iter, Slice lower_bound_key, Slice upper_bound_key,
    const uint64_t max_num_ranges, const uint64_t num_blocks_to_skip, const uint32_t max_key_length,
    std::function<void(Slice key)> callback) const {
  return STATUS(NotSupported, "Tablet::GetTabletKeyRanges in backward order is not yet supported");
}

// ------------------------------------------------------------------------------------------------

Result<ScopedReadOperation> ScopedReadOperation::Create(
    AbstractTablet* tablet,
    RequireLease require_lease,
    ReadHybridTime read_time) {
  if (!read_time) {
    read_time = ReadHybridTime::SingleTime(VERIFY_RESULT(tablet->SafeTime(require_lease)));
  }
  auto* retention_policy = tablet->RetentionPolicy();
  if (retention_policy) {
    RETURN_NOT_OK(retention_policy->RegisterReaderTimestamp(read_time.read));
  }
  return ScopedReadOperation(tablet, read_time);
}

ScopedReadOperation::ScopedReadOperation(
    AbstractTablet* tablet, const ReadHybridTime& read_time)
    : tablet_(tablet), read_time_(read_time) {
}

ScopedReadOperation::~ScopedReadOperation() {
  Reset();
}

void ScopedReadOperation::operator=(ScopedReadOperation&& rhs) {
  Reset();
  tablet_ = rhs.tablet_;
  read_time_ = rhs.read_time_;
  rhs.tablet_ = nullptr;
}

void ScopedReadOperation::Reset() {
  if (tablet_) {
    auto* retention_policy = tablet_->RetentionPolicy();
    if (retention_policy) {
      retention_policy->UnregisterReaderTimestamp(read_time_.read);
    }
    tablet_ = nullptr;
  }
}

}  // namespace tablet
}  // namespace yb
