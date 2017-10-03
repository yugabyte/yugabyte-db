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

#include "kudu/tablet/compaction.h"

#include <deque>
#include <glog/logging.h>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/tablet/cfile_set.h"
#include "kudu/tablet/delta_store.h"
#include "kudu/tablet/delta_tracker.h"
#include "kudu/tablet/diskrowset.h"
#include "kudu/tablet/tablet.pb.h"
#include "kudu/tablet/transactions/write_transaction.h"
#include "kudu/util/debug/trace_event.h"

using std::shared_ptr;
using std::unordered_set;
using strings::Substitute;

namespace kudu {
namespace tablet {

namespace {

// CompactionInput yielding rows and mutations from a MemRowSet.
class MemRowSetCompactionInput : public CompactionInput {
 public:
  MemRowSetCompactionInput(const MemRowSet& memrowset,
                           const MvccSnapshot& snap,
                           const Schema* projection)
    : iter_(memrowset.NewIterator(projection, snap)),
      arena_(32*1024, 128*1024),
      has_more_blocks_(false) {
  }

  virtual Status Init() OVERRIDE {
    RETURN_NOT_OK(iter_->Init(NULL));
    has_more_blocks_ = iter_->HasNext();
    return Status::OK();
  }

  virtual bool HasMoreBlocks() OVERRIDE {
    return has_more_blocks_;
  }

  virtual Status PrepareBlock(vector<CompactionInputRow> *block) OVERRIDE {
    int num_in_block = iter_->remaining_in_leaf();
    block->resize(num_in_block);

    // Realloc the internal block storage if we don't have enough space to
    // copy the whole leaf node's worth of data into it.
    if (PREDICT_FALSE(!row_block_ || num_in_block > row_block_->nrows())) {
      row_block_.reset(new RowBlock(iter_->schema(), num_in_block, nullptr));
    }

    arena_.Reset();
    RowChangeListEncoder undo_encoder(&buffer_);
    for (int i = 0; i < num_in_block; i++) {
      // TODO: A copy is performed to make all CompactionInputRow have the same schema
      CompactionInputRow &input_row = block->at(i);
      input_row.row.Reset(row_block_.get(), i);
      Timestamp insertion_timestamp;
      RETURN_NOT_OK(iter_->GetCurrentRow(&input_row.row,
                                         reinterpret_cast<Arena*>(NULL),
                                         &input_row.redo_head,
                                         &arena_,
                                         &insertion_timestamp));

      // Materialize MRSRow undo insert (delete)
      undo_encoder.SetToDelete();
      input_row.undo_head = Mutation::CreateInArena(&arena_,
                                                    insertion_timestamp,
                                                    undo_encoder.as_changelist());
      undo_encoder.Reset();
      iter_->Next();
    }

    has_more_blocks_ = iter_->HasNext();
    return Status::OK();
  }

  Arena* PreparedBlockArena() OVERRIDE { return &arena_; }

  virtual Status FinishBlock() OVERRIDE {
    return Status::OK();
  }

  virtual const Schema &schema() const OVERRIDE {
    return iter_->schema();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MemRowSetCompactionInput);
  gscoped_ptr<RowBlock> row_block_;

  gscoped_ptr<MemRowSet::Iterator> iter_;

  // Arena used to store the projected undo/redo mutations of the current block.
  Arena arena_;

  faststring buffer_;

  bool has_more_blocks_;
};

////////////////////////////////////////////////////////////

// CompactionInput yielding rows and mutations from an on-disk DiskRowSet.
class DiskRowSetCompactionInput : public CompactionInput {
 public:
  DiskRowSetCompactionInput(gscoped_ptr<RowwiseIterator> base_iter,
                            shared_ptr<DeltaIterator> redo_delta_iter,
                            shared_ptr<DeltaIterator> undo_delta_iter)
      : base_iter_(base_iter.Pass()),
        redo_delta_iter_(std::move(redo_delta_iter)),
        undo_delta_iter_(std::move(undo_delta_iter)),
        arena_(32 * 1024, 128 * 1024),
        block_(base_iter_->schema(), kRowsPerBlock, &arena_),
        redo_mutation_block_(kRowsPerBlock, reinterpret_cast<Mutation *>(NULL)),
        undo_mutation_block_(kRowsPerBlock, reinterpret_cast<Mutation *>(NULL)),
        first_rowid_in_block_(0) {}

  virtual Status Init() OVERRIDE {
    ScanSpec spec;
    spec.set_cache_blocks(false);
    RETURN_NOT_OK(base_iter_->Init(&spec));
    RETURN_NOT_OK(redo_delta_iter_->Init(&spec));
    RETURN_NOT_OK(redo_delta_iter_->SeekToOrdinal(0));
    RETURN_NOT_OK(undo_delta_iter_->Init(&spec));
    RETURN_NOT_OK(undo_delta_iter_->SeekToOrdinal(0));
    return Status::OK();
  }

