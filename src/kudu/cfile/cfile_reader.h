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

#ifndef KUDU_CFILE_CFILE_READER_H
#define KUDU_CFILE_CFILE_READER_H

#include <string>
#include <vector>

#include "kudu/common/columnblock.h"
#include "kudu/common/types.h"
#include "kudu/cfile/block_cache.h"
#include "kudu/cfile/block_encodings.h"
#include "kudu/cfile/block_handle.h"
#include "kudu/cfile/block_compression.h"
#include "kudu/cfile/cfile_util.h"
#include "kudu/cfile/index_btree.h"
#include "kudu/cfile/type_encodings.h"
#include "kudu/fs/block_id.h"
#include "kudu/fs/block_manager.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/port.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/object_pool.h"
#include "kudu/util/once.h"
#include "kudu/util/rle-encoding.h"
#include "kudu/util/status.h"
#include "kudu/common/iterator_stats.h"
#include "kudu/common/key_encoder.h"

namespace kudu {
namespace cfile {

class BlockCache;
class BlockDecoder;
class BlockPointer;
class CFileHeaderPB;
class CFileFooterPB;
class CFileIterator;
class BinaryPlainBlockDecoder;

class CFileReader {
 public:
  // Fully open a cfile using a previously opened block.
  //
  // After this call, the reader is safe for use.
  static Status Open(gscoped_ptr<fs::ReadableBlock> block,
                     const ReaderOptions& options,
                     gscoped_ptr<CFileReader>* reader);

  // Lazily open a cfile using a previously opened block. A lazy open does
  // not incur additional I/O, nor does it validate the contents of the
  // cfile.
  //
  // Init() must be called before using most methods. Exceptions include
  // NewIterator() and file_size().
  static Status OpenNoInit(gscoped_ptr<fs::ReadableBlock> block,
                           const ReaderOptions& options,
                           gscoped_ptr<CFileReader>* reader);

  // Fully opens a previously lazily opened cfile, parsing and validating
  // its contents.
  //
  // May be called multiple times; subsequent calls will no-op.
  Status Init();

  enum CacheControl {
    CACHE_BLOCK,
    DONT_CACHE_BLOCK
  };

  Status NewIterator(CFileIterator **iter, CacheControl cache_control);
  Status NewIterator(gscoped_ptr<CFileIterator> *iter,
                     CacheControl cache_control) {
    CFileIterator *iter_ptr;
    RETURN_NOT_OK(NewIterator(&iter_ptr, cache_control));
    (*iter).reset(iter_ptr);
    return Status::OK();
  }

  // TODO: make this private? should only be used
  // by the iterator and index tree readers, I think.
  Status ReadBlock(const BlockPointer &ptr, CacheControl cache_control,
                   BlockHandle *ret) const;

  // Return the number of rows in this cfile.
  // This is assumed to be reasonably fast (i.e does not scan
  // the data)
  Status CountRows(rowid_t *count) const;

  // Retrieve the given metadata entry into 'val'.
  // Returns true if the entry was found, otherwise returns false.
  //
  // Note that this implementation is currently O(n), so should not be used
  // in a hot path.
  bool GetMetadataEntry(const string &key, string *val);

  // Can be called before Init().
  uint64_t file_size() const {
    return file_size_;
  }

  const TypeInfo *type_info() const {
    DCHECK(init_once_.initted());
    return type_info_;
  }

  const TypeEncodingInfo *type_encoding_info() const {
    DCHECK(init_once_.initted());
    return type_encoding_info_;
  }

  bool is_nullable() const {
    return footer().is_type_nullable();
  }

  const CFileHeaderPB &header() const {
    DCHECK(init_once_.initted());
    return *DCHECK_NOTNULL(header_.get());
  }

  const CFileFooterPB &footer() const {
    DCHECK(init_once_.initted());
    return *DCHECK_NOTNULL(footer_.get());
  }

  bool is_compressed() const {
    return footer().compression() != NO_COMPRESSION;
  }

