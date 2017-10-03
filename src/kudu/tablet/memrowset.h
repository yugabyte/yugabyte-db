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
#ifndef KUDU_TABLET_MEMROWSET_H
#define KUDU_TABLET_MEMROWSET_H

#include <boost/optional.hpp>
#include <memory>
#include <string>
#include <vector>

#include "kudu/common/scan_spec.h"
#include "kudu/common/rowblock.h"
#include "kudu/common/schema.h"
#include "kudu/consensus/log_anchor_registry.h"
#include "kudu/tablet/concurrent_btree.h"
#include "kudu/tablet/mutation.h"
#include "kudu/tablet/rowset.h"
#include "kudu/tablet/tablet.pb.h"
#include "kudu/util/memory/arena.h"
#include "kudu/util/memory/memory.h"
#include "kudu/util/status.h"

namespace kudu {

class MemTracker;

namespace tablet {

//
// Implementation notes:
// --------------------------
// The MemRowSet is a concurrent b-tree which stores newly inserted data which
// has not yet been flushed to on-disk rowsets. In order to provide snapshot
// consistency, data is never updated in-place in the memrowset after insertion.
// Rather, a chain of mutations hangs off each row, acting as a per-row "redo log".
//
// Each row is stored in exactly one CBTree entry. Its key is the encoded form
// of the row's primary key, such that the entries sort correctly using the default
// lexicographic comparator. The value for each row is an instance of MRSRow.
//
// NOTE: all allocations done by the MemRowSet are done inside its associated
// thread-safe arena, and then freed in bulk when the MemRowSet is destructed.

class MemRowSet;

// The value stored in the CBTree for a single row.
class MRSRow {
 public:
  typedef ContiguousRowCell<MRSRow> Cell;

  MRSRow(const MemRowSet *memrowset, const Slice &s) {
    DCHECK_GE(s.size(), sizeof(Header));
    row_slice_ = s;
    header_ = reinterpret_cast<Header *>(row_slice_.mutable_data());
    row_slice_.remove_prefix(sizeof(Header));
    memrowset_ = memrowset;
  }

  const Schema* schema() const;

  Timestamp insertion_timestamp() const { return header_->insertion_timestamp; }

  Mutation* redo_head() { return header_->redo_head; }
  const Mutation* redo_head() const { return header_->redo_head; }

  const Slice &row_slice() const { return row_slice_; }

  const uint8_t* row_data() const { return row_slice_.data(); }

  bool is_null(size_t col_idx) const {
    return ContiguousRowHelper::is_null(*schema(), row_slice_.data(), col_idx);
  }

  void set_null(size_t col_idx, bool is_null) const {
    ContiguousRowHelper::SetCellIsNull(*schema(),
      const_cast<uint8_t*>(row_slice_.data()), col_idx, is_null);
  }

  const uint8_t *cell_ptr(size_t col_idx) const {
    return ContiguousRowHelper::cell_ptr(*schema(), row_slice_.data(), col_idx);
  }

  uint8_t *mutable_cell_ptr(size_t col_idx) const {
    return const_cast<uint8_t*>(cell_ptr(col_idx));
  }

  const uint8_t *nullable_cell_ptr(size_t col_idx) const {
    return ContiguousRowHelper::nullable_cell_ptr(*schema(), row_slice_.data(), col_idx);
  }

  Cell cell(size_t col_idx) const {
    return Cell(this, col_idx);
  }

  // Return true if this row is a "ghost" -- i.e its most recent mutation is
  // a deletion.
  //
  // NOTE: this call is O(n) in the number of mutations, since it has to walk
  // the linked list all the way to the end, checking if each mutation is a
  // DELETE or REINSERT. We expect the list is usually short (low-update use
  // cases) but if this becomes a bottleneck, we could cache the 'ghost' status
  // as a bit inside the row header.
  bool IsGhost() const;

 private:
  friend class MemRowSet;

  template <class ArenaType>
  Status CopyRow(const ConstContiguousRow& row, ArenaType *arena) {
    // the representation of the MRSRow and ConstContiguousRow is the same.
    // so, instead of using CopyRow we can just do a memcpy.
    memcpy(row_slice_.mutable_data(), row.row_data(), row_slice_.size());
    // Copy any referred-to memory to arena.
    return kudu::RelocateIndirectDataToArena(this, arena);
  }