  virtual bool HasMoreBlocks() OVERRIDE {
    return base_iter_->HasNext();
  }

  virtual Status PrepareBlock(vector<CompactionInputRow> *block) OVERRIDE {
    RETURN_NOT_OK(base_iter_->NextBlock(&block_));
    std::fill(redo_mutation_block_.begin(), redo_mutation_block_.end(),
              reinterpret_cast<Mutation *>(NULL));
    std::fill(undo_mutation_block_.begin(), undo_mutation_block_.end(),
                  reinterpret_cast<Mutation *>(NULL));
    RETURN_NOT_OK(redo_delta_iter_->PrepareBatch(
                      block_.nrows(), DeltaIterator::PREPARE_FOR_COLLECT));
    RETURN_NOT_OK(redo_delta_iter_->CollectMutations(&redo_mutation_block_, block_.arena()));
    RETURN_NOT_OK(undo_delta_iter_->PrepareBatch(
                      block_.nrows(), DeltaIterator::PREPARE_FOR_COLLECT));
    RETURN_NOT_OK(undo_delta_iter_->CollectMutations(&undo_mutation_block_, block_.arena()));

    block->resize(block_.nrows());
    for (int i = 0; i < block_.nrows(); i++) {
      CompactionInputRow &input_row = block->at(i);
      input_row.row.Reset(&block_, i);
      input_row.redo_head = redo_mutation_block_[i];
      input_row.undo_head = undo_mutation_block_[i];
    }

    first_rowid_in_block_ += block_.nrows();
    return Status::OK();
  }

  virtual Arena* PreparedBlockArena() OVERRIDE { return &arena_; }

  virtual Status FinishBlock() OVERRIDE {
    return Status::OK();
  }

  virtual const Schema &schema() const OVERRIDE {
    return base_iter_->schema();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DiskRowSetCompactionInput);
  gscoped_ptr<RowwiseIterator> base_iter_;
  shared_ptr<DeltaIterator> redo_delta_iter_;
  shared_ptr<DeltaIterator> undo_delta_iter_;

  Arena arena_;

  // The current block of data which has come from the input iterator
  RowBlock block_;
  vector<Mutation *> redo_mutation_block_;
  vector<Mutation *> undo_mutation_block_;

  rowid_t first_rowid_in_block_;

  enum {
    kRowsPerBlock = 100
  };
};

class MergeCompactionInput : public CompactionInput {
 private:
  // State kept for each of the inputs.
  struct MergeState {
    MergeState() :
      pending_idx(0)
    {}

    ~MergeState() {
      STLDeleteElements(&dominated);
    }

    bool empty() const {
      return pending_idx >= pending.size();
    }

    const CompactionInputRow &next() const {
      return pending[pending_idx];
    }

    void pop_front() {
      pending_idx++;
    }

    void Reset() {
      pending.clear();
      pending_idx = 0;
    }

    // Return true if the current block of this input fully dominates
    // the current block of the other input -- i.e that the last
    // row of this block is less than the first row of the other block.
    // In this case, we can remove the other input from the merge until
    // this input's current block has been exhausted.
    bool Dominates(const MergeState &other, const Schema &schema) const {
      DCHECK(!empty());
      DCHECK(!other.empty());

      return schema.Compare(pending.back().row, other.next().row) < 0;
    }

    shared_ptr<CompactionInput> input;
    vector<CompactionInputRow> pending;
    int pending_idx;

    vector<MergeState *> dominated;
  };

 public:
  MergeCompactionInput(const vector<shared_ptr<CompactionInput> > &inputs,
                       const Schema* schema)
    : schema_(schema) {
    for (const shared_ptr<CompactionInput> &input : inputs) {
      gscoped_ptr<MergeState> state(new MergeState);
      state->input = input;
      states_.push_back(state.release());
    }
  }

  virtual ~MergeCompactionInput() {
    STLDeleteElements(&states_);
  }

  virtual Status Init() OVERRIDE {
    for (MergeState *state : states_) {
      RETURN_NOT_OK(state->input->Init());
    }

    // Pull the first block of rows from each input.
    RETURN_NOT_OK(ProcessEmptyInputs());
    return Status::OK();
  }

  virtual bool HasMoreBlocks() OVERRIDE {
    // Return true if any of the input blocks has more rows pending
    // or more blocks which have yet to be pulled.
    for (MergeState *state : states_) {
      if (!state->empty() ||
          state->input->HasMoreBlocks()) {
        return true;
      }
    }

    return false;
  }

