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

#include <algorithm>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <ostream>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/optional.hpp>

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/utilities/checkpoint.h"
#include "yb/rocksdb/write_batch.h"
#include "yb/rocksdb/util/file_util.h"
#include "yb/rocksutil/write_batch_formatter.h"

#include "yb/client/error.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/session.h"
#include "yb/client/yb_op.h"

#include "yb/common/common.pb.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_rowblock.h"
#include "yb/common/pgsql_error.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/opid_util.h"

#include "yb/docdb/bounded_rocksdb_iterator.h"
#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/cql_operation.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/docdb_compaction_filter_intents.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/lock_batch.h"
#include "yb/docdb/pgsql_operation.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/redis_operation.h"

#include "yb/gutil/atomicops.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/rocksutil/yb_rocksdb_logger.h"
#include "yb/server/hybrid_clock.h"

#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_retention_policy.h"
#include "yb/tablet/transaction_coordinator.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/operations/truncate_operation.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/tablet_options.h"
#include "yb/util/bloom_filter.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/enums.h"
#include "yb/util/env.h"
#include "yb/util/flag_tags.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/locks.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/scope_exit.h"
#include "yb/util/slice.h"
#include "yb/util/stopwatch.h"
#include "yb/util/trace.h"
#include "yb/util/url-coding.h"

DEFINE_bool(tablet_do_dup_key_checks, true,
            "Whether to check primary keys for duplicate on insertion. "
            "Use at your own risk!");
TAG_FLAG(tablet_do_dup_key_checks, unsafe);

DEFINE_bool(tablet_do_compaction_cleanup_for_intents, true,
            "Whether to clean up intents for aborted transactions in compaction.");

DEFINE_int32(tablet_bloom_block_size, 4096,
             "Block size of the bloom filters used for tablet keys.");
TAG_FLAG(tablet_bloom_block_size, advanced);

DEFINE_double(tablet_bloom_target_fp_rate, 0.01f,
              "Target false-positive rate (between 0 and 1) to size tablet key bloom filters. "
              "A lower false positive rate may reduce the number of disk seeks required "
              "in heavy insert workloads, at the expense of more space and RAM "
              "required for bloom filters.");
TAG_FLAG(tablet_bloom_target_fp_rate, advanced);

METRIC_DEFINE_entity(tablet);

// TODO: use a lower default for truncate / snapshot restore Raft operations. The one-minute timeout
// is probably OK for shutdown.
DEFINE_int32(tablet_rocksdb_ops_quiet_down_timeout_ms, 60000,
             "Max amount of time we can wait for read/write operations on RocksDB to finish "
             "so that we can perform exclusive-ownership operations on RocksDB, such as removing "
             "all data in the tablet by replacing the RocksDB instance with an empty one.");

DEFINE_int32(intents_flush_max_delay_ms, 2000,
             "Max time to wait for regular db to flush during flush of intents. "
             "After this time flush of regular db will be forced.");

DEFINE_int32(num_raft_ops_to_force_idle_intents_db_to_flush, 1000,
             "When writes to intents RocksDB are stopped and the number of Raft operations after "
             "the last write to the intents RocksDB "
             "is greater than this value, the intents RocksDB would be requested to flush.");

DEFINE_test_flag(
    bool, tablet_verify_flushed_frontier_after_modifying, false,
    "After modifying the flushed frontier in RocksDB, verify that the restored value of it "
    "is as expected. Used for testing.");

DEFINE_test_flag(
    bool, docdb_log_write_batches, false,
    "Dump write batches being written to RocksDB");

DECLARE_int32(rocksdb_level0_slowdown_writes_trigger);
DECLARE_int32(rocksdb_level0_stop_writes_trigger);

using namespace std::placeholders;

using std::shared_ptr;
using std::make_shared;
using std::string;
using std::unordered_set;
using std::vector;
using std::unique_ptr;
using namespace std::literals;  // NOLINT

using rocksdb::WriteBatch;
using rocksdb::SequenceNumber;
using yb::util::ScopedPendingOperation;
using yb::util::ScopedPendingOperationPause;
using yb::tserver::WriteRequestPB;
using yb::tserver::WriteResponsePB;
using yb::docdb::KeyValueWriteBatchPB;
using yb::tserver::ReadRequestPB;
using yb::docdb::DocOperation;
using yb::docdb::RedisWriteOperation;
using yb::docdb::QLWriteOperation;
using yb::docdb::PgsqlWriteOperation;
using yb::docdb::DocDBCompactionFilterFactory;
using yb::docdb::InitMarkerBehavior;

namespace yb {
namespace tablet {

using yb::MaintenanceManager;
using consensus::OpId;
using consensus::MaximumOpId;
using log::LogAnchorRegistry;
using strings::Substitute;
using base::subtle::Barrier_AtomicIncrement;

using client::ChildTransactionData;
using client::TransactionManager;
using client::YBSession;
using client::YBTransaction;
using client::YBTablePtr;

using docdb::DocDbAwareFilterPolicy;
using docdb::DocKey;
using docdb::DocPath;
using docdb::DocRowwiseIterator;
using docdb::DocWriteBatch;
using docdb::SubDocKey;
using docdb::PrimitiveValue;
using docdb::StorageDbType;

////////////////////////////////////////////////////////////
// Tablet
////////////////////////////////////////////////////////////

namespace {

static const std::string kSnapshotsDirSuffix = ".snapshots";

void EmitRocksDbMetricsAsJson(
    std::shared_ptr<rocksdb::Statistics> rocksdb_statistics,
    JsonWriter* writer,
    const MetricJsonOptions& opts) {
  // Make sure the class member 'rocksdb_statistics_' exists, as this is the stats object
  // maintained by RocksDB for this tablet.
  if (rocksdb_statistics == nullptr) {
    return;
  }
  // Emit all the ticker (gauge) metrics.
  for (std::pair<rocksdb::Tickers, std::string> entry : rocksdb::TickersNameMap) {
    // Start the metric object.
    writer->StartObject();
    // Write the name.
    writer->String("name");
    writer->String(entry.second);
    // Write the value.
    uint64_t value = rocksdb_statistics->getTickerCount(entry.first);
    writer->String("value");
    writer->Uint64(value);
    // Finish the metric object.
    writer->EndObject();
  }
  // Emit all the histogram metrics.
  rocksdb::HistogramData histogram_data;
  for (std::pair<rocksdb::Histograms, std::string> entry : rocksdb::HistogramsNameMap) {
    // Start the metric object.
    writer->StartObject();
    // Write the name.
    writer->String("name");
    writer->String(entry.second);
    // Write the value.
    rocksdb_statistics->histogramData(entry.first, &histogram_data);
    writer->String("total_count");
    writer->Double(histogram_data.count);
    writer->String("min");
    writer->Double(histogram_data.min);
    writer->String("mean");
    writer->Double(histogram_data.average);
    writer->String("median");
    writer->Double(histogram_data.median);
    writer->String("std_dev");
    writer->Double(histogram_data.standard_deviation);
    writer->String("percentile_95");
    writer->Double(histogram_data.percentile95);
    writer->String("percentile_99");
    writer->Double(histogram_data.percentile99);
    writer->String("max");
    writer->Double(histogram_data.max);
    writer->String("total_sum");
    writer->Double(histogram_data.sum);
    // Finish the metric object.
    writer->EndObject();
  }
}

CHECKED_STATUS EmitRocksDbMetricsAsPrometheus(
    std::shared_ptr<rocksdb::Statistics> rocksdb_statistics,
    PrometheusWriter* writer,
    const MetricEntity::AttributeMap& attrs) {
  // Make sure the class member 'rocksdb_statistics_' exists, as this is the stats object
  // maintained by RocksDB for this tablet.
  if (rocksdb_statistics == nullptr) {
    return Status::OK();
  }
  // Emit all the ticker (gauge) metrics.
  for (std::pair<rocksdb::Tickers, std::string> entry : rocksdb::TickersNameMap) {
    RETURN_NOT_OK(writer->WriteSingleEntry(
        attrs, entry.second, rocksdb_statistics->getTickerCount(entry.first)));
  }
  // Emit all the histogram metrics.
  rocksdb::HistogramData histogram_data;
  for (std::pair<rocksdb::Histograms, std::string> entry : rocksdb::HistogramsNameMap) {
    rocksdb_statistics->histogramData(entry.first, &histogram_data);

    auto copy_of_attr = attrs;
    const std::string hist_name = entry.second;
    RETURN_NOT_OK(writer->WriteSingleEntry(
        copy_of_attr, hist_name + "_sum", histogram_data.sum));
    RETURN_NOT_OK(writer->WriteSingleEntry(
        copy_of_attr, hist_name + "_count", histogram_data.count));
  }
  return Status::OK();
}

docdb::PartialRangeKeyIntents UsePartialRangeKeyIntents(RaftGroupMetadata* metadata) {
  return docdb::PartialRangeKeyIntents(metadata->table_type() == TableType::PGSQL_TABLE_TYPE);
}

} // namespace

string DocDbOpIds::ToString() const {
  return Format("{ regular: $0 intents: $1 }", regular, intents);
}

Tablet::Tablet(
    const RaftGroupMetadataPtr& metadata,
    const std::shared_future<client::YBClient*> &client_future,
    const server::ClockPtr& clock,
    const shared_ptr<MemTracker>& parent_mem_tracker,
    std::shared_ptr<MemTracker> block_based_table_mem_tracker,
    MetricRegistry* metric_registry,
    const scoped_refptr<LogAnchorRegistry>& log_anchor_registry,
    const TabletOptions& tablet_options,
    std::string log_prefix_suffix,
    TransactionParticipantContext* transaction_participant_context,
    client::LocalTabletFilter local_tablet_filter,
    TransactionCoordinatorContext* transaction_coordinator_context)
    : key_schema_(metadata->schema().CreateKeyProjection()),
      metadata_(metadata),
      table_type_(metadata->table_type()),
      log_anchor_registry_(log_anchor_registry),
      mem_tracker_(MemTracker::CreateTracker(
          Format("tablet-$0", tablet_id()), parent_mem_tracker, AddToParent::kTrue,
          CreateMetrics::kFalse)),
      block_based_table_mem_tracker_(std::move(block_based_table_mem_tracker)),
      clock_(clock),
      mvcc_(Format("T $0$1: ", metadata_->raft_group_id(), log_prefix_suffix), clock),
      tablet_options_(tablet_options),
      client_future_(client_future),
      local_tablet_filter_(std::move(local_tablet_filter)),
      log_prefix_suffix_(std::move(log_prefix_suffix)) {
  CHECK(schema()->has_column_ids());

  if (metric_registry) {
    MetricEntity::AttributeMap attrs;
    // TODO(KUDU-745): table_id is apparently not set in the metadata.
    attrs["table_id"] = metadata_->table_id();
    attrs["table_name"] = metadata_->table_name();
    attrs["partition"] = metadata_->partition_schema().PartitionDebugString(metadata_->partition(),
                                                                            *schema());
    metric_entity_ = METRIC_ENTITY_tablet.Instantiate(metric_registry, tablet_id(), attrs);
    // If we are creating a KV table create the metrics callback.
    rocksdb_statistics_ = rocksdb::CreateDBStatistics();
    auto rocksdb_statistics = rocksdb_statistics_;
    metric_entity_->AddExternalJsonMetricsCb(
        [rocksdb_statistics](JsonWriter* jw, const MetricJsonOptions& opts) {
      EmitRocksDbMetricsAsJson(rocksdb_statistics, jw, opts);
    });

    metric_entity_->AddExternalPrometheusMetricsCb(
        [rocksdb_statistics, attrs](PrometheusWriter* pw) {
      auto s = EmitRocksDbMetricsAsPrometheus(rocksdb_statistics, pw, attrs);
      if (!s.ok()) {
        YB_LOG_EVERY_N(WARNING, 100) << "Failed to get Prometheus metrics: " << s.ToString();
      }
    });

    metrics_.reset(new TabletMetrics(metric_entity_));

    mem_tracker_->SetMetricEntity(metric_entity_);
  }

  if (transaction_participant_context && metadata->schema().table_properties().is_transactional()) {
    transaction_participant_ = std::make_unique<TransactionParticipant>(
        transaction_participant_context, this, metric_entity_);
    // Create transaction manager for secondary index update.
    if (!metadata_->index_map().empty()) {
      transaction_manager_.emplace(client_future_.get(),
                                   scoped_refptr<server::Clock>(clock_),
                                   local_tablet_filter_);
    }
  }

  // Create index table metadata cache for secondary index update.
  if (!metadata_->index_map().empty()) {
    metadata_cache_.emplace(client_future_.get(), false /* Update roles' permissions cache */);
  }

  // If this is a unique index tablet, set up the index primary key schema.
  if (metadata_->is_unique_index()) {
    unique_index_key_schema_.emplace();
    const auto ids = metadata_->index_key_column_ids();
    CHECK_OK(metadata_->schema().CreateProjectionByIdsIgnoreMissing(ids,
                                                                    &*unique_index_key_schema_));
  }

  if (transaction_coordinator_context &&
      metadata_->table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    transaction_coordinator_ = std::make_unique<TransactionCoordinator>(
        metadata->fs_manager()->uuid(),
        transaction_coordinator_context,
        metrics_->expired_transactions.get());
  }
}

Tablet::~Tablet() {
  Shutdown();
  mem_tracker_->UnregisterFromParent();
}

Status Tablet::Open() {
  TRACE_EVENT0("tablet", "Tablet::Open");
  std::lock_guard<rw_spinlock> lock(component_lock_);
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

  return Status::OK();
}

template <class F>
auto MakeMemTableFlushFilterFactory(const F& f) {
  // Trick to get type of mem_table_flush_filter_factory field.
  typedef typename decltype(
      static_cast<rocksdb::Options*>(nullptr)->mem_table_flush_filter_factory)::element_type
      MemTableFlushFilterFactoryType;
  return std::make_shared<MemTableFlushFilterFactoryType>(f);
}

Result<bool> Tablet::IntentsDbFlushFilter(const rocksdb::MemTable& memtable) {
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
        return true;
      }
    }
  }

  // If regular db does not have anything to flush, it means that we have just added intents,
  // without apply, so it is OK to flush the intents RocksDB.
  auto flush_intention = regular_db_->GetFlushAbility();
  if (flush_intention == rocksdb::FlushAbility::kNoNewData) {
    return true;
  }

  // Force flush of regular DB if we were not able to flush for too long.
  auto timeout = std::chrono::milliseconds(FLAGS_intents_flush_max_delay_ms);
  if (flush_intention != rocksdb::FlushAbility::kAlreadyFlushing &&
      std::chrono::steady_clock::now() > memtable.FlushStartTime() + timeout) {
    rocksdb::FlushOptions options;
    options.wait = false;
    regular_db_->Flush(options);
  }

  return false;
}

