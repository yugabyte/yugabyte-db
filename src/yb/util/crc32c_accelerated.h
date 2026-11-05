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

#include <cstddef>
#include <cstdint>

namespace yb::crc {

class Crc32cEngine {
 public:
  virtual ~Crc32cEngine() = default;

  // Extends crc by length bytes. The caller applies the initial and final inversion of CRC32C.
  virtual void Extend(uint32_t* crc, const void* bytes, size_t length) const = 0;
};

// Returns nullptr when the CPU does not support the accelerated implementation.
// Crc32c and Crc32Accumulator use it when it is available.
Crc32cEngine* TryNewCRC32AcceleratedX86ARMCombined();

} // namespace yb::crc