  // Advanced access to the cfile. This is used by the
  // delta reader code. TODO: think about reorganizing this:
  // delta files can probably be done more cleanly.

  // Return true if there is a position-based index on this file.
  bool has_posidx() const { return footer().has_posidx_info(); }
  BlockPointer posidx_root() const {
    DCHECK(has_posidx());
    return BlockPointer(footer().posidx_info().root_block());
  }

  // Return true if there is a value-based index on this file.
  bool has_validx() const { return footer().has_validx_info(); }
  BlockPointer validx_root() const {
    DCHECK(has_validx());
    return BlockPointer(footer().validx_info().root_block());
  }

  std::string ToString() const { return block_->id().ToString(); }

 private:
  DISALLOW_COPY_AND_ASSIGN(CFileReader);

  CFileReader(const ReaderOptions &options,
              const uint64_t file_size,
              gscoped_ptr<fs::ReadableBlock> block);

  // Callback used in 'init_once_' to initialize this cfile.
  Status InitOnce();

  Status ReadMagicAndLength(uint64_t offset, uint32_t *len);
  Status ReadAndParseHeader();
  Status ReadAndParseFooter();

  // Returns the memory usage of the object including the object itself.
  size_t memory_footprint() const;

#ifdef __clang__
  __attribute__((__unused__))
#endif
  const gscoped_ptr<fs::ReadableBlock> block_;
  const uint64_t file_size_;

  gscoped_ptr<CFileHeaderPB> header_;
  gscoped_ptr<CFileFooterPB> footer_;

  gscoped_ptr<CompressedBlockDecoder> block_uncompressor_;

  const TypeInfo *type_info_;
  const TypeEncodingInfo *type_encoding_info_;

  KuduOnceDynamic init_once_;

  ScopedTrackedConsumption mem_consumption_;
};

// Column Iterator interface used by the CFileSet.
// Implemented by the CFileIterator, DefaultColumnValueIterator
// and the ColumnValueTypeAdaptorIterator.
// It is used to fill the data requested by the projection.
class ColumnIterator {
 public:
  virtual ~ColumnIterator() {}

  // Seek to the given ordinal entry in the file.
  // Entry 0 is the first entry written to the file.
  // If provided seek point is past the end of the file,
  // then returns a NotFound Status.
  // TODO: do we ever want to be able to seek to the end of the file?
  virtual Status SeekToOrdinal(rowid_t ord_idx) = 0;

  // Return true if this reader is currently seeked.
  // If the iterator is not seeked, it is an error to call any functions except
  // for seek (including GetCurrentOrdinal).
  virtual bool seeked() const = 0;

  // Get the ordinal index that the iterator is currently pointed to.
  //
  // Prior to calling PrepareBatch(), this returns the position after the last
  // seek. PrepareBatch() and Scan() do not change the position returned by this
  // function. FinishBatch() advances the ordinal to the position of the next
  // block to be prepared.
  virtual rowid_t GetCurrentOrdinal() const = 0;

  // Prepare to read up to *n into the given column block.
  // On return sets *n to the number of prepared rows, which is always
  // <= the requested value.
  //
  // This assumes that dst->size() >= *n on input.
  //
  // If there are at least dst->size() values remaining in the underlying file,
  // this will always return *n == dst->size(). In other words, this does not
  // ever result in a "short read".
  virtual Status PrepareBatch(size_t *n) = 0;

  // Copy values into the prepared column block.
  // Any indirected values (eg strings) are copied into the dst block's
  // arena.
  // This does _not_ advance the position in the underlying file. Multiple
  // calls to Scan() will re-read the same values.
  virtual Status Scan(ColumnBlock *dst) = 0;

  // Finish processing the current batch, advancing the iterators
  // such that the next call to PrepareBatch() will start where the previous
  // batch left off.
  virtual Status FinishBatch() = 0;

