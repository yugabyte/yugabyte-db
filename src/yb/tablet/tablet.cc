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
#include <boost/scope_exit.hpp>

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/utilities/checkpoint.h"
#include "yb/rocksdb/write_batch.h"

#include "yb/common/common.pb.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/common/ql_rowblock.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/opid_util.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/lock_batch.h"

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
#include "yb/tablet/operations/alter_schema_operation.h"
#include "yb/tablet/operations/truncate_operation.h"
#include "yb/tablet/operations/write_operation.h"
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
#include "yb/util/slice.h"
#include "yb/util/stopwatch.h"
#include "yb/util/string_packer.h"
#include "yb/util/trace.h"
#include "yb/util/url-coding.h"

DEFINE_bool(tablet_do_dup_key_checks, true,
            "Whether to check primary keys for duplicate on insertion. "
            "Use at your own risk!");
TAG_FLAG(tablet_do_dup_key_checks, unsafe);

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

using namespace std::placeholders;

using std::shared_ptr;
using std::make_shared;
using std::string;
using std::unordered_set;
using std::vector;
using std::unique_ptr;
using namespace std::literals;

using rocksdb::WriteBatch;
using rocksdb::SequenceNumber;
using yb::util::ScopedPendingOperation;
using yb::util::ScopedPendingOperationPause;
using yb::tserver::WriteRequestPB;
using yb::tserver::WriteResponsePB;
using yb::docdb::KeyValueWriteBatchPB;
using yb::tserver::ReadRequestPB;
using yb::docdb::ValueType;
using yb::docdb::KeyBytes;
using yb::docdb::DocOperation;
using yb::docdb::RedisWriteOperation;
using yb::docdb::QLWriteOperation;
using yb::docdb::DocDBCompactionFilterFactory;
using yb::docdb::IntentKind;
using yb::docdb::IntentTypePair;
using yb::docdb::KeyToIntentTypeMap;
using yb::docdb::InitMarkerBehavior;

namespace yb {
namespace tablet {

using yb::MaintenanceManager;
using consensus::OpId;
using consensus::MaximumOpId;
using log::LogAnchorRegistry;
using strings::Substitute;
using base::subtle::Barrier_AtomicIncrement;

using docdb::DocDbAwareFilterPolicy;
using docdb::DocKey;
using docdb::DocPath;
using docdb::DocRowwiseIterator;
using docdb::DocWriteBatch;
using docdb::SubDocKey;
using docdb::PrimitiveValue;

////////////////////////////////////////////////////////////
// Tablet
////////////////////////////////////////////////////////////
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

// Struct to pass data to WriteOperation related functions.
struct WriteOperationData {
  WriteOperationState* operation_state;
  LockBatch *keys_locked;
  HybridTime* restart_read_ht;

  tserver::WriteRequestPB* write_request() const {
    return operation_state->mutable_request();
  }

  ReadHybridTime read_time() const {
    return ReadHybridTime::FromReadTimePB(*write_request());
  }
};

const char* Tablet::kDMSMemTrackerId = "DeltaMemStores";

Tablet::Tablet(
    const scoped_refptr<TabletMetadata>& metadata,
    const server::ClockPtr& clock,
    const shared_ptr<MemTracker>& parent_mem_tracker,
    MetricRegistry* metric_registry,
    const scoped_refptr<LogAnchorRegistry>& log_anchor_registry,
    const TabletOptions& tablet_options,
    TransactionParticipantContext* transaction_participant_context,
    TransactionCoordinatorContext* transaction_coordinator_context)
    : key_schema_(metadata->schema().CreateKeyProjection()),
      metadata_(metadata),
      table_type_(metadata->table_type()),
      log_anchor_registry_(log_anchor_registry),
      mem_tracker_(
          MemTracker::CreateTracker(-1, Substitute("tablet-$0", tablet_id()), parent_mem_tracker)),
      dms_mem_tracker_(MemTracker::CreateTracker(-1, kDMSMemTrackerId, mem_tracker_)),
      clock_(clock),
      mvcc_(Format("T $0 ", static_cast<void*>(this)), clock),
      tablet_options_(tablet_options) {
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
  }

  if (transaction_participant_context) {
    transaction_participant_ = std::make_unique<TransactionParticipant>(
        transaction_participant_context);
  }

  if (transaction_coordinator_context) { // TODO(dtxn) Create coordinator only for status tablets
    CHECK_NOTNULL(transaction_participant_context);
    transaction_coordinator_ = std::make_unique<TransactionCoordinator>(
        transaction_coordinator_context, transaction_participant_.get());
  }

