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
//
// Dictionary encoding for strings. There is only one dictionary block
// for all the data blocks within a cfile.
// layout for dictionary encoded block:
// Either header + embedded codeword block, which can be encoded with any
//        int blockbuilder, when mode_ = kCodeWordMode.
// Or     header + embedded StringPlainBlock, when mode_ = kPlainStringMode.
// Data blocks start with mode_ = kCodeWordMode, when the the size of dictionary
// block go beyond the option_->block_size, the subsequent data blocks will switch
// to string plain block automatically.

// You can embed any int block builder encoding formats, such as group-varint,
// bitshuffle. Currently, we use bitshuffle builder for codewords.
//
// To use other block builder/decoder, just make sure that BlockDecoder has
// interface CopyNextValuesToArray(size_t*, uint8_t*). To do that, just replace
// BShufBuilder/Decoder is ok.
//
//
#ifndef YB_CFILE_BINARY_DICT_BLOCK_H
#define YB_CFILE_BINARY_DICT_BLOCK_H

#include <string>
#include <unordered_map>
#include <vector>

#include "yb/cfile/block_encodings.h"
#include "yb/cfile/block_pointer.h"
#include "yb/cfile/cfile.pb.h"
#include "yb/cfile/binary_plain_block.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/stringpiece.h"
#include "yb/util/faststring.h"
#include "yb/util/memory/arena.h"

namespace yb {
class Arena;
namespace cfile {

struct WriterOptions;

// Header Mode type
enum DictEncodingMode {
  DictEncodingMode_min = 1,
  kCodeWordMode = 1,
  kPlainBinaryMode = 2,
  DictEncodingMode_max = 2
};

class BinaryDictBlockBuilder : public BlockBuilder {
 public:
  explicit BinaryDictBlockBuilder(const WriterOptions* options);

  bool IsBlockFull(size_t limit) const OVERRIDE;

  // Append the dictionary block for the current cfile to the end of the cfile and set the footer
  // accordingly.
  CHECKED_STATUS AppendExtraInfo(CFileWriter* c_writer, CFileFooterPB* footer) OVERRIDE;

  int Add(const uint8_t* vals, size_t count) OVERRIDE;

  Slice Finish(rowid_t ordinal_pos) OVERRIDE;

  void Reset() OVERRIDE;

  size_t Count() const OVERRIDE;

  CHECKED_STATUS GetFirstKey(void* key) const OVERRIDE;

  static const size_t kMaxHeaderSize = sizeof(uint32_t) * 1;

 private:
  int AddCodeWords(const uint8_t* vals, size_t count);

  faststring buffer_;
  bool finished_;
  const WriterOptions* options_;

  gscoped_ptr<BlockBuilder> data_builder_;

  // dict_block_, dictionary_, dictionary_strings_arena_
  // is related to the dictionary block (one per cfile).
  // They should NOT be clear in the Reset() method.
  BinaryPlainBlockBuilder dict_block_;

  std::unordered_map<StringPiece, uint32_t, GoodFastHash<StringPiece> > dictionary_;
  // Memory to hold the actual content for strings in the dictionary_.
  //
  // The size of it should be bigger than the size limit for dictionary block
  // (e.g option_->block_size).
  //
  // Currently, it can hold at most 64MB content.
  Arena dictionary_strings_arena_;

  DictEncodingMode mode_;

  // First key when mode_ = kCodeWodeMode
  faststring first_key_;
};

class CFileIterator;

class BinaryDictBlockDecoder : public BlockDecoder {
 public:
  explicit BinaryDictBlockDecoder(Slice slice, CFileIterator* iter);

  virtual CHECKED_STATUS ParseHeader() OVERRIDE;
  virtual void SeekToPositionInBlock(uint pos) OVERRIDE;
  virtual CHECKED_STATUS SeekAtOrAfterValue(const void* value, bool* exact_match) OVERRIDE;
  CHECKED_STATUS CopyNextValues(size_t* n, ColumnDataView* dst) OVERRIDE;

  virtual bool HasNext() const OVERRIDE {
    return data_decoder_->HasNext();
  }

  virtual size_t Count() const OVERRIDE {
    return data_decoder_->Count();
  }

  virtual size_t GetCurrentIndex() const OVERRIDE {
    return data_decoder_->GetCurrentIndex();
  }

  virtual rowid_t GetFirstRowId() const OVERRIDE {
    return data_decoder_->GetFirstRowId();
  }

  static const size_t kMinHeaderSize = sizeof(uint32_t) * 1;

 private:
  CHECKED_STATUS CopyNextDecodeStrings(size_t* n, ColumnDataView* dst);

  Slice data_;
  bool parsed_;

  // Dictionary block decoder
  BinaryPlainBlockDecoder* dict_decoder_;

  gscoped_ptr<BlockDecoder> data_decoder_;

  DictEncodingMode mode_;

  // buffer to hold the codewords, needed by CopyNextDecodeStrings()
  faststring codeword_buf_;

};

} // namespace cfile
} // namespace yb

// Defined for tight_enum_test_cast<> -- has to be defined outside of any namespace.
MAKE_ENUM_LIMITS(yb::cfile::DictEncodingMode, yb::cfile::DictEncodingMode_min,
                 yb::cfile::DictEncodingMode_max);

#endif // YB_CFILE_BINARY_DICT_BLOCK_H
