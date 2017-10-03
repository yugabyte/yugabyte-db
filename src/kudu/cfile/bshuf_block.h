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
// Make use of bitshuffle and lz4 to encode the fixed size
// type blocks, such as UINT8, INT8, UINT16, INT16,
//                      UINT32, INT32, FLOAT, DOUBLE.
// Reference:
// https://github.com/kiyo-masui/bitshuffle.git
#ifndef KUDU_CFILE_BSHUF_BLOCK_H
#define KUDU_CFILE_BSHUF_BLOCK_H

#include <algorithm>
#include <stdint.h>
#include <bitshuffle.h>

#include "kudu/cfile/block_encodings.h"
#include "kudu/cfile/cfile_util.h"
#include "kudu/common/columnblock.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/coding.h"
#include "kudu/util/coding-inl.h"
#include "kudu/util/hexdump.h"

namespace kudu {
namespace cfile {


// Log a FATAL error message and exit.
void AbortWithBitShuffleError(int64_t val) ATTRIBUTE_NORETURN;

// BshufBlockBuilder bitshuffles and compresses the bits of fixed
// size type blocks with lz4.
//
// Header includes:
// 1. ordinal of the first element within the block (uint32_t, little endian).
// 2. num of element within the block (uint32_t, little endian).
// 3. compressed_size, including the header size (uint32_t, little endian).
// 4. number of element after padding, padding is needed to meet the requirement
//    by bitshuffle library that the number of element in the block must be
//    multiple of 8. That means some psudo elements are appended at the end of the
//    block if necessary (uint32_t, little endian).
// 5. the size of the elements in bytes, as actually encoded. In the case that all of the
//    data in a block can fit into a smaller integer type, then we may choose to encode
//    that smaller type to save CPU costs. This is currently done only for the UINT32
//    block type.  (uint32_t, little endian).
template<DataType Type>
class BShufBlockBuilder : public BlockBuilder {
 public:
  explicit BShufBlockBuilder(const WriterOptions* options)
    : count_(0),
      options_(options) {
    Reset();
  }

  void Reset() OVERRIDE {
    count_ = 0;
    data_.clear();
    data_.reserve(options_->storage_attributes.cfile_block_size);
    buffer_.clear();
    buffer_.resize(kMaxHeaderSize);
  }

  bool IsBlockFull(size_t limit) const OVERRIDE {
    return EstimateEncodedSize() > limit;
  }

  int Add(const uint8_t* vals_void, size_t count) OVERRIDE {
    const CppType* vals = reinterpret_cast<const CppType* >(vals_void);
    int added = 0;
    // If the current block is full, stop adding more items.
    while (!IsBlockFull(options_->storage_attributes.cfile_block_size) && added < count) {
      const uint8_t* ptr = reinterpret_cast<const uint8_t*>(vals);
      data_.append(ptr, size_of_type);
      vals++;
      added++;
      count_++;
    }
    return added;
  }

  size_t Count() const OVERRIDE {
    return count_;
  }

  Status GetFirstKey(void* key) const OVERRIDE {
    if (count_ == 0) {
      return Status::NotFound("no keys in data block");
    }
    memcpy(key, &data_[0], size_of_type);
    return Status::OK();
  }

  Slice Finish(rowid_t ordinal_pos) OVERRIDE {
    return Finish(ordinal_pos, size_of_type);
  }

 private:
  size_t EstimateEncodedSize() const {
    int num = count_ + NumOfPaddingNeeded();
    // The result of bshuf_compress_lz4_bound(num, size_of_type, 0)
    // is always bigger than the original size (num * size_of_type).
    // However, the compression ratio in most cases is larger than 1,
    // Therefore, using the original size may be more accurate and
    // cause less overhead.
    return kMaxHeaderSize + num * size_of_type;
  }

  uint32_t NumOfPaddingNeeded() const {
    return (count_ % 8 == 0) ? 0 : 8 - (count_ % 8);
  }

