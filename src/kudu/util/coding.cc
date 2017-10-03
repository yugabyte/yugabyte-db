// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "kudu/util/coding.h"
#include "kudu/util/coding-inl.h"

namespace kudu {

void PutVarint32(faststring* dst, uint32_t v) {
  uint8_t buf[5];
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
  uint8_t buf[10];
  uint8_t* ptr = EncodeVarint64(buf, v);
  dst->append(buf, ptr - buf);
}

void PutLengthPrefixedSlice(faststring* dst, const Slice& value) {
  PutVarint32(dst, value.size());
  dst->append(value.data(), value.size());
}

void PutFixed32LengthPrefixedSlice(faststring* dst, const Slice& value) {
  PutFixed32(dst, value.size());
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

}  // namespace kudu
