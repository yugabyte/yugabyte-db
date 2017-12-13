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

// Portions Copyright (c) YugaByte, Inc.

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
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/utilities/checkpoint.h"
#include "yb/rocksdb/write_batch.h"

#include "yb/common/common.pb.h"
#include "yb/common/generic_iterators.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/iterator.h"
#include "yb/common/row_changelist.h"
#include "yb/common/row_operations.h"
#include "yb/common/scan_spec.h"
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

#include "yb/tablet/key_value_iterator.h"
#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/row_op.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_retention_policy.h"
#include "yb/tablet/tablet-internal.h"
#include "yb/tablet/transaction_coordinator.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/operations/alter_schema_operation.h"
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

using rocksdb::WriteBatch;
using rocksdb::SequenceNumber;
using yb::util::ScopedPendingOperation;
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
using yb::docdb::KuduWriteOperation;
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

const char* Tablet::kDMSMemTrackerId = "DeltaMemStores";

Tablet::Tablet(
    const scoped_refptr<TabletMetadata>& metadata,
    const scoped_refptr<server::Clock>& clock,
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
      mvcc_(clock, EnforceInvariants::kTrue),
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

  LOG_SLOW_EXECUTION(WARNING, 1000,
                     Substitute("Tablet $0: Waiting for pending ops to complete", tablet_id())) {
    CHECK_OK(pending_op_counter_.WaitForAllOpsToFinish(MonoDelta::FromSeconds(60)));
  }

  if (transaction_coordinator_) {
    transaction_coordinator_->Shutdown();
  }

  std::lock_guard<rw_spinlock> lock(component_lock_);
  // Shutdown the RocksDB instance for this table, if present.
  rocksdb_.reset();
  state_ = kShutdown;

  // In the case of deleting a tablet, we still keep the metadata around after
  // ShutDown(), and need to flush the metadata to indicate that the tablet is deleted.
  // During that flush, we don't want metadata to call back into the Tablet, so we
  // have to unregister the pre-flush callback.
  metadata_->SetPreFlushCallback(Bind(DoNothingStatusClosure));
}

Status Tablet::GetMappedReadProjection(const Schema& projection,
                                       Schema *mapped_projection) const {
  const Schema* cur_schema = schema();
  return cur_schema->GetMappedReadProjection(projection, mapped_projection);
}

Status Tablet::NewRowIterator(const Schema &projection,
                              const boost::optional<TransactionId>& transaction_id,
                              gscoped_ptr<RowwiseIterator> *iter) const {
  // Yield current rows.
  MvccSnapshot snap(mvcc_);
  return NewRowIterator(projection, snap, Tablet::UNORDERED, transaction_id, iter);
}

Status Tablet::NewRowIterator(const Schema &projection,
                              const MvccSnapshot &snap,
                              const OrderMode order,
                              const boost::optional<TransactionId>& transaction_id,
                              gscoped_ptr<RowwiseIterator> *iter) const {
  CHECK_EQ(state_, kOpen);
  if (metrics_) {
    metrics_->scans_started->Increment();
  }
  VLOG(2) << "Created new Iterator under snap: " << snap.ToString();
  iter->reset(new Iterator(this, projection, snap, order, transaction_id));
  return Status::OK();
}

void Tablet::StartOperation(WriteOperationState* operation_state) {
  std::unique_ptr<ScopedWriteOperation> mvcc_tx;

  // If the state already has a hybrid_time then we're replaying a transaction that occurred
  // before a crash or at another node...
  const HybridTime existing_hybrid_time = operation_state->hybrid_time_even_if_unset();

  if (existing_hybrid_time != HybridTime::kInvalidHybridTime) {
    mvcc_tx.reset(new ScopedWriteOperation(&mvcc_, existing_hybrid_time));
    // ... otherwise this is a new transaction and we must assign a new hybrid_time. We either
    // assign a hybrid_time in the future, if the consistency mode is COMMIT_WAIT, or we assign
    // one in the present if the consistency mode is any other one.
  } else if (operation_state->external_consistency_mode() == COMMIT_WAIT) {
    mvcc_tx.reset(new ScopedWriteOperation(&mvcc_, ScopedWriteOperation::NOW_LATEST));
  } else {
    mvcc_tx.reset(new ScopedWriteOperation(&mvcc_, ScopedWriteOperation::NOW));
  }
  operation_state->SetMvccTxAndHybridTime(std::move(mvcc_tx));
}