std::string Tablet::LogPrefix() const {
  return Format("T $0$1: ", tablet_id(), log_prefix_suffix_);
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

} // namespace

std::string Tablet::LogPrefix(docdb::StorageDbType db_type) const {
  return Format("T $0$1 [$2]: ", tablet_id(), log_prefix_suffix_, LogDbTypePrefix(db_type));
}

Status Tablet::OpenKeyValueTablet() {
  static const std::string kRegularDB = "RegularDB"s;
  static const std::string kIntentsDB = "IntentsDB"s;

  rocksdb::Options rocksdb_options;
  docdb::InitRocksDBOptions(
      &rocksdb_options, LogPrefix(docdb::StorageDbType::kRegular), rocksdb_statistics_,
      tablet_options_);
  rocksdb_options.mem_tracker = MemTracker::FindOrCreateTracker(kRegularDB, mem_tracker_);
  rocksdb_options.block_based_table_mem_tracker = MemTracker::FindOrCreateTracker(
      Format("$0-$1", kRegularDB, tablet_id()), block_based_table_mem_tracker_);

  key_bounds_ = docdb::KeyBounds(metadata()->lower_bound_key(), metadata()->upper_bound_key());

  // Install the history cleanup handler. Note that TabletRetentionPolicy is going to hold a raw ptr
  // to this tablet. So, we ensure that rocksdb_ is reset before this tablet gets destroyed.
  rocksdb_options.compaction_filter_factory = make_shared<DocDBCompactionFilterFactory>(
      make_shared<TabletRetentionPolicy>(this), &key_bounds_);

  rocksdb_options.mem_table_flush_filter_factory = MakeMemTableFlushFilterFactory([this] {
    if (mem_table_flush_filter_factory_) {
      return mem_table_flush_filter_factory_();
    }
    return rocksdb::MemTableFilter();
  });

  rocksdb_options.disable_auto_compactions = true;
  rocksdb_options.level0_slowdown_writes_trigger = std::numeric_limits<int>::max();
  rocksdb_options.level0_stop_writes_trigger = std::numeric_limits<int>::max();

  const string db_dir = metadata()->rocksdb_dir();
  RETURN_NOT_OK(CreateTabletDirectories(db_dir, metadata()->fs_manager()));

  LOG(INFO) << "Opening RocksDB at: " << db_dir;
  rocksdb::DB* db = nullptr;
  rocksdb::Status rocksdb_open_status = rocksdb::DB::Open(rocksdb_options, db_dir, &db);
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
    docdb::SetLogPrefix(&rocksdb_options, LogPrefix(docdb::StorageDbType::kIntents));

    rocksdb_options.mem_table_flush_filter_factory = MakeMemTableFlushFilterFactory([this] {
      return std::bind(&Tablet::IntentsDbFlushFilter, this, _1);
    });

    rocksdb_options.compaction_filter_factory =
        FLAGS_tablet_do_compaction_cleanup_for_intents ?
        std::make_shared<docdb::DocDBIntentsCompactionFilterFactory>(this, &key_bounds_) : nullptr;

    rocksdb_options.mem_tracker = MemTracker::FindOrCreateTracker(kIntentsDB, mem_tracker_);
    rocksdb_options.block_based_table_mem_tracker = MemTracker::FindOrCreateTracker(
      Format("$0-$1", kIntentsDB, tablet_id()), block_based_table_mem_tracker_);

    rocksdb::DB* intents_db = nullptr;
    RETURN_NOT_OK(rocksdb::DB::Open(rocksdb_options, db_dir + kIntentsDBSuffix, &intents_db));
    intents_db_.reset(intents_db);
  }

  ql_storage_.reset(new docdb::QLRocksDBStorage(doc_db()));
  if (transaction_participant_) {
    transaction_participant_->SetDB(intents_db_.get(), &key_bounds_);
  }

  // Don't allow reads at timestamps lower than the highest history cutoff of a past compaction.
  auto regular_flushed_frontier = regular_db_->GetFlushedFrontier();
  if (regular_flushed_frontier) {
    const auto& regular_flushed_largest =
        static_cast<const docdb::ConsensusFrontier&>(*regular_flushed_frontier);
    if (regular_flushed_largest.history_cutoff()) {
      std::lock_guard<std::mutex> lock(active_readers_mutex_);
      earliest_read_time_allowed_ = regular_flushed_largest.history_cutoff();
    }
  }

  LOG_WITH_PREFIX(INFO) << "Successfully opened a RocksDB database at " << db_dir
                        << ", obj: " << db;

  return Status::OK();
}

void Tablet::RegularDbFilesChanged() {
  std::lock_guard<std::mutex> lock(num_sst_files_changed_listener_mutex_);
  if (num_sst_files_changed_listener_) {
    num_sst_files_changed_listener_();
  }
}

