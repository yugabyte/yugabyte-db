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
#ifndef KUDU_TABLET_DELTAFILE_H
#define KUDU_TABLET_DELTAFILE_H

#include <boost/ptr_container/ptr_deque.hpp>
#include <memory>
#include <string>
#include <vector>

#include "kudu/cfile/block_handle.h"
#include "kudu/cfile/cfile_reader.h"
#include "kudu/cfile/cfile_writer.h"
#include "kudu/cfile/index_btree.h"
#include "kudu/common/columnblock.h"
#include "kudu/common/schema.h"
#include "kudu/fs/block_id.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/tablet/deltamemstore.h"
#include "kudu/tablet/delta_key.h"
#include "kudu/tablet/tablet.pb.h"
#include "kudu/util/once.h"

namespace kudu {

class ScanSpec;

namespace cfile {
class BinaryPlainBlockDecoder;
} // namespace cfile

namespace tablet {

class DeltaFileIterator;
class DeltaKey;
template<DeltaType Type>
struct ApplyingVisitor;
template<DeltaType Type>
struct CollectingVisitor;
template<DeltaType Type>
struct DeletingVisitor;

class DeltaFileWriter {
 public:
  // Construct a new delta file writer.
  //
  // The writer takes ownership of the block and will Close it in Finish().
  explicit DeltaFileWriter(gscoped_ptr<fs::WritableBlock> block);

  Status Start();

  // Closes the delta file, including the underlying writable block.
  Status Finish();

  // Closes the delta file, releasing the underlying block to 'closer'.
  Status FinishAndReleaseBlock(fs::ScopedWritableBlockCloser* closer);

  // Append a given delta to the file. This must be called in ascending order
  // of (key, timestamp) for REDOS and ascending order of key, descending order
  // of timestamp for UNDOS.
  template<DeltaType Type>
  Status AppendDelta(const DeltaKey &key, const RowChangeList &delta);

  Status WriteDeltaStats(const DeltaStats& stats);

 private:
  Status DoAppendDelta(const DeltaKey &key, const RowChangeList &delta);

  gscoped_ptr<cfile::CFileWriter> writer_;

  // Buffer used as a temporary for storing the serialized form
  // of the deltas
  faststring tmp_buf_;

  #ifndef NDEBUG
  // The index of the previously written row.
  // This is used in debug mode to make sure that rows are appended
  // in order.
  DeltaKey last_key_;
  bool has_appended_;
  #endif

  DISALLOW_COPY_AND_ASSIGN(DeltaFileWriter);
};

class DeltaFileReader : public DeltaStore,
                        public std::enable_shared_from_this<DeltaFileReader> {
 public:
  static const char * const kDeltaStatsEntryName;

  // Fully open a delta file using a previously opened block.
  //
  // After this call, the delta reader is safe for use.
  static Status Open(gscoped_ptr<fs::ReadableBlock> file,
                     const BlockId& block_id,
                     std::shared_ptr<DeltaFileReader>* reader_out,
                     DeltaType delta_type);

  // Lazily opens a delta file using a previously opened block. A lazy open
  // does not incur additional I/O, nor does it validate the contents of
  // the delta file.
  //
  // Init() must be called before using the file's stats.
  static Status OpenNoInit(gscoped_ptr<fs::ReadableBlock> file,
                           const BlockId& block_id,
                           std::shared_ptr<DeltaFileReader>* reader_out,
                           DeltaType delta_type);

  virtual Status Init() OVERRIDE;

  virtual bool Initted() OVERRIDE {
    return init_once_.initted();
  }

  // See DeltaStore::NewDeltaIterator(...)
  Status NewDeltaIterator(const Schema *projection,
                          const MvccSnapshot &snap,
                          DeltaIterator** iterator) const OVERRIDE;

  // See DeltaStore::CheckRowDeleted
  virtual Status CheckRowDeleted(rowid_t row_idx, bool *deleted) const OVERRIDE;

  virtual uint64_t EstimateSize() const OVERRIDE;

  const BlockId& block_id() const { return block_id_; }

  virtual const DeltaStats& delta_stats() const OVERRIDE {
    DCHECK(init_once_.initted());
    return *delta_stats_;
  }

  virtual std::string ToString() const OVERRIDE {
    return reader_->ToString();
  }

  // Returns true if this delta file may include any deltas which need to be
  // applied when scanning the given snapshot, or if the file has not yet
  // been fully initialized.
  bool IsRelevantForSnapshot(const MvccSnapshot& snap) const;

 private:
  friend class DeltaFileIterator;

  DISALLOW_COPY_AND_ASSIGN(DeltaFileReader);

  const std::shared_ptr<cfile::CFileReader> &cfile_reader() const {
    return reader_;
  }

  DeltaFileReader(BlockId block_id, cfile::CFileReader *cf_reader,
                  DeltaType delta_type);

  // Callback used in 'init_once_' to initialize this delta file.
  Status InitOnce();

  Status ReadDeltaStats();

  std::shared_ptr<cfile::CFileReader> reader_;
  gscoped_ptr<DeltaStats> delta_stats_;

  const BlockId block_id_;

  // The type of this delta, i.e. UNDO or REDO.
  const DeltaType delta_type_;

  KuduOnceDynamic init_once_;
};

// Iterator over the deltas contained in a delta file.
//
// See DeltaIterator for details.
class DeltaFileIterator : public DeltaIterator {
 public:
  Status Init(ScanSpec *spec) OVERRIDE;

