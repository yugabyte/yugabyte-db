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

#include <glog/logging.h>
#include <algorithm>

#include "kudu/cfile/block_compression.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/util/coding.h"
#include "kudu/util/coding-inl.h"

namespace kudu {
namespace cfile {

using std::vector;

CompressedBlockBuilder::CompressedBlockBuilder(const CompressionCodec* codec,
                                               size_t size_limit)
  : codec_(DCHECK_NOTNULL(codec)),
    compressed_size_limit_(size_limit) {
}

Status CompressedBlockBuilder::Compress(const Slice& data, Slice *result) {
  vector<Slice> v;
  v.push_back(data);
  return Compress(v, result);
}

Status CompressedBlockBuilder::Compress(const vector<Slice> &data_slices, Slice *result) {
  size_t data_size = 0;
  for (const Slice& data : data_slices) {
    data_size += data.size();
  }

  // Ensure that the buffer for header + compressed data is large enough
  size_t max_compressed_size = codec_->MaxCompressedLength(data_size);
  if (max_compressed_size > compressed_size_limit_) {
    return Status::InvalidArgument(
      StringPrintf("estimated max size %lu is greater than the expected %lu",
        max_compressed_size, compressed_size_limit_));
  }

  buffer_.resize(kHeaderReservedLength + max_compressed_size);

  // Compress
  size_t compressed_size;
  RETURN_NOT_OK(codec_->Compress(data_slices,
                                 buffer_.data() + kHeaderReservedLength, &compressed_size));

  // Set up the header
  InlineEncodeFixed32(&buffer_[0], compressed_size);
  InlineEncodeFixed32(&buffer_[4], data_size);
  *result = Slice(buffer_.data(), compressed_size + kHeaderReservedLength);

  return Status::OK();
}

CompressedBlockDecoder::CompressedBlockDecoder(const CompressionCodec* codec,
                                               size_t size_limit)
  : codec_(DCHECK_NOTNULL(codec)),
    uncompressed_size_limit_(size_limit) {
}

Status CompressedBlockDecoder::ValidateHeader(const Slice& data, uint32_t *uncompressed_size) {
  // Check if the on-disk size is correct.
  if (data.size() < CompressedBlockBuilder::kHeaderReservedLength) {
    return Status::Corruption(
      StringPrintf("data size %lu is not enough to contains the header. "
        "required %lu, buffer",
        data.size(), CompressedBlockBuilder::kHeaderReservedLength),
        data.ToDebugString(50));
  }

  // Decode the header
  uint32_t compressed_size = DecodeFixed32(data.data());
  *uncompressed_size = DecodeFixed32(data.data() + 4);

  // Check if the on-disk data size matches with the buffer
  if (data.size() != (CompressedBlockBuilder::kHeaderReservedLength + compressed_size)) {
    return Status::Corruption(
      StringPrintf("compressed size %u does not match remaining length in buffer %lu, buffer",
        compressed_size, data.size() - CompressedBlockBuilder::kHeaderReservedLength),
        data.ToDebugString(50));
  }

  // Check if uncompressed size seems to be reasonable
  if (*uncompressed_size > uncompressed_size_limit_) {
    return Status::Corruption(
      StringPrintf("uncompressed size %u overflows the maximum length %lu, buffer",
        compressed_size, uncompressed_size_limit_), data.ToDebugString(50));
  }

  return Status::OK();
}

Status CompressedBlockDecoder::UncompressIntoBuffer(const Slice& data, uint8_t* dst,
                                                    uint32_t uncompressed_size) {
  Slice compressed = data;
  compressed.remove_prefix(CompressedBlockBuilder::kHeaderReservedLength);
  RETURN_NOT_OK(codec_->Uncompress(compressed, dst, uncompressed_size));

  return Status::OK();
}

Status CompressedBlockDecoder::Uncompress(const Slice& data, Slice *result) {
  // Decode the header
  uint32_t uncompressed_size;
  RETURN_NOT_OK(ValidateHeader(data, &uncompressed_size));

  // Allocate the buffer for the uncompressed data and uncompress
  ::gscoped_array<uint8_t> buffer(new uint8_t[uncompressed_size]);
  RETURN_NOT_OK(UncompressIntoBuffer(data, buffer.get(), uncompressed_size));
  *result = Slice(buffer.release(), uncompressed_size);

  return Status::OK();
}

} // namespace cfile
} // namespace kudu