  virtual Status PrepareBlock(vector<CompactionInputRow> *block) OVERRIDE {
    CHECK(!states_.empty());

    block->clear();

    while (true) {
      int smallest_idx = -1;
      CompactionInputRow smallest;

      // Iterate over the inputs to find the one with the smallest next row.
      // It may seem like an O(n lg k) merge using a heap would be more efficient,
      // but some benchmarks indicated that the simpler code path of the O(n k) merge
      // actually ends up a bit faster.
      for (int i = 0; i < states_.size(); i++) {
        MergeState *state = states_[i];

        if (state->empty()) {
          prepared_block_arena_ = state->input->PreparedBlockArena();
          // If any of our inputs runs out of pending entries, then we can't keep
          // merging -- this input may have further blocks to process.
          // Rather than pulling another block here, stop the loop. If it's truly
          // out of blocks, then FinishBlock() will remove this input entirely.
          return Status::OK();
        }

        if (smallest_idx < 0) {
          smallest_idx = i;
          smallest = state->next();
          continue;
        }
        int row_comp = schema_->Compare(state->next().row, smallest.row);
        if (row_comp < 0) {
          smallest_idx = i;
          smallest = state->next();
          continue;
        }
        // If we found two duplicated rows, we want the row with the highest
        // live version. If they're equal, that can only be because they're both
        // dead, in which case it doesn't matter.
        // TODO: this is going to change with historical REINSERT handling.
        if (PREDICT_FALSE(row_comp == 0)) {
          int mutation_comp = CompareLatestLiveVersion(state->next(), smallest);
          if (mutation_comp > 0) {
            // If the previous smallest row has a highest version that is lower
            // than this one, discard it.
            states_[smallest_idx]->pop_front();
            smallest_idx = i;
            smallest = state->next();
            continue;
          } else {
            // .. otherwise pop the other one.
            //
            // NOTE: If they're equal, then currently that means that both versions are
            // ghosts. Once we handle REINSERTS, we'll have to figure out which one "comes
            // first" and deal with this properly. For now, we can just pick arbitrarily.
            states_[i]->pop_front();
            continue;
          }
        }
      }
      DCHECK_GE(smallest_idx, 0);

      states_[smallest_idx]->pop_front();
      block->push_back(smallest);
    }

    return Status::OK();
  }

  virtual Arena* PreparedBlockArena() OVERRIDE { return prepared_block_arena_; }

  virtual Status FinishBlock() OVERRIDE {
    return ProcessEmptyInputs();
  }

  virtual const Schema &schema() const OVERRIDE {
    return *schema_;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MergeCompactionInput);

  // Look through our current set of inputs. For any that are empty,
  // pull the next block into its pending list. If there is no next
  // block, remove it from our input set.
  //
  // Postcondition: every input has a non-empty pending list.
  Status ProcessEmptyInputs() {
    int j = 0;
    for (int i = 0; i < states_.size(); i++) {
      MergeState *state = states_[i];
      states_[j++] = state;

      if (!state->empty()) {
        continue;
      }

      RETURN_NOT_OK(state->input->FinishBlock());

      // If an input is fully exhausted, no need to consider it
      // in the merge anymore.
      if (!state->input->HasMoreBlocks()) {

        // Any inputs that were dominated by the last block of this input
        // need to be re-added into the merge.
        states_.insert(states_.end(), state->dominated.begin(), state->dominated.end());
        state->dominated.clear();
        delete state;
        j--;
        continue;
      }

      state->Reset();
      RETURN_NOT_OK(state->input->PrepareBlock(&state->pending));

      // Now that this input has moved to its next block, it's possible that
      // it no longer dominates the inputs in it 'dominated' list. Re-check
      // all of those dominance relations and remove any that are no longer
      // valid.
      for (auto it = state->dominated.begin(); it != state->dominated.end(); ++it) {
        MergeState *dominated = *it;
        if (!state->Dominates(*dominated, *schema_)) {
          states_.push_back(dominated);
          it = state->dominated.erase(it);
          --it;
        }
      }
    }
    // We may have removed exhausted states as we iterated through the
    // array, so resize them away.
    states_.resize(j);

    // Check pairs of states to see if any have dominance relations.
    // This algorithm is probably not the most efficient, but it's the
    // most obvious, and this doesn't ever show up in the profiler as
    // much of a hot spot.
    check_dominance:
    for (int i = 0; i < states_.size(); i++) {
      for (int j = i + 1; j < states_.size(); j++) {
        if (TryInsertIntoDominanceList(states_[i], states_[j])) {
          states_.erase(states_.begin() + j);
          // Since we modified the vector, re-start iteration from the
          // top.
          goto check_dominance;
        } else if (TryInsertIntoDominanceList(states_[j], states_[i])) {
          states_.erase(states_.begin() + i);
          // Since we modified the vector, re-start iteration from the
          // top.
          goto check_dominance;
        }
      }
    }

    return Status::OK();
  }

