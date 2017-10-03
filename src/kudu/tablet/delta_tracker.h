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
#ifndef KUDU_TABLET_DELTATRACKER_H
#define KUDU_TABLET_DELTATRACKER_H

#include <gtest/gtest_prod.h>
#include <memory>
#include <string>
#include <vector>

#include "kudu/common/iterator.h"
#include "kudu/common/rowid.h"
#include "kudu/gutil/macros.h"
#include "kudu/server/metadata.h"
#include "kudu/tablet/delta_store.h"
#include "kudu/tablet/cfile_set.h"
#include "kudu/util/status.h"

namespace kudu {

class MemTracker;

namespace consensus {
class OpId;
}

namespace log {
class LogAnchorRegistry;
}

namespace metadata {
class RowSetMetadata;
}

namespace tablet {

class DeltaMemStore;
class DeltaFileReader;
class OperationResultPB;
class MemStoreTargetPB;
struct ProbeStats;

// The DeltaTracker is the part of a DiskRowSet which is responsible for
// tracking modifications against the base data. It consists of a set of
// DeltaStores which each contain a set of mutations against the base data.
// These DeltaStores may be on disk (DeltaFileReader) or in-memory (DeltaMemStore).
//
// This class is also responsible for flushing the in-memory deltas to disk.
class DeltaTracker {
 public:
  enum MetadataFlushType {
    FLUSH_METADATA,
    NO_FLUSH_METADATA
  };

  DeltaTracker(std::shared_ptr<RowSetMetadata> rowset_metadata,
               rowid_t num_rows, log::LogAnchorRegistry* log_anchor_registry,
               std::shared_ptr<MemTracker> parent_tracker);

  Status WrapIterator(const std::shared_ptr<CFileSet::Iterator> &base,
                      const MvccSnapshot &mvcc_snap,
                      gscoped_ptr<ColumnwiseIterator>* out) const;

  // TODO: this shouldn't need to return a shared_ptr, but there is some messiness
  // where this has bled around.
  //
  // 'schema' is the schema of the rows that are being read by the client.
  // It must remain valid for the lifetime of the returned iterator.
  Status NewDeltaIterator(const Schema* schema,
                          const MvccSnapshot& snap,
                          std::shared_ptr<DeltaIterator>* out) const;

  // Like NewDeltaIterator() but only includes file based stores, does not include
  // the DMS.
  // Returns the delta stores being merged in *included_stores.
  Status NewDeltaFileIterator(
    const Schema* schema,
    const MvccSnapshot &snap,
    DeltaType type,
    std::vector<std::shared_ptr<DeltaStore> >* included_stores,
    std::shared_ptr<DeltaIterator>* out) const;

  // CHECKs that the given snapshot includes all of the UNDO stores in this
  // delta tracker. If this is not the case, crashes the process. This is
  // used as an assertion during compaction, where we always expect the
  // compaction snapshot to be in the future relative to any UNDOs.
  //
  // Returns a bad status in the event of an I/O related error.
  Status CheckSnapshotComesAfterAllUndos(const MvccSnapshot& snap) const;

  Status Open();

  // Flushes the current DeltaMemStore and replaces it with a new one.
  // Caller selects whether to also have the RowSetMetadata (and consequently
  // the TabletMetadata) flushed.
  //
  // NOTE: 'flush_type' should almost always be set to 'FLUSH_METADATA', or else
  // delta stores might become unrecoverable. TODO: see KUDU-204 to clean this up
  // a bit.
  Status Flush(MetadataFlushType flush_type);

  // Update the given row in the database.
  // Copies the data, as well as any referenced values into a local arena.
  // "result" tracks the status of the update as well as which data
  // structure(s) it ended up at.
  Status Update(Timestamp timestamp,
                rowid_t row_idx,
                const RowChangeList &update,
                const consensus::OpId& op_id,
                OperationResultPB* result);

  // Check if the given row has been deleted -- i.e if the most recent
  // delta for this row is a deletion.
  //
  // Sets *deleted to true if so; otherwise sets it to false.
  Status CheckRowDeleted(rowid_t row_idx, bool *deleted, ProbeStats* stats) const;

  // Compacts all deltafiles
  //
  // TODO keep metadata in the delta stores to indicate whether or not
  // a minor (or -- when implemented -- major) compaction is warranted
  // and if so, compact the stores.
  Status Compact();

  // Performs minor compaction on all delta files between index
  // "start_idx" and "end_idx" (inclusive) and writes this to a
  // new delta block. If "end_idx" is set to -1, then delta files at
  // all indexes starting with "start_idx" will be compacted.
  Status CompactStores(int start_idx, int end_idx);

  // Replace the subsequence of stores that matches 'stores_to_replace' with
  // delta file readers corresponding to 'new_delta_blocks', which may be empty.
  Status AtomicUpdateStores(const SharedDeltaStoreVector& stores_to_replace,
                            const std::vector<BlockId>& new_delta_blocks,
                            DeltaType type);

