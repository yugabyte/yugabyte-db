//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include <sys/mman.h>

#include <cstdint>
#include <utility>

#include "yb/util/slice.h"

namespace rocksdb {

// An RAII wrapper for mmaped memory
class MemMapping {
 public:
  static constexpr bool kHugePageSupported =
#if defined(MAP_HUGETLB) || defined(FILE_MAP_LARGE_PAGES)
      true;
#else
      false;
#endif

  // Allocate memory requesting to be backed by huge pages
  static MemMapping AllocateHuge(size_t length);

  // Allocate memory that is only lazily mapped to resident memory and
  // guaranteed to be zero-initialized. Note that some platforms like
  // Linux allow memory over-commit, where only the used portion of memory
  // matters, while other platforms require enough swap space (page file) to
  // back the full mapping.
  static MemMapping AllocateLazyZeroed(size_t length);

  // No copies
  MemMapping(const MemMapping&) = delete;
  MemMapping& operator=(const MemMapping&) = delete;
  // Move
  MemMapping(MemMapping&&) noexcept;
  MemMapping& operator=(MemMapping&&) noexcept;

  // Releases the mapping
  ~MemMapping();

  inline void* Get() const { return addr_; }
  // The requested length of the mapping, which may be smaller than the
  // actual usable length
  inline size_t Length() const { return length_; }

  // Return the mapping as a Slice (zero-copy view of the mapped memory)
  inline Slice AsSlice() const {
    return Slice(static_cast<const char*>(addr_), length_);
  }

 private:
  MemMapping() {}

  // The mapped memory, or nullptr on failure / not supported
  void* addr_ = nullptr;
  // The known usable number of bytes starting at that address
  size_t length_ = 0;

#ifdef OS_WIN
  HANDLE page_file_handle_ = NULL;
#endif  // OS_WIN

  static MemMapping AllocateAnonymous(size_t length, bool huge);
};

// Simple MemMapping wrapper that presents the memory as an array of T.
// For example,
//  TypedMemMapping<uint64_t> arr = MemMapping::AllocateLazyZeroed(num_bytes);
template <typename T>
class TypedMemMapping : public MemMapping {
 public:
  /*implicit*/ TypedMemMapping(MemMapping&& v) noexcept  // NOLINT(runtime/explicit)
      : MemMapping(std::move(v)) {}
  TypedMemMapping& operator=(MemMapping&& v) noexcept {
    MemMapping& base = *this;
    base = std::move(v);
    return *this;
  }

  inline T* Get() const { return static_cast<T*>(MemMapping::Get()); }
  inline size_t Count() const { return MemMapping::Length() / sizeof(T); }

  inline T& operator[](size_t index) const { return Get()[index]; }
};

}  // namespace rocksdb
