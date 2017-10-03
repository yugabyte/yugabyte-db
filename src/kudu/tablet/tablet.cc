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

#include <algorithm>
#include <boost/bind.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <iterator>
#include <limits>
#include <memory>
#include <ostream>
#include <unordered_set>
#include <utility>
#include <vector>

#include "kudu/cfile/cfile_writer.h"
#include "kudu/common/iterator.h"
#include "kudu/common/row_changelist.h"
#include "kudu/common/row_operations.h"
#include "kudu/common/scan_spec.h"
#include "kudu/common/schema.h"
#include "kudu/consensus/consensus.pb.h"
#include "kudu/consensus/log_anchor_registry.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/gutil/atomicops.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/numbers.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/tablet/compaction.h"
#include "kudu/tablet/compaction_policy.h"
#include "kudu/tablet/delta_compaction.h"
#include "kudu/tablet/diskrowset.h"
#include "kudu/tablet/maintenance_manager.h"
#include "kudu/tablet/row_op.h"
#include "kudu/tablet/rowset_info.h"
#include "kudu/tablet/rowset_tree.h"
#include "kudu/tablet/svg_dump.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/tablet_metrics.h"
#include "kudu/tablet/tablet_mm_ops.h"
#include "kudu/tablet/transactions/alter_schema_transaction.h"
#include "kudu/tablet/transactions/write_transaction.h"
#include "kudu/util/bloom_filter.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/env.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/locks.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/metrics.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/trace.h"
#include "kudu/util/url-coding.h"

DEFINE_bool(tablet_do_dup_key_checks, true,
            "Whether to check primary keys for duplicate on insertion. "
            "Use at your own risk!");
TAG_FLAG(tablet_do_dup_key_checks, unsafe);

DEFINE_int32(tablet_compaction_budget_mb, 128,
             "Budget for a single compaction");
TAG_FLAG(tablet_compaction_budget_mb, experimental);

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
METRIC_DEFINE_gauge_size(tablet, memrowset_size, "MemRowSet Memory Usage",
                         kudu::MetricUnit::kBytes,
                         "Size of this tablet's memrowset");
METRIC_DEFINE_gauge_size(tablet, on_disk_size, "Tablet Size On Disk",
                         kudu::MetricUnit::kBytes,
                         "Size of this tablet on disk.");

using std::shared_ptr;
using std::string;
using std::unordered_set;
using std::vector;

