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

#include "yb/docdb/properties_collector/encoded_hybrid_time_range.h"

#include "yb/rocksdb/options.h"
#include "yb/rocksdb/table_properties.h"

namespace yb::docdb {

// Records the range of the write hybrid times of every entry of a regular RocksDB SST file, in the
// file's properties block. Unlike the SST stats collector's min_write_ht / max_write_ht, which
// cover only chain-tracked entries, the range covers meta records too.
//
// The hybrid times of the consensus frontiers do not bound the entries: a write with an external
// hybrid time (xCluster, index backfill) stores keys older than its Raft operation. So the
// frontiers cannot tell that a hybrid time filter set by a restore hides every entry of a file,
// while this range can (see RocksDBPatcher::SetHybridTimeFilter).
struct WriteHybridTimeRangePropertyKeys {
  // HybridTime::ToUint64(), as decimal strings.
  static constexpr std::string_view kFileMinWriteHt = "yb.docdb.file_min_write_ht";
  static constexpr std::string_view kFileMaxWriteHt = "yb.docdb.file_max_write_ht";
};

std::shared_ptr<rocksdb::TablePropertiesCollectorFactory>
    MakeWriteHybridTimeRangeCollectorFactory();

// Registers the collector in options, unless it is already registered.
void AddWriteHybridTimeRangeCollector(rocksdb::Options* options);

// Returns nullopt when the file was built without the collector or is empty, or when the collector
// could not take a hybrid time from one of its keys.
std::optional<WriteHybridTimeRange> GetWriteHybridTimeRange(
    const rocksdb::UserCollectedProperties& properties);

}  // namespace yb::docdb
