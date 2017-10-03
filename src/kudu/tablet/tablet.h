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
#ifndef KUDU_TABLET_TABLET_H
#define KUDU_TABLET_TABLET_H

#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <boost/thread/shared_mutex.hpp>

#include "kudu/common/iterator.h"
#include "kudu/common/predicate_encoder.h"
#include "kudu/common/schema.h"
#include "kudu/gutil/atomicops.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/tablet/rowset_metadata.h"
#include "kudu/tablet/tablet_metadata.h"
#include "kudu/tablet/lock_manager.h"
#include "kudu/tablet/mvcc.h"
#include "kudu/tablet/rowset.h"
#include "kudu/util/locks.h"
#include "kudu/util/metrics.h"
#include "kudu/util/semaphore.h"
#include "kudu/util/slice.h"
#include "kudu/util/status.h"

namespace kudu {

class MemTracker;
class MetricEntity;
class RowChangeList;
class UnionIterator;

namespace log {
class LogAnchorRegistry;
}

namespace server {
class Clock;
}

class MaintenanceManager;
class MaintenanceOp;
class MaintenanceOpStats;

namespace tablet {

class AlterSchemaTransactionState;
class CompactionPolicy;
class MemRowSet;
class MvccSnapshot;
struct RowOp;
class RowSetsInCompaction;
class RowSetTree;
struct TabletComponents;
struct TabletMetrics;
class WriteTransactionState;

class Tablet {
 public:
  typedef std::map<int64_t, int64_t> MaxIdxToSegmentMap;
  friend class CompactRowSetsOp;
  friend class FlushMRSOp;

  class CompactionFaultHooks;
  class FlushCompactCommonHooks;
  class FlushFaultHooks;
  class Iterator;

  // Create a new tablet.
  //
  // If 'metric_registry' is non-NULL, then this tablet will create a 'tablet' entity
  // within the provided registry. Otherwise, no metrics are collected.
  Tablet(const scoped_refptr<TabletMetadata>& metadata,
         const scoped_refptr<server::Clock>& clock,
         const std::shared_ptr<MemTracker>& parent_mem_tracker,
         MetricRegistry* metric_registry,
         const scoped_refptr<log::LogAnchorRegistry>& log_anchor_registry);

  ~Tablet();

  // Open the tablet.
  // Upon completion, the tablet enters the kBootstrapping state.
  Status Open();

  // Mark that the tablet has finished bootstrapping.
  // This transitions from kBootstrapping to kOpen state.
  void MarkFinishedBootstrapping();

  void Shutdown();

  // Decode the Write (insert/mutate) operations from within a user's
  // request.
  Status DecodeWriteOperations(const Schema* client_schema,
                               WriteTransactionState* tx_state);

  // Acquire locks for each of the operations in the given txn.
  //
  // Note that, if this fails, it's still possible that the transaction
  // state holds _some_ of the locks. In that case, we expect that
  // the transaction will still clean them up when it is aborted (or
  // otherwise destructed).
  Status AcquireRowLocks(WriteTransactionState* tx_state);

  // Finish the Prepare phase of a write transaction.
  //
  // Starts an MVCC transaction and assigns a timestamp for the transaction.
  // This also snapshots the current set of tablet components into the transaction
  // state.
  //
  // This should always be done _after_ any relevant row locks are acquired
  // (using CreatePreparedInsert/CreatePreparedMutate). This ensures that,
  // within each row, timestamps only move forward. If we took a timestamp before
  // getting the row lock, we could have the following situation:
  //
  //   Thread 1         |  Thread 2
  //   ----------------------
  //   Start tx 1       |
  //                    |  Start tx 2
  //                    |  Obtain row lock
  //                    |  Update row
  //                    |  Commit tx 2
  //   Obtain row lock  |
  //   Delete row       |
  //   Commit tx 1
  //
  // This would cause the mutation list to look like: @t1: DELETE, @t2: UPDATE
  // which is invalid, since we expect to be able to be able to replay mutations
  // in increasing timestamp order on a given row.
  //
  // This requirement is basically two-phase-locking: the order in which row locks
  // are acquired for transactions determines their serialization order. If/when
  // we support multi-node serializable transactions, we'll have to acquire _all_
  // row locks (across all nodes) before obtaining a timestamp.
  //
  // TODO: rename this to something like "FinishPrepare" or "StartApply", since
  // it's not the first thing in a transaction!
  void StartTransaction(WriteTransactionState* tx_state);