Status Tablet::EnableCompactions() {
  Status regular_db_status;
  std::unordered_map<std::string, std::string> new_options = {
      { "level0_slowdown_writes_trigger"s,
        std::to_string(max_if_negative(FLAGS_rocksdb_level0_slowdown_writes_trigger))},
      { "level0_stop_writes_trigger"s,
        std::to_string(max_if_negative(FLAGS_rocksdb_level0_stop_writes_trigger))},
  };
  if (regular_db_) {
    WARN_WITH_PREFIX_NOT_OK(
        regular_db_->SetOptions(new_options),
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
        intents_db_->SetOptions(new_options),
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

void Tablet::SetShutdownRequestedFlag() {
  shutdown_requested_.store(true, std::memory_order::memory_order_release);
}

void Tablet::Shutdown(IsDropTable is_drop_table) {
  SetShutdownRequestedFlag();

  auto op_pause = PauseReadWriteOperations();
  if (!op_pause.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Failed to shut down: " << op_pause.status();
    return;
  }

  if (transaction_coordinator_) {
    transaction_coordinator_->Shutdown();
  }

  std::lock_guard<rw_spinlock> lock(component_lock_);
  if (intents_db_) {
    intents_db_.get()->SetDisableFlushOnShutdown(is_drop_table);
  }
  if (regular_db_) {
    regular_db_.get()->SetDisableFlushOnShutdown(is_drop_table);
  }

  // Shutdown the RocksDB instance for this table, if present.
  // Destroy intents and regular DBs in reverse order to their creation.
  // Also it makes sure that regular DB is alive during flush filter of intents db.
  intents_db_.reset();
  regular_db_.reset();
  key_bounds_ = docdb::KeyBounds();
  state_ = kShutdown;

  // Release the mutex that prevents snapshot restore / truncate operations from running. Such
  // operations are no longer possible because the tablet has shut down. When we start the
  // "read/write operation pause", we incremented the "exclusive operation" counter. This will
  // prevent us from decrementing that counter back, disabling read/write operations permanently.
  op_pause.ReleaseMutexButKeepDisabled();
  DCHECK(op_pause.status().ok());  // Ensure that op_pause stays in scope throughout this function.
}

Result<std::unique_ptr<common::YQLRowwiseIteratorIf>> Tablet::NewRowIterator(
    const Schema &projection, const boost::optional<TransactionId>& transaction_id,
    const TableId& table_id) const {
  if (state_ != kOpen) {
    return STATUS_FORMAT(IllegalState, "Tablet in wrong state: $0", state_);
  }

  if (table_type_ != TableType::YQL_TABLE_TYPE && table_type_ != TableType::PGSQL_TABLE_TYPE) {
    return STATUS_FORMAT(NotSupported, "Invalid table type: $0", table_type_);
  }

  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  VLOG_WITH_PREFIX(2) << "Created new Iterator";

  const tablet::TableInfo* table_info = VERIFY_RESULT(metadata_->GetTableInfo(table_id));
  const Schema& schema = table_info->schema;
  auto mapped_projection = std::make_unique<Schema>();
  RETURN_NOT_OK(schema.GetMappedReadProjection(projection, mapped_projection.get()));

  auto txn_op_ctx = CreateTransactionOperationContext(transaction_id);
  auto read_time = ReadHybridTime::SingleTime(SafeTime(RequireLease::kFalse));
  auto result = std::make_unique<DocRowwiseIterator>(
      std::move(mapped_projection), schema, txn_op_ctx, doc_db(),
      CoarseTimePoint::max() /* deadline */, read_time, &pending_op_counter_);
  RETURN_NOT_OK(result->Init());
  return std::move(result);
}

Result<std::unique_ptr<common::YQLRowwiseIteratorIf>> Tablet::NewRowIterator(
    const TableId& table_id) const {
  const tablet::TableInfo* table_info = VERIFY_RESULT(metadata_->GetTableInfo(table_id));
  return NewRowIterator(table_info->schema, boost::none, table_id);
}

void Tablet::StartOperation(WriteOperationState* operation_state) {
  // If the state already has a hybrid_time then we're replaying a transaction that occurred
  // before a crash or at another node.
  HybridTime ht = operation_state->hybrid_time_even_if_unset();
  bool was_valid = ht.is_valid();
  mvcc_.AddPending(&ht);
  if (!was_valid) {
    operation_state->set_hybrid_time(ht);
  }
}

Status Tablet::ApplyRowOperations(WriteOperationState* operation_state) {
  const KeyValueWriteBatchPB& put_batch =
      operation_state->consensus_round() && operation_state->consensus_round()->replicate_msg()
          // Online case.
          ? operation_state->consensus_round()->replicate_msg()->write_request().write_batch()
          // Bootstrap case.
          : operation_state->request()->write_batch();
  if (metrics_) {
    metrics_->rows_inserted->IncrementBy(put_batch.write_pairs().size());
  }

  docdb::ConsensusFrontiers frontiers;
  set_op_id({operation_state->op_id().term(), operation_state->op_id().index()}, &frontiers);

  auto hybrid_time = operation_state->request()->has_external_hybrid_time() ?
      HybridTime(operation_state->request()->external_hybrid_time()) :
      operation_state->hybrid_time();

  set_hybrid_time(hybrid_time, &frontiers);
  return ApplyKeyValueRowOperations(put_batch, &frontiers, hybrid_time);
}

Status Tablet::CreateCheckpoint(const std::string& dir) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  auto temp_intents_dir = dir + kIntentsDBSuffix;
  auto final_intents_dir = JoinPathSegments(dir, kIntentsSubdir);

  std::lock_guard<std::mutex> lock(create_checkpoint_lock_);

  Status status;
  if (!regular_db_) {
    LOG_WITH_PREFIX(INFO) << "Skipped creating checkpoint in " << dir;
    return STATUS(NotSupported,
                  "Tablet does not have a RocksDB (could be a transaction status tablet)");
  }

  auto parent_dir = DirName(dir);
  RETURN_NOT_OK_PREPEND(metadata()->fs_manager()->CreateDirIfMissing(parent_dir),
                        Format("Unable to create checkpoints directory $0", parent_dir));

  // Order does not matter because we flush both DBs and does not have parallel writes.
  if (intents_db_) {
    status = rocksdb::checkpoint::CreateCheckpoint(intents_db_.get(), temp_intents_dir);
  }
  if (status.ok()) {
    status = rocksdb::checkpoint::CreateCheckpoint(regular_db_.get(), dir);
  }
  if (status.ok() && intents_db_) {
    status = Env::Default()->RenameFile(temp_intents_dir, final_intents_dir);
  }

  if (!status.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Create checkpoint status: " << status;
    return STATUS_FORMAT(IllegalState, "Unable to create checkpoint: $0", status);
  }
  LOG_WITH_PREFIX(INFO) << "Checkpoint created in " << dir;

  last_rocksdb_checkpoint_dir_ = dir;

  return Status::OK();
}

Status Tablet::PrepareTransactionWriteBatch(
    const KeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    rocksdb::WriteBatch* rocksdb_write_batch) {
  auto transaction_id = CHECK_RESULT(
      FullyDecodeTransactionId(put_batch.transaction().transaction_id()));
  if (put_batch.transaction().has_isolation()) {
    // Store transaction metadata (status tablet, isolation level etc.)
    if (!transaction_participant()->Add(put_batch.transaction(), rocksdb_write_batch)) {
      return STATUS(TryAgain,
                    Format("Transaction was recently aborted: $0", transaction_id), Slice(),
                    PgsqlError(YBPgErrorCode::YB_PG_IN_FAILED_SQL_TRANSACTION));
    }
  }
  auto metadata_with_write_id = transaction_participant()->MetadataWithWriteId(transaction_id);
  if (!metadata_with_write_id) {
    // If metadata is missing it could be caused by aborted and removed transaction.
    // In this case we should not add new intents for it.
    return STATUS(TryAgain,
                  Format("Transaction metadata missing: $0, looks like it was just aborted",
                         transaction_id), Slice(),
                         PgsqlError(YBPgErrorCode::YB_PG_IN_FAILED_SQL_TRANSACTION));
  }

  auto isolation_level = metadata_with_write_id->first.isolation;
  if (put_batch.row_mark_type_size() > 0) {
    // We used this as a shorthand to acquire the right locks for this operation. This doesn't
    // change the isolation level of the current transaction.
    // TODO https://github.com/yugabyte/yugabyte-db/issues/2496:
    // Use a new method to acquire the right locks. This would avoid any confusion.
    isolation_level = IsolationLevel::SERIALIZABLE_ISOLATION;
  }

  auto write_id = metadata_with_write_id->second;
  yb::docdb::PrepareTransactionWriteBatch(
      put_batch, hybrid_time, rocksdb_write_batch, transaction_id, isolation_level,
      UsePartialRangeKeyIntents(metadata_.get()), &write_id);
  transaction_participant()->UpdateLastWriteId(transaction_id, write_id);

  return Status::OK();
}

Status Tablet::ApplyKeyValueRowOperations(const KeyValueWriteBatchPB& put_batch,
                                          const rocksdb::UserFrontiers* frontiers,
                                          const HybridTime hybrid_time) {
  if (put_batch.write_pairs().empty() && put_batch.read_pairs().empty()) {
    return Status::OK();
  }

  // Could return failure only for cases where it is safe to skip applying operations to DB.
  // For instance where aborted transaction intents are written.
  // In all other cases we should crash instead of skipping apply.

  rocksdb::WriteBatch write_batch;
  if (put_batch.has_transaction()) {
    RequestScope request_scope(transaction_participant_.get());
    RETURN_NOT_OK(PrepareTransactionWriteBatch(put_batch, hybrid_time, &write_batch));
    WriteToRocksDB(frontiers, &write_batch, StorageDbType::kIntents);
  } else {
    PrepareNonTransactionWriteBatch(put_batch, hybrid_time, &write_batch);
    WriteToRocksDB(frontiers, &write_batch, StorageDbType::kRegular);
  }

  return Status::OK();
}

void Tablet::WriteToRocksDB(
    const rocksdb::UserFrontiers* frontiers,
    rocksdb::WriteBatch* write_batch,
    docdb::StorageDbType storage_db_type) {
  if (write_batch->Count() == 0) {
    return;
  }
  rocksdb::DB* dest_db = nullptr;
  switch (storage_db_type) {
    case StorageDbType::kRegular: dest_db = regular_db_.get(); break;
    case StorageDbType::kIntents: dest_db = intents_db_.get(); break;
  }

  write_batch->SetFrontiers(frontiers);

  // We are using Raft replication index for the RocksDB sequence number for
  // all members of this write batch.
  rocksdb::WriteOptions write_options;
  InitRocksDBWriteOptions(&write_options);

  auto rocksdb_write_status = dest_db->Write(write_options, write_batch);
  if (!rocksdb_write_status.ok()) {
    LOG_WITH_PREFIX(FATAL) << "Failed to write a batch with " << write_batch->Count()
                           << " operations into RocksDB: " << rocksdb_write_status;
  }

  if (FLAGS_docdb_log_write_batches) {
    LOG_WITH_PREFIX(INFO)
        << "Wrote " << write_batch->Count() << " key/value pairs to " << storage_db_type
        << " RocksDB:\n" << docdb::WriteBatchToString(
            *write_batch, storage_db_type, BinaryOutputFormat::kEscapedAndHex);
  }
}

namespace {

// Separate Redis / QL / row operations write batches from write_request in preparation for the
// write transaction. Leave just the tablet id behind. Return Redis / QL / row operations, etc.
// in batch_request.
void SetupKeyValueBatch(WriteRequestPB* write_request, WriteRequestPB* batch_request) {
  batch_request->Swap(write_request);
  write_request->set_allocated_tablet_id(batch_request->release_tablet_id());
  if (batch_request->has_read_time()) {
    write_request->set_allocated_read_time(batch_request->release_read_time());
  }
  if (batch_request->write_batch().has_transaction()) {
    write_request->mutable_write_batch()->mutable_transaction()->Swap(
        batch_request->mutable_write_batch()->mutable_transaction());
  }
  write_request->mutable_write_batch()->set_deprecated_may_have_metadata(true);
  if (batch_request->has_request_id()) {
    write_request->set_client_id1(batch_request->client_id1());
    write_request->set_client_id2(batch_request->client_id2());
    write_request->set_request_id(batch_request->request_id());
    write_request->set_min_running_request_id(batch_request->min_running_request_id());
  }
  if (batch_request->has_external_hybrid_time()) {
    write_request->set_external_hybrid_time(batch_request->external_hybrid_time());
  }
}

} // namespace

//--------------------------------------------------------------------------------------------------
// Redis Request Processing.
Status Tablet::KeyValueBatchFromRedisWriteBatch(WriteOperation* operation) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);
  docdb::DocOperations& doc_ops = operation->doc_ops();
  // Since we take exclusive locks, it's okay to use Now as the read TS for writes.
  WriteRequestPB batch_request;
  SetupKeyValueBatch(operation->request(), &batch_request);
  auto* redis_write_batch = batch_request.mutable_redis_write_batch();

  doc_ops.reserve(redis_write_batch->size());
  for (size_t i = 0; i < redis_write_batch->size(); i++) {
    doc_ops.emplace_back(new RedisWriteOperation(redis_write_batch->Mutable(i)));
  }
  RETURN_NOT_OK(StartDocWriteOperation(operation));
  if (operation->restart_read_ht().is_valid()) {
    return Status::OK();
  }
  auto* response = operation->response();
  for (size_t i = 0; i < doc_ops.size(); i++) {
    auto* redis_write_operation = down_cast<RedisWriteOperation*>(doc_ops[i].get());
    response->add_redis_response_batch()->Swap(&redis_write_operation->response());
  }

  return Status::OK();
}