  bool TryInsertIntoDominanceList(MergeState *dominator, MergeState *candidate) {
    if (dominator->Dominates(*candidate, *schema_)) {
      dominator->dominated.push_back(candidate);
      return true;
    } else {
      return false;
    }
  }

  // Compare the mutations of two duplicated rows.
  // Returns -1 if latest_version(left) < latest_version(right)
  static int CompareLatestLiveVersion(const CompactionInputRow& left,
                                      const CompactionInputRow& right) {
    if (left.redo_head == nullptr) {
      // left must still be alive
      DCHECK(right.redo_head != nullptr);
      return 1;
    }
    if (right.redo_head == nullptr) {
      DCHECK(left.redo_head != nullptr);
      return -1;
    }

    // Duplicated rows have disjoint histories, we don't need to get the latest
    // mutation, the first one should be enough for the sake of determining the most recent
    // row, but in debug mode do get the latest to make sure one of the rows is a ghost.
    const Mutation* left_latest = left.redo_head;
    const Mutation* right_latest = right.redo_head;
    int ret = left_latest->timestamp().CompareTo(right_latest->timestamp());
#ifndef NDEBUG
    AdvanceToLastInList(&left_latest);
    AdvanceToLastInList(&right_latest);
    int debug_ret = left_latest->timestamp().CompareTo(right_latest->timestamp());
    if (debug_ret != 0) {
      // If in fact both rows were deleted at the same time, this is OK -- we could
      // have a case like TestRandomAccess.TestFuzz3, in which a single batch
      // DELETED from the DRS, INSERTed into MRS, and DELETED from MRS. In that case,
      // the timestamp of the last REDO will be the same and we can pick whichever
      // we like.
      CHECK_EQ(ret, debug_ret);
    }
#endif
    return ret;
  }

  static void AdvanceToLastInList(const Mutation** m) {
    while ((*m)->next() != nullptr) {
      *m = (*m)->next();
    }
  }

