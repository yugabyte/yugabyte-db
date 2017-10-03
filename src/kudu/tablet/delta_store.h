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
#ifndef KUDU_TABLET_DELTA_STORE_H
#define KUDU_TABLET_DELTA_STORE_H

#include <memory>
#include <string>
#include <vector>

#include "kudu/common/columnblock.h"
#include "kudu/common/schema.h"
#include "kudu/util/status.h"
#include "kudu/tablet/mutation.h"
#include "kudu/tablet/mvcc.h"
#include "kudu/tablet/delta_key.h"
#include "kudu/tablet/delta_stats.h"
#include "kudu/tablet/tablet_metadata.h"

namespace kudu {

class ScanSpec;
class SelectionVector;

namespace tablet {

class DeltaIterator;
class DeltaFileWriter;

// Interface for the pieces of the system that track deltas/updates.
// This is implemented by DeltaMemStore and by DeltaFileReader.
class DeltaStore {
 public:
  // Performs any post-construction work for the DeltaStore, which may
  // include additional I/O.
  virtual Status Init() = 0;

  // Whether this delta store was initialized or not.
  virtual bool Initted() = 0;

  // Create a DeltaIterator for the given projection.
  //
  // The projection corresponds to whatever scan is currently ongoing.
  // All RowBlocks passed to this DeltaIterator must have this same schema.
  //
  // 'snapshot' is the MVCC state which determines which transactions
  // should be considered committed (and thus applied by the iterator).
  //
  // Returns Status::OK and sets 'iterator' to the new DeltaIterator, or
  // returns Status::NotFound if the mutations within this delta store
  // cannot include 'snap'.
  virtual Status NewDeltaIterator(const Schema *projection,
                                  const MvccSnapshot &snap,
                                  DeltaIterator** iterator) const = 0;

  // Set *deleted to true if the latest update for the given row is a deletion.
  virtual Status CheckRowDeleted(rowid_t row_idx, bool *deleted) const = 0;

  // Get the store's estimated size in bytes.
  virtual uint64_t EstimateSize() const = 0;

  virtual std::string ToString() const = 0;

  // TODO remove this once we don't need to have delta_stats for both DMS and DFR. Currently
  // DeltaTracker#GetColumnsIdxWithUpdates() needs to filter out DMS from the redo list but it
  // can't without RTTI.
  virtual const DeltaStats& delta_stats() const = 0;

  virtual ~DeltaStore() {}
};

typedef std::vector<std::shared_ptr<DeltaStore> > SharedDeltaStoreVector;

// Iterator over deltas.
// For each rowset, this iterator is constructed alongside the base data iterator,
// and used to apply any updates which haven't been yet compacted into the base
// (i.e. those edits in the DeltaMemStore or in delta files)
//
// Typically this is used as follows:
//
//   Open iterator, seek to particular point in file
//   RowBlock rowblock;
//   foreach RowBlock in base data {
//     clear row block
//     CHECK_OK(iter->PrepareBatch(rowblock.size()));
//     ... read column 0 from base data into row block ...
//     CHECK_OK(iter->ApplyUpdates(0, rowblock.column(0))
//     ... check predicates for column ...
//     ... read another column from base data...
//     CHECK_OK(iter->ApplyUpdates(1, rowblock.column(1)))
//     ...
//  }

struct DeltaKeyAndUpdate {
  DeltaKey key;
  Slice cell;

  std::string Stringify(DeltaType type, const Schema& schema) const;
};

class DeltaIterator {
 public:
  // Initialize the iterator. This must be called once before any other
  // call.
  virtual Status Init(ScanSpec *spec) = 0;

  // Seek to a particular ordinal position in the delta data. This cancels any prepared
  // block, and must be called at least once prior to PrepareBatch().
  virtual Status SeekToOrdinal(rowid_t idx) = 0;

  // Argument to PrepareBatch(). See below.
  enum PrepareFlag {
    PREPARE_FOR_APPLY,
    PREPARE_FOR_COLLECT
  };

  // Prepare to apply deltas to a block of rows. This takes a consistent snapshot
  // of all updates to the next 'nrows' rows, so that subsequent calls to
  // ApplyUpdates() will not cause any "tearing"/non-atomicity.
  //
  // 'flag' denotes whether the batch will be used for collecting mutations or
  // for applying them. Some implementations may choose to prepare differently.
  //
  // Each time this is called, the iterator is advanced by the full length
  // of the previously prepared block.
  virtual Status PrepareBatch(size_t nrows, PrepareFlag flag) = 0;

  // Apply the snapshotted updates to one of the columns.
  // 'dst' must be the same length as was previously passed to PrepareBatch()
  // Must have called PrepareBatch() with flag = PREPARE_FOR_APPLY.
  virtual Status ApplyUpdates(size_t col_to_apply, ColumnBlock *dst) = 0;

  // Apply any deletes to the given selection vector.
  // Rows which have been deleted in the associated MVCC snapshot are set to
  // 0 in the selection vector so that they don't show up in the output.
  // Must have called PrepareBatch() with flag = PREPARE_FOR_APPLY.
  virtual Status ApplyDeletes(SelectionVector *sel_vec) = 0;

  // Collect the mutations associated with each row in the current prepared batch.
  //
  // Each entry in the vector will be treated as a singly linked list of Mutation
  // objects. If there are no mutations for that row, the entry will be unmodified.
  // If there are mutations, they will be appended at the tail of the linked list
  // (i.e in ascending timestamp order)
  //
  // The Mutation objects will be allocated out of the provided Arena, which must be non-NULL.
  // Must have called PrepareBatch() with flag = PREPARE_FOR_COLLECT.
  virtual Status CollectMutations(vector<Mutation *> *dst, Arena *arena) = 0;

  // Iterate through all deltas, adding deltas for columns not
  // specified in 'col_ids' to 'out'.
  //
  // The delta objects will be allocated out the provided Arena which
  // must be non-NULL.
  // Must have called PrepareBatch() with flag = PREPARE_FOR_COLLECT.
  virtual Status FilterColumnIdsAndCollectDeltas(const std::vector<ColumnId>& col_ids,
                                                 vector<DeltaKeyAndUpdate>* out,
                                                 Arena* arena) = 0;

  // Returns true if there are any more rows left in this iterator.
  virtual bool HasNext() = 0;

  // Return a string representation suitable for debug printouts.
  virtual std::string ToString() const = 0;

  virtual ~DeltaIterator() {}
};

enum {
  ITERATE_OVER_ALL_ROWS = 0
};

// Dumps contents of 'iter' to 'out', line-by-line.  Used to unit test
// minor delta compaction.
//
// If nrows is 0, all rows will be dumped.
Status DebugDumpDeltaIterator(DeltaType type,
                              DeltaIterator* iter,
                              const Schema& schema,
                              size_t nrows,
                              vector<std::string>* out);

// Writes the contents of 'iter' to 'out', block by block.  Used by
// minor delta compaction.
//
// If nrows is 0, all rows will be dumped.
template<DeltaType Type>
Status WriteDeltaIteratorToFile(DeltaIterator* iter,
                                size_t nrows,
                                DeltaFileWriter* out);

} // namespace tablet
} // namespace kudu

#endif
