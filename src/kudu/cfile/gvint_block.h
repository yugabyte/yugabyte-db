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
#ifndef KUDU_CFILE_GVINT_BLOCK_H
#define KUDU_CFILE_GVINT_BLOCK_H

#include <stdint.h>

#include <vector>

#include <gtest/gtest_prod.h>

#include "kudu/cfile/block_encodings.h"

namespace kudu {
namespace cfile {

struct WriterOptions;
typedef uint32_t IntType;

using std::vector;

// Builder for an encoded block of ints.
// The encoding is group-varint plus frame-of-reference:
//
// Header group (gvint): <num_elements, min_element, [unused], [unused]
// followed by enough group varints to represent the total number of
// elements, including padding 0s at the end. Each element is a delta
// from the min_element frame-of-reference.
//
// See AppendGroupVarInt32(...) for details on the varint
// encoding.
class GVIntBlockBuilder : public BlockBuilder {
 public:
  explicit GVIntBlockBuilder(const WriterOptions *options);

  bool IsBlockFull(size_t limit) const OVERRIDE;

  int Add(const uint8_t *vals, size_t count) OVERRIDE;

  Slice Finish(rowid_t ordinal_pos) OVERRIDE;

  void Reset() OVERRIDE;

  size_t Count() const OVERRIDE;

  // Return the first added key.
  // key should be a uint32_t *
  Status GetFirstKey(void *key) const OVERRIDE;

 private:

  // TODO: this currently does not do a good job of estimating
  // when the ints are large but clustered together,
  // since it doesn't take into account the delta coding relative
  // to the min int. We could track the min int along the way
  // but then we have extra branches in the add loop. Come back to this,
  // probably the branches don't matter since this is write-side.
  uint64_t EstimateEncodedSize() const {
    return estimated_raw_size_ + (ints_.size() + 3) / 4
    + kEstimatedHeaderSizeBytes + kTrailerExtraPaddingBytes;
  }

  friend class TestEncoding;
  FRIEND_TEST(TestEncoding, TestGroupVarInt);
  FRIEND_TEST(TestEncoding, TestIntBlockEncoder);

  vector<IntType> ints_;
  faststring buffer_;
  uint64_t estimated_raw_size_;

  const WriterOptions *options_;

  enum {
    kEstimatedHeaderSizeBytes = 10,

    // Up to 3 "0s" can be tacked on the end of the block to round out
    // the groups of 4
    kTrailerExtraPaddingBytes = 3
  };
};

// Decoder for UINT32 type, GROUP_VARINT coding
class GVIntBlockDecoder : public BlockDecoder {
 public:
  explicit GVIntBlockDecoder(Slice slice);

  Status ParseHeader() OVERRIDE;
  void SeekToStart() {
    SeekToPositionInBlock(0);
  }

  void SeekToPositionInBlock(uint pos) OVERRIDE;

  Status SeekAtOrAfterValue(const void *value, bool *exact_match) OVERRIDE;

  Status CopyNextValues(size_t *n, ColumnDataView *dst) OVERRIDE;

  // Copy the integers to a temporary buffer, it is used by StringDictDecoder
  // in its CopyNextValues() method.
  Status CopyNextValuesToArray(size_t *n, uint8_t* array);

  size_t GetCurrentIndex() const OVERRIDE {
    DCHECK(parsed_) << "must parse header first";
    return cur_idx_;
  }

  virtual rowid_t GetFirstRowId() const OVERRIDE {
    return ordinal_pos_base_;
  }

  size_t Count() const OVERRIDE {
    return num_elems_;
  }

  bool HasNext() const OVERRIDE {
    return (num_elems_ - cur_idx_) > 0;
  }

 private:
  friend class TestEncoding;

  template<class IntSink>
  Status DoGetNextValues(size_t *n, IntSink *sink);

  Slice data_;

  bool parsed_;
  const uint8_t *ints_start_;
  uint32_t num_elems_;
  uint32_t min_elem_;
  rowid_t ordinal_pos_base_;

  const uint8_t *cur_pos_;
  size_t cur_idx_;

  // Items that have been decoded but not yet yielded
  // to the user. The next one to be yielded is at the
  // *end* of the vector!
  std::vector<uint32_t> pending_;

  // Min Length of a header. (prefix + 4 tags)
  static const size_t kMinHeaderSize = 5;
};

} // namespace cfile
} // namespace kudu
#endif