  flush_stats_ = make_shared<TabletFlushStats>();
  tablet_options_.listeners.emplace_back(flush_stats_);
}

Tablet::~Tablet() {
  Shutdown();
  dms_mem_tracker_->UnregisterFromParent();
  mem_tracker_->UnregisterFromParent();
}

Status Tablet::Open() {
  TRACE_EVENT0("tablet", "Tablet::Open");
  std::lock_guard<rw_spinlock> lock(component_lock_);
  CHECK_EQ(state_, kInitialized) << "already open";
  CHECK(schema()->has_column_ids());

  switch (table_type_) {
    case TableType::YQL_TABLE_TYPE: FALLTHROUGH_INTENDED;
    case TableType::REDIS_TABLE_TYPE:
      RETURN_NOT_OK(OpenKeyValueTablet());
      break;
    default:
      LOG(FATAL) << "Cannot open tablet " << tablet_id() << " with unknown table type "
                 << table_type_;
  }

  state_ = kBootstrapping;
  return Status::OK();
}

Status Tablet::OpenKeyValueTablet() {
  rocksdb::Options rocksdb_options;
  docdb::InitRocksDBOptions(&rocksdb_options, tablet_id(), rocksdb_statistics_, tablet_options_);

  // Install the history cleanup handler. Note that TabletRetentionPolicy is going to hold a raw ptr
  // to this tablet. So, we ensure that rocksdb_ is reset before this tablet gets destroyed.
  rocksdb_options.compaction_filter_factory = make_shared<DocDBCompactionFilterFactory>(
      make_shared<TabletRetentionPolicy>(this));

  const string db_dir = metadata()->rocksdb_dir();
  LOG(INFO) << "Creating RocksDB database in dir " << db_dir;

  // Create the directory table-uuid first.
  RETURN_NOT_OK_PREPEND(metadata()->fs_manager()->CreateDirIfMissing(DirName(db_dir)),
                        Substitute("Failed to create RocksDB table directory $0",
                                   DirName(db_dir)));

  RETURN_NOT_OK_PREPEND(metadata()->fs_manager()->CreateDirIfMissing(db_dir),
                        Substitute("Failed to create RocksDB tablet directory $0",
                                   db_dir));

  LOG(INFO) << "Opening RocksDB at: " << db_dir;
  rocksdb::DB* db = nullptr;
  rocksdb::Status rocksdb_open_status = rocksdb::DB::Open(rocksdb_options, db_dir, &db);
  if (!rocksdb_open_status.ok()) {
    LOG(ERROR) << "Failed to open a RocksDB database in directory " << db_dir << ": "
               << rocksdb_open_status.ToString();
    if (db != nullptr) {
      delete db;
    }
    return STATUS(IllegalState, rocksdb_open_status.ToString());
  }
  rocksdb_.reset(db);
  ql_storage_.reset(new docdb::QLRocksDBStorage(rocksdb_.get()));
  if (transaction_participant_) {
    transaction_participant_->SetDB(db);
  }
  LOG(INFO) << "Successfully opened a RocksDB database at " << db_dir;
  return Status::OK();
}

void Tablet::MarkFinishedBootstrapping() {
  CHECK_EQ(state_, kBootstrapping);
  state_ = kOpen;
}

void Tablet::SetShutdownRequestedFlag() {
  shutdown_requested_.store(true, std::memory_order::memory_order_release);
}

void Tablet::Shutdown() {
  SetShutdownRequestedFlag();

  auto op_pause = PauseReadWriteOperations();
  if (!op_pause.ok()) {
    LOG(WARNING) << Substitute("Tablet $0: failed to shut down", tablet_id());
    return;
  }

  if (transaction_coordinator_) {
    transaction_coordinator_->Shutdown();
  }

  std::lock_guard<rw_spinlock> lock(component_lock_);
  // Shutdown the RocksDB instance for this table, if present.
  rocksdb_.reset();
  state_ = kShutdown;
}

Result<std::unique_ptr<common::QLRowwiseIteratorIf>> Tablet::NewRowIterator(
    const Schema &projection, const boost::optional<TransactionId>& transaction_id) const {
  if (state_ != kOpen) {
    return STATUS_FORMAT(IllegalState, "Tablet in wrong state: $0", state_);
  }

  if (table_type_ != TableType::YQL_TABLE_TYPE) {
    return STATUS_FORMAT(NotSupported, "Invalid table type: $0", table_type_);
  }

  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  VLOG(2) << "Created new Iterator on " << tablet_id();

  auto mapped_projection = std::make_unique<Schema>();
  RETURN_NOT_OK(schema()->GetMappedReadProjection(projection, mapped_projection.get()));

  auto txn_op_ctx = CreateTransactionOperationContext(transaction_id);
  auto read_time = ReadHybridTime::SingleTime(HybridTime::kMax);
  auto result = std::make_unique<DocRowwiseIterator>(
      std::move(mapped_projection), *schema(), txn_op_ctx, rocksdb_.get(), read_time,
      &pending_op_counter_);
  RETURN_NOT_OK(result->Init());
  return std::move(result);
}

void Tablet::StartOperation(WriteOperationState* operation_state) {
  // If the state already has a hybrid_time then we're replaying a transaction that occurred
  // before a crash or at another node...
  HybridTime ht = operation_state->hybrid_time_even_if_unset();
  bool was_valid = ht.is_valid();
  mvcc_.AddPending(&ht);
  if (!was_valid) {
    operation_state->set_hybrid_time(ht);
  }
}

void Tablet::ApplyRowOperations(WriteOperationState* operation_state) {
  last_committed_write_index_.store(operation_state->op_id().index(), std::memory_order_release);
  const KeyValueWriteBatchPB& put_batch =
      operation_state->consensus_round() && operation_state->consensus_round()->replicate_msg()
          // Online case.
          ? operation_state->consensus_round()->replicate_msg()->write_request().write_batch()
          // Bootstrap case.
          : operation_state->request()->write_batch();

  ApplyKeyValueRowOperations(put_batch,
                             operation_state->op_id(),
                             operation_state->hybrid_time());
}

Status Tablet::CreateCheckpoint(const std::string& dir,
                                google::protobuf::RepeatedPtrField<RocksDBFilePB>* rocksdb_files) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  std::lock_guard<std::mutex> lock(create_checkpoint_lock_);

