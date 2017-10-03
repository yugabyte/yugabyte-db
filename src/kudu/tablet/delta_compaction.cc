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

#include "kudu/tablet/delta_compaction.h"

#include <algorithm>
#include <string>
#include <vector>

#include "kudu/common/generic_iterators.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/strcat.h"
#include "kudu/common/columnblock.h"
#include "kudu/cfile/cfile_reader.h"
#include "kudu/tablet/cfile_set.h"
#include "kudu/tablet/compaction.h"
#include "kudu/tablet/delta_key.h"
#include "kudu/tablet/deltamemstore.h"
#include "kudu/tablet/multi_column_writer.h"
#include "kudu/tablet/mvcc.h"

using std::shared_ptr;

namespace kudu {

using cfile::CFileIterator;
using cfile::CFileReader;
using cfile::IndexTreeIterator;
using fs::WritableBlock;
using std::vector;
using strings::Substitute;

namespace tablet {

namespace {

const size_t kRowsPerBlock = 100; // Number of rows per block of columns

} // anonymous namespace

// TODO: can you major-delta-compact a new column after an alter table in order
// to materialize it? should write a test for this.
MajorDeltaCompaction::MajorDeltaCompaction(
    FsManager* fs_manager, const Schema& base_schema, CFileSet* base_data,
    shared_ptr<DeltaIterator> delta_iter,
    vector<shared_ptr<DeltaStore> > included_stores,
    const vector<ColumnId>& col_ids)
    : fs_manager_(fs_manager),
      base_schema_(base_schema),
      column_ids_(col_ids),
      base_data_(base_data),
      included_stores_(std::move(included_stores)),
      delta_iter_(std::move(delta_iter)),
      redo_delta_mutations_written_(0),
      undo_delta_mutations_written_(0),
      state_(kInitialized) {
  CHECK(!col_ids.empty());
}

MajorDeltaCompaction::~MajorDeltaCompaction() {
}

string MajorDeltaCompaction::ColumnNamesToString() const {
  std::string result;
  for (ColumnId col_id : column_ids_) {
    int col_idx = base_schema_.find_column_by_id(col_id);
    if (col_idx != Schema::kColumnNotFound) {
      result += base_schema_.column_by_id(col_id).ToString() + " ";
    } else {
      result += Substitute("[deleted column id $0]  ", col_id);
    }
  }
  return result;
}

Status MajorDeltaCompaction::FlushRowSetAndDeltas() {
  CHECK_EQ(state_, kInitialized);

  shared_ptr<ColumnwiseIterator> old_base_data_cwise(base_data_->NewIterator(&partial_schema_));
  gscoped_ptr<RowwiseIterator> old_base_data_rwise(new MaterializingIterator(old_base_data_cwise));

  ScanSpec spec;
  spec.set_cache_blocks(false);
  RETURN_NOT_OK_PREPEND(
      old_base_data_rwise->Init(&spec),
      "Unable to open iterator for specified columns (" + partial_schema_.ToString() + ")");

  RETURN_NOT_OK(delta_iter_->Init(&spec));
  RETURN_NOT_OK(delta_iter_->SeekToOrdinal(0));

  Arena arena(32 * 1024, 128 * 1024);
  RowBlock block(partial_schema_, kRowsPerBlock, &arena);

  DVLOG(1) << "Applying deltas and rewriting columns (" << partial_schema_.ToString() << ")";
  DeltaStats redo_stats;
  DeltaStats undo_stats;
  uint64_t num_rows_history_truncated = 0;
  size_t nrows = 0;
  // We know that we're reading everything from disk so we're including all transactions.
  MvccSnapshot snap = MvccSnapshot::CreateSnapshotIncludingAllTransactions();
  while (old_base_data_rwise->HasNext()) {

    // 1) Get the next batch of base data for the columns we're compacting.
    arena.Reset();
    RETURN_NOT_OK(old_base_data_rwise->NextBlock(&block));
    size_t n = block.nrows();

    // 2) Fetch all the REDO mutations.
    vector<Mutation *> redo_mutation_block(kRowsPerBlock, reinterpret_cast<Mutation *>(NULL));
    RETURN_NOT_OK(delta_iter_->PrepareBatch(n, DeltaIterator::PREPARE_FOR_COLLECT));
    RETURN_NOT_OK(delta_iter_->CollectMutations(&redo_mutation_block, block.arena()));

    // 3) Apply new UNDO mutations for the current block. The REDO mutations are picked up
    //    at step 6).
    vector<CompactionInputRow> input_rows;
    input_rows.resize(block.nrows());
    for (int i = 0; i < block.nrows(); i++) {
      CompactionInputRow &input_row = input_rows.at(i);
      input_row.row.Reset(&block, i);
      input_row.redo_head = redo_mutation_block[i];
      input_row.undo_head = nullptr;

      RowBlockRow dst_row = block.row(i);
      RETURN_NOT_OK(CopyRow(input_row.row, &dst_row, reinterpret_cast<Arena*>(NULL)));

      Mutation* new_undos_head = nullptr;
      // We're ignoring the result from new_redos_head because we'll find them later at step 5).
      Mutation* new_redos_head = nullptr;

      bool is_garbage_collected;

      RETURN_NOT_OK(ApplyMutationsAndGenerateUndos(snap,
                                                   input_row,
                                                   &base_schema_,
                                                   &new_undos_head,
                                                   &new_redos_head,
                                                   &arena,
                                                   &dst_row,
                                                   &is_garbage_collected,
                                                   &num_rows_history_truncated));

      VLOG(2) << "Output Row: " << dst_row.schema()->DebugRow(dst_row)
        << " Undo Mutations: " << Mutation::StringifyMutationList(partial_schema_, new_undos_head)
        << " Redo Mutations: " << Mutation::StringifyMutationList(partial_schema_, new_redos_head);

      // We only create a new undo delta file if we need to.
      if (new_undos_head != nullptr && !new_undo_delta_writer_) {
        RETURN_NOT_OK(OpenUndoDeltaFileWriter());
      }
      for (const Mutation *mut = new_undos_head; mut != nullptr; mut = mut->next()) {
        DeltaKey undo_key(nrows + dst_row.row_index(), mut->timestamp());
        RETURN_NOT_OK(new_undo_delta_writer_->AppendDelta<UNDO>(undo_key, mut->changelist()));
        undo_stats.UpdateStats(mut->timestamp(), mut->changelist());
        undo_delta_mutations_written_++;
      }
    }

    // 4) Write the new base data.
    RETURN_NOT_OK(base_data_writer_->AppendBlock(block));

    // 5) Remove the columns that we're compacting from the delta flush, but keep all the
    //    delete mutations.
    arena.Reset();
    vector<DeltaKeyAndUpdate> out;
    RETURN_NOT_OK(delta_iter_->FilterColumnIdsAndCollectDeltas(column_ids_, &out, &arena));

    // We only create a new redo delta file if we need to.
    if (!out.empty() && !new_redo_delta_writer_) {
      RETURN_NOT_OK(OpenRedoDeltaFileWriter());
    }

    // 6) Write the deltas we're not compacting back into a delta file.
    for (const DeltaKeyAndUpdate& key_and_update : out) {
      RowChangeList update(key_and_update.cell);
      RETURN_NOT_OK_PREPEND(new_redo_delta_writer_->AppendDelta<REDO>(key_and_update.key, update),
                            "Failed to append a delta");
      WARN_NOT_OK(redo_stats.UpdateStats(key_and_update.key.timestamp(), update),
                  "Failed to update stats");
    }
    redo_delta_mutations_written_ += out.size();
    nrows += n;
  }

  RETURN_NOT_OK(base_data_writer_->Finish());

  if (redo_delta_mutations_written_ > 0) {
    RETURN_NOT_OK(new_redo_delta_writer_->WriteDeltaStats(redo_stats));
    RETURN_NOT_OK(new_redo_delta_writer_->Finish());
  }

  if (undo_delta_mutations_written_ > 0) {
    RETURN_NOT_OK(new_undo_delta_writer_->WriteDeltaStats(undo_stats));
    RETURN_NOT_OK(new_undo_delta_writer_->Finish());
  }

  DVLOG(1) << "Applied all outstanding deltas for columns "
           << partial_schema_.ToString()
           << ", and flushed the resulting rowsets and a total of "
           << redo_delta_mutations_written_
           << " REDO delta mutations and "
           << undo_delta_mutations_written_
           << " UNDO delta mutations to disk.";

  state_ = kFinished;
  return Status::OK();
}

Status MajorDeltaCompaction::OpenBaseDataWriter() {
  CHECK(!base_data_writer_);

  gscoped_ptr<MultiColumnWriter> w(new MultiColumnWriter(fs_manager_, &partial_schema_));
  RETURN_NOT_OK(w->Open());
  base_data_writer_.swap(w);
  return Status::OK();
}

Status MajorDeltaCompaction::OpenRedoDeltaFileWriter() {
  gscoped_ptr<WritableBlock> block;
  RETURN_NOT_OK_PREPEND(fs_manager_->CreateNewBlock(&block),
                        "Unable to create REDO delta output block");
  new_redo_delta_block_ = block->id();
  new_redo_delta_writer_.reset(new DeltaFileWriter(block.Pass()));
  return new_redo_delta_writer_->Start();
}

Status MajorDeltaCompaction::OpenUndoDeltaFileWriter() {
  gscoped_ptr<WritableBlock> block;
  RETURN_NOT_OK_PREPEND(fs_manager_->CreateNewBlock(&block),
                        "Unable to create UNDO delta output block");
  new_undo_delta_block_ = block->id();
  new_undo_delta_writer_.reset(new DeltaFileWriter(block.Pass()));
  return new_undo_delta_writer_->Start();
}

Status MajorDeltaCompaction::Compact() {
  CHECK_EQ(state_, kInitialized);

  LOG(INFO) << "Starting major delta compaction for columns " << ColumnNamesToString();
  RETURN_NOT_OK(base_schema_.CreateProjectionByIdsIgnoreMissing(column_ids_, &partial_schema_));

  for (const shared_ptr<DeltaStore>& ds : included_stores_) {
    LOG(INFO) << "Preparing to major compact delta file: " << ds->ToString();
  }

  // We defer on calling OpenNewDeltaBlock since we might not need to flush.
  RETURN_NOT_OK(OpenBaseDataWriter());
  RETURN_NOT_OK(FlushRowSetAndDeltas());
  LOG(INFO) << "Finished major delta compaction of columns " <<
      ColumnNamesToString();
  return Status::OK();
}

Status MajorDeltaCompaction::CreateMetadataUpdate(
    RowSetMetadataUpdate* update) {
  CHECK(update);
  CHECK_EQ(state_, kFinished);

  vector<BlockId> compacted_delta_blocks;
  for (const shared_ptr<DeltaStore>& store : included_stores_) {
    DeltaFileReader* dfr = down_cast<DeltaFileReader*>(store.get());
    compacted_delta_blocks.push_back(dfr->block_id());
  }

  vector<BlockId> new_delta_blocks;
  if (redo_delta_mutations_written_ > 0) {
    new_delta_blocks.push_back(new_redo_delta_block_);
  }

  update->ReplaceRedoDeltaBlocks(compacted_delta_blocks,
                                 new_delta_blocks);

  if (undo_delta_mutations_written_ > 0) {
    update->SetNewUndoBlock(new_undo_delta_block_);
  }

  // Replace old column blocks with new ones
  RowSetMetadata::ColumnIdToBlockIdMap new_column_blocks;
  base_data_writer_->GetFlushedBlocksByColumnId(&new_column_blocks);

  // NOTE: in the case that one of the columns being compacted is deleted,
  // we may have fewer elements in new_column_blocks compared to 'column_ids'.
  // For those deleted columns, we just remove the old column data.
  CHECK_LE(new_column_blocks.size(), column_ids_.size());

  for (ColumnId col_id : column_ids_) {
    BlockId new_block;
    if (FindCopy(new_column_blocks, col_id, &new_block)) {
      update->ReplaceColumnId(col_id, new_block);
    } else {
      // The column has been deleted.
      // If the base data has a block for this column, we need to remove it.
      // NOTE: It's possible that the base data has no data for this column in the
      // case that the column was added and removed in succession after the base
      // data was flushed.
      CHECK_EQ(base_schema_.find_column_by_id(col_id), Schema::kColumnNotFound)
        << "major compaction removing column " << col_id << " but still present in Schema!";
      if (base_data_->has_data_for_column_id(col_id)) {
        update->RemoveColumnId(col_id);
      }
    }
  }

  return Status::OK();
}

// We're called under diskrowset's component_lock_ and delta_tracker's compact_flush_lock_
// so both AtomicUpdateStores calls can be done separately and still be seen as one atomic
// operation.
Status MajorDeltaCompaction::UpdateDeltaTracker(DeltaTracker* tracker) {
  CHECK_EQ(state_, kFinished);
  vector<BlockId> new_delta_blocks;
  // We created a new delta block only if we had deltas to write back. We still need to update
  // the tracker so that it removes the included_stores_.
  if (redo_delta_mutations_written_ > 0) {
    new_delta_blocks.push_back(new_redo_delta_block_);
  }
  RETURN_NOT_OK(tracker->AtomicUpdateStores(included_stores_,
                                            new_delta_blocks,
                                            REDO));

  // We only call AtomicUpdateStores() if we wrote UNDOs, we're not removing stores so we don't
  // need to call it otherwise.
  if (undo_delta_mutations_written_ > 0) {
    vector<BlockId> new_undo_blocks;
    new_undo_blocks.push_back(new_undo_delta_block_);
    return tracker->AtomicUpdateStores(SharedDeltaStoreVector(),
                                       new_undo_blocks,
                                       UNDO);
  } else {
    return Status::OK();
  }
}

} // namespace tablet
} // namespace kudu
