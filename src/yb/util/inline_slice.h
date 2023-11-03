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
#pragma once

#include <atomic>
#include <new>

#include <boost/static_assert.hpp>
#include "yb/util/logging.h"

#include "yb/gutil/atomicops.h"
#include "yb/gutil/casts.h"

#include "yb/util/slice.h"

namespace yb {

#if __BYTE_ORDER != __LITTLE_ENDIAN
#error This needs to be ported for big endian
#endif

// Class which represents short strings inline, and stores longer ones
// by instead storing a pointer.
//
// Internal format:
// The buffer must be at least as large as a pointer (eg 8 bytes for 64-bit).
// Let ptr = bit-casting the first 8 bytes as a pointer:
// If buf_[0] < 0xff:
//   buf_[0] == length of stored data
//   buf_[1..1 + buf_[0]] == inline data
// If buf_[0] == 0xff:
//   buf_[1..sizeof(uint8_t *)] == pointer to indirect data, minus the MSB.
//   buf_[sizeof(uint8_t *)..] = unused
// TODO: we could store a prefix of the indirect data in this unused space
// in the future, which might be able to short-circuit some comparisons
//
// The indirect data which is pointed to is stored as a 4 byte length followed by
// the actual data.
//
// This class relies on the fact that the most significant bit of any x86 pointer is
// 0 (i.e pointers only use the bottom 48 bits)
//
// If ATOMIC is true, then this class has the semantics that readers will never see
// invalid pointers, even in the case of concurrent access. However, they _may_ see
// invalid *data*. That is to say, calling 'as_slice()' will always return a slice
// which points to a valid memory region -- the memory region may contain garbage
// but will not cause a segfault on access.
//
// These ATOMIC semantics may seem too loose to be useful, but can be used in
// optimistic concurrency control schemes -- so long as accessing the slice doesn't
// produce a segfault, it's OK to read bad data on a race because the higher-level
// concurrency control will cause a retry.
template<size_t STORAGE_SIZE, bool ATOMIC = false>
class InlineSlice {
 private:
  enum {
    kPointerByteWidth = sizeof(uintptr_t),
    kPointerBitWidth = kPointerByteWidth * 8,
    kMaxInlineData = STORAGE_SIZE - 1
  };

  BOOST_STATIC_ASSERT(STORAGE_SIZE >= kPointerByteWidth);
  BOOST_STATIC_ASSERT(STORAGE_SIZE <= 256);
 public:
  InlineSlice() {
  }

  inline const Slice as_slice() const ATTRIBUTE_ALWAYS_INLINE {
    DiscriminatedPointer dptr = LoadValue();

    if (dptr.is_indirect()) {
      const uint8_t *indir_data = reinterpret_cast<const uint8_t *>(dptr.pointer);
      uint32_t len = *reinterpret_cast<const uint32_t *>(indir_data);
      indir_data += sizeof(uint32_t);
      return Slice(indir_data, (size_t)len);
    } else {
      uint8_t len = dptr.discriminator;
      DCHECK_LE(len, STORAGE_SIZE - 1);
      return Slice(&buf_[1], len);
    }
  }

  template<class ArenaType>
  void set(const Slice &src, ArenaType *alloc_arena) {
    set(src.data(), src.size(), alloc_arena);
  }

  template<class ArenaType>
  void set(const uint8_t *src, size_t len, ArenaType *alloc_arena) {
    if (len <= kMaxInlineData) {
      if (ATOMIC) {
        // If atomic, we need to make sure that we store the discriminator
        // before we copy in any data. Otherwise the data would overwrite
        // part of a pointer and a reader might see an invalid address.
        DiscriminatedPointer dptr;
        dptr.discriminator = len;
        dptr.pointer = 0; // will be overwritten
        // "Acquire" ensures that the later memcpy doesn't reorder above the
        // set of the discriminator bit.
        base::subtle::Acquire_Store(reinterpret_cast<volatile AtomicWord *>(buf_),
                                    bit_cast<uintptr_t>(dptr));
      } else {
        buf_[0] = len;
      }
      memcpy(&buf_[1], src, len);

    } else {
      // TODO: if already indirect and the current storage has enough space, just reuse that.

      // Set up the pointed-to data before setting a pointer to it. This ensures that readers
      // never see a pointer to an invalid region (i.e one without a proper length header).
      void *in_arena = CHECK_NOTNULL(alloc_arena->AllocateBytesAligned(len + sizeof(uint32_t),
                                                                       alignof(uint32_t)));
      *reinterpret_cast<uint32_t *>(in_arena) = narrow_cast<uint32_t>(len);
      memcpy(reinterpret_cast<uint8_t *>(in_arena) + sizeof(uint32_t), src, len);
      set_ptr(in_arena);
    }
  }

 private:
  struct DiscriminatedPointer {
    uint8_t discriminator : 8;
    uintptr_t pointer : 54;

    bool is_indirect() const {
      return discriminator == 0xff;
    }
  };

  DiscriminatedPointer LoadValue() const {
    if (ATOMIC) {
      // Load with "Acquire" semantics -- if we load a pointer, this ensures
      // that we also see the pointed-to data.
      uintptr_t ptr_val = base::subtle::Acquire_Load(
        reinterpret_cast<volatile const AtomicWord *>(buf_));
      return bit_cast<DiscriminatedPointer>(ptr_val);
    } else {
      DiscriminatedPointer ret;
      memcpy(&ret, buf_, sizeof(ret));
      return ret;
    }
  }

  // Set the internal storage to be an indirect pointer to the given
  // address.
  void set_ptr(void *ptr) {
    uintptr_t ptr_int = reinterpret_cast<uintptr_t>(ptr);
    DCHECK_EQ(ptr_int >> (kPointerBitWidth - 8), 0) <<
      "bad pointer (should have 0x00 MSB): " << ptr;

    DiscriminatedPointer dptr;
    dptr.discriminator = 0xff;
    dptr.pointer = ptr_int;

    if (ATOMIC) {
      // Store with "Release" semantics -- this ensures that the pointed-to data
      // is visible to any readers who see this pointer.
      uintptr_t to_store = bit_cast<uintptr_t>(dptr);
      base::subtle::Release_Store(reinterpret_cast<volatile AtomicWord *>(buf_),
                                  to_store);
    } else {
      memcpy(&buf_[0], &dptr, sizeof(dptr));
    }
  }

  uint8_t buf_[STORAGE_SIZE];

} PACKED;

} // namespace yb