  // Insert a new row into the tablet.
  //
  // The provided 'data' slice should have length equivalent to this
  // tablet's Schema.byte_size().
  //
  // After insert, the row and any referred-to memory (eg for strings)
  // have been copied into internal memory, and thus the provided memory
  // buffer may safely be re-used or freed.
  //
  // Returns Status::AlreadyPresent() if an entry with the same key is already
  // present in the tablet.
  // Returns Status::OK unless allocation fails.
  //
  // Acquires the row lock for the given operation, setting it in the
  // RowOp struct. This also sets the row op's RowSetKeyProbe.
  Status AcquireLockForOp(WriteTransactionState* tx_state,
                          RowOp* op);

  // Signal that the given transaction is about to Apply.
  void StartApplying(WriteTransactionState* tx_state);

  // Apply all of the row operations associated with this transaction.
  void ApplyRowOperations(WriteTransactionState* tx_state);

  // Apply a single row operation, which must already be prepared.
  // The result is set back into row_op->result
  void ApplyRowOperation(WriteTransactionState* tx_state,
                         RowOp* row_op);

  // Create a new row iterator which yields the rows as of the current MVCC
  // state of this tablet.
  // The returned iterator is not initialized.
  Status NewRowIterator(const Schema &projection,
                        gscoped_ptr<RowwiseIterator> *iter) const;

  // Whether the iterator should return results in order.
  enum OrderMode {
    UNORDERED = 0,
    ORDERED = 1
  };

  // Create a new row iterator for some historical snapshot.
  Status NewRowIterator(const Schema &projection,
                        const MvccSnapshot &snap,
                        const OrderMode order,
                        gscoped_ptr<RowwiseIterator> *iter) const;

  // Flush the current MemRowSet for this tablet to disk. This swaps
  // in a new (initially empty) MemRowSet in its place.
  //
  // This doesn't flush any DeltaMemStores for any existing RowSets.
  // To do that, call FlushBiggestDMS() for example.
  Status Flush();

  // Prepares the transaction context for the alter schema operation.
  // An error will be returned if the specified schema is invalid (e.g.
  // key mismatch, or missing IDs)
  Status CreatePreparedAlterSchema(AlterSchemaTransactionState *tx_state,
                                   const Schema* schema);

  // Apply the Schema of the specified transaction.
  // This operation will trigger a flush on the current MemRowSet.
  Status AlterSchema(AlterSchemaTransactionState* tx_state);

  // Rewind the schema to an earlier version than is written in the on-disk
  // metadata. This is done during bootstrap to roll the schema back to the
  // point in time where the logs-to-be-replayed begin, so we can then decode
  // the operations in the log with the correct schema.
  //
  // REQUIRES: state_ == kBootstrapping
  Status RewindSchemaForBootstrap(const Schema& schema,
                                  int64_t schema_version);

  // Prints current RowSet layout, taking a snapshot of the current RowSet interval
  // tree. Also prints the log of the compaction algorithm as evaluated
  // on the current layout.
  void PrintRSLayout(std::ostream* o);

  // Flags to change the behavior of compaction.
  enum CompactFlag {
    COMPACT_NO_FLAGS = 0,

    // Force the compaction to include all rowsets, regardless of the
    // configured compaction policy. This is currently only used in
    // tests.
    FORCE_COMPACT_ALL = 1 << 0
  };
  typedef int CompactFlags;