Status Tablet::HandleRedisReadRequest(CoarseTimePoint deadline,
                                      const ReadHybridTime& read_time,
                                      const RedisReadRequestPB& redis_read_request,
                                      RedisResponsePB* response) {
  // TODO: move this locking to the top-level read request handler in TabletService.
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  ScopedTabletMetricsTracker metrics_tracker(metrics_->redis_read_latency);

  docdb::RedisReadOperation doc_op(redis_read_request, doc_db(), deadline, read_time);
  RETURN_NOT_OK(doc_op.Execute());
  *response = std::move(doc_op.response());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// CQL Request Processing.
Status Tablet::HandleQLReadRequest(
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    const QLReadRequestPB& ql_read_request,
    const TransactionMetadataPB& transaction_metadata,
    QLReadRequestResult* result) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);
  ScopedTabletMetricsTracker metrics_tracker(metrics_->ql_read_latency);

  if (metadata()->schema_version() != ql_read_request.schema_version()) {
    result->response.set_status(QLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH);
    return Status::OK();
  }

  Result<TransactionOperationContextOpt> txn_op_ctx =
      CreateTransactionOperationContext(transaction_metadata);
  RETURN_NOT_OK(txn_op_ctx);
  return AbstractTablet::HandleQLReadRequest(
      deadline, read_time, ql_read_request, *txn_op_ctx, result);
}

CHECKED_STATUS Tablet::CreatePagingStateForRead(const QLReadRequestPB& ql_read_request,
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
      const string& next_partition_key = metadata_->partition().partition_key_end();
      if (!next_partition_key.empty()) {
        uint16_t next_hash_code = PartitionSchema::DecodeMultiColumnHashValue(next_partition_key);

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

void Tablet::KeyValueBatchFromQLWriteBatch(std::unique_ptr<WriteOperation> operation) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  if (!scoped_read_operation.ok()) {
    WriteOperation::StartSynchronization(std::move(operation), MoveStatus(scoped_read_operation));
    return;
  }

  docdb::DocOperations& doc_ops = operation->doc_ops();
  WriteRequestPB batch_request;
  SetupKeyValueBatch(operation->request(), &batch_request);
  auto* ql_write_batch = batch_request.mutable_ql_write_batch();

  doc_ops.reserve(ql_write_batch->size());

  Result<TransactionOperationContextOpt> txn_op_ctx =
      CreateTransactionOperationContext(operation->request()->write_batch().transaction());
  if (!txn_op_ctx.ok()) {
    WriteOperation::StartSynchronization(std::move(operation), txn_op_ctx.status());
    return;
  }
  for (size_t i = 0; i < ql_write_batch->size(); i++) {
    QLWriteRequestPB* req = ql_write_batch->Mutable(i);
    QLResponsePB* resp = operation->response()->add_ql_response_batch();
    if (metadata_->schema_version() != req->schema_version()) {
      resp->set_status(QLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH);
    } else {
      auto write_op = std::make_unique<QLWriteOperation>(
          metadata_->schema(), metadata_->index_map(), unique_index_key_schema_.get_ptr(),
          *txn_op_ctx);
      auto status = write_op->Init(req, resp);
      if (!status.ok()) {
        WriteOperation::StartSynchronization(std::move(operation), status);
        return;
      }
      doc_ops.emplace_back(std::move(write_op));
    }
  }

  // All operations has wrong schema version
  if (doc_ops.empty()) {
    WriteOperation::StartSynchronization(std::move(operation), Status::OK());
    return;
  }

  auto status = StartDocWriteOperation(operation.get());
  if (operation->restart_read_ht().is_valid()) {
    WriteOperation::StartSynchronization(std::move(operation), Status::OK());
    return;
  }

  if (status.ok()) {
    UpdateQLIndexes(std::move(operation));
  } else {
    CompleteQLWriteBatch(std::move(operation), status);
  }
}

void Tablet::CompleteQLWriteBatch(std::unique_ptr<WriteOperation> operation, const Status& status) {
  if (!status.ok()) {
    WriteOperation::StartSynchronization(std::move(operation), status);
    return;
  }
  auto& doc_ops = operation->doc_ops();

  for (size_t i = 0; i < doc_ops.size(); i++) {
    QLWriteOperation* ql_write_op = down_cast<QLWriteOperation*>(doc_ops[i].get());
    if (metadata_->is_unique_index() &&
        ql_write_op->request().type() == QLWriteRequestPB::QL_STMT_INSERT &&
        ql_write_op->response()->has_applied() && !ql_write_op->response()->applied()) {
      // If this is an insert into a unique index and it fails to apply, report duplicate value err.
      ql_write_op->response()->set_status(QLResponsePB::YQL_STATUS_USAGE_ERROR);
      ql_write_op->response()->set_error_message(
          Format("Duplicate value disallowed by unique index $0", metadata_->table_name()));
    } else if (ql_write_op->rowblock() != nullptr) {
      // If the QL write op returns a rowblock, move the op to the transaction state to return the
      // rows data as a sidecar after the transaction completes.
      doc_ops[i].release();
      operation->state()->ql_write_ops()->emplace_back(unique_ptr<QLWriteOperation>(ql_write_op));
    }
  }

  WriteOperation::StartSynchronization(std::move(operation), Status::OK());
}

void Tablet::UpdateQLIndexes(std::unique_ptr<WriteOperation> operation) {
  client::YBClient* client = nullptr;
  client::YBSessionPtr session;
  client::YBTransactionPtr txn;
  std::vector<std::pair<std::shared_ptr<client::YBqlWriteOp>, QLWriteOperation*>> index_ops;
  const ChildTransactionDataPB* child_transaction_data = nullptr;
  for (auto& doc_op : operation->doc_ops()) {
    auto* write_op = static_cast<QLWriteOperation*>(doc_op.get());
    if (write_op->index_requests()->empty()) {
      continue;
    }
    if (!client) {
      client = client_future_.get();
      session = std::make_shared<YBSession>(client);
      if (write_op->request().has_child_transaction_data()) {
        child_transaction_data = &write_op->request().child_transaction_data();
        if (!transaction_manager_) {
          auto status = STATUS(Corruption, "Transaction manager is not present for index update");
          operation->state()->CompleteWithStatus(status);
          return;
        }
        auto child_data = ChildTransactionData::FromPB(
            write_op->request().child_transaction_data());
        if (!child_data.ok()) {
          operation->state()->CompleteWithStatus(child_data.status());
          return;
        }
        txn = std::make_shared<YBTransaction>(&transaction_manager_.get(), *child_data);
        session->SetTransaction(txn);
      } else {
        child_transaction_data = nullptr;
      }
    } else if (write_op->request().has_child_transaction_data()) {
      DCHECK_ONLY_NOTNULL(child_transaction_data);
      DCHECK_EQ(child_transaction_data->ShortDebugString(),
                write_op->request().child_transaction_data().ShortDebugString());
    } else {
      DCHECK(child_transaction_data == nullptr) <<
          "Value: " << child_transaction_data->ShortDebugString();
    }

    // Apply the write ops to update the index
    for (auto& pair : *write_op->index_requests()) {
      client::YBTablePtr index_table;
      bool cache_used_ignored = false;
      if (!metadata_cache_) {
        auto status = STATUS(Corruption, "Table metadata cache is not present for index update");
        operation->state()->CompleteWithStatus(status);
        return;
      }
      // TODO create async version of GetTable.
      // It is ok to have sync call here, because we use cache and it should not take too long.
      auto status = metadata_cache_->GetTable(pair.first->table_id(), &index_table,
                                              &cache_used_ignored);
      if (!status.ok()) {
        operation->state()->CompleteWithStatus(status);
        return;
      }
      shared_ptr<client::YBqlWriteOp> index_op(index_table->NewQLWrite());
      index_op->mutable_request()->Swap(&pair.second);
      index_op->mutable_request()->MergeFrom(pair.second);
      status = session->Apply(index_op);
      if (!status.ok()) {
        operation->state()->CompleteWithStatus(status);
        return;
      }
      index_ops.emplace_back(std::move(index_op), write_op);
    }
  }

  if (!session) {
    CompleteQLWriteBatch(std::move(operation), Status::OK());
    return;
  }

  session->FlushAsync(
      [this, op = operation.release(), session, txn, index_ops = std::move(index_ops)]
          (const Status& status) {
    std::unique_ptr<WriteOperation> operation(op);

    if (PREDICT_FALSE(!status.ok())) {
      // When any error occurs during the dispatching of YBOperation, YBSession saves the error and
      // returns IOError. When it happens, retrieves the errors and discard the IOError.
      if (status.IsIOError()) {
        for (const auto& error : session->GetPendingErrors()) {
          // return just the first error seen.
          operation->state()->CompleteWithStatus(error->status());
          return;
        }
      }
      operation->state()->CompleteWithStatus(status);
      return;
    }

    ChildTransactionResultPB child_result;
    if (txn) {
      auto finish_result = txn->FinishChild();
      if (!finish_result.ok()) {
        operation->state()->CompleteWithStatus(finish_result.status());
        return;
      }
      child_result = std::move(*finish_result);
    }

    // Check the responses of the index write ops.
    for (const auto& pair : index_ops) {
      shared_ptr<client::YBqlWriteOp> index_op = pair.first;
      auto* response = pair.second->response();
      DCHECK_ONLY_NOTNULL(response);
      auto* index_response = index_op->mutable_response();

      if (index_response->status() != QLResponsePB::YQL_STATUS_OK) {
        response->set_status(index_response->status());
        response->set_error_message(std::move(*index_response->mutable_error_message()));
      }
      if (txn) {
        *response->mutable_child_transaction_result() = child_result;
      }
    }

    CompleteQLWriteBatch(std::move(operation), Status::OK());
  });
}

//--------------------------------------------------------------------------------------------------
// PGSQL Request Processing.
Status Tablet::HandlePgsqlReadRequest(
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    const PgsqlReadRequestPB& pgsql_read_request,
    const TransactionMetadataPB& transaction_metadata,
    PgsqlReadRequestResult* result) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);
  // TODO(neil) Work on metrics for PGSQL.
  // ScopedTabletMetricsTracker metrics_tracker(metrics_->pgsql_read_latency);

  const tablet::TableInfo* table_info =
      VERIFY_RESULT(metadata_->GetTableInfo(pgsql_read_request.table_id()));
  // Assert the table is a Postgres table.
  DCHECK_EQ(table_info->table_type, TableType::PGSQL_TABLE_TYPE);
  if (table_info->schema_version != pgsql_read_request.schema_version()) {
    result->response.set_status(PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH);
    return Status::OK();
  }

  Result<TransactionOperationContextOpt> txn_op_ctx =
      CreateTransactionOperationContext(transaction_metadata);
  RETURN_NOT_OK(txn_op_ctx);
  return AbstractTablet::HandlePgsqlReadRequest(
      deadline, read_time, pgsql_read_request, *txn_op_ctx, result);
}