namespace kudu {
namespace tablet {

using kudu::MaintenanceManager;
using consensus::OpId;
using consensus::MaximumOpId;
using log::LogAnchorRegistry;
using strings::Substitute;
using base::subtle::Barrier_AtomicIncrement;

static CompactionPolicy *CreateCompactionPolicy() {
  return new BudgetedCompactionPolicy(FLAGS_tablet_compaction_budget_mb);
}

////////////////////////////////////////////////////////////
// TabletComponents
////////////////////////////////////////////////////////////

TabletComponents::TabletComponents(shared_ptr<MemRowSet> mrs,
                                   shared_ptr<RowSetTree> rs_tree)
    : memrowset(std::move(mrs)), rowsets(std::move(rs_tree)) {}

////////////////////////////////////////////////////////////
// Tablet
////////////////////////////////////////////////////////////

const char* Tablet::kDMSMemTrackerId = "DeltaMemStores";

Tablet::Tablet(const scoped_refptr<TabletMetadata>& metadata,
               const scoped_refptr<server::Clock>& clock,
               const shared_ptr<MemTracker>& parent_mem_tracker,
               MetricRegistry* metric_registry,
               const scoped_refptr<LogAnchorRegistry>& log_anchor_registry)
  : key_schema_(metadata->schema().CreateKeyProjection()),
    metadata_(metadata),
    log_anchor_registry_(log_anchor_registry),
    mem_tracker_(MemTracker::CreateTracker(
        -1, Substitute("tablet-$0", tablet_id()),
                       parent_mem_tracker)),
    dms_mem_tracker_(MemTracker::CreateTracker(
        -1, kDMSMemTrackerId, mem_tracker_)),
    next_mrs_id_(0),
    clock_(clock),
    mvcc_(clock),
    rowsets_flush_sem_(1),
    state_(kInitialized) {
      CHECK(schema()->has_column_ids());
  compaction_policy_.reset(CreateCompactionPolicy());

  if (metric_registry) {
    MetricEntity::AttributeMap attrs;
    // TODO(KUDU-745): table_id is apparently not set in the metadata.
    attrs["table_id"] = metadata_->table_id();
    attrs["table_name"] = metadata_->table_name();
    attrs["partition"] = metadata_->partition_schema().PartitionDebugString(metadata_->partition(),
                                                                            *schema());
    metric_entity_ = METRIC_ENTITY_tablet.Instantiate(metric_registry, tablet_id(), attrs);
    metrics_.reset(new TabletMetrics(metric_entity_));
    METRIC_memrowset_size.InstantiateFunctionGauge(
      metric_entity_, Bind(&Tablet::MemRowSetSize, Unretained(this)))
      ->AutoDetach(&metric_detacher_);
    METRIC_on_disk_size.InstantiateFunctionGauge(
      metric_entity_, Bind(&Tablet::EstimateOnDiskSize, Unretained(this)))
      ->AutoDetach(&metric_detacher_);
  }
}

Tablet::~Tablet() {
  Shutdown();
  dms_mem_tracker_->UnregisterFromParent();
  mem_tracker_->UnregisterFromParent();
}

Status Tablet::Open() {
  TRACE_EVENT0("tablet", "Tablet::Open");
  boost::lock_guard<rw_spinlock> lock(component_lock_);
  CHECK_EQ(state_, kInitialized) << "already open";
  CHECK(schema()->has_column_ids());

  next_mrs_id_ = metadata_->last_durable_mrs_id() + 1;

  RowSetVector rowsets_opened;

  // open the tablet row-sets
  for (const shared_ptr<RowSetMetadata>& rowset_meta : metadata_->rowsets()) {
    shared_ptr<DiskRowSet> rowset;
    Status s = DiskRowSet::Open(rowset_meta, log_anchor_registry_.get(), &rowset, mem_tracker_);
    if (!s.ok()) {
      LOG(ERROR) << "Failed to open rowset " << rowset_meta->ToString() << ": "
                 << s.ToString();
      return s;
    }

    rowsets_opened.push_back(rowset);
  }

  shared_ptr<RowSetTree> new_rowset_tree(new RowSetTree());
  CHECK_OK(new_rowset_tree->Reset(rowsets_opened));
  // now that the current state is loaded, create the new MemRowSet with the next id
  shared_ptr<MemRowSet> new_mrs(new MemRowSet(next_mrs_id_++, *schema(),
                                              log_anchor_registry_.get(),
                                              mem_tracker_));
  components_ = new TabletComponents(new_mrs, new_rowset_tree);

  state_ = kBootstrapping;
  return Status::OK();
}

void Tablet::MarkFinishedBootstrapping() {
  CHECK_EQ(state_, kBootstrapping);
  state_ = kOpen;
}

void Tablet::Shutdown() {
  UnregisterMaintenanceOps();

  boost::lock_guard<rw_spinlock> lock(component_lock_);
  components_ = nullptr;
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

BloomFilterSizing Tablet::bloom_sizing() const {
  return BloomFilterSizing::BySizeAndFPRate(FLAGS_tablet_bloom_block_size,
                                            FLAGS_tablet_bloom_target_fp_rate);
}

Status Tablet::NewRowIterator(const Schema &projection,
                              gscoped_ptr<RowwiseIterator> *iter) const {
  // Yield current rows.
  MvccSnapshot snap(mvcc_);
  return NewRowIterator(projection, snap, Tablet::UNORDERED, iter);
}


Status Tablet::NewRowIterator(const Schema &projection,
                              const MvccSnapshot &snap,
                              const OrderMode order,
                              gscoped_ptr<RowwiseIterator> *iter) const {
  CHECK_EQ(state_, kOpen);
  if (metrics_) {
    metrics_->scans_started->Increment();
  }
  VLOG(2) << "Created new Iterator under snap: " << snap.ToString();
  iter->reset(new Iterator(this, projection, snap, order));
  return Status::OK();
}

Status Tablet::DecodeWriteOperations(const Schema* client_schema,
                                     WriteTransactionState* tx_state) {
  TRACE_EVENT0("tablet", "Tablet::DecodeWriteOperations");

  DCHECK_EQ(tx_state->row_ops().size(), 0);

  // Acquire the schema lock in shared mode, so that the schema doesn't
  // change while this transaction is in-flight.
  tx_state->AcquireSchemaLock(&schema_lock_);

  // The Schema needs to be held constant while any transactions are between
  // PREPARE and APPLY stages
  TRACE("PREPARE: Decoding operations");
  vector<DecodedRowOperation> ops;

  // Decode the ops
  RowOperationsPBDecoder dec(&tx_state->request()->row_operations(),
                             client_schema,
                             schema(),
                             tx_state->arena());
  RETURN_NOT_OK(dec.DecodeOperations(&ops));

  // Create RowOp objects for each
  vector<RowOp*> row_ops;
  ops.reserve(ops.size());
  for (const DecodedRowOperation& op : ops) {
    row_ops.push_back(new RowOp(op));
  }

  // Important to set the schema before the ops -- we need the
  // schema in order to stringify the ops.
  tx_state->set_schema_at_decode_time(schema());
  tx_state->swap_row_ops(&row_ops);

  return Status::OK();
}

Status Tablet::AcquireRowLocks(WriteTransactionState* tx_state) {
  TRACE_EVENT1("tablet", "Tablet::AcquireRowLocks",
               "num_locks", tx_state->row_ops().size());
  TRACE("PREPARE: Acquiring locks for $0 operations", tx_state->row_ops().size());
  for (RowOp* op : tx_state->row_ops()) {
    RETURN_NOT_OK(AcquireLockForOp(tx_state, op));
  }
  TRACE("PREPARE: locks acquired");
  return Status::OK();
}

Status Tablet::CheckRowInTablet(const ConstContiguousRow& row) const {
  bool contains_row;
  RETURN_NOT_OK(metadata_->partition_schema().PartitionContainsRow(metadata_->partition(),
                                                                   row,
                                                                   &contains_row));

  if (PREDICT_FALSE(!contains_row)) {
    return Status::NotFound(
        Substitute("Row not in tablet partition. Partition: '$0', row: '$1'.",
                   metadata_->partition_schema().PartitionDebugString(metadata_->partition(),
                                                                      *schema()),
                   metadata_->partition_schema().RowDebugString(row)));
  }
  return Status::OK();
}

Status Tablet::AcquireLockForOp(WriteTransactionState* tx_state, RowOp* op) {
  ConstContiguousRow row_key(&key_schema_, op->decoded_op.row_data);
  op->key_probe.reset(new tablet::RowSetKeyProbe(row_key));
  RETURN_NOT_OK(CheckRowInTablet(row_key));

  ScopedRowLock row_lock(&lock_manager_,
                         tx_state,
                         op->key_probe->encoded_key_slice(),
                         LockManager::LOCK_EXCLUSIVE);
  op->row_lock = row_lock.Pass();
  return Status::OK();
}

void Tablet::StartTransaction(WriteTransactionState* tx_state) {
  gscoped_ptr<ScopedTransaction> mvcc_tx;

  // If the state already has a timestamp then we're replaying a transaction that occurred
  // before a crash or at another node...
  if (tx_state->has_timestamp()) {
    mvcc_tx.reset(new ScopedTransaction(&mvcc_, tx_state->timestamp()));

  // ... otherwise this is a new transaction and we must assign a new timestamp. We either
  // assign a timestamp in the future, if the consistency mode is COMMIT_WAIT, or we assign
  // one in the present if the consistency mode is any other one.
  } else if (tx_state->external_consistency_mode() == COMMIT_WAIT) {
    mvcc_tx.reset(new ScopedTransaction(&mvcc_, ScopedTransaction::NOW_LATEST));
  } else {
    mvcc_tx.reset(new ScopedTransaction(&mvcc_, ScopedTransaction::NOW));
  }
  tx_state->SetMvccTxAndTimestamp(mvcc_tx.Pass());
}

Status Tablet::InsertUnlocked(WriteTransactionState *tx_state,
                              RowOp* insert) {
  const TabletComponents* comps = DCHECK_NOTNULL(tx_state->tablet_components());

  CHECK(state_ == kOpen || state_ == kBootstrapping);
  // make sure that the WriteTransactionState has the component lock and that
  // there the RowOp has the row lock.
  DCHECK(insert->has_row_lock()) << "RowOp must hold the row lock.";
  DCHECK_EQ(tx_state->schema_at_decode_time(), schema()) << "Raced against schema change";
  DCHECK(tx_state->op_id().IsInitialized()) << "TransactionState OpId needed for anchoring";

  ProbeStats stats;

  // Submit the stats before returning from this function
  ProbeStatsSubmitter submitter(stats, metrics_.get());

  // First, ensure that it is a unique key by checking all the open RowSets.
  if (FLAGS_tablet_do_dup_key_checks) {
    vector<RowSet *> to_check;
    comps->rowsets->FindRowSetsWithKeyInRange(insert->key_probe->encoded_key_slice(),
                                              &to_check);

    for (const RowSet *rowset : to_check) {
      bool present = false;
      RETURN_NOT_OK(rowset->CheckRowPresent(*insert->key_probe, &present, &stats));
      if (PREDICT_FALSE(present)) {
        Status s = Status::AlreadyPresent("key already present");
        if (metrics_) {
          metrics_->insertions_failed_dup_key->Increment();
        }
        insert->SetFailed(s);
        return s;
      }
    }
  }

  Timestamp ts = tx_state->timestamp();
  ConstContiguousRow row(schema(), insert->decoded_op.row_data);

  // TODO: the Insert() call below will re-encode the key, which is a
  // waste. Should pass through the KeyProbe structure perhaps.

  // Now try to insert into memrowset. The memrowset itself will return
  // AlreadyPresent if it has already been inserted there.
  Status s = comps->memrowset->Insert(ts, row, tx_state->op_id());
  if (PREDICT_TRUE(s.ok())) {
    insert->SetInsertSucceeded(comps->memrowset->mrs_id());
  } else {
    if (s.IsAlreadyPresent() && metrics_) {
      metrics_->insertions_failed_dup_key->Increment();
    }
    insert->SetFailed(s);
  }
  return s;
}

Status Tablet::MutateRowUnlocked(WriteTransactionState *tx_state,
                                 RowOp* mutate) {
  DCHECK(tx_state != nullptr) << "you must have a WriteTransactionState";
  DCHECK(tx_state->op_id().IsInitialized()) << "TransactionState OpId needed for anchoring";
  DCHECK_EQ(tx_state->schema_at_decode_time(), schema());

  gscoped_ptr<OperationResultPB> result(new OperationResultPB());

  const TabletComponents* comps = DCHECK_NOTNULL(tx_state->tablet_components());

  // Validate the update.
  RowChangeListDecoder rcl_decoder(mutate->decoded_op.changelist);
  Status s = rcl_decoder.Init();
  if (rcl_decoder.is_reinsert()) {
    // REINSERT mutations are the byproduct of an INSERT on top of a ghost
    // row, not something the user is allowed to specify on their own.
    s = Status::InvalidArgument("User may not specify REINSERT mutations");
  }
  if (!s.ok()) {
    mutate->SetFailed(s);
    return s;
  }

  Timestamp ts = tx_state->timestamp();

  ProbeStats stats;
  // Submit the stats before returning from this function
  ProbeStatsSubmitter submitter(stats, metrics_.get());

  // First try to update in memrowset.
  s = comps->memrowset->MutateRow(ts,
                            *mutate->key_probe,
                            mutate->decoded_op.changelist,
                            tx_state->op_id(),
                            &stats,
                            result.get());
  if (s.ok()) {
    mutate->SetMutateSucceeded(result.Pass());
    return s;
  }
  if (!s.IsNotFound()) {
    mutate->SetFailed(s);
    return s;
  }

  // Next, check the disk rowsets.

  // TODO: could iterate the rowsets in a smart order
  // based on recent statistics - eg if a rowset is getting
  // updated frequently, pick that one first.
  vector<RowSet *> to_check;
  comps->rowsets->FindRowSetsWithKeyInRange(mutate->key_probe->encoded_key_slice(),
                                            &to_check);
  for (RowSet *rs : to_check) {
    s = rs->MutateRow(ts,
                      *mutate->key_probe,
                      mutate->decoded_op.changelist,
                      tx_state->op_id(),
                      &stats,
                      result.get());
    if (s.ok()) {
      mutate->SetMutateSucceeded(result.Pass());
      return s;
    }
    if (!s.IsNotFound()) {
      mutate->SetFailed(s);
      return s;
    }
  }

  s = Status::NotFound("key not found");
  mutate->SetFailed(s);
  return s;
}

void Tablet::StartApplying(WriteTransactionState* tx_state) {
  boost::shared_lock<rw_spinlock> lock(component_lock_);
  tx_state->StartApplying();
  tx_state->set_tablet_components(components_);
}

void Tablet::ApplyRowOperations(WriteTransactionState* tx_state) {
  StartApplying(tx_state);
  for (RowOp* row_op : tx_state->row_ops()) {
    ApplyRowOperation(tx_state, row_op);
  }
}

void Tablet::ApplyRowOperation(WriteTransactionState* tx_state,
                               RowOp* row_op) {
  switch (row_op->decoded_op.type) {
    case RowOperationsPB::INSERT:
      ignore_result(InsertUnlocked(tx_state, row_op));
      return;

    case RowOperationsPB::UPDATE:
    case RowOperationsPB::DELETE:
      ignore_result(MutateRowUnlocked(tx_state, row_op));
      return;

    default:
      LOG(FATAL) << RowOperationsPB::Type_Name(row_op->decoded_op.type);
  }
}

void Tablet::ModifyRowSetTree(const RowSetTree& old_tree,
                              const RowSetVector& rowsets_to_remove,
                              const RowSetVector& rowsets_to_add,
                              RowSetTree* new_tree) {
  RowSetVector post_swap;

  // O(n^2) diff algorithm to collect the set of rowsets excluding
  // the rowsets that were included in the compaction
  int num_removed = 0;

  for (const shared_ptr<RowSet> &rs : old_tree.all_rowsets()) {
    // Determine if it should be removed
    bool should_remove = false;
    for (const shared_ptr<RowSet> &to_remove : rowsets_to_remove) {
      if (to_remove == rs) {
        should_remove = true;
        num_removed++;
        break;
      }
    }
    if (!should_remove) {
      post_swap.push_back(rs);
    }
  }

  CHECK_EQ(num_removed, rowsets_to_remove.size());

  // Then push the new rowsets on the end of the new list
  std::copy(rowsets_to_add.begin(),
            rowsets_to_add.end(),
            std::back_inserter(post_swap));

  CHECK_OK(new_tree->Reset(post_swap));
}

void Tablet::AtomicSwapRowSets(const RowSetVector &old_rowsets,
                               const RowSetVector &new_rowsets) {
  boost::lock_guard<rw_spinlock> lock(component_lock_);
  AtomicSwapRowSetsUnlocked(old_rowsets, new_rowsets);
}

void Tablet::AtomicSwapRowSetsUnlocked(const RowSetVector &to_remove,
                                       const RowSetVector &to_add) {
  DCHECK(component_lock_.is_locked());

  shared_ptr<RowSetTree> new_tree(new RowSetTree());
  ModifyRowSetTree(*components_->rowsets,
                   to_remove, to_add, new_tree.get());

  components_ = new TabletComponents(components_->memrowset, new_tree);
}

Status Tablet::DoMajorDeltaCompaction(const vector<ColumnId>& col_ids,
                                      shared_ptr<RowSet> input_rs) {
  CHECK_EQ(state_, kOpen);
  Status s = down_cast<DiskRowSet*>(input_rs.get())
      ->MajorCompactDeltaStoresWithColumnIds(col_ids);
  return s;
}

Status Tablet::Flush() {
  TRACE_EVENT1("tablet", "Tablet::Flush", "id", tablet_id());
  boost::lock_guard<Semaphore> lock(rowsets_flush_sem_);
  return FlushUnlocked();
}

Status Tablet::FlushUnlocked() {
  TRACE_EVENT0("tablet", "Tablet::FlushUnlocked");
  RowSetsInCompaction input;
  shared_ptr<MemRowSet> old_mrs;
  {
    // Create a new MRS with the latest schema.
    boost::lock_guard<rw_spinlock> lock(component_lock_);
    RETURN_NOT_OK(ReplaceMemRowSetUnlocked(&input, &old_mrs));
  }

  // Wait for any in-flight transactions to finish against the old MRS
  // before we flush it.
  mvcc_.WaitForApplyingTransactionsToCommit();

  // Note: "input" should only contain old_mrs.
  return FlushInternal(input, old_mrs);
}

Status Tablet::ReplaceMemRowSetUnlocked(RowSetsInCompaction *compaction,
                                        shared_ptr<MemRowSet> *old_ms) {
  *old_ms = components_->memrowset;
  // Mark the memrowset rowset as locked, so compactions won't consider it
  // for inclusion in any concurrent compactions.
  shared_ptr<boost::mutex::scoped_try_lock> ms_lock(
    new boost::mutex::scoped_try_lock(*((*old_ms)->compact_flush_lock())));
  CHECK(ms_lock->owns_lock());

  // Add to compaction.
  compaction->AddRowSet(*old_ms, ms_lock);

  shared_ptr<MemRowSet> new_mrs(new MemRowSet(next_mrs_id_++, *schema(), log_anchor_registry_.get(),
                                mem_tracker_));
  shared_ptr<RowSetTree> new_rst(new RowSetTree());
  ModifyRowSetTree(*components_->rowsets,
                   RowSetVector(), // remove nothing
                   { *old_ms }, // add the old MRS
                   new_rst.get());

  // Swap it in
  components_ = new TabletComponents(new_mrs, new_rst);
  return Status::OK();
}

Status Tablet::FlushInternal(const RowSetsInCompaction& input,
                             const shared_ptr<MemRowSet>& old_ms) {
  CHECK(state_ == kOpen || state_ == kBootstrapping);

  // Step 1. Freeze the old memrowset by blocking readers and swapping
  // it in as a new rowset, replacing it with an empty one.
  //
  // At this point, we have already swapped in a new empty rowset, and
  // any new inserts are going into that one. 'old_ms' is effectively
  // frozen -- no new inserts should arrive after this point.
  //
  // NOTE: updates and deletes may still arrive into 'old_ms' at this point.
  //
  // TODO(perf): there's a memrowset.Freeze() call which we might be able to
  // use to improve iteration performance during the flush. The old design
  // used this, but not certain whether it's still doable with the new design.

  uint64_t start_insert_count = old_ms->debug_insert_count();
  int64_t mrs_being_flushed = old_ms->mrs_id();

  if (flush_hooks_) {
    RETURN_NOT_OK_PREPEND(flush_hooks_->PostSwapNewMemRowSet(),
                          "PostSwapNewMemRowSet hook failed");
  }

  LOG(INFO) << "Flush: entering stage 1 (old memrowset already frozen for inserts)";
  input.DumpToLog();
  LOG(INFO) << "Memstore in-memory size: " << old_ms->memory_footprint() << " bytes";

  RETURN_NOT_OK(DoCompactionOrFlush(input, mrs_being_flushed));

  // Sanity check that no insertions happened during our flush.
  CHECK_EQ(start_insert_count, old_ms->debug_insert_count())
    << "Sanity check failed: insertions continued in memrowset "
    << "after flush was triggered! Aborting to prevent dataloss.";

  return Status::OK();
}

Status Tablet::CreatePreparedAlterSchema(AlterSchemaTransactionState *tx_state,
                                         const Schema* schema) {
  if (!key_schema_.KeyEquals(*schema)) {
    return Status::InvalidArgument("Schema keys cannot be altered",
                                   schema->CreateKeyProjection().ToString());
  }

  if (!schema->has_column_ids()) {
    // this probably means that the request is not from the Master
    return Status::InvalidArgument("Missing Column IDs");
  }

  // Alter schema must run when no reads/writes are in progress.
  // However, compactions and flushes can continue to run in parallel
  // with the schema change,
  tx_state->AcquireSchemaLock(&schema_lock_);

  tx_state->set_schema(schema);
  return Status::OK();
}

Status Tablet::AlterSchema(AlterSchemaTransactionState *tx_state) {
  DCHECK(key_schema_.KeyEquals(*DCHECK_NOTNULL(tx_state->schema()))) <<
    "Schema keys cannot be altered";

  // Prevent any concurrent flushes. Otherwise, we run into issues where
  // we have an MRS in the rowset tree, and we can't alter its schema
  // in-place.
  boost::lock_guard<Semaphore> lock(rowsets_flush_sem_);

  RowSetsInCompaction input;
  shared_ptr<MemRowSet> old_ms;
  {
    // If the current version >= new version, there is nothing to do.
    bool same_schema = schema()->Equals(*tx_state->schema());
    if (metadata_->schema_version() >= tx_state->schema_version()) {
      LOG(INFO) << "Already running schema version " << metadata_->schema_version()
                << " got alter request for version " << tx_state->schema_version();
      return Status::OK();
    }

    LOG(INFO) << "Alter schema from " << schema()->ToString()
              << " version " << metadata_->schema_version()
              << " to " << tx_state->schema()->ToString()
              << " version " << tx_state->schema_version();
    DCHECK(schema_lock_.is_locked());
    metadata_->SetSchema(*tx_state->schema(), tx_state->schema_version());
    if (tx_state->has_new_table_name()) {
      metadata_->SetTableName(tx_state->new_table_name());
      if (metric_entity_) {
        metric_entity_->SetAttribute("table_name", tx_state->new_table_name());
      }
    }

    // If the current schema and the new one are equal, there is nothing to do.
    if (same_schema) {
      return metadata_->Flush();
    }
  }


  // Replace the MemRowSet
  {
    boost::lock_guard<rw_spinlock> lock(component_lock_);
    RETURN_NOT_OK(ReplaceMemRowSetUnlocked(&input, &old_ms));
  }

  // TODO(KUDU-915): ideally we would release the schema_lock here so that
  // we don't block access to the tablet while we flush the MRS.
  // However, doing so opens up some subtle issues with the ordering of
  // the alter's COMMIT message against the COMMIT messages of other
  // writes. A "big hammer" fix has been applied here to hold the lock
  // all the way until the COMMIT message has been appended to the WAL.

  // Flush the old MemRowSet
  return FlushInternal(input, old_ms);
}

Status Tablet::RewindSchemaForBootstrap(const Schema& new_schema,
                                        int64_t schema_version) {
  CHECK_EQ(state_, kBootstrapping);

  // We know that the MRS should be empty at this point, because we
  // rewind the schema before replaying any operations. So, we just
  // swap in a new one with the correct schema, rather than attempting
  // to flush.
  LOG(INFO) << "Rewinding schema during bootstrap to " << new_schema.ToString();

  metadata_->SetSchema(new_schema, schema_version);
  {
    boost::lock_guard<rw_spinlock> lock(component_lock_);

    shared_ptr<MemRowSet> old_mrs = components_->memrowset;
    shared_ptr<RowSetTree> old_rowsets = components_->rowsets;
    CHECK(old_mrs->empty());
    int64_t old_mrs_id = old_mrs->mrs_id();
    // We have to reset the components here before creating the new MemRowSet,
    // or else the new MRS will end up trying to claim the same MemTracker ID
    // as the old one.
    components_.reset();
    old_mrs.reset();
    shared_ptr<MemRowSet> new_mrs(new MemRowSet(old_mrs_id, new_schema,
                                                log_anchor_registry_.get(), mem_tracker_));
    components_ = new TabletComponents(new_mrs, old_rowsets);
  }
  return Status::OK();
}

void Tablet::SetCompactionHooksForTests(
  const shared_ptr<Tablet::CompactionFaultHooks> &hooks) {
  compaction_hooks_ = hooks;
}

void Tablet::SetFlushHooksForTests(
  const shared_ptr<Tablet::FlushFaultHooks> &hooks) {
  flush_hooks_ = hooks;
}

void Tablet::SetFlushCompactCommonHooksForTests(
  const shared_ptr<Tablet::FlushCompactCommonHooks> &hooks) {
  common_hooks_ = hooks;
}

int32_t Tablet::CurrentMrsIdForTests() const {
  boost::shared_lock<rw_spinlock> lock(component_lock_);
  return components_->memrowset->mrs_id();
}

////////////////////////////////////////////////////////////
// CompactRowSetsOp
////////////////////////////////////////////////////////////

CompactRowSetsOp::CompactRowSetsOp(Tablet* tablet)
  : MaintenanceOp(Substitute("CompactRowSetsOp($0)", tablet->tablet_id()),
                  MaintenanceOp::HIGH_IO_USAGE),
    last_num_mrs_flushed_(0),
    last_num_rs_compacted_(0),
    tablet_(tablet) {
}

void CompactRowSetsOp::UpdateStats(MaintenanceOpStats* stats) {
  boost::lock_guard<simple_spinlock> l(lock_);

  // Any operation that changes the on-disk row layout invalidates the
  // cached stats.
  TabletMetrics* metrics = tablet_->metrics();
  if (metrics) {
    uint64_t new_num_mrs_flushed = metrics->flush_mrs_duration->TotalCount();
    uint64_t new_num_rs_compacted = metrics->compact_rs_duration->TotalCount();
    if (prev_stats_.valid() &&
        new_num_mrs_flushed == last_num_mrs_flushed_ &&
        new_num_rs_compacted == last_num_rs_compacted_) {
      *stats = prev_stats_;
      return;
    } else {
      last_num_mrs_flushed_ = new_num_mrs_flushed;
      last_num_rs_compacted_ = new_num_rs_compacted;
    }
  }

  tablet_->UpdateCompactionStats(&prev_stats_);
  *stats = prev_stats_;
}

bool CompactRowSetsOp::Prepare() {
  boost::lock_guard<simple_spinlock> l(lock_);
  // Invalidate the cached stats so that another section of the tablet can
  // be compacted concurrently.
  //
  // TODO: we should acquire the rowset compaction locks here. Otherwise, until
  // Compact() acquires them, the maintenance manager may compute the same
  // stats for this op and run it again, even though Perform() will end up
  // performing a much less fruitful compaction. See KUDU-790 for more details.
  prev_stats_.Clear();
  return true;
}

void CompactRowSetsOp::Perform() {
  WARN_NOT_OK(tablet_->Compact(Tablet::COMPACT_NO_FLAGS),
              Substitute("Compaction failed on $0", tablet_->tablet_id()));
}

scoped_refptr<Histogram> CompactRowSetsOp::DurationHistogram() const {
  return tablet_->metrics()->compact_rs_duration;
}

scoped_refptr<AtomicGauge<uint32_t> > CompactRowSetsOp::RunningGauge() const {
  return tablet_->metrics()->compact_rs_running;
}

////////////////////////////////////////////////////////////
// MinorDeltaCompactionOp
////////////////////////////////////////////////////////////

MinorDeltaCompactionOp::MinorDeltaCompactionOp(Tablet* tablet)
  : MaintenanceOp(Substitute("MinorDeltaCompactionOp($0)", tablet->tablet_id()),
                  MaintenanceOp::HIGH_IO_USAGE),
    last_num_mrs_flushed_(0),
    last_num_dms_flushed_(0),
    last_num_rs_compacted_(0),
    last_num_rs_minor_delta_compacted_(0),
    tablet_(tablet) {
}

void MinorDeltaCompactionOp::UpdateStats(MaintenanceOpStats* stats) {
  boost::lock_guard<simple_spinlock> l(lock_);

  // Any operation that changes the number of REDO files invalidates the
  // cached stats.
  TabletMetrics* metrics = tablet_->metrics();
  if (metrics) {
    uint64_t new_num_mrs_flushed = metrics->flush_mrs_duration->TotalCount();
    uint64_t new_num_dms_flushed = metrics->flush_dms_duration->TotalCount();
    uint64_t new_num_rs_compacted = metrics->compact_rs_duration->TotalCount();
    uint64_t new_num_rs_minor_delta_compacted =
        metrics->delta_minor_compact_rs_duration->TotalCount();
    if (prev_stats_.valid() &&
        new_num_mrs_flushed == last_num_mrs_flushed_ &&
        new_num_dms_flushed == last_num_dms_flushed_ &&
        new_num_rs_compacted == last_num_rs_compacted_ &&
        new_num_rs_minor_delta_compacted == last_num_rs_minor_delta_compacted_) {
      *stats = prev_stats_;
      return;
    } else {
      last_num_mrs_flushed_ = new_num_mrs_flushed;
      last_num_dms_flushed_ = new_num_dms_flushed;
      last_num_rs_compacted_ = new_num_rs_compacted;
      last_num_rs_minor_delta_compacted_ = new_num_rs_minor_delta_compacted;
    }
  }

  double perf_improv = tablet_->GetPerfImprovementForBestDeltaCompact(
      RowSet::MINOR_DELTA_COMPACTION, nullptr);
  prev_stats_.set_perf_improvement(perf_improv);
  prev_stats_.set_runnable(perf_improv > 0);
  *stats = prev_stats_;
}

bool MinorDeltaCompactionOp::Prepare() {
  boost::lock_guard<simple_spinlock> l(lock_);
  // Invalidate the cached stats so that another rowset in the tablet can
  // be delta compacted concurrently.
  //
  // TODO: See CompactRowSetsOp::Prepare().
  prev_stats_.Clear();
  return true;
}

void MinorDeltaCompactionOp::Perform() {
  WARN_NOT_OK(tablet_->CompactWorstDeltas(RowSet::MINOR_DELTA_COMPACTION),
              Substitute("Minor delta compaction failed on $0", tablet_->tablet_id()));
}

scoped_refptr<Histogram> MinorDeltaCompactionOp::DurationHistogram() const {
  return tablet_->metrics()->delta_minor_compact_rs_duration;
}

scoped_refptr<AtomicGauge<uint32_t> > MinorDeltaCompactionOp::RunningGauge() const {
  return tablet_->metrics()->delta_minor_compact_rs_running;
}

////////////////////////////////////////////////////////////
// MajorDeltaCompactionOp
////////////////////////////////////////////////////////////

MajorDeltaCompactionOp::MajorDeltaCompactionOp(Tablet* tablet)
  : MaintenanceOp(Substitute("MajorDeltaCompactionOp($0)", tablet->tablet_id()),
                  MaintenanceOp::HIGH_IO_USAGE),
    last_num_mrs_flushed_(0),
    last_num_dms_flushed_(0),
    last_num_rs_compacted_(0),
    last_num_rs_minor_delta_compacted_(0),
    last_num_rs_major_delta_compacted_(0),
    tablet_(tablet) {
}

void MajorDeltaCompactionOp::UpdateStats(MaintenanceOpStats* stats) {
  boost::lock_guard<simple_spinlock> l(lock_);

  // Any operation that changes the size of the on-disk data invalidates the
  // cached stats.
  TabletMetrics* metrics = tablet_->metrics();
  if (metrics) {
    int64_t new_num_mrs_flushed = metrics->flush_mrs_duration->TotalCount();
    int64_t new_num_dms_flushed = metrics->flush_dms_duration->TotalCount();
    int64_t new_num_rs_compacted = metrics->compact_rs_duration->TotalCount();
    int64_t new_num_rs_minor_delta_compacted =
        metrics->delta_minor_compact_rs_duration->TotalCount();
    int64_t new_num_rs_major_delta_compacted =
        metrics->delta_major_compact_rs_duration->TotalCount();
    if (prev_stats_.valid() &&
        new_num_mrs_flushed == last_num_mrs_flushed_ &&
        new_num_dms_flushed == last_num_dms_flushed_ &&
        new_num_rs_compacted == last_num_rs_compacted_ &&
        new_num_rs_minor_delta_compacted == last_num_rs_minor_delta_compacted_ &&
        new_num_rs_major_delta_compacted == last_num_rs_major_delta_compacted_) {
      *stats = prev_stats_;
      return;
    } else {
      last_num_mrs_flushed_ = new_num_mrs_flushed;
      last_num_dms_flushed_ = new_num_dms_flushed;
      last_num_rs_compacted_ = new_num_rs_compacted;
      last_num_rs_minor_delta_compacted_ = new_num_rs_minor_delta_compacted;
      last_num_rs_major_delta_compacted_ = new_num_rs_major_delta_compacted;
    }
  }

  double perf_improv = tablet_->GetPerfImprovementForBestDeltaCompact(
      RowSet::MAJOR_DELTA_COMPACTION, nullptr);
  prev_stats_.set_perf_improvement(perf_improv);
  prev_stats_.set_runnable(perf_improv > 0);
  *stats = prev_stats_;
}

bool MajorDeltaCompactionOp::Prepare() {
  boost::lock_guard<simple_spinlock> l(lock_);
  // Invalidate the cached stats so that another rowset in the tablet can
  // be delta compacted concurrently.
  //
  // TODO: See CompactRowSetsOp::Prepare().
  prev_stats_.Clear();
  return true;
}

void MajorDeltaCompactionOp::Perform() {
  WARN_NOT_OK(tablet_->CompactWorstDeltas(RowSet::MAJOR_DELTA_COMPACTION),
              Substitute("Major delta compaction failed on $0", tablet_->tablet_id()));
}

scoped_refptr<Histogram> MajorDeltaCompactionOp::DurationHistogram() const {
  return tablet_->metrics()->delta_major_compact_rs_duration;
}

scoped_refptr<AtomicGauge<uint32_t> > MajorDeltaCompactionOp::RunningGauge() const {
  return tablet_->metrics()->delta_major_compact_rs_running;
}

////////////////////////////////////////////////////////////
// Tablet
////////////////////////////////////////////////////////////

Status Tablet::PickRowSetsToCompact(RowSetsInCompaction *picked,
                                    CompactFlags flags) const {
  CHECK_EQ(state_, kOpen);
  // Grab a local reference to the current RowSetTree. This is to avoid
  // holding the component_lock_ for too long. See the comment on component_lock_
  // in tablet.h for details on why that would be bad.
  shared_ptr<RowSetTree> rowsets_copy;
  {
    boost::shared_lock<rw_spinlock> lock(component_lock_);
    rowsets_copy = components_->rowsets;
  }

  boost::lock_guard<boost::mutex> compact_lock(compact_select_lock_);
  CHECK_EQ(picked->num_rowsets(), 0);

  unordered_set<RowSet*> picked_set;

  if (flags & FORCE_COMPACT_ALL) {
    // Compact all rowsets, regardless of policy.
    for (const shared_ptr<RowSet>& rs : rowsets_copy->all_rowsets()) {
      if (rs->IsAvailableForCompaction()) {
        picked_set.insert(rs.get());
      }
    }
  } else {
    // Let the policy decide which rowsets to compact.
    double quality = 0;
    RETURN_NOT_OK(compaction_policy_->PickRowSets(*rowsets_copy, &picked_set, &quality, NULL));
    VLOG(2) << "Compaction quality: " << quality;
  }

  boost::shared_lock<rw_spinlock> lock(component_lock_);
  for (const shared_ptr<RowSet>& rs : components_->rowsets->all_rowsets()) {
    if (picked_set.erase(rs.get()) == 0) {
      // Not picked.
      continue;
    }

    // Grab the compact_flush_lock: this prevents any other concurrent
    // compaction from selecting this same rowset, and also ensures that
    // we don't select a rowset which is currently in the middle of being
    // flushed.
    shared_ptr<boost::mutex::scoped_try_lock> lock(
      new boost::mutex::scoped_try_lock(*rs->compact_flush_lock()));
    CHECK(lock->owns_lock()) << rs->ToString() << " appeared available for "
      "compaction when inputs were selected, but was unable to lock its "
      "compact_flush_lock to prepare for compaction.";

    // Push the lock on our scoped list, so we unlock when done.
    picked->AddRowSet(rs, lock);
  }

  // When we iterated through the current rowsets, we should have found all of the
  // rowsets that we picked. If we didn't, that implies that some other thread swapped
  // them out while we were making our selection decision -- that's not possible
  // since we only picked rowsets that were marked as available for compaction.
  if (!picked_set.empty()) {
    for (const RowSet* not_found : picked_set) {
      LOG(ERROR) << "Rowset selected for compaction but not available anymore: "
                 << not_found->ToString();
    }
    LOG(FATAL) << "Was unable to find all rowsets selected for compaction";
  }
  return Status::OK();
}

void Tablet::GetRowSetsForTests(RowSetVector* out) {
  shared_ptr<RowSetTree> rowsets_copy;
  {
    boost::shared_lock<rw_spinlock> lock(component_lock_);
    rowsets_copy = components_->rowsets;
  }
  for (const shared_ptr<RowSet>& rs : rowsets_copy->all_rowsets()) {
    out->push_back(rs);
  }
}

void Tablet::RegisterMaintenanceOps(MaintenanceManager* maint_mgr) {
  CHECK_EQ(state_, kOpen);
  DCHECK(maintenance_ops_.empty());

  gscoped_ptr<MaintenanceOp> rs_compact_op(new CompactRowSetsOp(this));
  maint_mgr->RegisterOp(rs_compact_op.get());
  maintenance_ops_.push_back(rs_compact_op.release());

  gscoped_ptr<MaintenanceOp> minor_delta_compact_op(new MinorDeltaCompactionOp(this));
  maint_mgr->RegisterOp(minor_delta_compact_op.get());
  maintenance_ops_.push_back(minor_delta_compact_op.release());

  gscoped_ptr<MaintenanceOp> major_delta_compact_op(new MajorDeltaCompactionOp(this));
  maint_mgr->RegisterOp(major_delta_compact_op.get());
  maintenance_ops_.push_back(major_delta_compact_op.release());
}

void Tablet::UnregisterMaintenanceOps() {
  for (MaintenanceOp* op : maintenance_ops_) {
    op->Unregister();
  }
  STLDeleteElements(&maintenance_ops_);
}

Status Tablet::FlushMetadata(const RowSetVector& to_remove,
                             const RowSetMetadataVector& to_add,
                             int64_t mrs_being_flushed) {
  RowSetMetadataIds to_remove_meta;
  for (const shared_ptr<RowSet>& rowset : to_remove) {
    // Skip MemRowSet & DuplicatingRowSets which don't have metadata.
    if (rowset->metadata().get() == nullptr) {
      continue;
    }
    to_remove_meta.insert(rowset->metadata()->id());
  }

  return metadata_->UpdateAndFlush(to_remove_meta, to_add, mrs_being_flushed);
}

Status Tablet::DoCompactionOrFlush(const RowSetsInCompaction &input, int64_t mrs_being_flushed) {
  const char *op_name =
        (mrs_being_flushed == TabletMetadata::kNoMrsFlushed) ? "Compaction" : "Flush";
  TRACE_EVENT2("tablet", "Tablet::DoCompactionOrFlush",
               "tablet_id", tablet_id(),
               "op", op_name);

  MvccSnapshot flush_snap(mvcc_);
  LOG(INFO) << op_name << ": entering phase 1 (flushing snapshot). Phase 1 snapshot: "
      << flush_snap.ToString();

  if (common_hooks_) {
    RETURN_NOT_OK_PREPEND(common_hooks_->PostTakeMvccSnapshot(),
                          "PostTakeMvccSnapshot hook failed");
  }

  shared_ptr<CompactionInput> merge;
  RETURN_NOT_OK(input.CreateCompactionInput(flush_snap, schema(), &merge));

  RollingDiskRowSetWriter drsw(metadata_.get(), merge->schema(), bloom_sizing(),
                               compaction_policy_->target_rowset_size());
  RETURN_NOT_OK_PREPEND(drsw.Open(), "Failed to open DiskRowSet for flush");
  RETURN_NOT_OK_PREPEND(FlushCompactionInput(merge.get(), flush_snap, &drsw),
                        "Flush to disk failed");
  RETURN_NOT_OK_PREPEND(drsw.Finish(), "Failed to finish DRS writer");

  if (common_hooks_) {
    RETURN_NOT_OK_PREPEND(common_hooks_->PostWriteSnapshot(),
                          "PostWriteSnapshot hook failed");
  }

  // Though unlikely, it's possible that all of the input rows were actually
  // GCed in this compaction. In that case, we don't actually want to reopen.
  bool gced_all_input = drsw.written_count() == 0;
  if (gced_all_input) {
    LOG(INFO) << op_name << " resulted in no output rows (all input rows "
              << "were GCed!)  Removing all input rowsets.";

    // Write out the new Tablet Metadata and remove old rowsets.
    // TODO: Consensus catch-up may want to preserve the compaction inputs.
    RETURN_NOT_OK_PREPEND(FlushMetadata(input.rowsets(),
                                        RowSetMetadataVector(),
                                        mrs_being_flushed),
                          "Failed to flush new tablet metadata");

    AtomicSwapRowSets(input.rowsets(), RowSetVector());

    return Status::OK();
  }

  // The RollingDiskRowSet writer wrote out one or more RowSets as the
  // output. Open these into 'new_rowsets'.
  vector<shared_ptr<RowSet> > new_disk_rowsets;
  RowSetMetadataVector new_drs_metas;
  drsw.GetWrittenRowSetMetadata(&new_drs_metas);

  if (metrics_.get()) metrics_->bytes_flushed->IncrementBy(drsw.written_size());
  CHECK(!new_drs_metas.empty());
  {
    TRACE_EVENT0("tablet", "Opening compaction results");
    for (const shared_ptr<RowSetMetadata>& meta : new_drs_metas) {
      shared_ptr<DiskRowSet> new_rowset;
      Status s = DiskRowSet::Open(meta, log_anchor_registry_.get(), &new_rowset, mem_tracker_);
      if (!s.ok()) {
        LOG(WARNING) << "Unable to open snapshot " << op_name << " results "
                     << meta->ToString() << ": " << s.ToString();
        return s;
      }
      new_disk_rowsets.push_back(new_rowset);
    }
  }

  // Setup for Phase 2: Start duplicating any new updates into the new on-disk
  // rowsets.
  //
  // During Phase 1, we may have missed some updates which came into the input
  // rowsets while we were writing. So, we can't immediately start reading from
  // the on-disk rowsets alone. Starting here, we continue to read from the
  // original rowset(s), but mirror updates to both the input and the output
  // data.
  //
  // It's crucial that, during the rest of the compaction, we do not allow the
  // output rowsets to flush their deltas to disk. This is to avoid the following
  // bug:
  // - during phase 1, timestamp 1 updates a flushed row. This is only reflected in the
  //   input rowset. (ie it is a "missed delta")
  // - during phase 2, timestamp 2 updates the same row. This is reflected in both the
  //   input and output, because of the DuplicatingRowSet.
  // - now suppose the output rowset were allowed to flush deltas. This would create the
  //   first DeltaFile for the output rowset, with only timestamp 2.
  // - Now we run the "ReupdateMissedDeltas", and copy over the first transaction to the output
  //   DMS, which later flushes.
  // The end result would be that redos[0] has timestamp 2, and redos[1] has timestamp 1.
  // This breaks an invariant that the redo files are time-ordered, and would we would probably
  // reapply the deltas in the wrong order on the read path.
  //
  // The way that we avoid this case is that DuplicatingRowSet's FlushDeltas method is a
  // no-op.
  LOG(INFO) << op_name << ": entering phase 2 (starting to duplicate updates "
            << "in new rowsets)";
  shared_ptr<DuplicatingRowSet> inprogress_rowset(
    new DuplicatingRowSet(input.rowsets(), new_disk_rowsets));

  // The next step is to swap in the DuplicatingRowSet, and at the same time, determine an
  // MVCC snapshot which includes all of the transactions that saw a pre-DuplicatingRowSet
  // version of components_.
  MvccSnapshot non_duplicated_txns_snap;
  vector<Timestamp> applying_during_swap;
  {
    TRACE_EVENT0("tablet", "Swapping DuplicatingRowSet");
    // Taking component_lock_ in write mode ensures that no new transactions
    // can StartApplying() (or snapshot components_) during this block.
    boost::lock_guard<rw_spinlock> lock(component_lock_);
    AtomicSwapRowSetsUnlocked(input.rowsets(), { inprogress_rowset });

    // NOTE: transactions may *commit* in between these two lines.
    // We need to make sure all such transactions end up in the
    // 'applying_during_swap' list, the 'non_duplicated_txns_snap' snapshot,
    // or both. Thus it's crucial that these next two lines are in this order!
    mvcc_.GetApplyingTransactionsTimestamps(&applying_during_swap);
    non_duplicated_txns_snap = MvccSnapshot(mvcc_);
  }

  // All transactions committed in 'non_duplicated_txns_snap' saw the pre-swap components_.
  // Additionally, any transactions that were APPLYING during the above block by definition
  // _started_ doing so before the swap. Hence those transactions also need to get included in
  // non_duplicated_txns_snap. To do so, we wait for them to commit, and then
  // manually include them into our snapshot.
  if (VLOG_IS_ON(1) && !applying_during_swap.empty()) {
    VLOG(1) << "Waiting for " << applying_during_swap.size() << " mid-APPLY txns to commit "
            << "before finishing compaction...";
    for (const Timestamp& ts : applying_during_swap) {
      VLOG(1) << "  " << ts.value();
    }
  }

  // This wait is a little bit conservative - technically we only need to wait for
  // those transactions in 'applying_during_swap', but MVCC doesn't implement the
  // ability to wait for a specific set. So instead we wait for all currently applying --
  // a bit more than we need, but still correct.
  mvcc_.WaitForApplyingTransactionsToCommit();

  // Then we want to consider all those transactions that were in-flight when we did the
  // swap as committed in 'non_duplicated_txns_snap'.
  non_duplicated_txns_snap.AddCommittedTimestamps(applying_during_swap);

  if (common_hooks_) {
    RETURN_NOT_OK_PREPEND(common_hooks_->PostSwapInDuplicatingRowSet(),
                          "PostSwapInDuplicatingRowSet hook failed");
  }

  // Phase 2. Here we re-scan the compaction input, copying those missed updates into the
  // new rowset's DeltaTracker.
  LOG(INFO) << op_name << " Phase 2: carrying over any updates which arrived during Phase 1";
  LOG(INFO) << "Phase 2 snapshot: " << non_duplicated_txns_snap.ToString();
  RETURN_NOT_OK_PREPEND(
      input.CreateCompactionInput(non_duplicated_txns_snap, schema(), &merge),
          Substitute("Failed to create $0 inputs", op_name).c_str());

  // Update the output rowsets with the deltas that came in in phase 1, before we swapped
  // in the DuplicatingRowSets. This will perform a flush of the updated DeltaTrackers
  // in the end so that the data that is reported in the log as belonging to the input
  // rowsets is flushed.
  RETURN_NOT_OK_PREPEND(ReupdateMissedDeltas(metadata_->tablet_id(),
                                             merge.get(),
                                             flush_snap,
                                             non_duplicated_txns_snap,
                                             new_disk_rowsets),
        Substitute("Failed to re-update deltas missed during $0 phase 1",
                     op_name).c_str());

  if (common_hooks_) {
    RETURN_NOT_OK_PREPEND(common_hooks_->PostReupdateMissedDeltas(),
                          "PostReupdateMissedDeltas hook failed");
  }

  // ------------------------------
  // Flush was successful.

  // Write out the new Tablet Metadata and remove old rowsets.
  RETURN_NOT_OK_PREPEND(FlushMetadata(input.rowsets(), new_drs_metas, mrs_being_flushed),
                        "Failed to flush new tablet metadata");

  // Replace the compacted rowsets with the new on-disk rowsets, making them visible now that
  // their metadata was written to disk.
  AtomicSwapRowSets({ inprogress_rowset }, new_disk_rowsets);

  LOG(INFO) << op_name << " successful on " << drsw.written_count()
            << " rows " << "(" << drsw.written_size() << " bytes)";

  if (common_hooks_) {
    RETURN_NOT_OK_PREPEND(common_hooks_->PostSwapNewRowSet(),
                          "PostSwapNewRowSet hook failed");
  }

  return Status::OK();
}

Status Tablet::Compact(CompactFlags flags) {
  CHECK_EQ(state_, kOpen);

  RowSetsInCompaction input;
  // Step 1. Capture the rowsets to be merged
  RETURN_NOT_OK_PREPEND(PickRowSetsToCompact(&input, flags),
                        "Failed to pick rowsets to compact");
  if (input.num_rowsets() < 2) {
    VLOG(1) << "Not enough rowsets to run compaction! Aborting...";
    return Status::OK();
  }
  LOG(INFO) << "Compaction: stage 1 complete, picked "
            << input.num_rowsets() << " rowsets to compact";
  if (compaction_hooks_) {
    RETURN_NOT_OK_PREPEND(compaction_hooks_->PostSelectIterators(),
                          "PostSelectIterators hook failed");
  }

  input.DumpToLog();

  return DoCompactionOrFlush(input,
                             TabletMetadata::kNoMrsFlushed);
}

void Tablet::UpdateCompactionStats(MaintenanceOpStats* stats) {

  // TODO: use workload statistics here to find out how "hot" the tablet has
  // been in the last 5 minutes, and somehow scale the compaction quality
  // based on that, so we favor hot tablets.
  double quality = 0;
  unordered_set<RowSet*> picked_set_ignored;

  shared_ptr<RowSetTree> rowsets_copy;
  {
    boost::shared_lock<rw_spinlock> lock(component_lock_);
    rowsets_copy = components_->rowsets;
  }

  {
    boost::lock_guard<boost::mutex> compact_lock(compact_select_lock_);
    WARN_NOT_OK(compaction_policy_->PickRowSets(*rowsets_copy, &picked_set_ignored, &quality, NULL),
                Substitute("Couldn't determine compaction quality for $0", tablet_id()));
  }

  VLOG(1) << "Best compaction for " << tablet_id() << ": " << quality;

  stats->set_runnable(quality >= 0);
  stats->set_perf_improvement(quality);
}


Status Tablet::DebugDump(vector<string> *lines) {
  boost::shared_lock<rw_spinlock> lock(component_lock_);

  LOG_STRING(INFO, lines) << "Dumping tablet:";
  LOG_STRING(INFO, lines) << "---------------------------";

  LOG_STRING(INFO, lines) << "MRS " << components_->memrowset->ToString() << ":";
  RETURN_NOT_OK(components_->memrowset->DebugDump(lines));

  for (const shared_ptr<RowSet> &rs : components_->rowsets->all_rowsets()) {
    LOG_STRING(INFO, lines) << "RowSet " << rs->ToString() << ":";
    RETURN_NOT_OK(rs->DebugDump(lines));
  }

  return Status::OK();
}

Status Tablet::CaptureConsistentIterators(
  const Schema *projection,
  const MvccSnapshot &snap,
  const ScanSpec *spec,
  vector<shared_ptr<RowwiseIterator> > *iters) const {
  boost::shared_lock<rw_spinlock> lock(component_lock_);

  // Construct all the iterators locally first, so that if we fail
  // in the middle, we don't modify the output arguments.
  vector<shared_ptr<RowwiseIterator> > ret;

  // Grab the memrowset iterator.
  gscoped_ptr<RowwiseIterator> ms_iter;
  RETURN_NOT_OK(components_->memrowset->NewRowIterator(projection, snap, &ms_iter));
  ret.push_back(shared_ptr<RowwiseIterator>(ms_iter.release()));

  // Cull row-sets in the case of key-range queries.
  if (spec != nullptr && spec->lower_bound_key() && spec->exclusive_upper_bound_key()) {
    // TODO : support open-ended intervals
    // TODO: the upper bound key is exclusive, but the RowSetTree function takes
    // an inclusive interval. So, we might end up fetching one more rowset than
    // necessary.
    vector<RowSet *> interval_sets;
    components_->rowsets->FindRowSetsIntersectingInterval(
        spec->lower_bound_key()->encoded_key(),
        spec->exclusive_upper_bound_key()->encoded_key(),
        &interval_sets);
    for (const RowSet *rs : interval_sets) {
      gscoped_ptr<RowwiseIterator> row_it;
      RETURN_NOT_OK_PREPEND(rs->NewRowIterator(projection, snap, &row_it),
                            Substitute("Could not create iterator for rowset $0",
                                       rs->ToString()));
      ret.push_back(shared_ptr<RowwiseIterator>(row_it.release()));
    }
    ret.swap(*iters);
    return Status::OK();
  }

  // If there are no encoded predicates or they represent an open-ended range, then
  // fall back to grabbing all rowset iterators
  for (const shared_ptr<RowSet> &rs : components_->rowsets->all_rowsets()) {
    gscoped_ptr<RowwiseIterator> row_it;
    RETURN_NOT_OK_PREPEND(rs->NewRowIterator(projection, snap, &row_it),
                          Substitute("Could not create iterator for rowset $0",
                                     rs->ToString()));
    ret.push_back(shared_ptr<RowwiseIterator>(row_it.release()));
  }

  // Swap results into the parameters.
  ret.swap(*iters);
  return Status::OK();
}

Status Tablet::CountRows(uint64_t *count) const {
  // First grab a consistent view of the components of the tablet.
  scoped_refptr<TabletComponents> comps;
  GetComponents(&comps);

  // Now sum up the counts.
  *count = comps->memrowset->entry_count();
  for (const shared_ptr<RowSet> &rowset : comps->rowsets->all_rowsets()) {
    rowid_t l_count;
    RETURN_NOT_OK(rowset->CountRows(&l_count));
    *count += l_count;
  }

  return Status::OK();
}

size_t Tablet::MemRowSetSize() const {
  scoped_refptr<TabletComponents> comps;
  GetComponents(&comps);

  if (comps) {
    return comps->memrowset->memory_footprint();
  }
  return 0;
}

bool Tablet::MemRowSetEmpty() const {
  scoped_refptr<TabletComponents> comps;
  GetComponents(&comps);

  return comps->memrowset->empty();
}

size_t Tablet::MemRowSetLogRetentionSize(const MaxIdxToSegmentMap& max_idx_to_segment_size) const {
  scoped_refptr<TabletComponents> comps;
  GetComponents(&comps);

  return GetLogRetentionSizeForIndex(comps->memrowset->MinUnflushedLogIndex(),
                                     max_idx_to_segment_size);
}

size_t Tablet::EstimateOnDiskSize() const {
  scoped_refptr<TabletComponents> comps;
  GetComponents(&comps);

  if (!comps) return 0;

  size_t ret = 0;
  for (const shared_ptr<RowSet> &rowset : comps->rowsets->all_rowsets()) {
    ret += rowset->EstimateOnDiskSize();
  }

  return ret;
}

size_t Tablet::DeltaMemStoresSize() const {
  scoped_refptr<TabletComponents> comps;
  GetComponents(&comps);

  size_t ret = 0;
  for (const shared_ptr<RowSet> &rowset : comps->rowsets->all_rowsets()) {
    ret += rowset->DeltaMemStoreSize();
  }

  return ret;
}

bool Tablet::DeltaMemRowSetEmpty() const {
  scoped_refptr<TabletComponents> comps;
  GetComponents(&comps);

  for (const shared_ptr<RowSet> &rowset : comps->rowsets->all_rowsets()) {
    if (!rowset->DeltaMemStoreEmpty()) {
      return false;
    }
  }

  return true;
}

void Tablet::GetInfoForBestDMSToFlush(const MaxIdxToSegmentMap& max_idx_to_segment_size,
                                      int64_t* mem_size, int64_t* retention_size) const {
  shared_ptr<RowSet> rowset = FindBestDMSToFlush(max_idx_to_segment_size);

  if (rowset) {
    *retention_size = GetLogRetentionSizeForIndex(rowset->MinUnflushedLogIndex(),
                                            max_idx_to_segment_size);
    *mem_size = rowset->DeltaMemStoreSize();
  } else {
    *retention_size = 0;
    *mem_size = 0;
  }
}

Status Tablet::FlushDMSWithHighestRetention(const MaxIdxToSegmentMap&
                                            max_idx_to_segment_size) const {
  shared_ptr<RowSet> rowset = FindBestDMSToFlush(max_idx_to_segment_size);
  if (rowset) {
    return rowset->FlushDeltas();
  }
  return Status::OK();
}

shared_ptr<RowSet> Tablet::FindBestDMSToFlush(const MaxIdxToSegmentMap&
                                              max_idx_to_segment_size) const {
  scoped_refptr<TabletComponents> comps;
  GetComponents(&comps);
  int64_t mem_size = 0;
  int64_t retention_size = 0;
  shared_ptr<RowSet> best_dms;
  for (const shared_ptr<RowSet> &rowset : comps->rowsets->all_rowsets()) {
    if (rowset->DeltaMemStoreEmpty()) {
      continue;
    }
    int64_t size = GetLogRetentionSizeForIndex(rowset->MinUnflushedLogIndex(),
                                               max_idx_to_segment_size);
    if ((size > retention_size) ||
        (size == retention_size &&
         (rowset->DeltaMemStoreSize() > mem_size))) {
      mem_size = rowset->DeltaMemStoreSize();
      retention_size = size;
      best_dms = rowset;
    }
  }
  return best_dms;
}

int64_t Tablet::GetLogRetentionSizeForIndex(int64_t min_log_index,
                                            const MaxIdxToSegmentMap& max_idx_to_segment_size) {
  if (max_idx_to_segment_size.size() == 0 || min_log_index == -1) {
    return 0;
  }
  int64_t total_size = 0;
  for (const MaxIdxToSegmentMap::value_type& entry : max_idx_to_segment_size) {
    if (min_log_index > entry.first) {
      continue; // We're not in this segment, probably someone else is retaining it.
    }
    total_size += entry.second;
  }
  return total_size;
}

Status Tablet::FlushBiggestDMS() {
  CHECK_EQ(state_, kOpen);
  scoped_refptr<TabletComponents> comps;
  GetComponents(&comps);

  int64_t max_size = -1;
  shared_ptr<RowSet> biggest_drs;
  for (const shared_ptr<RowSet> &rowset : comps->rowsets->all_rowsets()) {
    int64_t current = rowset->DeltaMemStoreSize();
    if (current > max_size) {
      max_size = current;
      biggest_drs = rowset;
    }
  }
  return max_size > 0 ? biggest_drs->FlushDeltas() : Status::OK();
}

Status Tablet::CompactWorstDeltas(RowSet::DeltaCompactionType type) {
  CHECK_EQ(state_, kOpen);
  shared_ptr<RowSet> rs;
  // We're required to grab the rowset's compact_flush_lock under the compact_select_lock_.
  shared_ptr<boost::mutex::scoped_try_lock> lock;
  double perf_improv;
  {
    // We only want to keep the selection lock during the time we look at rowsets to compact.
    // The returned rowset is guaranteed to be available to lock since locking must be done
    // under this lock.
    boost::lock_guard<boost::mutex> compact_lock(compact_select_lock_);
    perf_improv = GetPerfImprovementForBestDeltaCompactUnlocked(type, &rs);
    if (rs) {
      lock.reset(new boost::mutex::scoped_try_lock(*rs->compact_flush_lock()));
      CHECK(lock->owns_lock());
    } else {
      return Status::OK();
    }
  }

  // We just released compact_select_lock_ so other compactions can select and run, but the
  // rowset is ours.
  DCHECK(perf_improv != 0);
  if (type == RowSet::MINOR_DELTA_COMPACTION) {
    RETURN_NOT_OK_PREPEND(rs->MinorCompactDeltaStores(),
                          "Failed minor delta compaction on " + rs->ToString());
  } else if (type == RowSet::MAJOR_DELTA_COMPACTION) {
    RETURN_NOT_OK_PREPEND(down_cast<DiskRowSet*>(rs.get())->MajorCompactDeltaStores(),
                          "Failed major delta compaction on " + rs->ToString());
  }
  return Status::OK();
}

double Tablet::GetPerfImprovementForBestDeltaCompact(RowSet::DeltaCompactionType type,
                                                             shared_ptr<RowSet>* rs) const {
  boost::lock_guard<boost::mutex> compact_lock(compact_select_lock_);
  return GetPerfImprovementForBestDeltaCompactUnlocked(type, rs);
}

double Tablet::GetPerfImprovementForBestDeltaCompactUnlocked(RowSet::DeltaCompactionType type,
                                                             shared_ptr<RowSet>* rs) const {
  boost::mutex::scoped_try_lock cs_lock(compact_select_lock_);
  DCHECK(!cs_lock.owns_lock());
  scoped_refptr<TabletComponents> comps;
  GetComponents(&comps);
  double worst_delta_perf = 0;
  shared_ptr<RowSet> worst_rs;
  for (const shared_ptr<RowSet> &rowset : comps->rowsets->all_rowsets()) {
    if (!rowset->IsAvailableForCompaction()) {
      continue;
    }
    double perf_improv = rowset->DeltaStoresCompactionPerfImprovementScore(type);
    if (perf_improv > worst_delta_perf) {
      worst_rs = rowset;
      worst_delta_perf = perf_improv;
    }
  }
  if (rs && worst_delta_perf > 0) {
    *rs = worst_rs;
  }
  return worst_delta_perf;
}

size_t Tablet::num_rowsets() const {
  boost::shared_lock<rw_spinlock> lock(component_lock_);
  return components_->rowsets->all_rowsets().size();
}

void Tablet::PrintRSLayout(ostream* o) {
  shared_ptr<RowSetTree> rowsets_copy;
  {
    boost::shared_lock<rw_spinlock> lock(component_lock_);
    rowsets_copy = components_->rowsets;
  }
  boost::lock_guard<boost::mutex> compact_lock(compact_select_lock_);
  // Run the compaction policy in order to get its log and highlight those
  // rowsets which would be compacted next.
  vector<string> log;
  unordered_set<RowSet*> picked;
  double quality;
  Status s = compaction_policy_->PickRowSets(*rowsets_copy, &picked, &quality, &log);
  if (!s.ok()) {
    *o << "<b>Error:</b> " << EscapeForHtmlToString(s.ToString());
    return;
  }

  if (!picked.empty()) {
    *o << "<p>";
    *o << "Highlighted rowsets indicate those that would be compacted next if a "
       << "compaction were to run on this tablet.";
    *o << "</p>";
  }

  vector<RowSetInfo> min, max;
  RowSetInfo::CollectOrdered(*rowsets_copy, &min, &max);
  DumpCompactionSVG(min, picked, o, false);

  *o << "<h2>Compaction policy log</h2>" << std::endl;

  *o << "<pre>" << std::endl;
  for (const string& s : log) {
    *o << EscapeForHtmlToString(s) << std::endl;
  }
  *o << "</pre>" << std::endl;
}

////////////////////////////////////////////////////////////
// Tablet::Iterator
////////////////////////////////////////////////////////////

Tablet::Iterator::Iterator(const Tablet* tablet, const Schema& projection,
                           MvccSnapshot snap, const OrderMode order)
    : tablet_(tablet),
      projection_(projection),
      snap_(std::move(snap)),
      order_(order),
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
      &projection_, snap_, spec, &iters));

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

} // namespace tablet
} // namespace kudu
