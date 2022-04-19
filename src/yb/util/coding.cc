// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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

#include "yb/util/coding.h"

#include "yb/gutil/casts.h"

#include "yb/util/coding-inl.h"

namespace yb {

void PutVarint32(faststring* dst, uint32_t v) {
  uint8_t buf[16];
  uint8_t* ptr = InlineEncodeVarint32(buf, v);
  dst->append(buf, ptr - buf);
}

uint8_t* EncodeVarint64(uint8_t* dst, uint64_t v) {
  static const int B = 128;
  while (v >= B) {
    *(dst++) = (v & (B-1)) | B;
    v >>= 7;
  }
  *(dst++) = static_cast<uint8_t>(v);
  return dst;
}

void PutFixed32(faststring *dst, uint32_t value) {
  InlinePutFixed32(dst, value);
}

void PutFixed64(faststring *dst, uint64_t value) {
  InlinePutFixed64(dst, value);
}

void PutVarint64(faststring *dst, uint64_t v) {
  uint8_t buf[16];
  uint8_t* ptr = EncodeVarint64(buf, v);
  dst->append(buf, ptr - buf);
}

void PutVarint64(boost::container::small_vector_base<uint8_t>* dst, uint64_t value) {
  uint8_t buf[16];
  dst->insert(dst->end(), buf, EncodeVarint64(buf, value));
}

void PutLengthPrefixedSlice(faststring* dst, const Slice& value) {
  PutVarint32(dst, narrow_cast<uint32_t>(value.size()));
  dst->append(value.data(), value.size());
}

void PutFixed32LengthPrefixedSlice(faststring* dst, const Slice& value) {
  PutFixed32(dst, narrow_cast<uint32_t>(value.size()));
  dst->append(value.data(), value.size());
}

int VarintLength(uint64_t v) {
  int len = 1;
  while (v >= 128) {
    v >>= 7;
    len++;
  }
  return len;
}

const uint8_t *GetVarint32PtrFallback(const uint8_t *p,
                                   const uint8_t *limit,
                                   uint32_t* value) {
  uint32_t result = 0;
  for (uint32_t shift = 0; shift <= 28 && p < limit; shift += 7) {
    uint32_t byte = *p;
    p++;
    if (byte & 128) {
      // More bytes are present
      result |= ((byte & 127) << shift);
    } else {
      result |= (byte << shift);
      *value = result;
      return p;
    }
  }
  return nullptr;
}

bool GetVarint32(Slice* input, uint32_t* value) {
  const uint8_t *p = input->data();
  const uint8_t *limit = p + input->size();
  const uint8_t *q = GetVarint32Ptr(p, limit, value);
  if (q == nullptr) {
    return false;
  } else {
    *input = Slice(q, limit - q);
    return true;
  }
}

const uint8_t *GetVarint64Ptr(const uint8_t *p, const uint8_t *limit, uint64_t* value) {
  uint64_t result = 0;
  for (uint32_t shift = 0; shift <= 63 && p < limit; shift += 7) {
    uint64_t byte = *p;
    p++;
    if (byte & 128) {
      // More bytes are present
      result |= ((byte & 127) << shift);
    } else {
      result |= (byte << shift);
      *value = result;
      return p;
    }
  }
  return nullptr;
}

bool GetVarint64(Slice* input, uint64_t* value) {
  const uint8_t *p = input->data();
  const uint8_t *limit = p + input->size();
  const uint8_t *q = GetVarint64Ptr(p, limit, value);
  if (q == nullptr) {
    return false;
  } else {
    *input = Slice(q, limit - q);
    return true;
  }
}

const uint8_t *GetLengthPrefixedSlice(const uint8_t *p, const uint8_t *limit,
                                   Slice* result) {
  uint32_t len = 0;
  p = GetVarint32Ptr(p, limit, &len);
  if (p == nullptr) return nullptr;
  if (p + len > limit) return nullptr;
  *result = Slice(p, len);
  return p + len;
}

bool GetLengthPrefixedSlice(Slice* input, Slice* result) {
  uint32_t len = 0;
  if (GetVarint32(input, &len) &&
      input->size() >= len) {
    *result = Slice(input->data(), len);
    input->remove_prefix(len);
    return true;
  } else {
    return false;
  }
}

}  // namespace yb