CHECKED_STATUS Tablet::CreatePagingStateForRead(const PgsqlReadRequestPB& pgsql_read_request,
                                                const size_t row_count,
                                                PgsqlResponsePB* response) const {
  // If there is no hash column in the read request, this is a full-table query. And if there is no
  // paging state in the response, we are done reading from the current tablet. In this case, we
  // should return the exclusive end partition key of this tablet if not empty which is the start
  // key of the next tablet. Do so only if the request has no row count limit, or there is and we
  // haven't hit it, or we are asked to return paging state even when we have hit the limit.
  // Otherwise, leave the paging state empty which means we are completely done reading for the
  // whole SELECT statement.
  if (pgsql_read_request.partition_column_values().empty() &&
      pgsql_read_request.ybctid_column_value().value().binary_value().empty() &&
      !response->has_paging_state() &&
      (!pgsql_read_request.has_limit() || row_count < pgsql_read_request.limit() ||
       pgsql_read_request.return_paging_state())) {
    const string& next_partition_key = metadata_->partition().partition_key_end();
    if (!next_partition_key.empty()) {
      response->mutable_paging_state()->set_next_partition_key(next_partition_key);
    }
  }

  // If there is a paging state, update the total number of rows read so far.
  if (response->has_paging_state()) {
    response->mutable_paging_state()->set_total_num_rows_read(
        pgsql_read_request.paging_state().total_num_rows_read() + row_count);
  }
  return Status::OK();
}

Status Tablet::KeyValueBatchFromPgsqlWriteBatch(WriteOperation* operation) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);
  docdb::DocOperations& doc_ops = operation->doc_ops();
  WriteRequestPB batch_request;

  SetupKeyValueBatch(operation->request(), &batch_request);
  auto* pgsql_write_batch = batch_request.mutable_pgsql_write_batch();

  doc_ops.reserve(pgsql_write_batch->size());

  Result<TransactionOperationContextOpt> txn_op_ctx =
      CreateTransactionOperationContext(operation->request()->write_batch().transaction());
  RETURN_NOT_OK(txn_op_ctx);
  for (size_t i = 0; i < pgsql_write_batch->size(); i++) {
    PgsqlWriteRequestPB* req = pgsql_write_batch->Mutable(i);
    PgsqlResponsePB* resp = operation->response()->add_pgsql_response_batch();
    const tablet::TableInfo* table_info = VERIFY_RESULT(metadata_->GetTableInfo(req->table_id()));
    if (table_info->schema_version != req->schema_version()) {
      resp->set_status(PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH);
    } else {
      auto write_op = std::make_unique<PgsqlWriteOperation>(table_info->schema, *txn_op_ctx);
      RETURN_NOT_OK(write_op->Init(req, resp));
      doc_ops.emplace_back(std::move(write_op));
    }
  }

  // All operations have wrong schema version.
  if (doc_ops.empty()) {
    return Status::OK();
  }

  RETURN_NOT_OK(StartDocWriteOperation(operation));
  if (operation->restart_read_ht().is_valid()) {
    return Status::OK();
  }
  for (size_t i = 0; i < doc_ops.size(); i++) {
    PgsqlWriteOperation* pgsql_write_op = down_cast<PgsqlWriteOperation*>(doc_ops[i].get());
    // We'll need to return the number of rows inserted, updated, or deleted by each operation.
    doc_ops[i].release();
    operation->state()->pgsql_write_ops()
                      ->emplace_back(unique_ptr<PgsqlWriteOperation>(pgsql_write_op));
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

void Tablet::AcquireLocksAndPerformDocOperations(std::unique_ptr<WriteOperation> operation) {
  if (table_type_ == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    operation->state()->CompleteWithStatus(
        STATUS(NotSupported, "Transaction status table does not support write"));
    return;
  }

  const WriteRequestPB* key_value_write_request = operation->state()->request();

  if (!key_value_write_request->redis_write_batch().empty()) {
    auto status = KeyValueBatchFromRedisWriteBatch(operation.get());
    WriteOperation::StartSynchronization(std::move(operation), status);
    return;
  }

  if (!key_value_write_request->ql_write_batch().empty()) {
    KeyValueBatchFromQLWriteBatch(std::move(operation));
    return;
  }

  if (!key_value_write_request->pgsql_write_batch().empty()) {
    auto status = KeyValueBatchFromPgsqlWriteBatch(operation.get());
    WriteOperation::StartSynchronization(std::move(operation), status);
    return;
  }

  if (key_value_write_request->has_write_batch()) {
    Status status;
    if (!key_value_write_request->write_batch().read_pairs().empty()) {
      status = StartDocWriteOperation(operation.get());
    } else {
      DCHECK(key_value_write_request->has_external_hybrid_time());
    }
    WriteOperation::StartSynchronization(std::move(operation), status);
    return;
  }

  // Empty write should not happen, but we could handle it.
  // Just report it as error in release mode.
  LOG(DFATAL) << "Empty write";

  operation->state()->CompleteWithStatus(Status::OK());
}

Status Tablet::Flush(FlushMode mode, FlushFlags flags, int64_t ignore_if_flushed_after_tick) {
  TRACE_EVENT0("tablet", "Tablet::Flush");

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
    intents_db_->WaitForFlush();
  }

  return Status::OK();
}

Status Tablet::WaitForFlush() {
  TRACE_EVENT0("tablet", "Tablet::WaitForFlush");

  RETURN_NOT_OK(regular_db_->WaitForFlush());
  if (intents_db_) {
    RETURN_NOT_OK(intents_db_->WaitForFlush());
  }

  return Status::OK();
}

Status Tablet::ImportData(const std::string& source_dir) {
  // We import only regular records, so don't have to deal with intents here.
  return regular_db_->Import(source_dir);
}

template <class Data>
void InitFrontiers(const Data& data, docdb::ConsensusFrontiers* frontiers) {
  set_op_id({data.op_id.term(), data.op_id.index()}, frontiers);
  set_hybrid_time(data.log_ht, frontiers);
}

// We apply intents by iterating over whole transaction reverse index.
// Using value of reverse index record we find original intent record and apply it.
// After that we delete both intent record and reverse index record.
// TODO(dtxn) use multiple batches when applying really big transaction.
Status Tablet::ApplyIntents(const TransactionApplyData& data) {
  rocksdb::WriteBatch regular_write_batch;
  RETURN_NOT_OK(docdb::PrepareApplyIntentsBatch(
      data.transaction_id, data.commit_ht, &key_bounds_,
      &regular_write_batch, intents_db_.get(), nullptr /* intents_write_batch */));

  // data.hybrid_time contains transaction commit time.
  // We don't set transaction field of put_batch, otherwise we would write another bunch of intents.
  docdb::ConsensusFrontiers frontiers;
  InitFrontiers(data, &frontiers);
  WriteToRocksDB(&frontiers, &regular_write_batch, StorageDbType::kRegular);
  return Status::OK();
}

template <class Ids>
CHECKED_STATUS Tablet::RemoveIntentsImpl(const RemoveIntentsData& data, const Ids& ids) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  rocksdb::WriteBatch intents_write_batch;
  for (const auto& id : ids) {
    RETURN_NOT_OK(docdb::PrepareApplyIntentsBatch(
        id, HybridTime() /* commit_ht */, &key_bounds_, nullptr /* regular_write_batch */,
        intents_db_.get(), &intents_write_batch));
  }

  docdb::ConsensusFrontiers frontiers;
  InitFrontiers(data, &frontiers);
  WriteToRocksDB(&frontiers, &intents_write_batch, StorageDbType::kIntents);
  return Status::OK();
}


Status Tablet::RemoveIntents(const RemoveIntentsData& data, const TransactionId& id) {
  return RemoveIntentsImpl(data, std::initializer_list<TransactionId>{id});
}

Status Tablet::RemoveIntents(const RemoveIntentsData& data, const TransactionIdSet& transactions) {
  return RemoveIntentsImpl(data, transactions);
}

HybridTime Tablet::ApplierSafeTime(HybridTime min_allowed, CoarseTimePoint deadline) {
  // We could not use mvcc_ directly, because correct lease should be passed to it.
  return SafeTime(RequireLease::kFalse, min_allowed, deadline);
}