  Status Compact(CompactFlags flags);

  // Update the statistics for performing a compaction.
  void UpdateCompactionStats(MaintenanceOpStats* stats);

  // Returns the exact current size of the MRS, in bytes. A value greater than 0 doesn't imply
  // that the MRS has data, only that it has allocated that amount of memory.
  // This method takes a read lock on component_lock_ and is thread-safe.
  size_t MemRowSetSize() const;

  // Returns true if the MRS is empty, else false. Doesn't rely on size and
  // actually verifies that the MRS has no elements.
  // This method takes a read lock on component_lock_ and is thread-safe.
  bool MemRowSetEmpty() const;

  // Returns the size in bytes for the MRS's log retention.
  size_t MemRowSetLogRetentionSize(const MaxIdxToSegmentMap& max_idx_to_segment_size) const;

  // Estimate the total on-disk size of this tablet, in bytes.
  size_t EstimateOnDiskSize() const;

  // Get the total size of all the DMS
  size_t DeltaMemStoresSize() const;

  // Same as MemRowSetEmpty(), but for the DMS.
  bool DeltaMemRowSetEmpty() const;

  // Fills in the in-memory size and retention size in bytes for the DMS with the
  // highest retention.
  void GetInfoForBestDMSToFlush(const MaxIdxToSegmentMap& max_idx_to_segment_size,
                                int64_t* mem_size, int64_t* retention_size) const;

  // Flushes the DMS with the highest retention.
  Status FlushDMSWithHighestRetention(const MaxIdxToSegmentMap& max_idx_to_segment_size) const;

  // Flush only the biggest DMS
  Status FlushBiggestDMS();

  // Finds the RowSet which has the most separate delta files and
  // issues a minor delta compaction.
  Status CompactWorstDeltas(RowSet::DeltaCompactionType type);

  // Get the highest performance improvement that would come from compacting the delta stores
  // of one of the rowsets. If the returned performance improvement is 0, or if 'rs' is NULL,
  // then 'rs' isn't set. Callers who already own compact_select_lock_
  // can call GetPerfImprovementForBestDeltaCompactUnlocked().
  double GetPerfImprovementForBestDeltaCompact(RowSet::DeltaCompactionType type,
                                               std::shared_ptr<RowSet>* rs) const;

  // Same as GetPerfImprovementForBestDeltaCompact(), but doesn't take a lock on
  // compact_select_lock_.
  double GetPerfImprovementForBestDeltaCompactUnlocked(RowSet::DeltaCompactionType type,
                                                       std::shared_ptr<RowSet>* rs) const;

  // Return the current number of rowsets in the tablet.
  size_t num_rowsets() const;

  // Attempt to count the total number of rows in the tablet.
  // This is not super-efficient since it must iterate over the
  // memrowset in the current implementation.
  Status CountRows(uint64_t *count) const;


  // Verbosely dump this entire tablet to the logs. This is only
  // really useful when debugging unit tests failures where the tablet
  // has a very small number of rows.
  Status DebugDump(vector<std::string> *lines = NULL);

  const Schema* schema() const {
    return &metadata_->schema();
  }

  // Returns a reference to the key projection of the tablet schema.
  // The schema keys are immutable.
  const Schema& key_schema() const { return key_schema_; }

  // Return the MVCC manager for this tablet.
  MvccManager* mvcc_manager() { return &mvcc_; }

  // Return the Lock Manager for this tablet
  LockManager* lock_manager() { return &lock_manager_; }

  const TabletMetadata *metadata() const { return metadata_.get(); }
  TabletMetadata *metadata() { return metadata_.get(); }

  void SetCompactionHooksForTests(const std::shared_ptr<CompactionFaultHooks> &hooks);
  void SetFlushHooksForTests(const std::shared_ptr<FlushFaultHooks> &hooks);
  void SetFlushCompactCommonHooksForTests(
      const std::shared_ptr<FlushCompactCommonHooks> &hooks);