void Tablet::StartApplying(WriteOperationState* operation_state) {
  operation_state->StartApplying();
}

void Tablet::ApplyRowOperations(WriteOperationState* operation_state) {
  last_committed_write_index_.store(operation_state->op_id().index(), std::memory_order_release);
  StartApplying(operation_state);
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
  GUARD_AGAINST_ROCKSDB_SHUTDOWN;

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

  if (put_batch.kv_pairs_size() == 0) {
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
  if (batch_request->write_batch().has_transaction()) {
    write_request->mutable_write_batch()->mutable_transaction()->Swap(
        batch_request->mutable_write_batch()->mutable_transaction());
  }
}

} // namespace

Status Tablet::KeyValueBatchFromRedisWriteBatch(
    WriteRequestPB* redis_write_request,
    LockBatch* keys_locked,
    vector<RedisResponsePB>* responses) {
  GUARD_AGAINST_ROCKSDB_SHUTDOWN;
  docdb::DocOperations doc_ops;
  // Since we take exclusive locks, it's okay to use Now as the read TS for writes.
  WriteRequestPB batch_request;
  SetupKeyValueBatch(redis_write_request, &batch_request);
  auto* redis_write_batch = batch_request.mutable_redis_write_batch();

  doc_ops.reserve(redis_write_batch->size());
  for (size_t i = 0; i < redis_write_batch->size(); i++) {
    doc_ops.emplace_back(new RedisWriteOperation(redis_write_batch->Mutable(i)));
  }
  auto read_time = ReadHybridTime::FromPB(*redis_write_request);
  RETURN_NOT_OK(StartDocWriteOperation(
      doc_ops, read_time, keys_locked, redis_write_request->mutable_write_batch()));
  for (size_t i = 0; i < doc_ops.size(); i++) {
    responses->emplace_back(
        (down_cast<RedisWriteOperation*>(doc_ops[i].get()))->response());
  }

  return Status::OK();
}

Status Tablet::HandleRedisReadRequest(const ReadHybridTime& read_time,
                                      const RedisReadRequestPB& redis_read_request,
                                      RedisResponsePB* response) {
  GUARD_AGAINST_ROCKSDB_SHUTDOWN;
  ScopedTabletMetricsTracker metrics_tracker(metrics_->redis_read_latency);

  docdb::RedisReadOperation doc_op(redis_read_request, rocksdb_.get(), read_time);
  RETURN_NOT_OK(doc_op.Execute());
  *response = std::move(doc_op.response());
  return Status::OK();
}

Status Tablet::HandleQLReadRequest(
    const ReadHybridTime& read_time,
    const QLReadRequestPB& ql_read_request,
    const TransactionMetadataPB& transaction_metadata, QLResponsePB* response,
    gscoped_ptr<faststring>* rows_data) {
  GUARD_AGAINST_ROCKSDB_SHUTDOWN;
  ScopedTabletMetricsTracker metrics_tracker(metrics_->ql_read_latency);

  if (metadata()->schema_version() != ql_read_request.schema_version()) {
    response->set_status(QLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH);
    return Status::OK();
  }

  Result<TransactionOperationContextOpt> txn_op_ctx =
      CreateTransactionOperationContext(transaction_metadata);
  RETURN_NOT_OK(txn_op_ctx);
  return AbstractTablet::HandleQLReadRequest(
      read_time, ql_read_request, *txn_op_ctx, response, rows_data);
}

CHECKED_STATUS Tablet::CreatePagingStateForRead(const QLReadRequestPB& ql_read_request,
                                                const size_t row_count,
                                                QLResponsePB* response) const {
  // If there is no hash column in the read request, this is a full-table query. And if there is no
  // paging state in the response, we are done reading from the current tablet. In this case, we
  // should return the exclusive end partition key of this tablet if not empty which is the start
  // key of the next tablet. Do so only if the request has no row count limit, or there is and we
  // haven't hit it, or we are asked to return paging state even when we have hit the limit.
  // Otherwise, leave the paging state empty which means we are completely done reading for the
  // whole SELECT statement.
  if (ql_read_request.hashed_column_values().empty() && !response->has_paging_state() &&
      (!ql_read_request.has_limit() || row_count < ql_read_request.limit() ||
          ql_read_request.return_paging_state())) {
    const string& next_partition_key = metadata_->partition().partition_key_end();
    if (!next_partition_key.empty()) {
      response->mutable_paging_state()->set_next_partition_key(next_partition_key);
    }
  }

  // If there is a paging state, update the total number of rows read so far.
  if (response->has_paging_state()) {
    response->mutable_paging_state()->set_total_num_rows_read(
        ql_read_request.paging_state().total_num_rows_read() + row_count);
  }
  return Status::OK();
}