Status Tablet::CreatePreparedChangeMetadata(ChangeMetadataOperationState *operation_state,
                                            const Schema* schema) {
  if (schema) {
    if (!key_schema_.KeyEquals(*schema)) {
      return STATUS(InvalidArgument, "Schema keys cannot be altered",
          schema->CreateKeyProjection().ToString());
    }

    if (!schema->has_column_ids()) {
      // this probably means that the request is not from the Master
      return STATUS(InvalidArgument, "Missing Column IDs");
    }
  }

  // Alter schema must run when no reads/writes are in progress.
  // However, compactions and flushes can continue to run in parallel
  // with the schema change,
  operation_state->AcquireSchemaLock(&schema_lock_);

  operation_state->set_schema(schema);
  return Status::OK();
}

Status Tablet::AddTable(const TableInfoPB& table_info) {
  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(table_info.schema(), &schema));

  PartitionSchema partition_schema;
  RETURN_NOT_OK(PartitionSchema::FromPB(table_info.partition_schema(), schema, &partition_schema));

  metadata_->AddTable(
      table_info.table_id(), table_info.table_name(), table_info.table_type(), schema, IndexMap(),
      partition_schema, boost::none, table_info.schema_version());

  RETURN_NOT_OK(metadata_->Flush());

  return Status::OK();
}

Status Tablet::AlterSchema(ChangeMetadataOperationState *operation_state) {
  DCHECK(key_schema_.KeyEquals(*DCHECK_NOTNULL(operation_state->schema())))
      << "Schema keys cannot be altered";

  // If the current version >= new version, there is nothing to do.
  if (metadata_->schema_version() >= operation_state->schema_version()) {
    LOG_WITH_PREFIX(INFO)
        << "Already running schema version " << metadata_->schema_version()
        << " got alter request for version " << operation_state->schema_version();
    return Status::OK();
  }

  LOG_WITH_PREFIX(INFO) << "Alter schema from " << schema()->ToString()
                        << " version " << metadata_->schema_version()
                        << " to " << operation_state->schema()->ToString()
                        << " version " << operation_state->schema_version();
  DCHECK(schema_lock_.is_locked());

  // Find out which columns have been deleted in this schema change, and add them to metadata.
  vector<DeletedColumn> deleted_cols;
  for (const auto& col : schema()->column_ids()) {
    if (operation_state->schema()->find_column_by_id(col) == Schema::kColumnNotFound) {
      deleted_cols.emplace_back(col, clock_->Now());
      LOG_WITH_PREFIX(INFO) << "Column " << col << " recorded as deleted.";
    }
  }

  metadata_->SetSchema(*operation_state->schema(), operation_state->index_map(), deleted_cols,
                       operation_state->schema_version());
  if (operation_state->has_new_table_name()) {
    metadata_->SetTableName(operation_state->new_table_name());
    if (metric_entity_) {
      metric_entity_->SetAttribute("table_name", operation_state->new_table_name());
    }
  }

  // Clear old index table metadata cache.
  metadata_cache_ = boost::none;

  // Create transaction manager and index table metadata cache for secondary index update.
  if (!metadata_->index_map().empty()) {
    if (metadata_->schema().table_properties().is_transactional() && !transaction_manager_) {
      transaction_manager_.emplace(client_future_.get(),
                                   scoped_refptr<server::Clock>(clock_),
                                   local_tablet_filter_);
    }
    metadata_cache_.emplace(client_future_.get(), false /* Update permissions cache */);
  }

  // Flush the updated schema metadata to disk.
  return metadata_->Flush();
}

Status Tablet::AlterWalRetentionSecs(ChangeMetadataOperationState* operation_state) {
  if (operation_state->has_wal_retention_secs()) {
    LOG_WITH_PREFIX(INFO) << "Altering metadata wal_retention_secs from "
                          << metadata_->wal_retention_secs()
                          << " to " << operation_state->wal_retention_secs();
    metadata_->set_wal_retention_secs(operation_state->wal_retention_secs());
    // Flush the updated schema metadata to disk.
    return metadata_->Flush();
  }
  return STATUS_SUBSTITUTE(InvalidArgument, "Invalid ChangeMetadataOperationState: $0",
      operation_state->ToString());
}

ScopedPendingOperationPause Tablet::PauseReadWriteOperations() {
  LOG_SLOW_EXECUTION(WARNING, 1000,
                     Substitute("Tablet $0: Waiting for pending ops to complete", tablet_id())) {
    return ScopedPendingOperationPause(
        &pending_op_counter_,
        MonoDelta::FromMilliseconds(FLAGS_tablet_rocksdb_ops_quiet_down_timeout_ms));
  }
  FATAL_ERROR("Unreachable code -- the previous block must always return");
}

Status Tablet::ModifyFlushedFrontier(
    const docdb::ConsensusFrontier& frontier,
    rocksdb::FrontierModificationMode mode) {
  const Status s = regular_db_->ModifyFlushedFrontier(frontier.Clone(), mode);
  if (PREDICT_FALSE(!s.ok())) {
    auto status = STATUS(IllegalState, "Failed to set flushed frontier", s.ToString());
    LOG_WITH_PREFIX(WARNING) << status;
    return status;
  }
  DCHECK_EQ(frontier, *regular_db_->GetFlushedFrontier());

  if (FLAGS_tablet_verify_flushed_frontier_after_modifying &&
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
        &rocksdb_options, LogPrefix(), /* statistics */ nullptr, tablet_options_);
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

  return Flush(FlushMode::kAsync);
}

Status Tablet::Truncate(TruncateOperationState *state) {
  if (metadata_->table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    // We use only Raft log for transaction status table.
    return Status::OK();
  }

  auto op_pause = PauseReadWriteOperations();
  RETURN_NOT_OK(op_pause);

  // Check if tablet is in shutdown mode.
  if (IsShutdownRequested()) {
    return STATUS(IllegalState, "Tablet was shut down");
  }

  const rocksdb::SequenceNumber sequence_number = regular_db_->GetLatestSequenceNumber();
  const string db_dir = regular_db_->GetName();

  rocksdb::Options rocksdb_options;
  docdb::InitRocksDBOptions(&rocksdb_options, LogPrefix(), rocksdb_statistics_, tablet_options_);

  Status intents_status;
  if (intents_db_) {
    auto intents_dir = intents_db_->GetName();
    intents_db_.reset();
    intents_status = rocksdb::DestroyDB(intents_dir, rocksdb_options);
  }
  regular_db_.reset();
  auto s = rocksdb::DestroyDB(db_dir, rocksdb_options);
  if (s.ok() && !intents_status.ok()) {
    s = intents_status;
  }
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Failed to clean up db dir " << db_dir << ": " << s;
    return STATUS(IllegalState, "Failed to clean up db dir", s.ToString());
  }
  key_bounds_ = docdb::KeyBounds::kNoBounds;

  // Create a new database.
  // Note: db_dir == metadata()->rocksdb_dir() is still valid db dir.
  s = OpenKeyValueTablet();
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Failed to create a new db: " << s;
    return s;
  }

  docdb::ConsensusFrontier frontier;
  frontier.set_op_id({state->op_id().term(), state->op_id().index()});
  frontier.set_hybrid_time(state->hybrid_time());
  // We use the kUpdate mode here, because unlike the case of restoring a snapshot to a completely
  // different tablet in an arbitrary Raft group, here there is no possibility of the flushed
  // frontier needing to go backwards.
  RETURN_NOT_OK(ModifyFlushedFrontier(frontier, rocksdb::FrontierModificationMode::kUpdate));

  LOG_WITH_PREFIX(INFO) << "Created new db for truncated tablet";
  LOG_WITH_PREFIX(INFO) << "Sequence numbers: old=" << sequence_number
                        << ", new=" << regular_db_->GetLatestSequenceNumber();
  DCHECK(op_pause.status().ok());  // Ensure that op_pause stays in scope throughout this function.
  return EnableCompactions();
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

  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
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
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  return DocDbOpIds{
      MaxPersistentOpIdForDb(regular_db_.get(), invalid_if_no_new_data),
      MaxPersistentOpIdForDb(intents_db_.get(), invalid_if_no_new_data)
  };
}

void Tablet::FlushIntentsDbIfNecessary(const yb::OpId& lastest_log_entry_op_id) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  if (!scoped_read_operation.ok()) {
    return;
  }

  auto intents_frontier = intents_db_
      ? intents_db_->GetMutableMemTableFrontier(rocksdb::UpdateUserValueType::kLargest) : nullptr;
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
        intents_db_->Flush(options);
      }
    }
  }
}

Result<HybridTime> Tablet::MaxPersistentHybridTime() const {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
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
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  HybridTime result = HybridTime::kMax;
  for (auto* db : { regular_db_.get(), intents_db_.get() }) {
    if (db) {
      auto mem_frontier = db->GetMutableMemTableFrontier(rocksdb::UpdateUserValueType::kSmallest);
      if (mem_frontier) {
        const auto hybrid_time =
            static_cast<const docdb::ConsensusFrontier&>(*mem_frontier).hybrid_time();
        result = std::min(result, hybrid_time);
      }
    }
  }
  return result;
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
  docdb::DocDBDebugDump(regular_db_.get(), LOG_STRING(INFO, lines), docdb::StorageDbType::kRegular);
}

Status Tablet::TEST_SwitchMemtable() {
  ScopedPendingOperation scoped_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_operation);

  if (regular_db_) {
    regular_db_->TEST_SwitchMemtable();
  } else {
    LOG_WITH_PREFIX(INFO) << "Ignoring TEST_SwitchMemtable: no regular RocksDB";
  }
  return Status::OK();
}