  Slice Finish(rowid_t ordinal_pos, int final_size_of_type) {
    data_.resize(kMaxHeaderSize + final_size_of_type * count_);

    // Do padding so that the input num of element is multiple of 8.
    uint32_t num_of_padding = NumOfPaddingNeeded() * final_size_of_type;
    for (int i = 0; i < num_of_padding; i++) {
      data_.push_back(0);
    }

    int num_elems_after_padding = count_ + NumOfPaddingNeeded();
    buffer_.resize(kMaxHeaderSize +
                   bshuf_compress_lz4_bound(num_elems_after_padding, final_size_of_type, 0));

    InlineEncodeFixed32(&buffer_[0], ordinal_pos);
    InlineEncodeFixed32(&buffer_[4], count_);
    int64_t bytes = bshuf_compress_lz4(data_.data(), &buffer_[kMaxHeaderSize],
                                    num_elems_after_padding, final_size_of_type, 0);
    if (PREDICT_FALSE(bytes < 0)) {
      // This means the bitshuffle function fails.
      // Ideally, this should not happen.
      AbortWithBitShuffleError(bytes);
      // It does not matter what will be returned here,
      // since we have logged fatal in AbortWithBitShuffleError().
      return Slice();
    }
    InlineEncodeFixed32(&buffer_[8], kMaxHeaderSize + bytes);
    InlineEncodeFixed32(&buffer_[12], num_elems_after_padding);
    InlineEncodeFixed32(&buffer_[16], final_size_of_type);
    return Slice(buffer_.data(), kMaxHeaderSize + bytes);
  }

  // Length of a header.
  static const size_t kMaxHeaderSize = sizeof(uint32_t) * 5;
  typedef typename TypeTraits<Type>::cpp_type CppType;
  enum {
    size_of_type = TypeTraits<Type>::size
  };

  faststring data_;
  faststring buffer_;
  uint32_t count_;
  const WriterOptions* options_;
};

template<>
Slice BShufBlockBuilder<UINT32>::Finish(rowid_t ordinal_pos);

template<DataType Type>
class BShufBlockDecoder : public BlockDecoder {
 public:
  explicit BShufBlockDecoder(Slice slice)
      : data_(std::move(slice)),
        parsed_(false),
        ordinal_pos_base_(0),
        num_elems_(0),
        compressed_size_(0),
        num_elems_after_padding_(0),
        cur_idx_(0) {
  }

  Status ParseHeader() OVERRIDE {
    CHECK(!parsed_);
    if (data_.size() < kHeaderSize) {
      return Status::Corruption(
        strings::Substitute("not enough bytes for header: bitshuffle block header "
          "size ($0) less than expected header length ($1)",
          data_.size(), kHeaderSize));
    }

    ordinal_pos_base_  = DecodeFixed32(&data_[0]);
    num_elems_         = DecodeFixed32(&data_[4]);
    compressed_size_   = DecodeFixed32(&data_[8]);
    if (compressed_size_ != data_.size()) {
      return Status::Corruption("Size Information unmatched");
    }
    num_elems_after_padding_ = DecodeFixed32(&data_[12]);
    if (num_elems_after_padding_ != num_elems_ + NumOfPaddingNeeded()) {
      return Status::Corruption("num of element information corrupted");
    }
    size_of_elem_ = DecodeFixed32(&data_[16]);
    switch (size_of_elem_) {
      case 1:
      case 2:
      case 4:
      case 8:
        break;
      default:
        return Status::Corruption(strings::Substitute("invalid size_of_elem: $0", size_of_elem_));
    }

    // Currently, only the UINT32 block encoder supports expanding size:
    if (PREDICT_FALSE(Type != UINT32 && size_of_elem_ != size_of_type)) {
      return Status::Corruption(strings::Substitute("size_of_elem $0 != size_of_type $1",
                                                    size_of_elem_, size_of_type));
    }
    if (PREDICT_FALSE(size_of_elem_ > size_of_type)) {
      return Status::Corruption(strings::Substitute("size_of_elem $0 > size_of_type $1",
                                                    size_of_elem_, size_of_type));
    }

    RETURN_NOT_OK(Expand());

    parsed_ = true;
    return Status::OK();
  }

  void SeekToPositionInBlock(uint pos) OVERRIDE {
    CHECK(parsed_) << "Must call ParseHeader()";
    if (PREDICT_FALSE(num_elems_ == 0)) {
      DCHECK_EQ(0, pos);
      return;
    }

    DCHECK_LE(pos, num_elems_);
    cur_idx_ = pos;
  }