  virtual const IteratorStats& io_statistics() const = 0;
};

// ColumnIterator that fills the ColumnBlock with the specified value.
// It is used by the CFileSet to handle the case of a column present
// in the projection schema but not in the base data.
//
// Example:
//    DefaultColumnValueIterator iter;
//    iter.Scan(&column_block);
class DefaultColumnValueIterator : public ColumnIterator {
 public:
  DefaultColumnValueIterator(const TypeInfo* typeinfo, const void *value)
    : typeinfo_(typeinfo), value_(value), ordinal_(0) {
  }

  Status SeekToOrdinal(rowid_t ord_idx) OVERRIDE;

  bool seeked() const OVERRIDE { return true; }

  rowid_t GetCurrentOrdinal() const OVERRIDE { return ordinal_; }

  Status PrepareBatch(size_t *n) OVERRIDE;
  Status Scan(ColumnBlock *dst) OVERRIDE;
  Status FinishBatch() OVERRIDE;

  const IteratorStats& io_statistics() const OVERRIDE { return io_stats_; }

 private:
  const TypeInfo* typeinfo_;
  const void *value_;

  size_t batch_;
  rowid_t ordinal_;
  IteratorStats io_stats_;
};


class CFileIterator : public ColumnIterator {
 public:
  CFileIterator(CFileReader* reader,
                CFileReader::CacheControl cache_control);
  ~CFileIterator();

  // Seek to the first entry in the file. This works for both
  // ordinal-indexed and value-indexed files.
  Status SeekToFirst();

  // Seek to the given ordinal entry in the file.
  // Entry 0 is the first entry written to the file.
  // If provided seek point is past the end of the file,
  // then returns a NotFound Status.
  // TODO: do we ever want to be able to seek to the end of the file?
  Status SeekToOrdinal(rowid_t ord_idx) OVERRIDE;

  // Seek the index to the given row_key, or to the index entry immediately
  // before it. Then (if the index is sparse) seek the data block to the
  // value matching value or to the value immediately after it.
  //
  // Sets *exact_match to indicate whether the seek found the exact
  // key requested.
  //
  // If this iterator was constructed without no value index,
  // then this will return a NotSupported status.
  Status SeekAtOrAfter(const EncodedKey &encoded_key,
                       bool *exact_match);

  // Return true if this reader is currently seeked.
  // If the iterator is not seeked, it is an error to call any functions except
  // for seek (including GetCurrentOrdinal).
  bool seeked() const OVERRIDE { return seeked_; }

  // Get the ordinal index that the iterator is currently pointed to.
  //
  // Prior to calling PrepareBatch(), this returns the position after the last
  // seek. PrepareBatch() and Scan() do not change the position returned by this
  // function. FinishBatch() advances the ordinal to the position of the next
  // block to be prepared.
  rowid_t GetCurrentOrdinal() const OVERRIDE;

  // Prepare to read up to *n into the given column block.
  // On return sets *n to the number of prepared rows, which is always
  // <= the requested value.
  //
  // This assumes that dst->size() >= *n on input.
  //
  // If there are at least dst->size() values remaining in the underlying file,
  // this will always return *n == dst->size(). In other words, this does not
  // ever result in a "short read".
  Status PrepareBatch(size_t *n) OVERRIDE;

  // Copy values into the prepared column block.
  // Any indirected values (eg strings) are copied into the dst block's
  // arena.
  // This does _not_ advance the position in the underlying file. Multiple
  // calls to Scan() will re-read the same values.
  Status Scan(ColumnBlock *dst) OVERRIDE;

  // Finish processing the current batch, advancing the iterators
  // such that the next call to PrepareBatch() will start where the previous
  // batch left off.
  Status FinishBatch() OVERRIDE;

  // Return true if the next call to PrepareBatch will return at least one row.
  bool HasNext() const;

  // Convenience method to prepare a batch, scan it, and finish it.
  Status CopyNextValues(size_t *n, ColumnBlock *dst);

  const IteratorStats &io_statistics() const OVERRIDE {
    return io_stats_;
  }