  struct Header {
    // Timestamp for the transaction which inserted this row. If a scanner with an
    // older snapshot sees this row, it will be ignored.
    Timestamp insertion_timestamp;

    // Pointer to the first mutation which has been applied to this row. Each
    // mutation is an instance of the Mutation class, making up a singly-linked
    // list for any mutations applied to the row.
    Mutation* redo_head;
  };

  Header *header_;

  // Actual row data.
  Slice row_slice_;

  const MemRowSet *memrowset_;
};

struct MSBTreeTraits : public btree::BTreeTraits {
  typedef ThreadSafeMemoryTrackingArena ArenaType;
};

// Define an MRSRow instance using on-stack storage.
// This defines an array on the stack which is sized correctly for an MRSRow::Header
// plus a single row of the given schema, then constructs an MRSRow object which
// points into that stack storage.
#define DEFINE_MRSROW_ON_STACK(memrowset, varname, slice_name) \
  size_t varname##_size = sizeof(MRSRow::Header) + \
                           ContiguousRowHelper::row_size((memrowset)->schema_nonvirtual()); \
  uint8_t varname##_storage[varname##_size]; \
  Slice slice_name(varname##_storage, varname##_size); \
  ContiguousRowHelper::InitNullsBitmap((memrowset)->schema_nonvirtual(), slice_name); \
  MRSRow varname(memrowset, slice_name);


// In-memory storage for data currently being written to the tablet.
// This is a holding area for inserts, currently held in row form
// (i.e not columnar)
//
// The data is kept sorted.
class MemRowSet : public RowSet,
                  public std::enable_shared_from_this<MemRowSet> {
 public:
  class Iterator;

  MemRowSet(int64_t id,
            const Schema &schema,
            log::LogAnchorRegistry* log_anchor_registry,
            const std::shared_ptr<MemTracker>& parent_tracker =
            std::shared_ptr<MemTracker>());

  ~MemRowSet();

  // Insert a new row into the memrowset.
  //
  // The provided 'row' must have the same memrowset's Schema.
  // (TODO: Different schema are not yet supported)
  //
  // After insert, the row and any referred-to memory (eg for strings)
  // have been copied into this MemRowSet's internal storage, and thus
  // the provided memory buffer may safely be re-used or freed.
  //
  // Returns Status::OK unless allocation fails.
  Status Insert(Timestamp timestamp,
                const ConstContiguousRow& row,
                const consensus::OpId& op_id);


  // Update or delete an existing row in the memrowset.
  //
  // Returns Status::NotFound if the row doesn't exist.
  virtual Status MutateRow(Timestamp timestamp,
                           const RowSetKeyProbe &probe,
                           const RowChangeList &delta,
                           const consensus::OpId& op_id,
                           ProbeStats* stats,
                           OperationResultPB *result) OVERRIDE;

  // Return the number of entries in the memrowset.
  // NOTE: this requires iterating all data, and is thus
  // not very fast.
  uint64_t entry_count() const {
    return tree_.count();
  }

  // Conform entry_count to RowSet
  Status CountRows(rowid_t *count) const OVERRIDE {
    *count = entry_count();
    return Status::OK();
  }

  virtual Status GetBounds(Slice *min_encoded_key,
                           Slice *max_encoded_key) const OVERRIDE;

  uint64_t EstimateOnDiskSize() const OVERRIDE {
    return 0;
  }

  boost::mutex *compact_flush_lock() OVERRIDE {
    return &compact_flush_lock_;
  }

  // MemRowSets are never available for compaction, currently.
  virtual bool IsAvailableForCompaction() OVERRIDE {
    return false;
  }

  // Return true if there are no entries in the memrowset.
  bool empty() const {
    return tree_.empty();
  }

  // TODO: unit test me
  Status CheckRowPresent(const RowSetKeyProbe &probe, bool *present,
                         ProbeStats* stats) const OVERRIDE;

  // Return the memory footprint of this memrowset.
  // Note that this may be larger than the sum of the data
  // inserted into the memrowset, due to arena and data structure
  // overhead.
  size_t memory_footprint() const {
    return arena_->memory_footprint();
  }

  // Return an iterator over the items in this memrowset.
  //
  // NOTE: for this function to work, there must be a shared_ptr
  // referring to this MemRowSet. Otherwise, this will throw
  // a C++ exception and all bets are off.
  //
  // TODO: clarify the consistency of this iterator in the method doc
  Iterator *NewIterator() const;
  Iterator *NewIterator(const Schema *projection,
                        const MvccSnapshot &snap) const;

  // Alias to conform to DiskRowSet interface
  virtual Status NewRowIterator(const Schema* projection,
                                const MvccSnapshot& snap,
                                gscoped_ptr<RowwiseIterator>* out) const OVERRIDE;

  // Create compaction input.
  virtual Status NewCompactionInput(const Schema* projection,
                                    const MvccSnapshot& snap,
                                    gscoped_ptr<CompactionInput>* out) const OVERRIDE;

  // Return the Schema for the rows in this memrowset.
   const Schema &schema() const {
    return schema_;
  }

  // Same as schema(), but non-virtual method
  const Schema& schema_nonvirtual() const {
    return schema_;
  }

  int64_t mrs_id() const {
    return id_;
  }

  std::shared_ptr<RowSetMetadata> metadata() OVERRIDE {
    return std::shared_ptr<RowSetMetadata>(
        reinterpret_cast<RowSetMetadata *>(NULL));
  }

  // Dump the contents of the memrowset to the given vector.
  // If 'lines' is NULL, dumps to LOG(INFO).
  //
  // This dumps every row, so should only be used in tests, etc.
  virtual Status DebugDump(vector<string> *lines = NULL) OVERRIDE;

  string ToString() const OVERRIDE {
    return string("memrowset");
  }

  // Mark the memrowset as frozen. See CBTree::Freeze()
  void Freeze() {
    tree_.Freeze();
  }

  uint64_t debug_insert_count() const {
    return debug_insert_count_;
  }
  uint64_t debug_update_count() const {
    return debug_update_count_;
  }

  size_t DeltaMemStoreSize() const OVERRIDE { return 0; }

  bool DeltaMemStoreEmpty() const OVERRIDE { return true; }

  int64_t MinUnflushedLogIndex() const OVERRIDE {
    return anchorer_.minimum_log_index();
  }

  double DeltaStoresCompactionPerfImprovementScore(DeltaCompactionType type) const OVERRIDE {
    return 0;
  }

  Status FlushDeltas() OVERRIDE { return Status::OK(); }

  Status MinorCompactDeltaStores() OVERRIDE { return Status::OK(); }

 private:
  friend class Iterator;

  // Perform a "Reinsert" -- handle an insertion into a row which was previously
  // inserted and deleted, but still has an entry in the MemRowSet.
  Status Reinsert(Timestamp timestamp,
                  const ConstContiguousRow& row_data,
                  MRSRow *row);

  typedef btree::CBTree<MSBTreeTraits> MSBTree;

  int64_t id_;

  const Schema schema_;
  std::shared_ptr<MemTracker> parent_tracker_;
  std::shared_ptr<MemTracker> mem_tracker_;
  std::shared_ptr<MemoryTrackingBufferAllocator> allocator_;
  std::shared_ptr<ThreadSafeMemoryTrackingArena> arena_;

  typedef btree::CBTreeIterator<MSBTreeTraits> MSBTIter;

  MSBTree tree_;

  // Approximate counts of mutations. This variable is updated non-atomically,
  // so it cannot be relied upon to be in any way accurate. It's only used
  // as a sanity check during flush.
  volatile uint64_t debug_insert_count_;
  volatile uint64_t debug_update_count_;

  boost::mutex compact_flush_lock_;

  Atomic32 has_logged_throttling_;

  log::MinLogIndexAnchorer anchorer_;

  DISALLOW_COPY_AND_ASSIGN(MemRowSet);
};

// An iterator through in-memory data stored in a MemRowSet.
// This holds a reference to the MemRowSet, and so the memrowset
// must not be freed while this iterator is outstanding.
//
// This iterator is not a full snapshot, but individual rows
// are consistent, and it is safe to iterate during concurrent
// mutation. The consistency guarantee is that it will return
// at least all rows that were present at the time of construction,
// and potentially more. Each row will be at least as current as
// the time of construction, and potentially more current.
class MemRowSet::Iterator : public RowwiseIterator {
 public:
  class MRSRowProjector;

  virtual ~Iterator();

  virtual Status Init(ScanSpec *spec) OVERRIDE;

  Status SeekAtOrAfter(const Slice &key, bool *exact);

  virtual Status NextBlock(RowBlock *dst) OVERRIDE;

  bool has_upper_bound() const {
    return exclusive_upper_bound_.is_initialized();
  }

  bool out_of_bounds(const Slice &key) const {
    DCHECK(has_upper_bound()) << "No upper bound set!";

    return key.compare(*exclusive_upper_bound_) >= 0;
  }

  size_t remaining_in_leaf() const {
    DCHECK_NE(state_, kUninitialized) << "not initted";
    return iter_->remaining_in_leaf();
  }

  virtual bool HasNext() const OVERRIDE {
    DCHECK_NE(state_, kUninitialized) << "not initted";
    return state_ != kFinished && iter_->IsValid();
  }

  // NOTE: This method will return a MRSRow with the MemRowSet schema.
  //       The row is NOT projected using the schema specified to the iterator.
  const MRSRow GetCurrentRow() const {
    DCHECK_NE(state_, kUninitialized) << "not initted";
    Slice dummy, mrsrow_data;
    iter_->GetCurrentEntry(&dummy, &mrsrow_data);
    return MRSRow(memrowset_.get(), mrsrow_data);
  }

  // Copy the current MRSRow to the 'dst_row' provided using the iterator projection schema.
  Status GetCurrentRow(RowBlockRow* dst_row,
                       Arena* row_arena,
                       const Mutation** redo_head,
                       Arena* mutation_arena,
                       Timestamp* insertion_timestamp);

  bool Next() {
    DCHECK_NE(state_, kUninitialized) << "not initted";
    return iter_->Next();
  }

  string ToString() const OVERRIDE {
    return "memrowset iterator";
  }

  const Schema& schema() const OVERRIDE {
    return *projection_;
  }

  virtual void GetIteratorStats(std::vector<IteratorStats>* stats) const OVERRIDE {
    // Currently we do not expose any non-disk related statistics in
    // IteratorStats.  However, callers of GetIteratorStats expected
    // an IteratorStats object for every column; vector::resize() is
    // used as it will also fill the 'stats' with new instances of
    // IteratorStats.
    stats->resize(schema().num_columns());
  }

 private:
  friend class MemRowSet;

  enum ScanState {
    // Enumerated constants to indicate the iterator state:
    kUninitialized = 0,
    kScanning = 1,  // We may continue fetching and returning values.
    kFinished = 2   // We either know we can never reach the lower bound, or
                    // we've exceeded the upper bound.
  };

  DISALLOW_COPY_AND_ASSIGN(Iterator);

  Iterator(const std::shared_ptr<const MemRowSet> &mrs,
           MemRowSet::MSBTIter *iter, const Schema *projection,
           MvccSnapshot mvcc_snap);

  // Various helper functions called while getting the next RowBlock
  Status FetchRows(RowBlock* dst, size_t* fetched);
  Status ApplyMutationsToProjectedRow(const Mutation *mutation_head,
                                      RowBlockRow *dst_row,
                                      Arena *dst_arena);

  const std::shared_ptr<const MemRowSet> memrowset_;
  gscoped_ptr<MemRowSet::MSBTIter> iter_;

  // The MVCC snapshot which determines which rows and mutations are visible to
  // this iterator.
  const MvccSnapshot mvcc_snap_;

  // Mapping from projected column index back to memrowset column index.
  // Relies on the MRSRowProjector interface to abstract from the two
  // different implementations of the RowProjector, which may change
  // at runtime (using vs. not using code generation).
  const Schema* const projection_;
  gscoped_ptr<MRSRowProjector> projector_;
  DeltaProjector delta_projector_;

  // Temporary buffer used for RowChangeList projection.
  faststring delta_buf_;

  size_t prepared_count_;

  // Temporary local buffer used for seeking to hold the encoded
  // seek target.
  faststring tmp_buf;

  // State of the scanner: indicates whether we should keep scanning/fetching,
  // whether we've scanned the last batch, or whether we've reached the upper bounds
  // or will never reach the lower bounds (no more rows can be returned)
  ScanState state_;

  // Pushed down encoded upper bound key, if any
  boost::optional<const Slice &> exclusive_upper_bound_;
};

inline const Schema* MRSRow::schema() const {
  return &memrowset_->schema_nonvirtual();
}

} // namespace tablet
} // namespace kudu

#endif