Status Tablet::KeyValueBatchFromQLWriteBatch(
    WriteRequestPB* ql_write_request,
    LockBatch *keys_locked,
    WriteResponsePB* write_response,
    WriteOperationState* operation_state) {
  GUARD_AGAINST_ROCKSDB_SHUTDOWN;

  docdb::DocOperations doc_ops;
  WriteRequestPB batch_request;
  SetupKeyValueBatch(ql_write_request, &batch_request);
  auto* ql_write_batch = batch_request.mutable_ql_write_batch();

  doc_ops.reserve(ql_write_batch->size());

  Result<TransactionOperationContextOpt> txn_op_ctx =
      CreateTransactionOperationContext(ql_write_request->write_batch().transaction());
  RETURN_NOT_OK(txn_op_ctx);
  for (size_t i = 0; i < ql_write_batch->size(); i++) {
    QLWriteRequestPB* req = ql_write_batch->Mutable(i);
    QLResponsePB* resp = write_response->add_ql_response_batch();
    if (metadata_->schema_version() != req->schema_version()) {
      resp->set_status(QLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH);
    } else {
      doc_ops.emplace_back(new QLWriteOperation(req, metadata_->schema(), resp, *txn_op_ctx));
    }
  }
  auto read_time = ReadHybridTime::FromPB(*ql_write_request);
  RETURN_NOT_OK(StartDocWriteOperation(
      doc_ops, read_time, keys_locked, ql_write_request->mutable_write_batch()));
  for (size_t i = 0; i < doc_ops.size(); i++) {
    QLWriteOperation* ql_write_op = down_cast<QLWriteOperation*>(doc_ops[i].get());
    // If the QL write op returns a rowblock, move the op to the transaction state to return the
    // rows data as a sidecar after the transaction completes.
    if (ql_write_op->rowblock() != nullptr) {
      doc_ops[i].release();
      operation_state->ql_write_ops()->emplace_back(unique_ptr<QLWriteOperation>(ql_write_op));
    }
  }

  return Status::OK();
}