  const Schema* schema_;
  vector<MergeState *> states_;
  Arena* prepared_block_arena_;
};

} // anonymous namespace

////////////////////////////////////////////////////////////

Status CompactionInput::Create(const DiskRowSet &rowset,
                               const Schema* projection,
                               const MvccSnapshot &snap,
                               gscoped_ptr<CompactionInput>* out) {
  CHECK(projection->has_column_ids());

  // Assertion which checks for an earlier bug where the compaction snapshot
  // chosen was too early. This resulted in UNDO files being mistakenly
  // identified as REDO files and corruption ensued. If the assertion fails,
  // the process crashes; only unrelated I/O-related errors are returned.
  RETURN_NOT_OK_PREPEND(rowset.delta_tracker_->CheckSnapshotComesAfterAllUndos(snap),
                        "Could not open UNDOs");

  shared_ptr<ColumnwiseIterator> base_cwise(rowset.base_data_->NewIterator(projection));
  gscoped_ptr<RowwiseIterator> base_iter(new MaterializingIterator(base_cwise));
  // Creates a DeltaIteratorMerger that will only include part of the redo deltas,
  // since 'snap' will be after the snapshot of the last flush/compaction,
  // i.e. past all undo deltas's max transaction ID.
  shared_ptr<DeltaIterator> redo_deltas;
  RETURN_NOT_OK_PREPEND(rowset.delta_tracker_->NewDeltaIterator(projection, snap, &redo_deltas),
                        "Could not open REDOs");
  // Creates a DeltaIteratorMerger that will only include undo deltas, since
  // MvccSnapshot::CreateSnapshotIncludingNoTransactions() excludes all redo
  // deltas's min transaction ID.
  shared_ptr<DeltaIterator> undo_deltas;
  RETURN_NOT_OK_PREPEND(rowset.delta_tracker_->NewDeltaIterator(projection,
          MvccSnapshot::CreateSnapshotIncludingNoTransactions(),
          &undo_deltas), "Could not open UNDOs");

  out->reset(new DiskRowSetCompactionInput(base_iter.Pass(), redo_deltas, undo_deltas));
  return Status::OK();
}

CompactionInput *CompactionInput::Create(const MemRowSet &memrowset,
                                         const Schema* projection,
                                         const MvccSnapshot &snap) {
  CHECK(projection->has_column_ids());
  return new MemRowSetCompactionInput(memrowset, snap, projection);
}

CompactionInput *CompactionInput::Merge(const vector<shared_ptr<CompactionInput> > &inputs,
                                        const Schema* schema) {
  CHECK(schema->has_column_ids());
  return new MergeCompactionInput(inputs, schema);
}


Status RowSetsInCompaction::CreateCompactionInput(const MvccSnapshot &snap,
                                                  const Schema* schema,
                                                  shared_ptr<CompactionInput> *out) const {
  CHECK(schema->has_column_ids());

  vector<shared_ptr<CompactionInput> > inputs;
  for (const shared_ptr<RowSet> &rs : rowsets_) {
    gscoped_ptr<CompactionInput> input;
    RETURN_NOT_OK_PREPEND(rs->NewCompactionInput(schema, snap, &input),
                          Substitute("Could not create compaction input for rowset $0",
                                     rs->ToString()));
    inputs.push_back(shared_ptr<CompactionInput>(input.release()));
  }

  if (inputs.size() == 1) {
    out->swap(inputs[0]);
  } else {
    out->reset(CompactionInput::Merge(inputs, schema));
  }

  return Status::OK();
}

void RowSetsInCompaction::DumpToLog() const {
  LOG(INFO) << "Selected " << rowsets_.size() << " rowsets to compact:";
  // Dump the selected rowsets to the log, and collect corresponding iterators.
  for (const shared_ptr<RowSet> &rs : rowsets_) {
    LOG(INFO) << rs->ToString() << "(current size on disk: ~"
              << rs->EstimateOnDiskSize() << " bytes)";
  }
}


Status ApplyMutationsAndGenerateUndos(const MvccSnapshot& snap,
                                      const CompactionInputRow& src_row,
                                      const Schema* base_schema,
                                      Mutation** new_undo_head,
                                      Mutation** new_redo_head,
                                      Arena* arena,
                                      RowBlockRow* dst_row,
                                      bool* is_garbage_collected,
                                      uint64_t* num_rows_history_truncated) {
  // TODO actually perform garbage collection (KUDU-236).
  // Right now we persist all mutations.
  *is_garbage_collected = false;

  const Schema* dst_schema = dst_row->schema();

  bool is_deleted = false;

  #define ERROR_LOG_CONTEXT \
    "Source Row: " << dst_schema->DebugRow(src_row.row) << \
    " Redo Mutations: " << Mutation::StringifyMutationList(*base_schema, src_row.redo_head) << \
    " Undo Mutations: " << Mutation::StringifyMutationList(*base_schema, src_row.undo_head) << \
    "\nDest Row: " << dst_schema->DebugRow(*dst_row) << \
    " Redo Mutations: " << Mutation::StringifyMutationList(*dst_schema, redo_head) << \
    " Undo Mutations: " << Mutation::StringifyMutationList(*dst_schema, undo_head)

  faststring dst;
  RowChangeListEncoder undo_encoder(&dst);

  // Const cast this away here since we're ever only going to point to it
  // which doesn't actually mutate it and having Mutation::set_next()
  // take a non-const value is required in other places.
  Mutation* undo_head = const_cast<Mutation*>(src_row.undo_head);
  Mutation* redo_head = nullptr;

  for (const Mutation *redo_mut = src_row.redo_head;
       redo_mut != nullptr;
       redo_mut = redo_mut->next()) {

    // Skip anything not committed.
    if (!snap.IsCommitted(redo_mut->timestamp())) {
      continue;
    }

    undo_encoder.Reset();

    Mutation* current_undo;
    DVLOG(3) << "  @" << redo_mut->timestamp() << ": "
             << redo_mut->changelist().ToString(*base_schema);

    RowChangeListDecoder redo_decoder(redo_mut->changelist());
    Status s = redo_decoder.Init();
    if (PREDICT_FALSE(!s.ok())) {
      LOG(ERROR) << "Unable to decode changelist. " << ERROR_LOG_CONTEXT;
      return s;
    }

    if (redo_decoder.is_update()) {
      DCHECK(!is_deleted) << "Got UPDATE for deleted row. " << ERROR_LOG_CONTEXT;

      s = redo_decoder.ApplyRowUpdate(dst_row,
                                      reinterpret_cast<Arena *>(NULL), &undo_encoder);
      if (PREDICT_FALSE(!s.ok())) {
        LOG(ERROR) << "Unable to apply update/create undo: " << s.ToString()
                   << "\n" << ERROR_LOG_CONTEXT;
        return s;
      }

      // If all of the updates were for columns that we aren't projecting, we don't
      // need to push them into the UNDO file.
      if (undo_encoder.is_empty()) {
        continue;
      }

      // create the UNDO mutation in the provided arena.
      current_undo = Mutation::CreateInArena(arena, redo_mut->timestamp(),
                                             undo_encoder.as_changelist());

      // In the case where the previous undo was NULL just make this one
      // the head.
      if (undo_head == nullptr) {
        undo_head = current_undo;
      } else {
        current_undo->set_next(undo_head);
        undo_head = current_undo;
      }


    } else if (redo_decoder.is_delete() || redo_decoder.is_reinsert()) {
      redo_decoder.TwiddleDeleteStatus(&is_deleted);

      if (redo_decoder.is_reinsert()) {
        // Right now when a REINSERT mutation is found it is treated as a new insert and it
        // clears the whole row history before it.

        // Copy the reinserted row over.
        Slice reinserted_slice;
        RETURN_NOT_OK(redo_decoder.GetReinsertedRowSlice(*dst_schema, &reinserted_slice));
        ConstContiguousRow reinserted(dst_schema, reinserted_slice.data());
        // No need to copy into an arena -- can refer to the mutation's arena.
        Arena* null_arena = nullptr;
        RETURN_NOT_OK(CopyRow(reinserted, dst_row, null_arena));

        // Create an undo for the REINSERT
        undo_encoder.SetToDelete();
        // Reset the UNDO head, losing all previous undos.
        undo_head = Mutation::CreateInArena(arena,
                                            redo_mut->timestamp(),
                                            undo_encoder.as_changelist());

        // Also reset the previous redo head since it stored the delete which was nullified
        // by this reinsert
        redo_head = nullptr;

        if ((*num_rows_history_truncated)++ == 0) {
          LOG(WARNING) << "Found REINSERT REDO truncating row history for "
              << ERROR_LOG_CONTEXT << " Note: this warning will appear "
              "only for the first truncated row";
        }

        if (PREDICT_FALSE(VLOG_IS_ON(2))) {
          VLOG(2) << "Found REINSERT REDO, cannot create UNDO for it, resetting row history "
              " under snap: " << snap.ToString() << ERROR_LOG_CONTEXT;
        }
      } else {
        // Delete mutations are left as redos
        undo_encoder.SetToDelete();
        // Encode the DELETE as a redo
        redo_head = Mutation::CreateInArena(arena,
                                            redo_mut->timestamp(),
                                            undo_encoder.as_changelist());
      }
    } else {
      LOG(FATAL) << "Unknown mutation type!" << ERROR_LOG_CONTEXT;
    }
  }

  *new_undo_head = undo_head;
  *new_redo_head = redo_head;

  return Status::OK();

  #undef ERROR_LOG_CONTEXT
}

Status FlushCompactionInput(CompactionInput* input,
                            const MvccSnapshot& snap,
                            RollingDiskRowSetWriter* out) {
  RETURN_NOT_OK(input->Init());
  vector<CompactionInputRow> rows;

  DCHECK(out->schema().has_column_ids());

  RowBlock block(out->schema(), 100, nullptr);

  uint64_t num_rows_history_truncated = 0;

  while (input->HasMoreBlocks()) {
    RETURN_NOT_OK(input->PrepareBlock(&rows));

    int n = 0;
    for (const CompactionInputRow &input_row : rows) {
      RETURN_NOT_OK(out->RollIfNecessary());

      const Schema* schema = input_row.row.schema();
      DCHECK_SCHEMA_EQ(*schema, out->schema());
      DCHECK(schema->has_column_ids());

      RowBlockRow dst_row = block.row(n);
      RETURN_NOT_OK(CopyRow(input_row.row, &dst_row, reinterpret_cast<Arena*>(NULL)));

      DVLOG(2) << "Input Row: " << dst_row.schema()->DebugRow(dst_row) <<
        " RowId: " << input_row.row.row_index() <<
        " Undo Mutations: " << Mutation::StringifyMutationList(*schema, input_row.undo_head) <<
        " Redo Mutations: " << Mutation::StringifyMutationList(*schema, input_row.redo_head);

      // Collect the new UNDO/REDO mutations.
      Mutation* new_undos_head = nullptr;
      Mutation* new_redos_head = nullptr;

      bool is_garbage_collected;
      RETURN_NOT_OK(ApplyMutationsAndGenerateUndos(snap,
                                                   input_row,
                                                   schema,
                                                   &new_undos_head,
                                                   &new_redos_head,
                                                   input->PreparedBlockArena(),
                                                   &dst_row,
                                                   &is_garbage_collected,
                                                   &num_rows_history_truncated));

      // Whether this row was garbage collected
      if (is_garbage_collected) {
        DVLOG(2) << "Garbage Collected!";
        // Don't flush the row.
        continue;
      }

      rowid_t index_in_current_drs_;

      // We should always have UNDO deltas, until we implement delta GC. For now,
      // this is a convenient assertion to catch bugs like KUDU-632.
      CHECK(new_undos_head != nullptr) <<
        "Writing an output row with no UNDOs: "
        "Input Row: " << dst_row.schema()->DebugRow(dst_row) <<
        " RowId: " << input_row.row.row_index() <<
        " Undo Mutations: " << Mutation::StringifyMutationList(*schema, input_row.undo_head) <<
        " Redo Mutations: " << Mutation::StringifyMutationList(*schema, input_row.redo_head);
      out->AppendUndoDeltas(dst_row.row_index(), new_undos_head, &index_in_current_drs_);

      if (new_redos_head != nullptr) {
        out->AppendRedoDeltas(dst_row.row_index(), new_redos_head, &index_in_current_drs_);
      }

      DVLOG(2) << "Output Row: " << dst_row.schema()->DebugRow(dst_row) <<
          " RowId: " << index_in_current_drs_
          << " Undo Mutations: " << Mutation::StringifyMutationList(*schema, new_undos_head)
          << " Redo Mutations: " << Mutation::StringifyMutationList(*schema, new_redos_head);

      n++;
      if (n == block.nrows()) {
        RETURN_NOT_OK(out->AppendBlock(block));
        n = 0;
      }
    }

    if (n > 0) {
      block.Resize(n);
      RETURN_NOT_OK(out->AppendBlock(block));
    }

    RETURN_NOT_OK(input->FinishBlock());
  }

  if (num_rows_history_truncated > 0) {
    LOG(WARNING) << "Total " << num_rows_history_truncated
        << " rows lost some history due to REINSERT after DELETE";
  }
  return Status::OK();
}

Status ReupdateMissedDeltas(const string &tablet_name,
                            CompactionInput *input,
                            const MvccSnapshot &snap_to_exclude,
                            const MvccSnapshot &snap_to_include,
                            const RowSetVector &output_rowsets) {
  TRACE_EVENT0("tablet", "ReupdateMissedDeltas");
  RETURN_NOT_OK(input->Init());

  VLOG(1) << "Re-updating missed deltas between snapshot " <<
    snap_to_exclude.ToString() << " and " << snap_to_include.ToString();

  // Collect the delta trackers that we'll push the updates into.
  deque<DeltaTracker *> delta_trackers;
  for (const shared_ptr<RowSet> &rs : output_rowsets) {
    delta_trackers.push_back(down_cast<DiskRowSet *>(rs.get())->delta_tracker());
  }

  // The map of updated delta trackers, indexed by id.
  unordered_set<DeltaTracker*> updated_trackers;

  // When we apply the updates to the new DMS, there is no need to anchor them
  // since these stores are not yet part of the tablet.
  const consensus::OpId max_op_id = consensus::MaximumOpId();

  // The rowid where the current (front) delta tracker starts.
  int64_t delta_tracker_base_row = 0;

  // TODO: on this pass, we don't actually need the row data, just the
  // updates. So, this can be made much faster.
  vector<CompactionInputRow> rows;
  const Schema* schema = &input->schema();
  const Schema key_schema(input->schema().CreateKeyProjection());

  // Arena and projector to store/project row keys for missed delta updates
  Arena arena(1024, 1024*1024);
  RowProjector key_projector(schema, &key_schema);
  RETURN_NOT_OK(key_projector.Init());
  faststring buf;

  rowid_t row_idx = 0;
  while (input->HasMoreBlocks()) {
    RETURN_NOT_OK(input->PrepareBlock(&rows));

    for (const CompactionInputRow &row : rows) {
      DVLOG(2) << "Revisiting row: " << schema->DebugRow(row.row) <<
          " Redo Mutations: " << Mutation::StringifyMutationList(*schema, row.redo_head) <<
          " Undo Mutations: " << Mutation::StringifyMutationList(*schema, row.undo_head);

      for (const Mutation *mut = row.redo_head;
           mut != nullptr;
           mut = mut->next()) {
        RowChangeListDecoder decoder(mut->changelist());
        RETURN_NOT_OK(decoder.Init());

        if (snap_to_exclude.IsCommitted(mut->timestamp())) {
          // This update was already taken into account in the first phase of the
          // compaction.
          continue;
        }

        // We should never see a REINSERT in an input RowSet which was not
        // caught in the original flush. REINSERT only occurs when an INSERT is
        // done to a row when a ghost is already present for that row in
        // MemRowSet. If the ghost is in a disk RowSet, it is ignored and the
        // new row is inserted in the MemRowSet instead.
        //
        // At the beginning of a compaction/flush, a new empty MRS is swapped in for
        // the one to be flushed. Therefore, any INSERT that happens _after_ this swap
        // is made will not trigger a REINSERT: it sees the row as "deleted" in the
        // snapshotted MRS, and insert triggers an INSERT into the new MRS.
        //
        // Any INSERT that happened _before_ the swap-out would create a
        // REINSERT in the MRS to be flushed, but it would also be considered as
        // part of the MvccSnapshot which we flush from ('snap_to_exclude' here)
        // and therefore won't make it to this point in the code.
        CHECK(!decoder.is_reinsert())
          << "Shouldn't see REINSERT missed by first flush pass in compaction."
          << " snap_to_exclude=" << snap_to_exclude.ToString()
          << " row=" << schema->DebugRow(row.row)
          << " mutations=" << Mutation::StringifyMutationList(*schema, row.redo_head);

        if (!snap_to_include.IsCommitted(mut->timestamp())) {
          // The mutation was inserted after the DuplicatingRowSet was swapped in.
          // Therefore, it's already present in the output rowset, and we don't need
          // to copy it in.

          DVLOG(2) << "Skipping already-duplicated delta for row " << row_idx
                   << " @" << mut->timestamp() << ": " << mut->changelist().ToString(*schema);
          continue;
        }

        // Otherwise, this is an update that arrived after the snapshot for the first
        // pass, but before the DuplicatingRowSet was swapped in. We need to transfer
        // this over to the output rowset.
        DVLOG(1) << "Flushing missed delta for row " << row_idx
                  << " @" << mut->timestamp() << ": " << mut->changelist().ToString(*schema);

        DeltaTracker *cur_tracker = delta_trackers.front();

        // The index on the input side isn't necessarily the index on the output side:
        // we may have output several small DiskRowSets, so we need to find the index
        // relative to the current one.
        int64_t idx_in_delta_tracker = row_idx - delta_tracker_base_row;
        while (idx_in_delta_tracker >= cur_tracker->num_rows()) {
          // If the current index is higher than the total number of rows in the current
          // DeltaTracker, that means we're now processing the next one in the list.
          // Pop the current front tracker, and make the indexes relative to the next
          // in the list.
          delta_tracker_base_row += cur_tracker->num_rows();
          idx_in_delta_tracker -= cur_tracker->num_rows();
          DCHECK_GE(idx_in_delta_tracker, 0);
          delta_trackers.pop_front();
          cur_tracker = delta_trackers.front();
        }

        gscoped_ptr<OperationResultPB> result(new OperationResultPB);
        Status s = cur_tracker->Update(mut->timestamp(),
                                       idx_in_delta_tracker,
                                       mut->changelist(),
                                       max_op_id,
                                       result.get());
        DCHECK(s.ok()) << "Failed update on compaction for row " << row_idx
            << " @" << mut->timestamp() << ": " << mut->changelist().ToString(*schema);
        if (s.ok()) {
          // Update the set of delta trackers with the one we've just updated.
          InsertIfNotPresent(&updated_trackers, cur_tracker);
        }
      }

      // TODO when garbage collection kicks in we need to take care that
      // CGed rows do not increment this.
      row_idx++;
    }

    RETURN_NOT_OK(input->FinishBlock());
  }


  // Flush the trackers that got updated, this will make sure that all missed deltas
  // get flushed before we update the tablet's metadata at the end of compaction/flush.
  // Note that we don't flush the metadata here, as to we will update the metadata
  // at the end of the compaction/flush.
  //
  // TODO: there should be a more elegant way of preventing metadata flush at this point
  // using pinning, or perhaps a builder interface for new rowset metadata objects.
  // See KUDU-204.

  {
    TRACE_EVENT0("tablet", "Flushing missed deltas");
    for (DeltaTracker* tracker : updated_trackers) {
      VLOG(1) << "Flushing DeltaTracker updated with missed deltas...";
      RETURN_NOT_OK_PREPEND(tracker->Flush(DeltaTracker::NO_FLUSH_METADATA),
                            "Could not flush delta tracker after missed delta update");
    }
  }

  return Status::OK();
}


Status DebugDumpCompactionInput(CompactionInput *input, vector<string> *lines) {
  RETURN_NOT_OK(input->Init());
  vector<CompactionInputRow> rows;

  while (input->HasMoreBlocks()) {
    RETURN_NOT_OK(input->PrepareBlock(&rows));

    for (const CompactionInputRow &input_row : rows) {
      const Schema* schema = input_row.row.schema();
      LOG_STRING(INFO, lines) << schema->DebugRow(input_row.row) <<
        " Undos: " + Mutation::StringifyMutationList(*schema, input_row.undo_head) <<
        " Redos: " + Mutation::StringifyMutationList(*schema, input_row.redo_head);
    }

    RETURN_NOT_OK(input->FinishBlock());
  }
  return Status::OK();
}


} // namespace tablet
} // namespace kudu