  Status SeekAtOrAfterValue(const void* value_void, bool* exact) OVERRIDE {
    CppType target = *reinterpret_cast<const CppType*>(value_void);
    int32_t left = 0;
    int32_t right = num_elems_;
    while (left != right) {
      uint32_t mid = (left + right) / 2;
      CppType mid_key = Decode<CppType>(
            &decoded_[mid * size_of_type]);
      if (mid_key == target) {
        cur_idx_ = mid;
        *exact = true;
        return Status::OK();
      } else if (mid_key > target) {
        right = mid;
      } else {
        left = mid + 1;
      }
    }

    *exact = false;
    cur_idx_ = left;
    if (cur_idx_ == num_elems_) {
      return Status::NotFound("after last key in block");
    }
    return Status::OK();
  }

  Status CopyNextValues(size_t* n, ColumnDataView* dst) OVERRIDE {
    DCHECK_EQ(dst->stride(), sizeof(CppType));
    return CopyNextValuesToArray(n, dst->data());
  }

  // Copy the codewords to a temporary buffer.
  // This API provides a more convenient way for the dictionary decoder to copy out
  // integer codewords and then look up the strings. If we use the CopyNextValuesToArray()
  // instead of CopyNextValues(), we do not need to create ColumnDataView and ColumnBlock
  // object to wrap around the uint8_t pointer.
  Status CopyNextValuesToArray(size_t* n, uint8_t* array) {
    DCHECK(parsed_);
    if (PREDICT_FALSE(*n == 0 || cur_idx_ >= num_elems_)) {
      *n = 0;
      return Status::OK();
    }

    size_t max_fetch = std::min(*n, static_cast<size_t>(num_elems_ - cur_idx_));
    memcpy(array, &decoded_[cur_idx_ * size_of_type], max_fetch * size_of_type);

    *n = max_fetch;
    cur_idx_ += max_fetch;

    return Status::OK();
  }

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
  template<typename T>
  static T Decode(const uint8_t* ptr) {
    T result;
    memcpy(&result, ptr, sizeof(result));
    return result;
  }

  // Return the number of padding elements needed to ensure that the
  // number of elements is a multiple of 8.
  uint32_t NumOfPaddingNeeded() const {
    return KUDU_ALIGN_UP(num_elems_, 8) - num_elems_;
  }

  Status Expand() {
    if (num_elems_ > 0) {
      int64_t bytes;
      decoded_.resize(num_elems_after_padding_ * size_of_type);
      uint8_t* in = const_cast<uint8_t*>(&data_[kHeaderSize]);
      bytes = bshuf_decompress_lz4(in, decoded_.data(), num_elems_after_padding_, size_of_type, 0);
      if (PREDICT_FALSE(bytes < 0)) {
        // Ideally, this should not happen.
        AbortWithBitShuffleError(bytes);
        return Status::RuntimeError("Unshuffle Process failed");
      }
    }
    return Status::OK();
  }

  // Min Length of a header.
  static const size_t kHeaderSize = sizeof(uint32_t) * 5;
  typedef typename TypeTraits<Type>::cpp_type CppType;
  enum {
    size_of_type = TypeTraits<Type>::size
  };

  Slice data_;
  bool parsed_;

  rowid_t ordinal_pos_base_;
  uint32_t num_elems_;
  uint32_t compressed_size_;
  uint32_t num_elems_after_padding_;

  // The size of each decoded element. In the case that the input range was
  // smaller than the type, this may be smaller than 'size_of_type'.
  // Currently, this is always 1, 2, 4, or 8.
  int size_of_elem_;

  size_t cur_idx_;
  faststring decoded_;
};

template<>
Status BShufBlockDecoder<UINT32>::Expand();
template<>
Status BShufBlockDecoder<UINT32>::SeekAtOrAfterValue(const void* value_void, bool* exact);
template<>
Status BShufBlockDecoder<UINT32>::CopyNextValuesToArray(size_t* n, uint8_t* array);


} // namespace cfile
} // namespace kudu
#endif