  Status SeekToOrdinal(rowid_t idx) OVERRIDE;
  Status PrepareBatch(size_t nrows, PrepareFlag flag) OVERRIDE;
  Status ApplyUpdates(size_t col_to_apply, ColumnBlock *dst) OVERRIDE;
  Status ApplyDeletes(SelectionVector *sel_vec) OVERRIDE;
  Status CollectMutations(vector<Mutation *> *dst, Arena *arena) OVERRIDE;
  Status FilterColumnIdsAndCollectDeltas(const std::vector<ColumnId>& col_ids,
                                         vector<DeltaKeyAndUpdate>* out,
                                         Arena* arena) OVERRIDE;
  string ToString() const OVERRIDE;
  virtual bool HasNext() OVERRIDE;

 private:
  friend class DeltaFileReader;
  friend struct ApplyingVisitor<REDO>;
  friend struct ApplyingVisitor<UNDO>;
  friend struct CollectingVisitor<REDO>;
  friend struct CollectingVisitor<UNDO>;
  friend struct DeletingVisitor<REDO>;
  friend struct DeletingVisitor<UNDO>;
  friend struct FilterAndAppendVisitor;

  DISALLOW_COPY_AND_ASSIGN(DeltaFileIterator);

  // PrepareBatch() will read forward all blocks from the deltafile
  // which overlap with the block being prepared, enqueueing them onto
  // the 'delta_blocks_' deque. The prepared blocks are then used to
  // actually apply deltas in ApplyUpdates().
  struct PreparedDeltaBlock {
    // The pointer from which this block was read. This is only used for
    // logging, etc.
    cfile::BlockPointer block_ptr_;

    // Handle to the block, so it doesn't get freed from underneath us.
    cfile::BlockHandle block_;

    // The block decoder, to avoid having to re-parse the block header
    // on every ApplyUpdates() call
    gscoped_ptr<cfile::BinaryPlainBlockDecoder> decoder_;

    // The first row index for which there is an update in this delta block.
    rowid_t first_updated_idx_;

    // The last row index for which there is an update in this delta block.
    rowid_t last_updated_idx_;

    // Within this block, the index of the update which is the first one that
    // needs to be consulted. This allows deltas to be skipped at the beginning
    // of the block when the row block starts towards the end of the delta block.
    // For example:
    // <-- delta block ---->
    //                   <--- prepared row block --->
    // Here, we can skip a bunch of deltas at the beginning of the delta block
    // which we know don't apply to the prepared row block.
    rowid_t prepared_block_start_idx_;

    // Return a string description of this prepared block, for logging.
    string ToString() const;
  };


  // The passed 'projection' and 'dfr' must remain valid for the lifetime
  // of the iterator.
  DeltaFileIterator(std::shared_ptr<DeltaFileReader> dfr,
                    const Schema *projection, MvccSnapshot snap,
                    DeltaType delta_type);

  // Determine the row index of the first update in the block currently
  // pointed to by index_iter_.
  Status GetFirstRowIndexInCurrentBlock(rowid_t *idx);

  // Determine the last updated row index contained in the given decoded block.
  static Status GetLastRowIndexInDecodedBlock(
    const cfile::BinaryPlainBlockDecoder &dec, rowid_t *idx);

  // Read the current block of data from the current position in the file
  // onto the end of the delta_blocks_ queue.
  Status ReadCurrentBlockOntoQueue();

  // Visit all mutations in the currently prepared row range with the specified
  // visitor class.
  template<class Visitor>
  Status VisitMutations(Visitor *visitor);

  // Log a FATAL error message about a bad delta.
  void FatalUnexpectedDelta(const DeltaKey &key, const Slice &deltas, const string &msg);

  std::shared_ptr<DeltaFileReader> dfr_;

  // Schema used during projection.
  const Schema* projection_;

  // The MVCC state which determines which deltas should be applied.
  const MvccSnapshot mvcc_snap_;

  gscoped_ptr<cfile::IndexTreeIterator> index_iter_;

  // TODO: add better comments here.
  rowid_t prepared_idx_;
  uint32_t prepared_count_;
  bool prepared_;
  bool exhausted_;
  bool initted_;

  // After PrepareBatch(), the set of delta blocks in the delta file
  // which correspond to prepared_block_.
  boost::ptr_deque<PreparedDeltaBlock> delta_blocks_;

  // Temporary buffer used in seeking.
  faststring tmp_buf_;

  // Temporary buffer used for RowChangeList projection.
  faststring delta_buf_;

  // The type of this delta iterator, i.e. UNDO or REDO.
  const DeltaType delta_type_;

  CFileReader::CacheControl cache_blocks_;
};


} // namespace tablet
} // namespace kudu

#endif
