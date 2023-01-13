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

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/status.h"

#include "yb/util/logging.h"
#include "yb/util/slice.h"
#include "yb/util/tostring.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace rocksdb {

// Stores component sizes based on pattern described in FindComponents inside block_builder.cc.
struct ComponentSizes {
  size_t prev_key_non_shared_1_size = 0;
  size_t non_shared_1_size = 0;
  size_t shared_middle_size = 0;
  size_t prev_key_non_shared_2_size = 0;
  size_t non_shared_2_size = 0;

  static inline yb::Result<size_t> SafeAdd(size_t a, size_t b) {
    size_t sum = a + b;
    SCHECK_GE(sum, a, InternalError, yb::Format("Overflow: $0 + $1", a, b));
    return sum;
  }

  inline yb::Result<size_t> DebugGetPrevKeyPartSize() const {
    return SafeAdd(
        VERIFY_RESULT(SafeAdd(prev_key_non_shared_1_size, shared_middle_size)),
        prev_key_non_shared_2_size);
  }

  inline yb::Result<size_t> DebugGetCurKeyPartSize() const {
    return SafeAdd(
        VERIFY_RESULT(SafeAdd(non_shared_1_size, shared_middle_size)), non_shared_2_size);
  }

  inline Status DebugVerify(const Slice& prev_key_part, const Slice& cur_key_part) const {
    SCHECK_EQ(
        VERIFY_RESULT(DebugGetPrevKeyPartSize()), prev_key_part.size(), InternalError,
        "Prev key part size doesn't match");
    SCHECK_EQ(
        VERIFY_RESULT(DebugGetCurKeyPartSize()), cur_key_part.size(), InternalError,
        "Current key part size doesn't match");
    SCHECK(
        shared_middle_size > 0 || non_shared_2_size == 0, InternalError,
        "No shared middle, but non-empty non_shared_2");
    return Status::OK();
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        non_shared_1_size, prev_key_non_shared_1_size, shared_middle_size, non_shared_2_size,
        prev_key_non_shared_2_size);
  }
};

inline void ValidateThreeSharedPartsSizes(
    size_t shared_prefix_size, size_t last_internal_component_reuse_size,
    bool is_last_internal_component_inc, const ComponentSizes& rest_sizes,
    const size_t prev_key_size, const size_t key_size) {
  DCHECK(
      (last_internal_component_reuse_size == 0 && !is_last_internal_component_inc) ||
      (last_internal_component_reuse_size == kLastInternalComponentSize));
  DCHECK_EQ(
      shared_prefix_size + rest_sizes.prev_key_non_shared_1_size + rest_sizes.shared_middle_size +
          rest_sizes.prev_key_non_shared_2_size + last_internal_component_reuse_size,
      prev_key_size)
      << "shared_prefix_size: " << shared_prefix_size << " " << rest_sizes.ToString()
      << " last_internal_component_reuse_size: " << last_internal_component_reuse_size
      << " prev_key_size: " << prev_key_size;
  DCHECK_EQ(
      shared_prefix_size + rest_sizes.non_shared_1_size + rest_sizes.shared_middle_size +
          rest_sizes.non_shared_2_size + last_internal_component_reuse_size,
      key_size)
      << "shared_prefix_size: " << shared_prefix_size << " " << rest_sizes.ToString()
      << " last_internal_component_reuse_size: " << last_internal_component_reuse_size
      << " cur_key_size: " << key_size;
}