  // Returns the current MemRowSet id, for tests.
  // This method takes a read lock on component_lock_ and is thread-safe.
  int32_t CurrentMrsIdForTests() const;

  // Runs a major delta major compaction on columns with specified IDs.
  // NOTE: RowSet must presently be a DiskRowSet. (Perhaps the API should be
  // a shared_ptr API for now?)
  //
  // TODO: Handle MVCC to support MemRowSet and handle deltas in DeltaMemStore
  Status DoMajorDeltaCompaction(const std::vector<ColumnId>& column_ids,
                                std::shared_ptr<RowSet> input_rowset);

  // Method used by tests to retrieve all rowsets of this table. This
  // will be removed once code for selecting the appropriate RowSet is
  // finished and delta files is finished is part of Tablet class.
  void GetRowSetsForTests(vector<std::shared_ptr<RowSet> >* out);

  // Register the maintenance ops associated with this tablet
  void RegisterMaintenanceOps(MaintenanceManager* maintenance_manager);

  // Unregister the maintenance ops associated with this tablet.
  // This method is not thread safe.
  void UnregisterMaintenanceOps();

  const std::string& tablet_id() const { return metadata_->tablet_id(); }

  // Return the metrics for this tablet.
  // May be NULL in unit tests, etc.
  TabletMetrics* metrics() { return metrics_.get(); }

  // Return handle to the metric entity of this tablet.
  const scoped_refptr<MetricEntity>& GetMetricEntity() const { return metric_entity_; }

  // Returns a reference to this tablet's memory tracker.
  const std::shared_ptr<MemTracker>& mem_tracker() const { return mem_tracker_; }

  static const char* kDMSMemTrackerId;
 private:
  friend class Iterator;
  friend class TabletPeerTest;
  FRIEND_TEST(TestTablet, TestGetLogRetentionSizeForIndex);

  Status FlushUnlocked();

  // A version of Insert that does not acquire locks and instead assumes that
  // they were already acquired. Requires that handles for the relevant locks
  // and MVCC transaction are present in the transaction state.
  Status InsertUnlocked(WriteTransactionState *tx_state,
                        RowOp* insert);

  // A version of MutateRow that does not acquire locks and instead assumes
  // they were already acquired. Requires that handles for the relevant locks
  // and MVCC transaction are present in the transaction state.
  Status MutateRowUnlocked(WriteTransactionState *tx_state,
                           RowOp* mutate);

  // Capture a set of iterators which, together, reflect all of the data in the tablet.
  //
  // These iterators are not true snapshot iterators, but they are safe against
  // concurrent modification. They will include all data that was present at the time
  // of creation, and potentially newer data.
  //
  // The returned iterators are not Init()ed.
  // 'projection' must remain valid and unchanged for the lifetime of the returned iterators.
  Status CaptureConsistentIterators(const Schema *projection,
                                    const MvccSnapshot &snap,
                                    const ScanSpec *spec,
                                    vector<std::shared_ptr<RowwiseIterator> > *iters) const;

  Status PickRowSetsToCompact(RowSetsInCompaction *picked,
                              CompactFlags flags) const;

  Status DoCompactionOrFlush(const RowSetsInCompaction &input,
                             int64_t mrs_being_flushed);

  Status FlushMetadata(const RowSetVector& to_remove,
                       const RowSetMetadataVector& to_add,
                       int64_t mrs_being_flushed);

  static void ModifyRowSetTree(const RowSetTree& old_tree,
                               const RowSetVector& rowsets_to_remove,
                               const RowSetVector& rowsets_to_add,
                               RowSetTree* new_tree);

  // Swap out a set of rowsets, atomically replacing them with the new rowset
  // under the lock.
  void AtomicSwapRowSets(const RowSetVector &to_remove,
                         const RowSetVector &to_add);