  rocksdb::Status status = rocksdb::checkpoint::CreateCheckpoint(rocksdb_.get(), dir);

  if (!status.ok()) {
    LOG(WARNING) << "Create checkpoint status: " << status.ToString();
    return STATUS(IllegalState, Substitute("Unable to create checkpoint: $0", status.ToString()));
  }
  LOG(INFO) << "Checkpoint created in " << dir;

  if (rocksdb_files != nullptr) {
    vector<rocksdb::Env::FileAttributes> files_attrs;
    status = rocksdb_->GetEnv()->GetChildrenFileAttributes(dir, &files_attrs);
    if (!status.ok()) {
      return STATUS(IllegalState, Substitute("Unable to get RocksDB files in dir $0: $1", dir,
                                             status.ToString()));
    }

    for (const auto& file_attrs : files_attrs) {
      if (file_attrs.name == "." || file_attrs.name == "..") {
        continue;
      }
      auto rocksdb_file_pb = rocksdb_files->Add();
      rocksdb_file_pb->set_name(file_attrs.name);
      rocksdb_file_pb->set_size_bytes(file_attrs.size_bytes);
    }
  }

  last_rocksdb_checkpoint_dir_ = dir;

  return Status::OK();
}

void Tablet::PrepareTransactionWriteBatch(
    const KeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    WriteBatch* rocksdb_write_batch) {
  if (put_batch.transaction().has_isolation()) {
    // Store transaction metadata (status tablet, isolation level etc.)
    transaction_participant()->Add(put_batch.transaction(), rocksdb_write_batch);
  }
  auto transaction_id = FullyDecodeTransactionId(put_batch.transaction().transaction_id());
  CHECK_OK(transaction_id);
  auto metadata = transaction_participant()->Metadata(*transaction_id);
  CHECK(metadata) << "Transaction metadata missing: " << *transaction_id;

  auto isolation_level = metadata->isolation;
  yb::docdb::PrepareTransactionWriteBatch(
      put_batch, hybrid_time, rocksdb_write_batch, *transaction_id, isolation_level);
}

void Tablet::ApplyKeyValueRowOperations(const KeyValueWriteBatchPB& put_batch,
                                        const consensus::OpId& op_id,
                                        const HybridTime hybrid_time,
                                        rocksdb::WriteBatch* rocksdb_write_batch) {
  // Write batch could be preallocated, here we handle opposite case.
  if (rocksdb_write_batch == nullptr) {
    WriteBatch write_batch;
    ApplyKeyValueRowOperations(put_batch, op_id, hybrid_time, &write_batch);
    return;
  }

  if (put_batch.kv_pairs_size() == 0 && rocksdb_write_batch->Count() == 0) {
    return;
  }

  rocksdb_write_batch->SetUserOpId(rocksdb::OpId(op_id.term(), op_id.index()));

  if (put_batch.has_transaction()) {
    PrepareTransactionWriteBatch(put_batch, hybrid_time, rocksdb_write_batch);
  } else {
    PrepareNonTransactionWriteBatch(put_batch, hybrid_time, rocksdb_write_batch);
  }

  // We are using Raft replication index for the RocksDB sequence number for
  // all members of this write batch.
  rocksdb::WriteOptions write_options;
  InitRocksDBWriteOptions(&write_options);

  flush_stats_->AboutToWriteToDb(hybrid_time);
  auto rocksdb_write_status = rocksdb_->Write(write_options, rocksdb_write_batch);
  if (!rocksdb_write_status.ok()) {
    LOG(FATAL) << "Failed to write a batch with " << rocksdb_write_batch->Count() << " operations"
               << " into RocksDB: " << rocksdb_write_status.ToString();
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
}

} // namespace