Status Tablet::AcquireLocksAndPerformDocOperations(WriteOperationState *state) {
  LockBatch locks_held;
  WriteRequestPB* key_value_write_request = state->mutable_request();

  bool invalid_table_type = true;
  switch (table_type_) {
    case TableType::REDIS_TABLE_TYPE: {
      vector<RedisResponsePB> responses;
      RETURN_NOT_OK(KeyValueBatchFromRedisWriteBatch(key_value_write_request,
          &locks_held, &responses));
      for (auto &redis_resp : responses) {
        *(state->response()->add_redis_response_batch()) = std::move(redis_resp);
      }
      invalid_table_type = false;
      break;
    }
    case TableType::YQL_TABLE_TYPE: {
      CHECK_NE(key_value_write_request->ql_write_batch_size() > 0,
               key_value_write_request->row_operations().rows().size() > 0)
          << "QL write and Kudu row operations not supported in the same request";
      if (key_value_write_request->ql_write_batch_size() > 0) {
        std::vector<QLResponsePB> responses;
        RETURN_NOT_OK(KeyValueBatchFromQLWriteBatch(
            key_value_write_request, &locks_held, state->response(), state));
      } else {
        // TODO: Remove this row op based codepath after all tests set yql_write_batch.
        RETURN_NOT_OK(KeyValueBatchFromKuduRowOps(key_value_write_request, &locks_held));
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

  DCHECK(!key_value_write_request->has_schema()) << "Schema not empty in key-value batch";
  DCHECK(!key_value_write_request->has_row_operations())
      << "Rows operations not empty in key-value batch";
  DCHECK_EQ(key_value_write_request->redis_write_batch_size(), 0)
      << "Redis write batch not empty in key-value batch";
  DCHECK_EQ(key_value_write_request->ql_write_batch_size(), 0)
      << "QL write batch not empty in key-value batch";
  return Status::OK();
}

Status Tablet::KeyValueBatchFromKuduRowOps(WriteRequestPB* kudu_write_request,
                                           LockBatch *keys_locked) {
  GUARD_AGAINST_ROCKSDB_SHUTDOWN;

  TRACE("PREPARE: Decoding operations");

  WriteRequestPB row_operations_request;
  SetupKeyValueBatch(kudu_write_request, &row_operations_request);
  auto* write_batch = kudu_write_request->mutable_write_batch();

  TRACE("Acquiring schema lock in shared mode");
  shared_lock<rw_semaphore> schema_lock(schema_lock_);
  TRACE("Acquired schema lock");

  Schema client_schema;

  RETURN_NOT_OK(SchemaFromPB(row_operations_request.schema(), &client_schema));

  // Allocating temporary arena for decoding.
  Arena arena(32 * 1024, 4 * 1024 * 1024);

  vector<DecodedRowOperation> row_ops;
  RowOperationsPBDecoder row_operation_decoder(&row_operations_request.row_operations(),
                                               &client_schema,
                                               schema(),
                                               &arena);

  RETURN_NOT_OK(row_operation_decoder.DecodeOperations(&row_ops));

  RETURN_NOT_OK(CreateWriteBatchFromKuduRowOps(row_ops, write_batch, keys_locked));

  return Status::OK();
}

namespace {

DocPath DocPathForColumn(const KeyBytes& encoded_doc_key, ColumnId col_id) {
  return DocPath(encoded_doc_key, PrimitiveValue(col_id));
}

}  // namespace

Status Tablet::CreateWriteBatchFromKuduRowOps(const vector<DecodedRowOperation> &row_ops,
                                              KeyValueWriteBatchPB* write_batch,
                                              LockBatch* keys_locked) {
  GUARD_AGAINST_ROCKSDB_SHUTDOWN;
  docdb::DocOperations doc_ops;
  for (DecodedRowOperation row_op : row_ops) {
    // row_data contains the row key for all Kudu operation types (insert/update/delete).
    ConstContiguousRow contiguous_row(schema(), row_op.row_data);
    EncodedKeyBuilder key_builder(schema());
    for (int i = 0; i < schema()->num_key_columns(); i++) {
      DCHECK(!schema()->column(i).is_nullable())
          << "Column " << i << " (part of row key) cannot be nullable";
      key_builder.AddColumnKey(contiguous_row.cell_ptr(i));
    }
    unique_ptr<EncodedKey> encoded_key(key_builder.BuildEncodedKey());

    const DocKey doc_key = DocKey::FromKuduEncodedKey(*encoded_key, *schema());
    const auto encoded_doc_key = doc_key.Encode();

    switch (row_op.type) {
      case RowOperationsPB_Type_DELETE: {
        doc_ops.emplace_back(
            new KuduWriteOperation(DocPath(encoded_doc_key),
            PrimitiveValue::kTombstone));
        break;
      }
      case RowOperationsPB_Type_UPDATE: {
        RowChangeListDecoder decoder(row_op.changelist);
        RETURN_NOT_OK(decoder.Init());
        while (decoder.HasNext()) {
          CHECK(decoder.is_update());
          RowChangeListDecoder::DecodedUpdate update;
          RETURN_NOT_OK(decoder.DecodeNext(&update));
          doc_ops.emplace_back(new KuduWriteOperation(
              DocPathForColumn(encoded_doc_key, update.col_id),
              update.null ? PrimitiveValue::kTombstone
                          : PrimitiveValue::FromKuduValue(
                                schema()->column_by_id(update.col_id).type_info()->type(),
                                update.raw_value)));
        }
        break;
      }
      case RowOperationsPB_Type_INSERT: {
        for (int i = schema()->num_key_columns(); i < schema()->num_columns(); i++) {
          const ColumnSchema &col_schema = schema()->column(i);
          const DataType data_type = col_schema.type_info()->type();

          PrimitiveValue column_value;
          if (col_schema.is_nullable() && contiguous_row.is_null(i)) {
            // Skip this column as it is null and we are already overwriting the entire row at
            // the top. Another option would be to explicitly delete it like so:
            //
            //   column_value = PrimitiveValue::kTombstone;
            //
            // This would make sense in case we just wanted to update a few columns in a
            // Cassandra-style INSERT ("upsert").
            continue;
          } else {
            column_value = PrimitiveValue::FromKuduValue(data_type, contiguous_row.CellSlice(i));
          }
          doc_ops.emplace_back(new KuduWriteOperation(DocPathForColumn(
              encoded_doc_key, schema()->column_id(i)), column_value));
        }
        break;
      }
      default: {
        LOG(FATAL) << "Unsupported row operation type " << row_op.type
                   << " for a RocksDB-backed table";
      }
    }
  }
  return StartDocWriteOperation(doc_ops, ReadHybridTime(), keys_locked, write_batch);
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

  KeyValueWriteBatchPB put_batch;
  WriteBatch rocksdb_write_batch;

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
      if (intent_iter->Valid() && intent_iter->key() == reverse_index_iter->value()) {
        Slice intent_key(intent_iter->key());
        intent_key.consume_byte();
        auto intent_type = docdb::ExtractIntentType(
            intent_iter.get(), transaction_id_slice, &intent_key);
        RETURN_NOT_OK(intent_type);

        if (IsStrongIntent(*intent_type)) {
          Slice intent_value(intent_iter->value());
          INTENT_VALUE_SCHECK(intent_value[0], EQ, static_cast<uint8_t>(ValueType::kTransactionId),
                              "prefix expected");
          intent_value.consume_byte();
          INTENT_VALUE_SCHECK(intent_value.starts_with(transaction_id_slice), EQ, true,
                              "wrong transaction id");
          intent_value.remove_prefix(transaction_id_slice.size());

          auto* pair = put_batch.add_kv_pairs();
          // After strip of prefix and suffix intent_key contains just SubDocKey w/o a hybrid time.
          // Time will be added when writing batch to rocks db.
          pair->set_key(intent_key.cdata(), intent_key.size());
          pair->set_value(intent_value.cdata(), intent_value.size());
        }
        rocksdb_write_batch.Delete(intent_iter->key());
      } else {
        LOG(DFATAL) << "Unable to find intent: " << reverse_index_iter->value().ToDebugString()
                    << " for " << reverse_index_iter->key().ToDebugString();
      }
    }

    rocksdb_write_batch.Delete(reverse_index_iter->key());

    reverse_index_iter->Next();
  }

  // data.hybrid_time contains transaction commit time.
  // We don't set transaction field of put_batch, otherwise we would write another bunch of intents.
  // TODO(dtxn) commit_time?
  ApplyKeyValueRowOperations(put_batch, data.op_id, data.commit_time, &rocksdb_write_batch);
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

bool Tablet::HasSSTables() const {
  std::vector<rocksdb::LiveFileMetaData> live_files_metadata;
  rocksdb_->GetLiveFilesMetaData(&live_files_metadata);
  return !live_files_metadata.empty();
}

yb::OpId Tablet::MaxPersistentOpId() const {
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

Status Tablet::CaptureConsistentIterators(
    const Schema *projection,
    const MvccSnapshot &snap,
    const ScanSpec *spec,
    const boost::optional<TransactionId>& transaction_id,
    vector<shared_ptr<RowwiseIterator> > *iters) const {

  switch (table_type_) {
    case TableType::YQL_TABLE_TYPE:
      return QLCaptureConsistentIterators(projection, snap, spec, transaction_id, iters);
    default:
      LOG(FATAL) << __FUNCTION__ << " is undefined for table type " << table_type_;
  }
  return STATUS(IllegalState, "This should never happen");
}

Status Tablet::QLCaptureConsistentIterators(
    const Schema *projection,
    const MvccSnapshot &snap,
    const ScanSpec *spec,
    const boost::optional<TransactionId>& transaction_id,
    vector<shared_ptr<RowwiseIterator> > *iters) const {
  GUARD_AGAINST_ROCKSDB_SHUTDOWN;

  TransactionOperationContextOpt txn_op_ctx =
      CreateTransactionOperationContext(transaction_id);
  iters->clear();
  auto read_time = ReadHybridTime::SingleTime(snap.LastCommittedHybridTime());
  iters->push_back(std::make_shared<DocRowwiseIterator>(
      *projection, *schema(), txn_op_ctx, rocksdb_.get(), read_time,
      // We keep the pending operation counter incremented while the iterator exists so that
      // RocksDB does not get deallocated while we're using it.
      &pending_op_counter_));
  return Status::OK();
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
                                      const ReadHybridTime& read_time,
                                      LockBatch *keys_locked,
                                      KeyValueWriteBatchPB* write_batch) {
  auto isolation_level = GetIsolationLevel(*write_batch, transaction_participant_.get());
  RETURN_NOT_OK(isolation_level);
  bool need_read_snapshot = false;
  docdb::PrepareDocWriteOperation(
      doc_ops, metrics_->write_lock_latency, *isolation_level, &shared_lock_manager_, keys_locked,
      &need_read_snapshot);

  auto read_op = need_read_snapshot ? ScopedReadOperation(this, read_time)
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
      &monotonic_counter_));

  if (*isolation_level != IsolationLevel::NON_TRANSACTIONAL) {
    auto result = docdb::ResolveTransactionConflicts(*write_batch,
                                                     clock_->Now(),
                                                     rocksdb_.get(),
                                                     transaction_participant_.get());
    if (!result.ok()) {
      *keys_locked = LockBatch();  // Unlock the keys.
      return result;
    }
  }

  return Status::OK();
}

////////////////////////////////////////////////////////////
// Tablet::Iterator
////////////////////////////////////////////////////////////

Tablet::Iterator::Iterator(const Tablet* tablet, const Schema& projection,
                           MvccSnapshot snap, const OrderMode order,
                           const boost::optional<TransactionId>& transaction_id)
    : tablet_(tablet),
      projection_(projection),
      snap_(std::move(snap)),
      order_(order),
      transaction_id_(transaction_id),
      arena_(256, 4096),
      encoder_(&tablet_->key_schema(), &arena_) {}

Tablet::Iterator::~Iterator() {}

Status Tablet::Iterator::Init(ScanSpec *spec) {
  DCHECK(iter_.get() == nullptr);

  RETURN_NOT_OK(tablet_->GetMappedReadProjection(projection_, &projection_));

  vector<shared_ptr<RowwiseIterator> > iters;
  if (spec != nullptr) {
    VLOG(3) << "Before encoding range preds: " << spec->ToString();
    encoder_.EncodeRangePredicates(spec, true);
    VLOG(3) << "After encoding range preds: " << spec->ToString();
  }

  RETURN_NOT_OK(tablet_->CaptureConsistentIterators(
      &projection_, snap_, spec, transaction_id_, &iters));

  switch (order_) {
    case ORDERED:
      iter_.reset(new MergeIterator(projection_, iters));
      break;
    case UNORDERED:
    default:
      iter_.reset(new UnionIterator(iters));
      break;
  }

  RETURN_NOT_OK(iter_->Init(spec));
  return Status::OK();
}

bool Tablet::Iterator::HasNext() const {
  DCHECK(iter_.get() != nullptr) << "Not initialized!";
  return iter_->HasNext();
}

Status Tablet::Iterator::NextBlock(RowBlock *dst) {
  DCHECK(iter_.get() != nullptr) << "Not initialized!";
  return iter_->NextBlock(dst);
}

string Tablet::Iterator::ToString() const {
  string s;
  s.append("tablet iterator: ");
  if (iter_.get() == nullptr) {
    s.append("NULL");
  } else {
    s.append(iter_->ToString());
  }
  return s;
}

void Tablet::Iterator::GetIteratorStats(vector<IteratorStats>* stats) const {
  iter_->GetIteratorStats(stats);
}

HybridTime Tablet::SafeTimestampToRead() const {
  return mvcc_.GetMaxSafeTimeToReadAt();
}

HybridTime Tablet::OldestReadPoint() const {
  std::lock_guard<std::mutex> lock(active_readers_mutex_);
  if (active_readers_cnt_.empty()) {
    return SafeTimestampToRead();
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
  std::lock_guard<rw_spinlock> lock(component_lock_);
  if (!rocksdb_) {
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
    AbstractTablet* tablet, const ReadHybridTime& read_time)
    : tablet_(tablet), read_time_(read_time) {
  if (!read_time_.read.is_valid()) {
    read_time_.read = tablet->SafeTimestampToRead();
  }
  if (!read_time_.limit.is_valid()) {
    read_time_.limit = read_time_.read;
  }

  DCHECK_GE(read_time_.limit, read_time_.read);
  tablet_->RegisterReaderTimestamp(read_time_.read);
}

ScopedReadOperation::~ScopedReadOperation() {
  if (tablet_) {
    tablet_->UnregisterReader(read_time_.read);
  }
}

}  // namespace tablet
}  // namespace yb
