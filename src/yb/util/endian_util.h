// Copyright (c) YugabyteDB, Inc.
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

#include "yb/gutil/endian.h"

#include "yb/util/slice.h"
#include "yb/util/status.h"

namespace yb {

// Write value encoded with specified endian and update output address.
template <class T, class Endian, class SingleByteType>
std::enable_if_t<sizeof(SingleByteType) == 1, void> Write(SingleByteType*& p, T v) {
  Store<T, Endian>(p, v);
  p += sizeof(T);
}

// Read value encoded with specified endian and update input address.
template <class T, class Endian, class SingleByteType>
std::enable_if_t<sizeof(SingleByteType) == 1, T> Read(SingleByteType*& p) {
  auto ptr = p;
  p += sizeof(T);
  return Load<T, Endian>(ptr);
}

// Read value encoded with specified endian from slice and remove read prefix.
// Return failure if value cannot be read from the slice.
template <class T, class Endian>
Result<T> CheckedRead(Slice& slice) {
  if (slice.size() < sizeof(T)) {
    return STATUS_FORMAT(
        Corruption, "Not enough bytes to read: $0, need $1", slice.size(), sizeof(T));
  }

  auto ptr = slice.data();
  slice.RemovePrefix(sizeof(T));
  return Load<T, Endian>(ptr);
}

template <class T, class Endian>
Result<T> CheckedReadFull(Slice& slice) {
  auto result = CheckedRead<T, Endian>(slice);
  if (result.ok() && !slice.empty()) {
    return STATUS_FORMAT(Corruption, "Extra data: $0", slice.ToDebugHexString());
  }
  return result;
}

}  // namespace yb
