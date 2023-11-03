// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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

#include <string>

#include "yb/util/slice.h"
#include "yb/rocksdb/table.h"

namespace rocksdb {

class BlockBuilder;
struct Options;

// FlushBlockPolicy provides a configurable way to determine when to flush a
// block in the block based tables,
class FlushBlockPolicy {
 public:
  // Keep track of the key/value sequences and return the boolean value to
  // determine if table builder should flush current data block.
  virtual bool Update(const Slice& key,
                      const Slice& value) = 0;

  virtual ~FlushBlockPolicy() { }
};

class FlushBlockPolicyFactory {
 public:
  // Return the name of the flush block policy.
  virtual const char* Name() const = 0;

  // Return a new block flush policy that flushes data blocks by data size.
  // FlushBlockPolicy may need to access the metadata of the data block
  // builder to determine when to flush the blocks.
  //
  // Callers must delete the result after any database that is using the
  // result has been closed.
  virtual FlushBlockPolicy* NewFlushBlockPolicy(
      const BlockBasedTableOptions& table_options,
      const BlockBuilder& data_block_builder) const = 0;

  virtual ~FlushBlockPolicyFactory() { }
};

class FlushBlockBySizePolicyFactory : public FlushBlockPolicyFactory {
 public:
  FlushBlockBySizePolicyFactory() {}

  virtual const char* Name() const override {
    return "FlushBlockBySizePolicyFactory";
  }

  virtual FlushBlockPolicy* NewFlushBlockPolicy(
      const BlockBasedTableOptions& table_options,
      const BlockBuilder& data_block_builder) const override;

  static std::unique_ptr<FlushBlockPolicy> NewFlushBlockPolicy(
      const uint64_t size, const int deviation, const size_t min_keys_per_block,
      const BlockBuilder& data_block_builder);
};

}  // namespace rocksdb
