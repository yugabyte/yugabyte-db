//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#include <vector>
#include <string>
#include "yb/rocksdb/util/dynamic_bloom.h"

namespace rocksdb {
class Logger;

class BloomBlockBuilder {
 public:
  static const std::string kBloomBlock;

  explicit BloomBlockBuilder(uint32_t num_probes = 6)
      : bloom_(num_probes, nullptr) {}

  void SetTotalBits(Allocator* allocator, uint32_t total_bits,
                    uint32_t locality, size_t huge_page_tlb_size,
                    Logger* logger) {
    bloom_.SetTotalBits(allocator, total_bits, locality, huge_page_tlb_size,
                        logger);
  }

  uint32_t GetNumBlocks() const { return bloom_.GetNumBlocks(); }

  void AddKeysHashes(const std::vector<uint32_t>& keys_hashes);

  Slice Finish();

 private:
  DynamicBloom bloom_;
};

};  // namespace rocksdb