  // Return the number of rows encompassed by this DeltaTracker. Note that
  // this is _not_ the number of updated rows, but rather the number of rows
  // in the associated CFileSet base data. All updates must have a rowid
  // strictly less than num_rows().
  int64_t num_rows() const { return num_rows_; }

  // Get the delta MemStore's size in bytes, including pre-allocation.
  size_t DeltaMemStoreSize() const;

  // Returns true if the DMS has no entries. This doesn't rely on the size.
  bool DeltaMemStoreEmpty() const;

  // Get the minimum log index for this tracker's DMS, -1 if it wasn't set.
  int64_t MinUnflushedLogIndex() const;

  // Return the number of redo delta stores, not including the DeltaMemStore.
  size_t CountRedoDeltaStores() const;

  uint64_t EstimateOnDiskSize() const;

  // Retrieves the list of column indexes that currently have updates.
  void GetColumnIdsWithUpdates(std::vector<ColumnId>* col_ids) const;

  Mutex* compact_flush_lock() {
    return &compact_flush_lock_;
  }

 private:
  friend class DiskRowSet;

  DISALLOW_COPY_AND_ASSIGN(DeltaTracker);

  FRIEND_TEST(TestRowSet, TestRowSetUpdate);
  FRIEND_TEST(TestRowSet, TestDMSFlush);
  FRIEND_TEST(TestRowSet, TestMakeDeltaIteratorMergerUnlocked);
  FRIEND_TEST(TestRowSet, TestCompactStores);
  FRIEND_TEST(TestMajorDeltaCompaction, TestCompact);

  Status OpenDeltaReaders(const std::vector<BlockId>& blocks,
                          std::vector<std::shared_ptr<DeltaStore> >* stores,
                          DeltaType type);

  Status FlushDMS(DeltaMemStore* dms,
                  std::shared_ptr<DeltaFileReader>* dfr,
                  MetadataFlushType flush_type);

  // This collects all undo and redo stores.
  void CollectStores(vector<std::shared_ptr<DeltaStore> > *stores) const;

  // Performs the actual compaction. Results of compaction are written to "block",
  // while delta stores that underwent compaction are appended to "compacted_stores", while
  // their corresponding block ids are appended to "compacted_blocks".
  //
  // NOTE: the caller of this method should acquire or already hold an
  // exclusive lock on 'compact_flush_lock_' before calling this
  // method in order to protect 'redo_delta_stores_'.
  Status DoCompactStores(size_t start_idx, size_t end_idx,
                         gscoped_ptr<fs::WritableBlock> block,
                         vector<std::shared_ptr<DeltaStore> > *compacted_stores,
                         std::vector<BlockId>* compacted_blocks);

  // Creates a merge delta iterator and captures the delta stores and
  // delta blocks under compaction into 'target_stores' and
  // 'target_blocks', respectively.  The merge iterator is stored in
  // 'out'; 'out' is valid until this instance of DeltaTracker
  // is destroyed.
  //
  // NOTE: the caller of this method must first acquire or already
  // hold a lock on 'compact_flush_lock_'in order to guard against a
  // race on 'redo_delta_stores_'.
  Status MakeDeltaIteratorMergerUnlocked(size_t start_idx, size_t end_idx,
                                         const Schema* schema,
                                         vector<std::shared_ptr<DeltaStore > > *target_stores,
                                         vector<BlockId> *target_blocks,
                                         std::shared_ptr<DeltaIterator> *out);

  std::shared_ptr<RowSetMetadata> rowset_metadata_;

  // The number of rows in the DiskRowSet that this tracker is associated with.
  // This is just used for assertions to make sure that we don't update a row
  // which doesn't exist.
  rowid_t num_rows_;

  bool open_;

  log::LogAnchorRegistry* log_anchor_registry_;

  std::shared_ptr<MemTracker> parent_tracker_;

  // The current DeltaMemStore into which updates should be written.
  std::shared_ptr<DeltaMemStore> dms_;
  // The set of tracked REDO delta stores, in increasing timestamp order.
  SharedDeltaStoreVector redo_delta_stores_;
  // The set of tracked UNDO delta stores, in decreasing timestamp order.
  SharedDeltaStoreVector undo_delta_stores_;

  // read-write lock protecting dms_ and {redo,undo}_delta_stores_.
  // - Readers and mutators take this lock in shared mode.
  // - Flushers take this lock in exclusive mode before they modify the
  //   structure of the rowset.
  //
  // TODO(perf): convert this to a reader-biased lock to avoid any cacheline
  // contention between threads.
  mutable rw_spinlock component_lock_;

  // Exclusive lock that ensures that only one flush or compaction can run
  // at a time. Protects delta_stores_. NOTE: this lock cannot be acquired
  // while component_lock is held: otherwise, Flush and Compaction threads
  // (that both first acquire this lock and then component_lock) will deadlock.
  //
  // TODO(perf): this needs to be more fine grained
  mutable Mutex compact_flush_lock_;
};


} // namespace tablet
} // namespace kudu

#endif
