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
#include <memory>
#include <vector>

namespace yb::crc {

// Returns the CRC32C of the data that crc was computed for, followed by the length bytes at data.
using Crc32cExtendFunction = uint32_t (*)(uint32_t crc, const void* data, size_t length);

struct Crc32cImplementation {
  const char* name;
  Crc32cExtendFunction extend;
};

// The CRC32C implementations that this CPU supports, fastest first. The last one is crcutil, which
// needs no particular CPU features. Crc32c and Crc32Accumulator use the first one.
const std::vector<Crc32cImplementation>& Crc32cImplementations();

namespace internal {

// Copied from Abseil, see crc32c/. The engine is compiled for the CPU features it needs, so it may
// be used only after checking that the CPU has them.

class Crc32cEngine {
 public:
  virtual ~Crc32cEngine() = default;

  // Extends crc by length bytes. The caller applies the initial and final inversion of CRC32C.
  virtual void Extend(uint32_t* crc, const void* bytes, size_t length) const = 0;
};

// Needs SSE4.2, PCLMULQDQ and AVX on x86_64. Checks the other features it uses itself, CRC32 and
// PMULL on aarch64 among them. Returns nullptr when the implementation is not available.
Crc32cEngine* TryNewCRC32AcceleratedX86ARMCombined();

struct NamedCrc32cEngine {
  const char* name;
  std::unique_ptr<Crc32cEngine> engine;
};

// Every variant of the engine above that this CPU can run, so tests can check all of them.
std::vector<NamedCrc32cEngine> NewAbseilEnginesForTests();

} // namespace internal
} // namespace yb::crc
