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

#ifndef KUDU_CFILE_CFILE_WRITER_H
#define KUDU_CFILE_CFILE_WRITER_H

#include <boost/utility.hpp>
#include <unordered_map>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include "kudu/cfile/block_encodings.h"
#include "kudu/cfile/block_compression.h"
#include "kudu/cfile/cfile.pb.h"
#include "kudu/cfile/cfile_util.h"
#include "kudu/cfile/type_encodings.h"
#include "kudu/common/key_encoder.h"
#include "kudu/common/types.h"
#include "kudu/fs/block_id.h"
#include "kudu/fs/block_manager.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/strings/stringpiece.h"
#include "kudu/util/env.h"
#include "kudu/util/rle-encoding.h"
#include "kudu/util/status.h"

namespace kudu {
class Arena;

namespace cfile {
using std::unordered_map;

class BlockPointer;
class BTreeInfoPB;
class GVIntBlockBuilder;
class BinaryPrefixBlockBuilder;
class IndexTreeBuilder;

// Magic used in header/footer
extern const char kMagicString[];

const int kCFileMajorVersion = 1;
const int kCFileMinorVersion = 0;

class NullBitmapBuilder {
 public:
  explicit NullBitmapBuilder(size_t initial_row_capacity)
    : nitems_(0),
      bitmap_(BitmapSize(initial_row_capacity)),
      rle_encoder_(&bitmap_, 1) {
  }

  size_t nitems() const {
    return nitems_;
  }

  void AddRun(bool value, size_t run_length = 1) {
    nitems_ += run_length;
    rle_encoder_.Put(value, run_length);
  }

  // the returned Slice is only valid until this Builder is destroyed or Reset
  Slice Finish() {
    int len = rle_encoder_.Flush();
    return Slice(bitmap_.data(), len);
  }

  void Reset() {
    nitems_ = 0;
    rle_encoder_.Clear();
  }

 private:
  size_t nitems_;
  faststring bitmap_;
  RleEncoder<bool> rle_encoder_;
};

// Main class used to write a CFile.
class CFileWriter {
 public:
  explicit CFileWriter(const WriterOptions &options,
                       const TypeInfo* typeinfo,
                       bool is_nullable,
                       gscoped_ptr<fs::WritableBlock> block);
  ~CFileWriter();

  Status Start();

  // Close the CFile and close the underlying writable block.
  Status Finish();

  // Close the CFile and release the underlying writable block to 'closer'.
  Status FinishAndReleaseBlock(fs::ScopedWritableBlockCloser* closer);

  bool finished() {
    return state_ == kWriterFinished;
  }

  // Add a key-value pair of metadata to the file. Keys should be human-readable,
  // values may be arbitrary binary.
  //
  // If this is called prior to Start(), then the metadata pairs will be added in
  // the header. Otherwise, the pairs will be added in the footer during Finish().
  void AddMetadataPair(const Slice &key, const Slice &value);

  // Return the metadata value associated with the given key.
  //
  // If no such metadata has been added yet, logs a FATAL error.
  std::string GetMetaValueOrDie(Slice key) const;

  // Append a set of values to the file.
  Status AppendEntries(const void *entries, size_t count);

  // Append a set of values to the file with the relative null bitmap.
  // "entries" is not "compact" - ie if you're appending 10 rows, and 9 are NULL,
  // 'entries' still will have 10 elements in it
  Status AppendNullableEntries(const uint8_t *bitmap, const void *entries, size_t count);

  // Append a raw block to the file, adding it to the various indexes.
  //
  // The Slices in 'data_slices' are concatenated to form the block.
  //
  // validx_key may be NULL if this file writer has not been configured with
  // value indexing.
  Status AppendRawBlock(const vector<Slice> &data_slices,
                        size_t ordinal_pos,
                        const void *validx_key,
                        const char *name_for_log);


  // Return the amount of data written so far to this CFile.
  // More data may be written by Finish(), but this is an approximation.
  size_t written_size() const;

  std::string ToString() const { return block_->id().ToString(); }

  // Wrapper for AddBlock() to append the dictionary block to the end of a Cfile.
  Status AppendDictBlock(const vector<Slice> &data_slices, BlockPointer *block_ptr,
                         const char *name_for_log) {
    return AddBlock(data_slices, block_ptr, name_for_log);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CFileWriter);

  friend class IndexTreeBuilder;

  // Append the given block into the file.
  //
  // Sets *block_ptr to correspond to the newly inserted block.
  Status AddBlock(const vector<Slice> &data_slices,
                  BlockPointer *block_ptr,
                  const char *name_for_log);

  Status WriteRawData(const Slice& data);

  Status FinishCurDataBlock();

  // Flush the current unflushed_metadata_ entries into the given protobuf
  // field, clearing the buffer.
  void FlushMetadataToPB(google::protobuf::RepeatedPtrField<FileMetadataPairPB> *field);

  // Block being written.
  gscoped_ptr<fs::WritableBlock> block_;

  // Current file offset.
  uint64_t off_;

  // Current number of values that have been appended.
  rowid_t value_count_;

  WriterOptions options_;

  // Type of data being written
  bool is_nullable_;
  CompressionType compression_;
  const TypeInfo* typeinfo_;
  const TypeEncodingInfo* type_encoding_info_;

  // The key-encoder. Only set if the writer is writing an embedded
  // value index.
  const KeyEncoder<faststring>* key_encoder_;

  // a temporary buffer for encoding
  faststring tmp_buf_;

  // Metadata which has been added to the writer but not yet flushed.
  vector<pair<string, string> > unflushed_metadata_;

  gscoped_ptr<BlockBuilder> data_block_;
  gscoped_ptr<IndexTreeBuilder> posidx_builder_;
  gscoped_ptr<IndexTreeBuilder> validx_builder_;
  gscoped_ptr<NullBitmapBuilder> null_bitmap_builder_;
  gscoped_ptr<CompressedBlockBuilder> block_compressor_;

  enum State {
    kWriterInitialized,
    kWriterWriting,
    kWriterFinished
  };
  State state_;
};


} // namespace cfile
} // namespace kudu

#endif