Status Tablet::StartDocWriteOperation(WriteOperation* operation) {
  auto write_batch = operation->request()->mutable_write_batch();
  auto isolation_level = VERIFY_RESULT(GetIsolationLevelFromPB(*write_batch));

  const bool transactional_table = metadata_->schema().table_properties().is_transactional();

  const auto partial_range_key_intents = UsePartialRangeKeyIntents(metadata_.get());
  auto prepare_result = VERIFY_RESULT(docdb::PrepareDocWriteOperation(
      operation->doc_ops(), write_batch->read_pairs(), metrics_->write_lock_latency,
      isolation_level, operation->state()->kind(), transactional_table, operation->deadline(),
      partial_range_key_intents, &shared_lock_manager_));

  RequestScope request_scope;
  if (transaction_participant_) {
    request_scope = RequestScope(transaction_participant_.get());
  }

  auto read_time = operation->read_time();
  const bool allow_immediate_read_restart = !read_time;

  if (transactional_table) {
    if (isolation_level == IsolationLevel::NON_TRANSACTIONAL) {
      auto now = clock_->Now();
      auto result = VERIFY_RESULT(docdb::ResolveOperationConflicts(
          operation->doc_ops(), now, doc_db(), partial_range_key_intents,
          transaction_participant_.get()));
      if (now != result) {
        clock_->Update(result);
      }
    } else {
      if (isolation_level == IsolationLevel::SERIALIZABLE_ISOLATION &&
          prepare_result.need_read_snapshot) {
        boost::container::small_vector<RefCntPrefix, 16> paths;
        for (const auto& doc_op : operation->doc_ops()) {
          paths.clear();
          IsolationLevel ignored_isolation_level;
          RETURN_NOT_OK(doc_op->GetDocPaths(
              docdb::GetDocPathsMode::kLock, &paths, &ignored_isolation_level));
          for (const auto& path : paths) {
            auto key = path.as_slice();
            auto* pair = write_batch->mutable_read_pairs()->Add();
            pair->set_key(key.data(), key.size());
            // Empty values are disallowed by docdb.
            // https://github.com/YugaByte/yugabyte-db/issues/736
            pair->set_value(std::string(1, docdb::ValueTypeAsChar::kNullLow));
          }
        }
      }

      RETURN_NOT_OK(docdb::ResolveTransactionConflicts(
          operation->doc_ops(), *write_batch, clock_->Now(),
          read_time ? read_time.read : HybridTime::kMax, doc_db(), partial_range_key_intents,
          transaction_participant_.get(), metrics_->transaction_conflicts.get()));

      if (!read_time) {
        auto safe_time = SafeTime(RequireLease::kTrue);
        read_time = ReadHybridTime::FromHybridTimeRange({safe_time, clock_->NowRange().second});
      } else if (prepare_result.need_read_snapshot &&
                 isolation_level == IsolationLevel::SERIALIZABLE_ISOLATION) {
        auto status = STATUS_FORMAT(
            InvalidArgument,
            "Read time should NOT be specified for serializable isolation level: $0",
            read_time);
        LOG(DFATAL) << status;
        return status;
      }
    }
  }

  auto read_op = prepare_result.need_read_snapshot
      ? VERIFY_RESULT(ScopedReadOperation::Create(this, RequireLease::kTrue, read_time))
      : ScopedReadOperation();
  // Actual read hybrid time used for read-modify-write operation.
  auto real_read_time = prepare_result.need_read_snapshot
      ? read_op.read_time()
      // When need_read_snapshot is false, this time is used only to write TTL field of record.
      : ReadHybridTime::SingleTime(clock_->Now());


  // We expect all read operations for this transaction to be done in ExecuteDocWriteOperation.
  // Once read_txn goes out of scope, the read point is deregistered.
  HybridTime restart_read_ht;
  bool local_limit_updated = false;

  // This loop may be executed multiple times multiple times only for serializable isolation or
  // when read_time was not yet picked for snapshot isolation.
  // In all other cases it is executed only once.
  for (;;) {
    RETURN_NOT_OK(docdb::ExecuteDocWriteOperation(
        operation->doc_ops(), operation->deadline(), real_read_time, doc_db(), write_batch,
        table_type_ == TableType::REDIS_TABLE_TYPE
            ? InitMarkerBehavior::kRequired
            : InitMarkerBehavior::kOptional,
        &monotonic_counter_, &restart_read_ht));

    // For serializable isolation we don't fix read time, so could do read restart locally,
    // instead of failing whole transaction.
    if (!restart_read_ht.is_valid() || !allow_immediate_read_restart) {
      break;
    }

    real_read_time.read = restart_read_ht;
    if (!local_limit_updated) {
      local_limit_updated = true;
      real_read_time.local_limit =
          std::min(real_read_time.local_limit, SafeTime(RequireLease::kTrue));
    }

    restart_read_ht = HybridTime();

    operation->request()->mutable_write_batch()->clear_write_pairs();

    for (auto& doc_op : operation->doc_ops()) {
      doc_op->ClearResponse();
    }
  }

  operation->SetRestartReadHt(restart_read_ht);

  if (allow_immediate_read_restart && isolation_level != IsolationLevel::NON_TRANSACTIONAL &&
      operation->response()) {
    real_read_time.ToPB(operation->response()->mutable_used_read_time());
  }

  if (operation->restart_read_ht().is_valid()) {
    return Status::OK();
  }

  operation->state()->ReplaceDocDBLocks(std::move(prepare_result.lock_batch));

  return Status::OK();
}

HybridTime Tablet::DoGetSafeTime(
    tablet::RequireLease require_lease, HybridTime min_allowed, CoarseTimePoint deadline) const {
  HybridTime ht_lease;
  if (!require_lease) {
    return mvcc_.SafeTimeForFollower(min_allowed, deadline);
  }
  if (require_lease && ht_lease_provider_) {
    // min_allowed could contain non zero logical part, so we add one microsecond to be sure that
    // the resulting ht_lease is at least min_allowed.
    auto min_allowed_lease = min_allowed.GetPhysicalValueMicros();
    if (min_allowed.GetLogicalValue()) {
      ++min_allowed_lease;
    }
    // This will block until a leader lease reaches the given value or a timeout occurs.
    ht_lease = ht_lease_provider_(min_allowed_lease, deadline);
    if (!ht_lease) {
      // This could happen in case of timeout.
      return HybridTime::kInvalid;
    }
  } else {
    ht_lease = HybridTime::kMax;
  }
  if (min_allowed > ht_lease) {
    LOG_WITH_PREFIX(DFATAL)
        << "Read request hybrid time after leader lease: " << min_allowed << ", " << ht_lease;
    return HybridTime::kInvalid;
  }
  return mvcc_.SafeTime(min_allowed, deadline, ht_lease);
}

HybridTime Tablet::UpdateHistoryCutoff(HybridTime proposed_cutoff) {
  std::lock_guard<std::mutex> lock(active_readers_mutex_);
  HybridTime allowed_cutoff;
  if (active_readers_cnt_.empty()) {
    // There are no readers restricting our garbage collection of old records.
    allowed_cutoff = proposed_cutoff;
  } else {
    // Cannot garbage-collect any records that are still being read.
    allowed_cutoff = std::min(proposed_cutoff, active_readers_cnt_.begin()->first);
  }
  earliest_read_time_allowed_ = std::max(earliest_read_time_allowed_, proposed_cutoff);
  return allowed_cutoff;
}

Status Tablet::RegisterReaderTimestamp(HybridTime read_point) {
  std::lock_guard<std::mutex> lock(active_readers_mutex_);
  if (read_point < earliest_read_time_allowed_) {
    return STATUS_FORMAT(
        SnapshotTooOld,
        "Snapshot too old. Read point: $0, earliest read time allowed: $1, delta (usec): $2",
        read_point,
        earliest_read_time_allowed_,
        earliest_read_time_allowed_.PhysicalDiff(read_point));
}
  active_readers_cnt_[read_point]++;
  return Status::OK();
}

void Tablet::UnregisterReader(HybridTime timestamp) {
  std::lock_guard<std::mutex> lock(active_readers_mutex_);
  active_readers_cnt_[timestamp]--;
  if (active_readers_cnt_[timestamp] == 0) {
    active_readers_cnt_.erase(timestamp);
  }
}

namespace {

void ForceRocksDBCompact(rocksdb::DB* db) {
  db->CompactRange(rocksdb::CompactRangeOptions(), /* begin = */ nullptr, /* end = */ nullptr);
  while (true) {
    uint64_t compaction_pending = 0;
    uint64_t running_compactions = 0;
    db->GetIntProperty("rocksdb.compaction-pending", &compaction_pending);
    db->GetIntProperty("rocksdb.num-running-compactions", &running_compactions);
    if (!compaction_pending && !running_compactions) {
      return;
    }

    SleepFor(MonoDelta::FromMilliseconds(10));
  }
}

} // namespace

void Tablet::ForceRocksDBCompactInTest() {
  if (regular_db_) {
    ForceRocksDBCompact(regular_db_.get());
  }
  if (intents_db_) {
    intents_db_->Flush(rocksdb::FlushOptions());
    ForceRocksDBCompact(intents_db_.get());
  }
}

std::string Tablet::TEST_DocDBDumpStr(IncludeIntents include_intents) {
  if (!regular_db_) return "";

  if (!include_intents) {
    return docdb::DocDBDebugDumpToStr(doc_db().WithoutIntents());
  }

  return docdb::DocDBDebugDumpToStr(doc_db());
}

template <class T>
void Tablet::TEST_DocDBDumpToContainer(IncludeIntents include_intents, T* out) {
  if (!regular_db_) return;

  if (!include_intents) {
    return docdb::DocDBDebugDumpToContainer(doc_db().WithoutIntents(), out);
  }

  return docdb::DocDBDebugDumpToContainer(doc_db(), out);
}

template void Tablet::TEST_DocDBDumpToContainer(
    IncludeIntents include_intents, std::unordered_set<std::string>* out);

template void Tablet::TEST_DocDBDumpToContainer(
    IncludeIntents include_intents, std::vector<std::string>* out);

size_t Tablet::TEST_CountRegularDBRecords() {
  if (!regular_db_) return 0;
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  docdb::BoundedRocksDbIterator iter(regular_db_.get(), read_opts, &key_bounds_);

  size_t result = 0;
  for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
    ++result;
  }
  return result;
}

uint64_t Tablet::GetCurrentVersionSstFilesSize() const {
  ScopedPendingOperation scoped_operation(&pending_op_counter_);
  std::lock_guard<rw_spinlock> lock(component_lock_);

  // In order to get actual stats we would have to wait.
  // This would give us correct stats but would make this request slower.
  if (!pending_op_counter_.IsReady() || !regular_db_) {
    return 0;
  }
  return regular_db_->GetCurrentVersionSstFilesSize();
}

uint64_t Tablet::GetCurrentVersionSstFilesUncompressedSize() const {
  ScopedPendingOperation scoped_operation(&pending_op_counter_);
  std::lock_guard<rw_spinlock> lock(component_lock_);

  // In order to get actual stats we would have to wait.
  // This would give us correct stats but would make this request slower.
  if (!pending_op_counter_.IsReady() || !regular_db_) {
    return 0;
  }
  return regular_db_->GetCurrentVersionSstFilesUncompressedSize();
}

