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
#ifndef KUDU_TABLET_COMPACTION_H
#define KUDU_TABLET_COMPACTION_H

#include <memory>
#include <string>
#include <vector>

#include "kudu/common/generic_iterators.h"
#include "kudu/common/iterator.h"
#include "kudu/tablet/diskrowset.h"
#include "kudu/tablet/memrowset.h"

namespace kudu {
namespace tablet {
struct CompactionInputRow;
class WriteTransactionState;

// Interface for an input feeding into a compaction or flush.
class CompactionInput {
 public:
  // Create an input which reads from the given rowset, yielding base rows
  // prior to the given snapshot.
  //
  // NOTE: For efficiency, this doesn't currently filter the mutations to only
  // include those committed in the given snapshot. It does, however, filter out
  // rows that weren't inserted prior to this snapshot. Users of this input still
  // need to call snap.IsCommitted() on each mutation.
  //
  // TODO: can we make the above less messy?
  static Status Create(const DiskRowSet &rowset,
                       const Schema* projection,
                       const MvccSnapshot &snap,
                       gscoped_ptr<CompactionInput>* out);

  // Create an input which reads from the given memrowset, yielding base rows and updates
  // prior to the given snapshot.
  static CompactionInput *Create(const MemRowSet &memrowset,
                                 const Schema* projection,
                                 const MvccSnapshot &snap);

  // Create an input which merges several other compaction inputs. The inputs are merged
  // in key-order according to the given schema. All inputs must have matching schemas.
  static CompactionInput *Merge(const vector<std::shared_ptr<CompactionInput> > &inputs,
                                const Schema *schema);

  virtual Status Init() = 0;
  virtual Status PrepareBlock(vector<CompactionInputRow> *block) = 0;

  // Returns the arena for this compaction input corresponding to the last
  // prepared block. This must be called *after* PrepareBlock() as if this
  // is a MergeCompactionInput only then will the right arena be selected.
  virtual Arena*  PreparedBlockArena() = 0;
  virtual Status FinishBlock() = 0;

  virtual bool HasMoreBlocks() = 0;
  virtual const Schema &schema() const = 0;

  virtual ~CompactionInput() {}
};

// The set of rowsets which are taking part in a given compaction.
class RowSetsInCompaction {
 public:
  void AddRowSet(const std::shared_ptr<RowSet> &rowset,
                 const std::shared_ptr<boost::mutex::scoped_try_lock> &lock) {
    CHECK(lock->owns_lock());

    locks_.push_back(lock);
    rowsets_.push_back(rowset);
  }

  // Create the appropriate compaction input for this compaction -- either a merge
  // of all the inputs, or the single input if there was only one.
  //
  // 'schema' is the schema for the output of the compaction, and must remain valid
  // for the lifetime of the returned CompactionInput.
  Status CreateCompactionInput(const MvccSnapshot &snap,
                               const Schema* schema,
                               std::shared_ptr<CompactionInput> *out) const;

  // Dump a log message indicating the chosen rowsets.
  void DumpToLog() const;

  const RowSetVector &rowsets() const { return rowsets_; }

  size_t num_rowsets() const {
    return rowsets_.size();
  }

 private:
  typedef vector<std::shared_ptr<boost::mutex::scoped_try_lock> > LockVector;

  RowSetVector rowsets_;
  LockVector locks_;
};

// One row yielded by CompactionInput::PrepareBlock.
struct CompactionInputRow {
  // The compaction input base row.
  RowBlockRow row;
  // The current redo head for this row, may be null if the base row has no mutations.
  const Mutation* redo_head;
  // The current undo head for this row, may be null if all undos were garbage collected.
  const Mutation* undo_head;
};

// Function shared by flushes, compactions and major delta compactions. Applies all the REDO
// mutations from 'src_row' to the 'dst_row', and generates the related UNDO mutations. Some
// handling depends on the nature of the operation being performed:
//  - Flush: Applies all the REDOs to all the columns.
//  - Compaction: Applies all the REDOs to all the columns.
//  - Major delta compaction: Applies only the REDOs that have corresponding columns in the schema
//                            belonging to 'dst_row'. Those that don't belong to that schema are
//                            ignored.
//
// Currently, 'is_garbage_collected' is always false (KUDU-236).
Status ApplyMutationsAndGenerateUndos(const MvccSnapshot& snap,
                                      const CompactionInputRow& src_row,
                                      const Schema* base_schema,
                                      Mutation** new_undo_head,
                                      Mutation** new_redo_head,
                                      Arena* arena,
                                      RowBlockRow* dst_row,
                                      bool* is_garbage_collected,
                                      uint64_t* num_rows_history_truncated);


// Iterate through this compaction input, flushing all rows to the given RollingDiskRowSetWriter.
// The 'snap' argument should match the MvccSnapshot used to create the compaction input.
//
// After return of this function, this CompactionInput object is "used up" and will
// no longer be useful.
Status FlushCompactionInput(CompactionInput *input,
                            const MvccSnapshot &snap,
                            RollingDiskRowSetWriter *out);

// Iterate through this compaction input, finding any mutations which came between
// snap_to_exclude and snap_to_include (ie those transactions that were not yet
// committed in 'snap_to_exclude' but _are_ committed in 'snap_to_include'). For
// each such mutation, propagate it into the compaction's output rowsets.
//
// The output rowsets passed in must be non-overlapping and in ascending key order:
// typically they are the resulting rowsets from a RollingDiskRowSetWriter.
//
// After return of this function, this CompactionInput object is "used up" and will
// yield no further rows.
Status ReupdateMissedDeltas(const string &tablet_name,
                            CompactionInput *input,
                            const MvccSnapshot &snap_to_exclude,
                            const MvccSnapshot &snap_to_include,
                            const RowSetVector &output_rowsets);

// Dump the given compaction input to 'lines' or LOG(INFO) if it is NULL.
// This consumes all of the input in the compaction input.
Status DebugDumpCompactionInput(CompactionInput *input, vector<string> *lines);

} // namespace tablet
} // namespace kudu

#endif