  // Same as the above, but without taking the lock. This should only be used
  // in cases where the lock is already held.
  void AtomicSwapRowSetsUnlocked(const RowSetVector &to_remove,
                                 const RowSetVector &to_add);

  void GetComponents(scoped_refptr<TabletComponents>* comps) const {
    boost::shared_lock<rw_spinlock> lock(component_lock_);
    *comps = components_;
  }

  // Create a new MemRowSet, replacing the current one.
  // The 'old_ms' pointer will be set to the current MemRowSet set before the replacement.
  // If the MemRowSet is not empty it will be added to the 'compaction' input
  // and the MemRowSet compaction lock will be taken to prevent the inclusion
  // in any concurrent compactions.
  Status ReplaceMemRowSetUnlocked(RowSetsInCompaction *compaction,
                                  std::shared_ptr<MemRowSet> *old_ms);

  // TODO: Document me.
  Status FlushInternal(const RowSetsInCompaction& input,
                       const std::shared_ptr<MemRowSet>& old_ms);

  BloomFilterSizing bloom_sizing() const;

  // Convert the specified read client schema (without IDs) to a server schema (with IDs)
  // This method is used by NewRowIterator().
  Status GetMappedReadProjection(const Schema& projection,
                                 Schema *mapped_projection) const;

  Status CheckRowInTablet(const ConstContiguousRow& probe) const;

  // Helper method to find the rowset that has the DMS with the highest retention.
  std::shared_ptr<RowSet> FindBestDMSToFlush(
      const MaxIdxToSegmentMap& max_idx_to_segment_size) const;

  // Helper method to find how many bytes this index retains.
  static int64_t GetLogRetentionSizeForIndex(int64_t min_log_index,
                                             const MaxIdxToSegmentMap& max_idx_to_segment_size);

  // Lock protecting schema_ and key_schema_.
  //
  // Writers take this lock in shared mode before decoding and projecting
  // their requests. They hold the lock until after APPLY.
  //
  // Readers take this lock in shared mode only long enough to copy the
  // current schema into the iterator, after which all projection is taken
  // care of based on that copy.
  //
  // On an AlterSchema, this is taken in exclusive mode during Prepare() and
  // released after the schema change has been applied.
  mutable rw_semaphore schema_lock_;

  const Schema key_schema_;

  scoped_refptr<TabletMetadata> metadata_;

  // Lock protecting access to the 'components_' member (i.e the rowsets in the tablet)
  //
  // Shared mode:
  // - Writers take this in shared mode at the same time as they obtain an MVCC timestamp
  //   and capture a reference to components_. This ensures that we can use the MVCC timestamp
  //   to determine which writers are writing to which components during compaction.
  // - Readers take this in shared mode while capturing their iterators. This ensures that
  //   they see a consistent view when racing against flush/compact.
  //
  // Exclusive mode:
  // - Flushes/compactions take this lock in order to lock out concurrent updates when
  //   swapping in a new memrowset.
  //
  // NOTE: callers should avoid taking this lock for a long time, even in shared mode.
  // This is because the lock has some concept of fairness -- if, while a long reader
  // is active, a writer comes along, then all future short readers will be blocked.
  // TODO: now that this is single-threaded again, we should change it to rw_spinlock
  mutable rw_spinlock component_lock_;

  // The current components of the tablet. These should always be read
  // or swapped under the component_lock.
  scoped_refptr<TabletComponents> components_;

  scoped_refptr<log::LogAnchorRegistry> log_anchor_registry_;
  std::shared_ptr<MemTracker> mem_tracker_;
  std::shared_ptr<MemTracker> dms_mem_tracker_;

  scoped_refptr<MetricEntity> metric_entity_;
  gscoped_ptr<TabletMetrics> metrics_;
  FunctionGaugeDetacher metric_detacher_;

  int64_t next_mrs_id_;