uint64_t Tablet::GetCurrentVersionNumSSTFiles() const {
  ScopedPendingOperation scoped_operation(&pending_op_counter_);
  std::lock_guard<rw_spinlock> lock(component_lock_);

  // In order to get actual stats we would have to wait.
  // This would give us correct stats but would make this request slower.
  if (!pending_op_counter_.IsReady() || !regular_db_) {
    return 0;
  }
  return regular_db_->GetCurrentVersionNumSSTFiles();
}

// ------------------------------------------------------------------------------------------------

Result<TransactionOperationContextOpt> Tablet::CreateTransactionOperationContext(
    const TransactionMetadataPB& transaction_metadata) const {
  if (metadata_->schema().table_properties().is_transactional()) {
    if (transaction_metadata.has_transaction_id()) {
      Result<TransactionId> txn_id = FullyDecodeTransactionId(
          transaction_metadata.transaction_id());
      RETURN_NOT_OK(txn_id);
      return Result<TransactionOperationContextOpt>(boost::make_optional(
          TransactionOperationContext(*txn_id, transaction_participant())));
    } else {
      // We still need context with transaction participant in order to resolve intents during
      // possible reads.
      return Result<TransactionOperationContextOpt>(boost::make_optional(
          TransactionOperationContext(GenerateTransactionId(), transaction_participant())));
    }
  } else {
    return Result<TransactionOperationContextOpt>(boost::none);
  }
}

TransactionOperationContextOpt Tablet::CreateTransactionOperationContext(
    const boost::optional<TransactionId>& transaction_id) const {
  if (metadata_->schema().table_properties().is_transactional()) {
    if (transaction_id.is_initialized()) {
      return TransactionOperationContext(transaction_id.get(), transaction_participant());
    } else {
      // We still need context with transaction participant in order to resolve intents during
      // possible reads.
      return TransactionOperationContext(GenerateTransactionId(), transaction_participant());
    }
  } else {
    return boost::none;
  }
}

Status Tablet::CreateReadIntents(
    const TransactionMetadataPB& transaction_metadata,
    const google::protobuf::RepeatedPtrField<QLReadRequestPB>& ql_batch,
    const google::protobuf::RepeatedPtrField<PgsqlReadRequestPB>& pgsql_batch,
    docdb::KeyValueWriteBatchPB* write_batch) {
  auto txn_op_ctx = VERIFY_RESULT(CreateTransactionOperationContext(transaction_metadata));

  for (const auto& ql_read : ql_batch) {
    docdb::QLReadOperation doc_op(ql_read, txn_op_ctx);
    RETURN_NOT_OK(doc_op.GetIntents(SchemaRef(), write_batch));
  }

  for (const auto& pgsql_read : pgsql_batch) {
    docdb::PgsqlReadOperation doc_op(pgsql_read, txn_op_ctx);
    RETURN_NOT_OK(doc_op.GetIntents(SchemaRef(), write_batch));
  }

  return Status::OK();
}

bool Tablet::ShouldApplyWrite() {
  return !regular_db_->NeedsDelay();
}

// Create snapshot for this tablet.
Status Tablet::CreateSnapshot(SnapshotOperationState* tx_state) {
  return STATUS(NotSupported, "Internal error: CreateSnapshot called on the base Tablet class");
}

// Delete snapshot for this tablet.
Status Tablet::DeleteSnapshot(SnapshotOperationState* tx_state) {
  return STATUS(NotSupported, "Internal error: DeleteSnapshot called on the base Tablet class");
}

Status Tablet::RestoreCheckpoint(const std::string& dir, const docdb::ConsensusFrontier& frontier) {
  // The following two lines can't just be changed to RETURN_NOT_OK(PauseReadWriteOperations()):
  // op_pause has to stay in scope until the end of the function.
  auto op_pause = PauseReadWriteOperations();
  RETURN_NOT_OK(op_pause);

  // Check if tablet is in shutdown mode.
  if (IsShutdownRequested()) {
    return STATUS(IllegalState, "Tablet was shut down");
  }

  std::lock_guard<std::mutex> lock(create_checkpoint_lock_);

  const rocksdb::SequenceNumber sequence_number = regular_db_->GetLatestSequenceNumber();
  const string db_dir = regular_db_->GetName();
  const std::string intents_db_dir = intents_db_ ? intents_db_->GetName() : std::string();

  // Destroy DB object.
  // TODO: snapshot current DB and try to restore it in case of failure.
  intents_db_.reset();
  regular_db_.reset();
  key_bounds_ = docdb::KeyBounds::kNoBounds;

  rocksdb::Options rocksdb_options;
  docdb::InitRocksDBOptions(&rocksdb_options, LogPrefix(), rocksdb_statistics_, tablet_options_);

  Status s = rocksdb::DestroyDB(db_dir, rocksdb_options);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Cannot cleanup db files in directory " << db_dir << ": " << s;
    return STATUS(IllegalState, "Cannot cleanup db files", s.ToString());
  }

  if (!intents_db_dir.empty()) {
    s = rocksdb::DestroyDB(intents_db_dir, rocksdb_options);
    if (PREDICT_FALSE(!s.ok())) {
      LOG_WITH_PREFIX(WARNING) << "Cannot cleanup db files in directory " << intents_db_dir << ": "
                               << s;
      return STATUS(IllegalState, "Cannot cleanup intents db files", s.ToString());
    }
  }

  s = rocksdb::CopyDirectory(rocksdb_options.env, dir, db_dir, rocksdb::CreateIfMissing::kTrue);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Copy checkpoint files status: " << s;
    return STATUS(IllegalState, "Unable to copy checkpoint files", s.ToString());
  }

  if (!intents_db_dir.empty()) {
    auto intents_tmp_dir = JoinPathSegments(dir, tablet::kIntentsSubdir);
    rocksdb_options.env->RenameFile(intents_db_dir, intents_db_dir);
  }

  // Reopen database from copied checkpoint.
  // Note: db_dir == metadata()->rocksdb_dir() is still valid db dir.
  s = OpenKeyValueTablet();
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Failed tablet db opening from checkpoint: " << s;
    return s;
  }

  docdb::ConsensusFrontier final_frontier = frontier;
  rocksdb::UserFrontierPtr checkpoint_flushed_frontier = regular_db_->GetFlushedFrontier();

  // The history cutoff we are setting after restoring to this snapshot is determined by the
  // compactions that were done in the checkpoint, not in the old state of RocksDB in this replica.
  if (checkpoint_flushed_frontier) {
    final_frontier.set_history_cutoff(
        down_cast<docdb::ConsensusFrontier&>(*checkpoint_flushed_frontier).history_cutoff());
  }

  s = ModifyFlushedFrontier(final_frontier, rocksdb::FrontierModificationMode::kForce);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Failed tablet DB setting flushed op id: " << s;
    return s;
  }

  LOG_WITH_PREFIX(INFO) << "Checkpoint restored from " << dir;
  LOG_WITH_PREFIX(INFO) << "Sequence numbers: old=" << sequence_number
            << ", restored=" << regular_db_->GetLatestSequenceNumber();

  LOG_WITH_PREFIX(INFO) << "Re-enabling compactions";
  s = EnableCompactions();
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Failed to enable compactions after restoring a checkpoint";
    return s;
  }

  DCHECK(op_pause.status().ok());  // Ensure that op_pause stays in scope throughout this function.
  return Status::OK();
}

Status Tablet::PrepareForSnapshotOp(SnapshotOperationState* tx_state) {
  tx_state->AcquireSchemaLock(&schema_lock_);

  return Status::OK();
}

Status Tablet::RestoreSnapshot(SnapshotOperationState* tx_state) {
  const string top_snapshots_dir = Tablet::SnapshotsDirName(metadata_->rocksdb_dir());
  const string snapshot_dir = tx_state->GetSnapshotDir(top_snapshots_dir);

  docdb::ConsensusFrontier frontier;
  frontier.set_op_id(tx_state->op_id());
  frontier.set_hybrid_time(tx_state->hybrid_time());
  const Status s = RestoreCheckpoint(snapshot_dir, frontier);
  VLOG(1) << "Complete checkpoint restoring for tablet " << tablet_id()
          << " with result " << s << " in folder " << metadata_->rocksdb_dir();
  return s;
}

std::string Tablet::SnapshotsDirName(const std::string& rocksdb_dir) {
  return rocksdb_dir + kSnapshotsDirSuffix;
}

Result<IsolationLevel> Tablet::GetIsolationLevel(const TransactionMetadataPB& transaction) {
  if (transaction.has_isolation()) {
    return transaction.isolation();
  }
  return VERIFY_RESULT(transaction_participant_->PrepareMetadata(transaction)).isolation;
}


Status Tablet::CreateSubtablet(
    const TabletId& tablet_id, const Partition& partition,
    const docdb::KeyBounds& key_bounds) {
  auto metadata = VERIFY_RESULT(metadata_->CreateSubtabletMetadata(
      tablet_id, partition, key_bounds.lower.data(), key_bounds.upper.data()));
  return CreateCheckpoint(metadata->rocksdb_dir());
}

Result<int64_t> Tablet::CountIntents() {
  ScopedPendingOperation pending_op(&pending_op_counter_);
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
  return num_intents;
}

void Tablet::ListenNumSSTFilesChanged(std::function<void()> listener) {
  std::lock_guard<std::mutex> lock(num_sst_files_changed_listener_mutex_);
  bool has_new_listener = listener != nullptr;
  bool has_old_listener = num_sst_files_changed_listener_ != nullptr;
  LOG_IF_WITH_PREFIX(DFATAL, has_new_listener == has_old_listener)
      << __func__ << " in wrong state, has_old_listener: " << has_old_listener;
  num_sst_files_changed_listener_ = std::move(listener);
}

// ------------------------------------------------------------------------------------------------

Result<ScopedReadOperation> ScopedReadOperation::Create(
    AbstractTablet* tablet,
    RequireLease require_lease,
    ReadHybridTime read_time) {
  if (!read_time) {
    read_time = ReadHybridTime::SingleTime(tablet->SafeTime(require_lease));
  }
  RETURN_NOT_OK(tablet->RegisterReaderTimestamp(read_time.read));
  return ScopedReadOperation(tablet, require_lease, read_time);
}

ScopedReadOperation::ScopedReadOperation(
    AbstractTablet* tablet, RequireLease require_lease, const ReadHybridTime& read_time)
    : tablet_(tablet), read_time_(read_time) {
}

ScopedReadOperation::~ScopedReadOperation() {
  if (tablet_) {
    tablet_->UnregisterReader(read_time_.read);
  }
}

}  // namespace tablet
}  // namespace yb
