//
// Copyright (c) YugaByte, Inc.
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
//

#pragma once

#include "yb/util/logging.h"
#include "yb/util/std_util.h"

namespace rocksdb {

inline const char* DecodeVarint32Ptr(
    const char* p, const char* limit, uint32_t* value, const bool is_non_zero) {
  if (is_non_zero) {
    return GetVarint32Ptr(p, limit, value);
  }
  *value = 0;
  return p;
}

inline const char* DecodeSignedVarint64Ptr(
    const char* p, const char* limit, const char* read_allowed_from, int64_t* value,
    const bool is_non_zero) {
  if (is_non_zero) {
    return GetSignedVarint64Ptr(p, limit, read_allowed_from, value);
  }
  *value = 0;
  return p;
}

// This function decodes key components sizes, flags and value size starting at p.
// limit specifies exclusive upper bound on where we allowed to decode from.
// read_allowed_from specifies exclusive upper bound on where we allowed to read data from (used
// for performance optimization in some cases to read by multi-bytes chunks), but
// still only data before limit will be used for decoding.
//
// Parameters after read_allowed_from are the output parameters filled by decoded values.
// We don't use struct here because of performance reasons, since this function is in hot path.
//
// See EncodeThreeSharedPartsSizes inside block_builder.cc for the sizes & flags encoding
// format description.
//
// Returns either pointer to the next byte to read (containing key/value data) or nullptr in case
// of decoding failure or corruption.
inline const char* DecodeEntryThreeSharedParts(
    const char* p, const char* limit, const char* read_allowed_from, uint32_t* shared_prefix_size,
    uint32_t* non_shared_1_size, int64_t* non_shared_1_size_delta, bool* is_something_shared,
    uint32_t* non_shared_2_size, int64_t* non_shared_2_size_delta,
    uint32_t* shared_last_component_size, uint64_t* shared_last_component_increase,
    uint32_t* value_size) {
  if (limit - p < 2) {
    return nullptr;
  }

  uint64_t encoded_1;
  if ((p = GetVarint64Ptr(p, limit, &encoded_1)) == nullptr) {
    return nullptr;
  }
  *value_size = static_cast<uint32_t>(encoded_1 >> 2);
  *shared_last_component_increase = (encoded_1 & 2) << 7;
  const bool is_frequent_case_1 = encoded_1 & 1;

  if (PREDICT_TRUE(is_frequent_case_1)) {
    p = GetVarint32Ptr(p, limit, shared_prefix_size);
    if (PREDICT_FALSE(!p)) {
      return nullptr;
    }
    *shared_last_component_size = kLastInternalComponentSize;
    *is_something_shared = true;
    *non_shared_1_size = 1;
    *non_shared_1_size_delta = 0;
    *non_shared_2_size = 1;
    *non_shared_2_size_delta = 0;
  } else {
    const uint8_t encoded_2 = *pointer_cast<const uint8_t*>(p++);
    if ((encoded_2 & 1) == 0) {
      DVLOG_WITH_FUNC(4) << "encoded_2: " << static_cast<uint16_t>(encoded_2);
      *is_something_shared = false;
      if (encoded_2 == 0) {
        p = GetVarint32Ptr(p, limit, non_shared_1_size);
        if (PREDICT_FALSE(!p)) {
          return nullptr;
        }
      } else {
        *non_shared_1_size = encoded_2 >> 1;
      }
      *non_shared_1_size_delta = 0;
      *non_shared_2_size = 0;
      *non_shared_2_size_delta = 0;
      *shared_last_component_size = 0;
      *shared_prefix_size = 0;
    } else {
      *is_something_shared = true;
      if ((encoded_2 & 2) == 0) {
        DVLOG_WITH_FUNC(4) << "encoded_2: " << static_cast<uint16_t>(encoded_2);
        *shared_last_component_size = kLastInternalComponentSize;
        *non_shared_1_size_delta = 0;
        *non_shared_2_size_delta = (encoded_2 >> 2) & 1;
        *non_shared_1_size = (encoded_2 >> 3) & 7;
        *non_shared_2_size = (encoded_2 >> 6) & 3;
      } else {
        DVLOG_WITH_FUNC(4) << "encoded_2: " << static_cast<uint16_t>(encoded_2);
        *shared_last_component_size = (encoded_2 & 4) ? kLastInternalComponentSize : 0;
        p = GetVarint32Ptr(p, limit, non_shared_1_size);
        if (PREDICT_FALSE(!p)) {
          return nullptr;
        }
        p = DecodeSignedVarint64Ptr(
            p, limit, read_allowed_from, non_shared_1_size_delta, (encoded_2 & 8) != 0);
        if (PREDICT_FALSE(!p)) {
          return nullptr;
        }
        p = DecodeVarint32Ptr(p, limit, non_shared_2_size, (encoded_2 & 16) != 0);
        if (PREDICT_FALSE(!p)) {
          return nullptr;
        }
        p = DecodeSignedVarint64Ptr(
            p, limit, read_allowed_from, non_shared_2_size_delta, (encoded_2 & 32) != 0);
        if (PREDICT_FALSE(!p)) {
          return nullptr;
        }
      }
      p = GetVarint32Ptr(p, limit, shared_prefix_size);
      if (PREDICT_FALSE(!p)) {
        return nullptr;
      }
    }
  }

  if (PREDICT_FALSE(yb::std_util::cmp_less(
          limit - p, *non_shared_1_size + *non_shared_2_size + *value_size))) {
    return nullptr;
  }
  return p;
}

}  // namespace rocksdb