Status Tablet::KeyValueBatchFromRedisWriteBatch(const WriteOperationData& data) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);
  docdb::DocOperations doc_ops;
  // Since we take exclusive locks, it's okay to use Now as the read TS for writes.
  WriteRequestPB batch_request;
  SetupKeyValueBatch(data.write_request(), &batch_request);
  auto* redis_write_batch = batch_request.mutable_redis_write_batch();

  doc_ops.reserve(redis_write_batch->size());
  for (size_t i = 0; i < redis_write_batch->size(); i++) {
    doc_ops.emplace_back(new RedisWriteOperation(redis_write_batch->Mutable(i)));
  }
  RETURN_NOT_OK(StartDocWriteOperation(doc_ops, data));
  if (data.restart_read_ht->is_valid()) {
    return Status::OK();
  }
  auto* response = data.operation_state->response();
  for (size_t i = 0; i < doc_ops.size(); i++) {
    auto* redis_write_operation = down_cast<RedisWriteOperation*>(doc_ops[i].get());
    response->add_redis_response_batch()->Swap(&redis_write_operation->response());
  }

  return Status::OK();
}

Status Tablet::HandleRedisReadRequest(const ReadHybridTime& read_time,
                                      const RedisReadRequestPB& redis_read_request,
                                      RedisResponsePB* response) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  ScopedTabletMetricsTracker metrics_tracker(metrics_->redis_read_latency);

  docdb::RedisReadOperation doc_op(redis_read_request, rocksdb_.get(), read_time);
  RETURN_NOT_OK(doc_op.Execute());
  *response = std::move(doc_op.response());
  return Status::OK();
}

Status Tablet::HandleQLReadRequest(
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
      read_time, ql_read_request, *txn_op_ctx, result);
}

