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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
#include "yb/util/bitmap.h"

#include <string>

#include "yb/util/logging.h"

#include "yb/gutil/stringprintf.h"
#include "yb/util/coding.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {

void BitmapChangeBits(uint8_t *bitmap, size_t offset, size_t num_bits, bool value) {
  DCHECK_GT(num_bits, 0);

  size_t start_byte = (offset >> 3);
  size_t end_byte = (offset + num_bits - 1) >> 3;
  int single_byte = (start_byte == end_byte);

  // Change the last bits of the first byte
  size_t left = offset & 0x7;
  size_t right = (single_byte) ? (left + num_bits) : 8;
  uint8_t mask = ((0xff << left) & (0xff >> (8 - right)));
  if (value) {
    bitmap[start_byte++] |= mask;
  } else {
    bitmap[start_byte++] &= ~mask;
  }

  // Nothing left... I'm done
  if (single_byte) {
    return;
  }

  // change the middle bits
  if (end_byte > start_byte) {
    const uint8_t pattern8[2] = { 0x00, 0xff };
    memset(bitmap + start_byte, pattern8[value], end_byte - start_byte);
  }

  // change the first bits of the last byte
  right = offset + num_bits - (end_byte << 3);
  mask = (0xff >> (8 - right));
  if (value) {
    bitmap[end_byte] |= mask;
  } else {
    bitmap[end_byte] &= ~mask;
  }
}

bool BitmapFindFirst(const uint8_t *bitmap, size_t offset, size_t bitmap_size,
                     bool value, size_t *idx) {
  const uint64_t pattern64[2] = { 0xffffffffffffffff, 0x0000000000000000 };
  const uint8_t pattern8[2] = { 0xff, 0x00 };
  size_t bit;

  DCHECK_LE(offset, bitmap_size);

  // Jump to the byte at specified offset
  const uint8_t *p = bitmap + (offset >> 3);
  size_t num_bits = bitmap_size - offset;

  // Find a 'value' bit at the end of the first byte
  if ((bit = offset & 0x7)) {
    for (; bit < 8 && num_bits > 0; ++bit) {
      if (BitmapTest(p, bit) == value) {
        *idx = ((p - bitmap) << 3) + bit;
        return true;
      }

      num_bits--;
    }

    p++;
  }

  // check 64bit at the time for a 'value' bit
  const uint64_t *u64 = (const uint64_t *)p;
  while (num_bits >= 64 && *u64 == pattern64[value]) {
    num_bits -= 64;
    u64++;
  }

  // check 8bit at the time for a 'value' bit
  p = (const uint8_t *)u64;
  while (num_bits >= 8 && *p == pattern8[value]) {
    num_bits -= 8;
    p++;
  }

  // Find a 'value' bit at the beginning of the last byte
  for (bit = 0; num_bits > 0; ++bit) {
    if (BitmapTest(p, bit) == value) {
      *idx = ((p - bitmap) << 3) + bit;
      return true;
    }
    num_bits--;
  }

  return false;
}

std::string BitmapToString(const uint8_t *bitmap, size_t num_bits) {
  std::string s;
  size_t index = 0;
  while (index < num_bits) {
    StringAppendF(&s, "%4zu: ", index);
    for (int i = 0; i < 8 && index < num_bits; ++i) {
      for (int j = 0; j < 8 && index < num_bits; ++j) {
        StringAppendF(&s, "%d", BitmapTest(bitmap, index));
        index++;
      }
      StringAppendF(&s, " ");
    }
    StringAppendF(&s, "\n");
  }
  return s;
}

void OneWayBitmap::Set(size_t bit) {
  if (bit < fully_filled_bits_) {
    return;
  }
  bit -= fully_filled_bits_;
  data_.resize(std::max(data_.size(), bit / kBitsPerElement + 1));
  auto& element = data_[bit / kBitsPerElement];
  auto mask = 1ULL << (bit % kBitsPerElement);
  if ((element & mask) != 0) {
    return;
  }

  element |= mask;
  ++ones_counter_;

  size_t i = 0;
  while (i < data_.size() && data_[i] == kAllOnes) {
    ++i;
  }

  if (i) {
    fully_filled_bits_ += kBitsPerElement * i;
    data_.erase(data_.begin(), data_.begin() + i);
  }
}

bool OneWayBitmap::Test(size_t bit) const {
  if (bit < fully_filled_bits_) {
    return true;
  }
  bit -= fully_filled_bits_;
  size_t idx = bit / kBitsPerElement;
  if (idx >= data_.size()) {
    return false;
  }

  return (data_[idx] & 1ULL << (bit % kBitsPerElement)) != 0;
}

std::string OneWayBitmap::ToString() const {
  std::vector<size_t> values;
  size_t max = fully_filled_bits_ + data_.size() * 8 * sizeof(size_t);
  for (size_t idx = 0; idx != max; ++idx) {
    if (Test(idx)) {
      values.push_back(idx);
    }
  }
  return AsString(values);
}

void OneWayBitmap::EncodeTo(boost::container::small_vector_base<uint8_t>* out) const {
  uint8_t buf[16];
  auto fully_filled_bytes_size = EncodeVarint64(buf, fully_filled_bits_ / 8) - buf;

  size_t len = data_.size();
  size_t body_len = len + fully_filled_bytes_size;
  PutVarint64(out, body_len);
  size_t new_size = out->size() + body_len;
  out->resize(new_size);
  memcpy(out->data() + new_size - body_len, buf, fully_filled_bytes_size);
#ifdef IS_LITTLE_ENDIAN
  memcpy(out->data() + out->size() - len, data_.data(), len);
#else
  #error Not implemented
#endif
}

std::string OneWayBitmap::EncodeToHexString() const {
  boost::container::small_vector<uint8_t, 16> buffer;
  EncodeTo(&buffer);
  return Slice(buffer.data(), buffer.size()).ToDebugHexString();
}

Result<Slice> DecodeBody(Slice* slice) {
  uint64_t body_len;
  if (!GetVarint64(slice, &body_len)) {
    return STATUS(Corruption, "Unable to decode data bytes");
  }
  Slice body(slice->data(), body_len);
  if (body.end() > slice->end()) {
    return STATUS(Corruption, "Not enough body bytes");
  }
  slice->remove_prefix(body.size());
  return body;
}

Status OneWayBitmap::Skip(Slice* slice) {
  return ResultToStatus(DecodeBody(slice));
}

Result<OneWayBitmap> OneWayBitmap::Decode(Slice* slice) {
  OneWayBitmap result;

  auto body = VERIFY_RESULT(DecodeBody(slice));

  uint64_t fully_filled_bytes;
  if (!GetVarint64(&body, &fully_filled_bytes)) {
    return STATUS(Corruption, "Unable to decode fully filled bytes");
  }
  result.fully_filled_bits_ = fully_filled_bytes * 8;
  result.ones_counter_ = result.fully_filled_bits_;
#ifdef IS_LITTLE_ENDIAN
  result.data_.resize(body.size());
  memcpy(result.data_.data(), body.data(), body.size());
#else
  #error Not implemented
#endif
  for (auto v : result.data_) {
    result.ones_counter_ += __builtin_popcount(v);
  }
  // Data was
  if (!result.data_.empty() && result.data_.back() == 0) {
    return STATUS_FORMAT(Corruption, "Last bitmap byte is zero: $0", body.ToDebugHexString());
  }

  return result;
}

} // namespace yb
