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

#include <memory>
#include <optional>
#include <string_view>

#include "yb/common/hybrid_time.h"

#include "yb/rocksdb/options.h"
#include "yb/rocksdb/table_properties.h"

namespace yb::docdb {

// Records the exact range of the hybrid times encoded at the end of every key of a regular RocksDB
// SST file, in the file's properties block.
//
// The hybrid times of the consensus frontiers do not bound the keys: a write with an external
// hybrid time (xCluster, index backfill) stores keys older than its Raft operation. So the
// frontiers cannot tell that a hybrid time filter set by a restore hides every key of a file, while
// this range can (see RocksDBPatcher::SetHybridTimeFilter).
struct KeyHybridTimeRangePropertyKeys {
  // HybridTime::ToUint64(), as decimal strings.
  static constexpr std::string_view kMinKeyHt = "yb.docdb.min_key_ht";
  static constexpr std::string_view kMaxKeyHt = "yb.docdb.max_key_ht";
};

struct KeyHybridTimeRange {
  HybridTime min;
  HybridTime max;

  std::string ToString() const;
};

std::shared_ptr<rocksdb::TablePropertiesCollectorFactory>
    MakeKeyHybridTimeRangeCollectorFactory();

// Registers the collector in options, unless it is already registered.
void AddKeyHybridTimeRangeCollector(rocksdb::Options* options);

// Returns nullopt when the file was built without the collector, is empty, or has a key whose
// hybrid time could not be decoded.
std::optional<KeyHybridTimeRange> GetKeyHybridTimeRange(
    const rocksdb::UserCollectedProperties& properties);

}  // namespace yb::docdb