CHECKED_STATUS Tablet::CreatePagingStateForRead(const QLReadRequestPB& ql_read_request,
                                                const size_t row_count,
                                                QLResponsePB* response) const {

  // If the response does not have a paging state, it means we are done reading the current tablet.
  // But, if the request does not have the hash columns set, this must be a table-scan, so we need
  // to decide if we are done or if we need to move to the next tablet.
  // If we did not reach the:
  //   1. max number of results (LIMIT clause -- if set)
  //   2. end of the table (this was the last tablet)
  //   3. max partition key (upper bound condition using 'token' -- if set)
  // we set the paging state to point to the exclusive end partition key of this tablet, which is
  // the start key of the next tablet).
  if (ql_read_request.hashed_column_values().empty() && !response->has_paging_state()) {

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

Status Tablet::KeyValueBatchFromQLWriteBatch(const WriteOperationData& data) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  docdb::DocOperations doc_ops;
  WriteRequestPB batch_request;
  SetupKeyValueBatch(data.write_request(), &batch_request);
  auto* ql_write_batch = batch_request.mutable_ql_write_batch();

  doc_ops.reserve(ql_write_batch->size());

  Result<TransactionOperationContextOpt> txn_op_ctx =
      CreateTransactionOperationContext(data.write_request()->write_batch().transaction());
  RETURN_NOT_OK(txn_op_ctx);
  for (size_t i = 0; i < ql_write_batch->size(); i++) {
    QLWriteRequestPB* req = ql_write_batch->Mutable(i);
    QLResponsePB* resp = data.operation_state->response()->add_ql_response_batch();
    if (metadata_->schema_version() != req->schema_version()) {
      resp->set_status(QLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH);
    } else {
      const auto& schema = metadata_->schema();
      auto write_op = std::make_unique<QLWriteOperation>(schema, *txn_op_ctx);
      RETURN_NOT_OK(write_op->Init(req, resp));
      doc_ops.emplace_back(std::move(write_op));
    }
  }
  RETURN_NOT_OK(StartDocWriteOperation(doc_ops, data));
  if (data.restart_read_ht->is_valid()) {
    return Status::OK();
  }
  for (size_t i = 0; i < doc_ops.size(); i++) {
    QLWriteOperation* ql_write_op = down_cast<QLWriteOperation*>(doc_ops[i].get());
    // If the QL write op returns a rowblock, move the op to the transaction state to return the
    // rows data as a sidecar after the transaction completes.
    if (ql_write_op->rowblock() != nullptr) {
      doc_ops[i].release();
      data.operation_state->ql_write_ops()->emplace_back(unique_ptr<QLWriteOperation>(ql_write_op));
    }
  }

  return Status::OK();
}

Status Tablet::AcquireLocksAndPerformDocOperations(
    WriteOperationState *state, HybridTime* restart_read_ht) {
  LockBatch locks_held;
  WriteRequestPB* key_value_write_request = state->mutable_request();

  bool invalid_table_type = true;
  WriteOperationData data = {
    state,
    &locks_held,
    restart_read_ht
  };
  switch (table_type_) {
    case TableType::REDIS_TABLE_TYPE: {
      RETURN_NOT_OK(KeyValueBatchFromRedisWriteBatch(data));
      invalid_table_type = false;
      break;
    }
    case TableType::YQL_TABLE_TYPE: {
      CHECK_GT(key_value_write_request->ql_write_batch_size(), 0);
      RETURN_NOT_OK(KeyValueBatchFromQLWriteBatch(data));
      if (restart_read_ht->is_valid()) {
        return Status::OK();
      }
      invalid_table_type = false;
      break;
    }
  }
  if (invalid_table_type) {
    FATAL_INVALID_ENUM_VALUE(TableType, table_type_);
  }
  // If there is a non-zero number of operations, we expect to be holding locks. The reverse is
  // not always true, because we could decide to avoid writing based on results of reading.
  DCHECK(!locks_held.empty() ||
         key_value_write_request->write_batch().kv_pairs_size() == 0)
      << "Expect to be holding locks for a non-zero number of write operations: "
      << key_value_write_request->write_batch().DebugString();
  state->ReplaceDocDBLocks(std::move(locks_held));

  DCHECK_EQ(key_value_write_request->redis_write_batch_size(), 0)
      << "Redis write batch not empty in key-value batch";
  DCHECK_EQ(key_value_write_request->ql_write_batch_size(), 0)
      << "QL write batch not empty in key-value batch";
  return Status::OK();
}

Status Tablet::Flush(FlushMode mode) {
  return FlushUnlocked(mode);
}

Status Tablet::FlushUnlocked(FlushMode mode) {
  TRACE_EVENT0("tablet", "Tablet::FlushUnlocked");

  // TODO(bojanserafimov): Can raise null pointer exception if
  // the tablet just got shutdown. Acquire a read lock on component_lock_?
  rocksdb::FlushOptions options;
  options.wait = mode == FlushMode::kSync;
  rocksdb_->Flush(options);
  return Status::OK();
}

Status Tablet::ImportData(const std::string& source_dir) {
  return rocksdb_->Import(source_dir);
}

#define INTENT_VALUE_SCHECK(lhs, op, rhs, msg) \
  BOOST_PP_CAT(SCHECK_, op)(lhs, \
                            rhs, \
                            Corruption, \
                            Format("Bad intent value, $0 in $1, transaction: $2", \
                                   msg, \
                                   intent_iter->value().ToDebugHexString(), \
                                   transaction_id_slice.ToDebugHexString()))

// We apply intents using by iterating over whole transaction reverse index.
// Using value of reverse index record we find original intent record and apply it.
// After that we delete both intent record and reverse index record.
// TODO(dtxn) use separate thread for applying intents.
// TODO(dtxn) use multiple batches when applying really big transaction.
Status Tablet::ApplyIntents(const TransactionApplyData& data) {
  auto reverse_index_iter = docdb::CreateRocksDBIterator(
      rocksdb_.get(),
      docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
      boost::none,
      rocksdb::kDefaultQueryId);

  auto intent_iter = docdb::CreateRocksDBIterator(rocksdb_.get(),
                                                  docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
                                                  boost::none,
                                                  rocksdb::kDefaultQueryId);

  KeyBytes txn_reverse_index_prefix;
  Slice transaction_id_slice(data.transaction_id.data, TransactionId::static_size());
  AppendTransactionKeyPrefix(data.transaction_id, &txn_reverse_index_prefix);

  reverse_index_iter->Seek(txn_reverse_index_prefix.data());

  WriteBatch rocksdb_write_batch;

  docdb::DocHybridTimeBuffer doc_ht_buffer;

  IntraTxnWriteId write_id = 0;
  while (reverse_index_iter->Valid()) {
    rocksdb::Slice key_slice(reverse_index_iter->key());

    if (!key_slice.starts_with(txn_reverse_index_prefix.data())) {
      break;
    }

    // If the key ends at the transaction id then it is transaction metadata (status tablet,
    // isolation level etc.).
    if (key_slice.size() > txn_reverse_index_prefix.size()) {
      // Value of reverse index is a key of original intent record, so seek it and check match.
      intent_iter->Seek(reverse_index_iter->value());
      if (!intent_iter->Valid() || intent_iter->key() != reverse_index_iter->value()) {
        LOG(DFATAL) << "Unable to find intent: " << reverse_index_iter->value().ToDebugString()
                    << " for " << reverse_index_iter->key().ToDebugString();
        continue;
      }
      auto intent = docdb::ParseIntentKey(intent_iter->key(), transaction_id_slice);
      RETURN_NOT_OK(intent);

      if (IsStrongIntent(intent->type)) {
        Slice intent_value(intent_iter->value());
        INTENT_VALUE_SCHECK(intent_value[0], EQ, static_cast<uint8_t>(ValueType::kTransactionId),
                            "prefix expected");
        intent_value.consume_byte();
        INTENT_VALUE_SCHECK(intent_value.starts_with(transaction_id_slice), EQ, true,
                            "wrong transaction id");
        intent_value.remove_prefix(transaction_id_slice.size());

        // After strip of prefix and suffix intent_key contains just SubDocKey w/o a hybrid time.
        // Time will be added when writing batch to rocks db.
        std::array<Slice, 2> key_parts = {{
            intent->doc_path,
            doc_ht_buffer.EncodeWithValueType(data.commit_time, write_id),
        }};
        std::array<Slice, 2> value_parts = {{
            intent->doc_ht,
            intent_value,
        }};
        rocksdb_write_batch.Put(key_parts, value_parts);
        ++write_id;
      }

      rocksdb_write_batch.Delete(intent_iter->key());
    }

    rocksdb_write_batch.Delete(reverse_index_iter->key());

    reverse_index_iter->Next();
  }

  // data.hybrid_time contains transaction commit time.
  // We don't set transaction field of put_batch, otherwise we would write another bunch of intents.
  ApplyKeyValueRowOperations(
      KeyValueWriteBatchPB(), data.op_id, data.commit_time, &rocksdb_write_batch);
  return Status::OK();
}

Status Tablet::CreatePreparedAlterSchema(AlterSchemaOperationState *operation_state,
                                         const Schema* schema) {
  if (!key_schema_.KeyEquals(*schema)) {
    return STATUS(InvalidArgument, "Schema keys cannot be altered",
                  schema->CreateKeyProjection().ToString());
  }

  if (!schema->has_column_ids()) {
    // this probably means that the request is not from the Master
    return STATUS(InvalidArgument, "Missing Column IDs");
  }

  // Alter schema must run when no reads/writes are in progress.
  // However, compactions and flushes can continue to run in parallel
  // with the schema change,
  operation_state->AcquireSchemaLock(&schema_lock_);

  operation_state->set_schema(schema);
  return Status::OK();
}

Status Tablet::AlterSchema(AlterSchemaOperationState *operation_state) {
  DCHECK(key_schema_.KeyEquals(*DCHECK_NOTNULL(operation_state->schema())))
      << "Schema keys cannot be altered";

  // Prevent any concurrent flushes. Otherwise, we run into issues where
  // we have an MRS in the rowset tree, and we can't alter its schema
  // in-place.
  std::lock_guard<Semaphore> lock(rowsets_flush_sem_);

  {
    bool same_schema = schema()->Equals(*operation_state->schema());

    // If the current version >= new version, there is nothing to do.
    if (metadata_->schema_version() >= operation_state->schema_version()) {
      LOG(INFO) << "Already running schema version " << metadata_->schema_version()
                << " got alter request for version " << operation_state->schema_version();
      return Status::OK();
    }

    LOG(INFO) << "Alter schema from " << schema()->ToString()
              << " version " << metadata_->schema_version()
              << " to " << operation_state->schema()->ToString()
              << " version " << operation_state->schema_version();
    DCHECK(schema_lock_.is_locked());

    // Find out which columns have been deleted in this schema change, and add them to metadata.
    for (const auto& col : schema()->column_ids()) {
      if (operation_state->schema()->find_column_by_id(col) == Schema::kColumnNotFound) {
        DeletedColumn deleted_col(col, clock_->Now());
        LOG(INFO) << "Column " << col.ToString() << " recorded as deleted.";
        metadata_->AddDeletedColumn(deleted_col);
      }
    }

    metadata_->SetSchema(*operation_state->schema(), operation_state->schema_version());
    if (operation_state->has_new_table_name()) {
      metadata_->SetTableName(operation_state->new_table_name());
      if (metric_entity_) {
        metric_entity_->SetAttribute("table_name", operation_state->new_table_name());
      }
    }

    // If the current schema and the new one are equal, there is nothing to do.
    if (same_schema) {
      return metadata_->Flush();
    }
  }

  return Status::OK();
}

Result<ScopedPendingOperationPause> Tablet::PauseReadWriteOperations() {
  LOG_SLOW_EXECUTION(WARNING, 1000,
                     Substitute("Tablet $0: Waiting for pending ops to complete", tablet_id())) {
    return ScopedPendingOperationPause(&pending_op_counter_, 60s);
  }
  return STATUS(InternalError, "unexpected return"); // should not happen
}

Status Tablet::SetFlushedOpId(const consensus::OpId& op_id) {
  const rocksdb::OpId flushed_op_id(op_id.term(), op_id.index());
  const Status s = rocksdb_->SetFlushedOpId(flushed_op_id);
  if (PREDICT_FALSE(!s.ok())) {
    LOG(WARNING) << "Failed to set flushed op id: " << s;
    return STATUS(IllegalState, "Failed to set flushed op id", s.ToString());
  }
  DCHECK_EQ(flushed_op_id, rocksdb_->GetFlushedOpId());
  return Flush(FlushMode::kAsync);
}

Status Tablet::Truncate(TruncateOperationState *state) {
  auto op_pause = PauseReadWriteOperations();
  RETURN_NOT_OK(op_pause);

  // Check if tablet is in shutdown mode.
  if (IsShutdownRequested()) {
    return STATUS(IllegalState, "Tablet was shut down");
  }

  const rocksdb::SequenceNumber sequence_number = rocksdb_->GetLatestSequenceNumber();
  const string db_dir = rocksdb_->GetName();

  rocksdb_ = nullptr;
  rocksdb::Options rocksdb_options;
  docdb::InitRocksDBOptions(&rocksdb_options, tablet_id(), rocksdb_statistics_, tablet_options_);
  Status s = rocksdb::DestroyDB(db_dir, rocksdb_options);
  if (PREDICT_FALSE(!s.ok())) {
    LOG(WARNING) << "Failed to clean up db dir " << db_dir << ": " << s;
    return STATUS(IllegalState, "Failed to clean up db dir", s.ToString());
  }

  // Creata a new database.
  // Note: db_dir == metadata()->rocksdb_dir() is still valid db dir.
  s = OpenKeyValueTablet();
  if (PREDICT_FALSE(!s.ok())) {
    LOG(WARNING) << "Failed to create a new db: " << s;
    return s;
  }

  RETURN_NOT_OK(SetFlushedOpId(state->op_id()));

  LOG(INFO) << "Created new db for truncated tablet " << tablet_id();
  LOG(INFO) << "Sequence numbers: old=" << sequence_number
            << ", new=" << rocksdb_->GetLatestSequenceNumber();
  return Status::OK();
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
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  std::vector<rocksdb::LiveFileMetaData> live_files_metadata;
  rocksdb_->GetLiveFilesMetaData(&live_files_metadata);
  return !live_files_metadata.empty();
}

Result<yb::OpId> Tablet::MaxPersistentOpId() const {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  return rocksdb_->GetFlushedOpId();
}

Status Tablet::DebugDump(vector<string> *lines) {
  switch (table_type_) {
    case TableType::YQL_TABLE_TYPE:
    case TableType::REDIS_TABLE_TYPE:
      DocDBDebugDump(lines);
      return Status::OK();
  }
  FATAL_INVALID_ENUM_VALUE(TableType, table_type_);
}

void Tablet::DocDBDebugDump(vector<string> *lines) {
  LOG_STRING(INFO, lines) << "Dumping tablet:";
  LOG_STRING(INFO, lines) << "---------------------------";
  yb::docdb::DocDBDebugDump(rocksdb_.get(), LOG_STRING(INFO, lines));
}

namespace {

Result<IsolationLevel> GetIsolationLevel(const KeyValueWriteBatchPB& write_batch,
                                         TransactionParticipant* transaction_participant) {
  if (!write_batch.has_transaction()) {
    return IsolationLevel::NON_TRANSACTIONAL;
  }
  if (write_batch.transaction().has_isolation()) {
    return write_batch.transaction().isolation();
  }
  auto id = FullyDecodeTransactionId(write_batch.transaction().transaction_id());
  RETURN_NOT_OK(id);
  auto stored_metadata = transaction_participant->Metadata(*id);
  if (!stored_metadata) {
    return STATUS_FORMAT(IllegalState, "Missing metadata for transaction: $0", *id);
  }
  return stored_metadata->isolation;
}

} // namespace

Status Tablet::StartDocWriteOperation(const docdb::DocOperations &doc_ops,
                                      const WriteOperationData& data) {
  auto write_batch = data.write_request()->mutable_write_batch();
  auto isolation_level = GetIsolationLevel(*write_batch, transaction_participant_.get());
  RETURN_NOT_OK(isolation_level);
  bool need_read_snapshot = false;
  docdb::PrepareDocWriteOperation(
      doc_ops, metrics_->write_lock_latency, *isolation_level, &shared_lock_manager_,
      data.keys_locked, &need_read_snapshot);

  auto read_op = need_read_snapshot
      ? ScopedReadOperation(this, RequireLease::kTrue, data.read_time())
      : ScopedReadOperation();
  auto real_read_time = need_read_snapshot ? read_op.read_time()
                                           : ReadHybridTime::SingleTime(clock_->Now());

  if (*isolation_level == IsolationLevel::NON_TRANSACTIONAL &&
      metadata_->schema().table_properties().is_transactional()) {
    auto now = clock_->Now();
    auto result = docdb::ResolveOperationConflicts(
        doc_ops, now, rocksdb_.get(), transaction_participant_.get());
    RETURN_NOT_OK(result);
    if (now != *result) {
      clock_->Update(*result);
    }
  }

  // We expect all read operations for this transaction to be done in ExecuteDocWriteOperation.
  // Once read_txn goes out of scope, the read point is deregistered.
  RETURN_NOT_OK(docdb::ExecuteDocWriteOperation(
      doc_ops, real_read_time, rocksdb_.get(), write_batch,
      table_type_ == TableType::REDIS_TABLE_TYPE ? InitMarkerBehavior::kRequired
                                                 : InitMarkerBehavior::kOptional,
      &monotonic_counter_,
      data.restart_read_ht));

  if (data.restart_read_ht->is_valid()) {
    return Status::OK();
  }

  if (*isolation_level != IsolationLevel::NON_TRANSACTIONAL) {
    auto result = docdb::ResolveTransactionConflicts(*write_batch,
                                                     clock_->Now(),
                                                     rocksdb_.get(),
                                                     transaction_participant_.get());
    if (!result.ok()) {
      *data.keys_locked = LockBatch();  // Unlock the keys.
      return result;
    }
  }

  return Status::OK();
}

HybridTime Tablet::DoGetSafeHybridTimeToReadAt(
    tablet::RequireLease require_lease, HybridTime min_allowed, MonoTime deadline) const {
  HybridTime max_allowed;
  if (require_lease && ht_lease_provider_) {
    // min_allowed could contain non zero logical part, so we add one microsecond to be sure that
    // max_allowed >= min_allowed.
    auto min_allowed_lease = min_allowed.GetPhysicalValueMicros();
    if (min_allowed.GetLogicalValue()) {
      ++min_allowed_lease;
    }
    auto lease = ht_lease_provider_(min_allowed_lease, deadline);
    if (!lease) {
      return HybridTime::kInvalid;
    }
    max_allowed = HybridTime(lease, 0);
  } else {
    max_allowed = HybridTime::kMax;
  }
  if (min_allowed > max_allowed) {
    LOG(DFATAL) << "Read request hybrid time after leader lease: " << min_allowed << ", "
                << max_allowed;
    return HybridTime::kInvalid;
  }
  return mvcc_.SafeHybridTimeToReadAt(min_allowed, deadline, max_allowed);
}

HybridTime Tablet::OldestReadPoint() const {
  std::lock_guard<std::mutex> lock(active_readers_mutex_);
  if (active_readers_cnt_.empty()) {
    return mvcc_.LastReplicatedHybridTime();
  }
  return active_readers_cnt_.begin()->first;
}

void Tablet::RegisterReaderTimestamp(HybridTime read_point) {
  std::lock_guard<std::mutex> lock(active_readers_mutex_);
  active_readers_cnt_[read_point]++;
}

void Tablet::UnregisterReader(HybridTime timestamp) {
  std::lock_guard<std::mutex> lock(active_readers_mutex_);
  active_readers_cnt_[timestamp]--;
  if (active_readers_cnt_[timestamp] == 0) {
    active_readers_cnt_.erase(timestamp);
  }
}

void Tablet::ForceRocksDBCompactInTest() {
  rocksdb_->CompactRange(rocksdb::CompactRangeOptions(),
      /* begin = */ nullptr,
      /* end = */ nullptr);
  uint64_t compaction_pending, running_compactions;

  while (true) {
    rocksdb_->GetIntProperty("rocksdb.compaction-pending", &compaction_pending);
    rocksdb_->GetIntProperty("rocksdb.num-running-compactions", &running_compactions);
    if (!compaction_pending && !running_compactions) {
      return;
    }

    SleepFor(MonoDelta::FromMilliseconds(10));
  }
}

std::string Tablet::DocDBDumpStrInTest() {
  return docdb::DocDBDebugDumpToStr(rocksdb_.get());
}

void Tablet::LostLeadership() {
  if (transaction_coordinator_) {
    transaction_coordinator_->ClearLocks();
  }
}

uint64_t Tablet::GetTotalSSTFileSizes() const {
  ScopedPendingOperation scoped_operation(&pending_op_counter_);
  std::lock_guard<rw_spinlock> lock(component_lock_);

  // In order to get actual stats we would have to wait.
  // This would give us correct stats but would make this request slower.
  if (!pending_op_counter_.IsReady() || !rocksdb_) {
    return 0;
  }
  return rocksdb_->GetTotalSSTFileSize();
}

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

ScopedReadOperation::ScopedReadOperation(
    AbstractTablet* tablet, RequireLease require_lease, const ReadHybridTime& read_time)
    : tablet_(tablet), read_time_(read_time) {
  if (!read_time_) {
    read_time_ = ReadHybridTime::SingleTime(tablet->SafeHybridTimeToReadAt(require_lease));
  }

  tablet_->RegisterReaderTimestamp(read_time_.read);
}

ScopedReadOperation::~ScopedReadOperation() {
  if (tablet_) {
    tablet_->UnregisterReader(read_time_.read);
  }
}

}  // namespace tablet
}  // namespace yb