// Takes the components sizes computed by ThreeSharedPartsEncoder::Calculate and encodes into
// buffer.
// Sizes encoding format is described inside the function body.
inline void EncodeThreeSharedPartsSizes(
    const size_t shared_prefix_size,
    const size_t last_internal_component_reuse_size,
    const bool is_last_internal_component_inc,
    const ComponentSizes& rest_sizes,
    const size_t prev_key_size,
    const size_t key_size,
    const size_t value_size,
    std::string* buffer) {
  ValidateThreeSharedPartsSizes(
      shared_prefix_size, last_internal_component_reuse_size, is_last_internal_component_inc,
      rest_sizes, prev_key_size, key_size);

  const int64_t non_shared_1_size_delta =
      static_cast<int64_t>(rest_sizes.non_shared_1_size) -
      static_cast<int64_t>(rest_sizes.prev_key_non_shared_1_size);
  const int64_t non_shared_2_size_delta =
      static_cast<int64_t>(rest_sizes.non_shared_2_size) -
      static_cast<int64_t>(rest_sizes.prev_key_non_shared_2_size);

  DVLOG_WITH_FUNC(4) << "shared_prefix_size: " << shared_prefix_size
            << " other_components_sizes: " << rest_sizes.ToString()
            << " non_shared_1_size_delta: " << non_shared_1_size_delta
            << " non_shared_2_size_delta: " << non_shared_2_size_delta
            << " last_internal_component_reuse_size: " << last_internal_component_reuse_size
            << " key_size_delta: "
            << static_cast<int64_t>(key_size) - static_cast<int64_t>(prev_key_size);

  // This is the most frequent case we are optimizing for based on our custom docdb encoding format.
  const bool is_frequent_case_1 =
      (last_internal_component_reuse_size > 0) &&
      (rest_sizes.non_shared_1_size == 1) && (rest_sizes.non_shared_2_size == 1) &&
      (non_shared_1_size_delta == 0) && (non_shared_2_size_delta == 0);

  // We have different cases encoded differently into the buffer:
  // 1) is_frequent_case_1 == true (most of non-restart keys):
  //   <encoded_1><shared_prefix_size>
  // encoded_1 is: <value_size, is_last_internal_component_inc, is_frequent_case_1>
  // We encode these two flags together with the value_size, because if value_size is less than 32
  // bytes, then encoded_1 will still be encoded as 1 byte. Otherwise one more byte is negligible
  // comparing to size of the value that is not delta compressed.
  //
  // 2) is_frequent_case_1 == false: see below inside the code.
  const auto encoded_1 =
      (value_size << 2) | (is_last_internal_component_inc << 1) | is_frequent_case_1;
  FastPutVarint64(buffer, encoded_1);
  if (is_frequent_case_1) {
    PutVarint32(buffer, static_cast<uint32_t>(shared_prefix_size));
  } else {
    const bool is_something_reused = rest_sizes.non_shared_1_size < key_size;
#ifndef NDEBUG
    const auto reused_size =
        shared_prefix_size + rest_sizes.shared_middle_size + last_internal_component_reuse_size;
    if (is_something_reused) {
      DCHECK_GT(reused_size, 0);
    } else {
      DCHECK_EQ(reused_size, 0);
      DCHECK_EQ(rest_sizes.non_shared_1_size, key_size);
    }
#endif // NDEBUG
    if (is_something_reused) {
      // 2.1) something is reused from the prev key:
      if (last_internal_component_reuse_size > 0
          && non_shared_1_size_delta == 0
          && (non_shared_2_size_delta == 0 || non_shared_2_size_delta == 1)
          && rest_sizes.non_shared_1_size < 8
          && rest_sizes.non_shared_2_size < 4) {
        // 2.1.1) last internal component is reused, non_shared_1_size_delta == 0,
        //   non_shared_1_size_delta == 0 or 1, non_shared_1_size and non_shared_2_size fits within
        //   encoded_2 (one byte) in a following way:
        //     bit 0: 1
        //     bit 1: 0
        //     bit 2: non_shared_2_size_delta == 1
        //     bits 3-5: non_shared_1_size
        //     bits 6-7: non_shared_2_size
        // All sizes are encoded as:
        // <encoded_1><encoded_2><shared_prefix_size>
        const auto encoded_2 = 0b01 | ((non_shared_2_size_delta == 1) << 2) |
                               (rest_sizes.non_shared_1_size << 3) |
                               (rest_sizes.non_shared_2_size << 6);
        DCHECK_LE(encoded_2, 0xff);
        DVLOG_WITH_FUNC(4) << "encoded_2: " << encoded_2;
        PutFixed8(buffer, encoded_2);
      } else {
        // 2.1.2) rest of the cases when we reuse something from prev key are encoded as:
        //   <encoded_1><encoded_2><non_shared_1_size>[<non_shared_1_size_delta>]
        //     [<non_shared_2_size>][<non_shared_2_size_delta>]<shared_prefix_size>
        //
        //   encoded_2 (one byte) is:
        //     bit 0: 1
        //     bit 1: 1
        //     bit 2: whether we reuse last internal component
        //     bit 3: non_shared_1_size_delta != 0
        //     bit 4: non_shared_2_size != 0
        //     bit 5: non_shared_2_size_delta != 0
        //     bits 6-7: not used
        const auto encoded_2 = 0b11 | ((last_internal_component_reuse_size > 0) << 2) |
                               ((non_shared_1_size_delta != 0) << 3) |
                               ((rest_sizes.non_shared_2_size != 0) << 4) |
                               ((non_shared_2_size_delta != 0) << 5);
        DCHECK_LE(encoded_2, 0xff);
        DVLOG_WITH_FUNC(4) << "encoded_2: " << encoded_2;
        PutFixed8(buffer, encoded_2);

        PutVarint32(buffer, static_cast<uint32_t>(rest_sizes.non_shared_1_size));
        if (non_shared_1_size_delta != 0) {
          PutSignedVarint(buffer, non_shared_1_size_delta);
        }
        if (rest_sizes.non_shared_2_size != 0) {
          PutVarint32(buffer, static_cast<uint32_t>(rest_sizes.non_shared_2_size));
        }
        if (non_shared_2_size_delta != 0) {
          PutSignedVarint(buffer, non_shared_2_size_delta);
        }
      }
      PutVarint32(buffer, static_cast<uint32_t>(shared_prefix_size));
    } else {
      // 2.2) nothing is reused from the prev key (mostly restart keys):
      if (key_size < (1 << 7) && key_size > 0) {
        // 0 < key_size < 128
        // <encoded_1><encoded_2>
        //   encoded_2 (one byte) is:
        //     bit 0: 0
        //     bits 1-7: key_size
        const auto encoded_2 = 0b0 | (key_size << 1);
        DCHECK_LE(encoded_2, 0xff);
        DVLOG_WITH_FUNC(4) << "encoded_2: " << encoded_2;
        PutFixed8(buffer, encoded_2);
      } else {
        // key_size == 0 || key_size > 128
        // <encoded_1><encoded_2 = 0><key_size>
        const auto encoded_2 = 0;
        DVLOG_WITH_FUNC(4) << "encoded_2: " << encoded_2;
        PutFixed8(buffer, encoded_2);
        PutVarint32(buffer, static_cast<uint32_t>(key_size));
      }
    }
  }
}

}  // namespace rocksdb
