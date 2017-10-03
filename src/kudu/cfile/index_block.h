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

#ifndef KUDU_CFILE_INDEX_BLOCK_H
#define KUDU_CFILE_INDEX_BLOCK_H

#include <boost/lexical_cast.hpp>
#include <boost/utility.hpp>
#include <glog/logging.h>
#include <string>
#include <vector>

#include "kudu/common/types.h"
#include "kudu/cfile/block_pointer.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/port.h"
#include "kudu/util/coding-inl.h"

namespace kudu {
namespace cfile {

using std::string;
using std::vector;
using kudu::DataTypeTraits;

// Forward decl.
class IndexBlockIterator;

struct WriterOptions;

// Index Block Builder for a particular key type.
// This works like the rest of the builders in the cfile package.
// After repeatedly calling Add(), call Finish() to encode it
// into a Slice, then you may Reset to re-use buffers.
class IndexBlockBuilder {
 public:
  explicit IndexBlockBuilder(const WriterOptions *options,
                             bool is_leaf);

  // Append an entry into the index.
  void Add(const Slice &key, const BlockPointer &ptr);

  // Finish the current index block.
  // Returns a fully encoded Slice including the data
  // as well as any necessary footer.
  // The Slice is only valid until the next call to
  // Reset().
  Slice Finish();

  // Return the key of the first entry in this index block.
  // The pointed-to data is only valid until the next call to this builder.
  Status GetFirstKey(Slice *key) const;

  size_t Count() const;

  // Return an estimate of the post-encoding size of this
  // index block. This estimate should be conservative --
  // it will over-estimate rather than under-estimate, and
  // should be accurate to within a reasonable threshold,
  // but is not exact.
  size_t EstimateEncodedSize() const;

  void Reset();

 private:
  DISALLOW_COPY_AND_ASSIGN(IndexBlockBuilder);

#ifdef __clang__
  __attribute__((__unused__))
#endif
  const WriterOptions *options_;

  // Is the builder currently between Finish() and Reset()
  bool finished_;

  // Is this a leaf block?
  bool is_leaf_;

  faststring buffer_;
  vector<uint32_t> entry_offsets_;
};

class IndexBlockReader {
 public:
  IndexBlockReader();

  void Reset();

  // Parse the given index block.
  //
  // This function may be called repeatedly to "reset" the reader to process
  // a new block.
  //
  // Note: this does not copy the data, so the slice must
  // remain valid for the lifetime of the reader (or until the next Parse call)
  Status Parse(const Slice &data);

  size_t Count() const;

  IndexBlockIterator *NewIterator() const;

  bool IsLeaf();

 private:
  friend class IndexBlockIterator;

  int CompareKey(int idx_in_block, const Slice &search_key) const;

  Status ReadEntry(size_t idx, Slice *key, BlockPointer *block_ptr) const;

  // Set *ptr to the beginning of the index data for the given index
  // entry.
  // Set *limit to the 'limit' pointer for that entry (i.e a pointer
  // beyond which the data no longer is part of that entry).
  //   - *limit can be used to prevent overrunning in the case of a
  //     corrupted length varint or length prefix
  void GetKeyPointer(int idx_in_block, const uint8_t **ptr, const uint8_t **limit) const;

  static const int kMaxTrailerSize = 64*1024;
  Slice data_;

  IndexBlockTrailerPB trailer_;
  const uint8_t *key_offsets_;
  bool parsed_;

  DISALLOW_COPY_AND_ASSIGN(IndexBlockReader);
};

class IndexBlockIterator {
 public:
  explicit IndexBlockIterator(const IndexBlockReader *reader);

  // Reset the state of this iterator. This should be used
  // after the associated 'reader' object parses a different block.
  void Reset();

  // Find the highest block pointer in this index
  // block which has a value <= the given key.
  // If such a block is found, returns OK status.
  // If no such block is found (i.e the smallest key in the
  // index is still larger than the provided key), then
  // Status::NotFound is returned.
  //
  // If this function returns an error, then the state of this
  // iterator is undefined (i.e it may or may not have moved
  // since the previous call)
  Status SeekAtOrBefore(const Slice &search_key);

  Status SeekToIndex(size_t idx);

  bool HasNext() const;

  Status Next();

  const BlockPointer &GetCurrentBlockPointer() const;

  const Slice GetCurrentKey() const;

 private:
  const IndexBlockReader *reader_;
  size_t cur_idx_;
  Slice cur_key_;
  BlockPointer cur_ptr_;
  bool seeked_;

  DISALLOW_COPY_AND_ASSIGN(IndexBlockIterator);
};

} // namespace cfile
} // namespace kudu
#endif