  // It the column is dictionary-coded, returns the decoder
  // for the cfile's dictionary block. This is called by the
  // StringDictBlockDecoder.
  BinaryPlainBlockDecoder* GetDictDecoder() { return dict_decoder_.get();}

 private:
  DISALLOW_COPY_AND_ASSIGN(CFileIterator);

  struct PreparedBlock {
    BlockPointer dblk_ptr_;
    BlockHandle dblk_data_;
    gscoped_ptr<BlockDecoder> dblk_;

    // The rowid of the first row in this block.
    rowid_t first_row_idx() const {
      return dblk_->GetFirstRowId();
    }

    // The index of the seeked position, relative to the start of the block.
    // In case of null bitmap present, dblk_->GetCurrentIndex() is not aligned
    // with the row number, since null values are not written to the data block.
    // check CFileIterator::SeekToPositionInBlock()
    uint32_t idx_in_block_;

    // When the block is first read, it is seeked to the proper position
    // and rewind_idx_ is set to that offset in the block. needs_rewind_
    // is initially false, but after any values are read from the block,
    // it becomes true. This indicates that dblk_ is pointed at a later
    // position in the block, and should be rewound if a second call to
    // Scan() is made.
    // rewind_idx is relative to the first entry in the block (i.e. not a rowid)
    bool needs_rewind_;
    uint32_t rewind_idx_;

    // Total number of rows in the block (nulls + not nulls)
    uint32_t num_rows_in_block_;

    // Null bitmap and bitmap (RLE) decoder
    Slice rle_bitmap;
    RleDecoder<bool> rle_decoder_;

    rowid_t last_row_idx() const {
      return first_row_idx() + num_rows_in_block_ - 1;
    }

    string ToString() const;
  };

  // Seek the given PreparedBlock to the given index within it.
  void SeekToPositionInBlock(PreparedBlock *pb, uint32_t idx_in_block);

  // Read the data block currently pointed to by idx_iter_
  // into the given PreparedBlock structure.
  //
  // This does not advance the iterator.
  Status ReadCurrentDataBlock(const IndexTreeIterator &idx_iter,
                              PreparedBlock *prep_block);

  // Read the data block currently pointed to by idx_iter_, and enqueue
  // it onto the end of the prepared_blocks_ deque.
  Status QueueCurrentDataBlock(const IndexTreeIterator &idx_iter);

  // Fully initialize the underlying cfile reader if needed, and clear any
  // seek-related state.
  Status PrepareForNewSeek();

  CFileReader* reader_;

  gscoped_ptr<IndexTreeIterator> posidx_iter_;
  gscoped_ptr<IndexTreeIterator> validx_iter_;

  // Decoder for the dictionary block
  gscoped_ptr<BinaryPlainBlockDecoder> dict_decoder_;
  BlockHandle dict_block_handle_;

  // The currently in-use index iterator. This is equal to either
  // posidx_iter_.get(), validx_iter_.get(), or NULL if not seeked.
  IndexTreeIterator *seeked_;

  // Data blocks that contain data relevant to the currently Prepared
  // batch of rows.
  // These pointers are allocated from the prepared_block_pool_ below.
  vector<PreparedBlock *> prepared_blocks_;

  ObjectPool<PreparedBlock> prepared_block_pool_;
  typedef ObjectPool<PreparedBlock>::scoped_ptr pblock_pool_scoped_ptr;

  // True if PrepareBatch() has been called more recently than FinishBatch().
  bool prepared_;

  // Whether this iterator will ask the cfile to cache the blocks it requests or not.
  const CFileReader::CacheControl cache_control_;

  // RowID of the current prepared batch, if prepared_ is true.
  // Otherwise, the RowID of the next batch that will be prepared.
  rowid_t last_prepare_idx_;

  // Number of rows in the current batch, if prepared_ is true.
  // Otherwise, 0.
  uint32_t last_prepare_count_;

  IteratorStats io_stats_;

  // a temporary buffer for encoding
  faststring tmp_buf_;
};

} // namespace cfile
} // namespace kudu

#endif
