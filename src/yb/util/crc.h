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
//
#pragma once

#include <stdint.h>
#include <stdlib.h>

#include <crcutil/interface.h>

#include "yb/util/slice.h"

namespace yb::crc {

typedef crcutil_interface::CRC Crc;

// Returns pointer to singleton instance of CRC32C implementation.
Crc* GetCrc32cInstance();

// Helper function to simply calculate a CRC32C of the given data.
uint32_t Crc32c(const void* data, size_t length);

inline uint32_t Crc32c(Slice slice) {
  return Crc32c(slice.data(), slice.size());
}

class Crc32Accumulator {
 public:
  explicit Crc32Accumulator(uint64_t state = 0) : state_(state) {}

  void Feed(Slice slice) {
    Feed(slice.data(), slice.size());
  }

  void Feed(const void* data, size_t length);

  uint32_t result() const {
    return static_cast<uint32_t>(state_);
  }

 private:
  // CRC32C has 64 bits state, but top 32 bits are always zero.
  uint64_t state_;
};

} // namespace yb::crc