  // A pointer to the server's clock.
  scoped_refptr<server::Clock> clock_;

  MvccManager mvcc_;
  LockManager lock_manager_;

  gscoped_ptr<CompactionPolicy> compaction_policy_;


  // Lock protecting the selection of rowsets for compaction.
  // Only one thread may run the compaction selection algorithm at a time
  // so that they don't both try to select the same rowset.
  mutable boost::mutex compact_select_lock_;

  // We take this lock when flushing the tablet's rowsets in Tablet::Flush.  We
  // don't want to have two flushes in progress at once, in case the one which
  // started earlier completes after the one started later.
  mutable Semaphore rowsets_flush_sem_;

  enum State {
    kInitialized,
    kBootstrapping,
    kOpen,
    kShutdown
  };
  State state_;

  // Fault hooks. In production code, these will always be NULL.
  std::shared_ptr<CompactionFaultHooks> compaction_hooks_;
  std::shared_ptr<FlushFaultHooks> flush_hooks_;
  std::shared_ptr<FlushCompactCommonHooks> common_hooks_;

  std::vector<MaintenanceOp*> maintenance_ops_;

  DISALLOW_COPY_AND_ASSIGN(Tablet);
};


// Hooks used in test code to inject faults or other code into interesting
// parts of the compaction code.
class Tablet::CompactionFaultHooks {
 public:
  virtual Status PostSelectIterators() { return Status::OK(); }
  virtual ~CompactionFaultHooks() {}
};

class Tablet::FlushCompactCommonHooks {
 public:
  virtual Status PostTakeMvccSnapshot() { return Status::OK(); }
  virtual Status PostWriteSnapshot() { return Status::OK(); }
  virtual Status PostSwapInDuplicatingRowSet() { return Status::OK(); }
  virtual Status PostReupdateMissedDeltas() { return Status::OK(); }
  virtual Status PostSwapNewRowSet() { return Status::OK(); }
  virtual ~FlushCompactCommonHooks() {}
};

// Hooks used in test code to inject faults or other code into interesting
// parts of the Flush() code.
class Tablet::FlushFaultHooks {
 public:
  virtual Status PostSwapNewMemRowSet() { return Status::OK(); }
  virtual ~FlushFaultHooks() {}
};

class Tablet::Iterator : public RowwiseIterator {
 public:
  virtual ~Iterator();

  virtual Status Init(ScanSpec *spec) OVERRIDE;

  virtual bool HasNext() const OVERRIDE;

  virtual Status NextBlock(RowBlock *dst) OVERRIDE;

  std::string ToString() const OVERRIDE;

  const Schema &schema() const OVERRIDE {
    return projection_;
  }

  virtual void GetIteratorStats(std::vector<IteratorStats>* stats) const OVERRIDE;

 private:
  friend class Tablet;

  DISALLOW_COPY_AND_ASSIGN(Iterator);

  Iterator(const Tablet* tablet, const Schema& projection, MvccSnapshot snap,
           const OrderMode order);

  const Tablet *tablet_;
  Schema projection_;
  const MvccSnapshot snap_;
  const OrderMode order_;
  gscoped_ptr<RowwiseIterator> iter_;

  // TODO: we could probably share an arena with the Scanner object inside the
  // tserver, but piping it in would require changing a lot of call-sites.
  Arena arena_;
  RangePredicateEncoder encoder_;
};

// Structure which represents the components of the tablet's storage.
// This structure is immutable -- a transaction can grab it and be sure
// that it won't change.
struct TabletComponents : public RefCountedThreadSafe<TabletComponents> {
  TabletComponents(std::shared_ptr<MemRowSet> mrs,
                   std::shared_ptr<RowSetTree> rs_tree);
  const std::shared_ptr<MemRowSet> memrowset;
  const std::shared_ptr<RowSetTree> rowsets;
};

} // namespace tablet
} // namespace kudu

#endif
