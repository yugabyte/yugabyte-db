// Copyright (c) Yugabyte, Inc.
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

#include "yb/util/slice.h"
#include "yb/util/byte_buffer.h"

namespace yb {

struct SliceParts {
  SliceParts(const Slice* _parts, int _num_parts) :
      parts(_parts), num_parts(_num_parts) { }
  SliceParts() : parts(nullptr), num_parts(0) {}

  template<size_t N>
  SliceParts(const std::array<Slice, N>& input) // NOLINT
      : parts(input.data()), num_parts(N) {
  }

  std::string ToDebugHexString() const;

  // Sum of sizes of all slices.
  size_t SumSizes() const;

  // Copy content of all slice to specified buffer.
  void* CopyAllTo(void* out) const {
    return CopyAllTo(static_cast<char*>(out));
  }

  char* CopyAllTo(char* out) const;

  Slice TheOnlyPart() const;

  // Ensures that the combined data is available as a single slice. If there is zero or one part,
  // no copying is done. Uses the given buffer for two or more parts. The buffer must be alive as
  // long as the returned slice is being used.
  template <size_t SmallLen>
  Slice AsSingleSlice(ByteBuffer<SmallLen>* buffer) const {
    if (num_parts == 0) {
      return Slice();
    }
    if (num_parts == 1) {
      return parts[0];
    }
    size_t size = SumSizes();
    buffer->reserve(size);
    buffer->Truncate(size);
    uint8_t* dest = buffer->mutable_data();
    CopyAllTo(dest);
    return buffer->AsSlice();
  }

  const Slice* parts;
  int num_parts;
};

}  // namespace yb

namespace rocksdb {

typedef yb::SliceParts SliceParts;

}  // namespace rocksdb
