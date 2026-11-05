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

#include "yb/docdb/properties_collector/write_hybrid_time_range_collector.h"

#include <algorithm>

#include "yb/common/doc_hybrid_time.h"

#include "yb/util/flags.h"
#include "yb/util/stol_utils.h"

DEFINE_NON_RUNTIME_bool(docdb_enable_sst_write_hybrid_time_range_collector, true,
    "Whether to register the collector that stores the range of the write hybrid times of every "
    "entry in each SST file's properties block, on the regular RocksDB of each tablet. A restore "
    "to a point in time removes the SST files it hides entirely only when they have this range. "
    "Takes effect on tablet (re)open.");

namespace yb::docdb {

namespace {

constexpr const char* kCollectorName = "DocDbWriteHybridTimeRangeCollector";

class WriteHybridTimeRangeCollector : public rocksdb::TablePropertiesCollector {
 public:
  Status AddUserKey(
      const Slice& key, const Slice& value, rocksdb::EntryType type, rocksdb::SequenceNumber seq,
      uint64_t file_size) override {
    if (!valid_) {
      return Status::OK();
    }
    // A properties collector must never fail the table build. A key without a valid hybrid time
    // size leaves the file without a range. Only that size is checked, to keep this cheap: the
    // range assumes well-formed hybrid times, and decodes only its two extremes.
    auto size = DocHybridTime::GetEncodedSize(key);
    if (!size.ok()) {
      valid_ = false;
      return Status::OK();
    }
    range_.Add(key.Suffix(*size));
    return Status::OK();
  }

  // Called more than once, see properties_collector/README.md.
  Status Finish(rocksdb::UserCollectedProperties* properties) override {
    if (auto range = Range()) {
      using K = WriteHybridTimeRangePropertyKeys;
      (*properties)[std::string(K::kFileMinWriteHt)] = std::to_string(range->min.ToUint64());
      (*properties)[std::string(K::kFileMaxWriteHt)] = std::to_string(range->max.ToUint64());
    }
    return Status::OK();
  }

  rocksdb::UserCollectedProperties GetReadableProperties() const override {
    auto range = Range();
    if (!range) {
      return {};
    }
    return {
      {std::string(WriteHybridTimeRangePropertyKeys::kFileMinWriteHt), range->min.ToString()},
      {std::string(WriteHybridTimeRangePropertyKeys::kFileMaxWriteHt), range->max.ToString()},
    };
  }

  const char* Name() const override { return kCollectorName; }

 private:
  std::optional<WriteHybridTimeRange> Range() const {
    if (!valid_ || range_.empty()) {
      return std::nullopt;
    }
    auto range = range_.Decode();
    if (!range.ok()) {
      return std::nullopt;
    }
    return *range;
  }

  bool valid_ = true;
  EncodedHybridTimeRange range_;
};

class WriteHybridTimeRangeCollectorFactory : public rocksdb::TablePropertiesCollectorFactory {
 public:
  rocksdb::TablePropertiesCollector* CreateTablePropertiesCollector(
      rocksdb::TablePropertiesCollectorFactory::Context context) override {
    return new WriteHybridTimeRangeCollector();
  }

  const char* Name() const override { return kCollectorName; }
};

std::optional<HybridTime> GetHybridTime(
    const rocksdb::UserCollectedProperties& properties, std::string_view key) {
  auto it = properties.find(std::string(key));
  if (it == properties.end()) {
    return std::nullopt;
  }
  auto value = CheckedStoull(Slice(it->second));
  if (!value.ok()) {
    return std::nullopt;
  }
  HybridTime result(*value);
  if (!result.is_valid()) {
    return std::nullopt;
  }
  return result;
}

}  // namespace

std::shared_ptr<rocksdb::TablePropertiesCollectorFactory>
    MakeWriteHybridTimeRangeCollectorFactory() {
  return std::make_shared<WriteHybridTimeRangeCollectorFactory>();
}

void AddWriteHybridTimeRangeCollector(rocksdb::Options* options) {
  auto& factories = options->table_properties_collector_factories;
  if (std::any_of(factories.begin(), factories.end(), [](const auto& factory) {
        return std::string_view(factory->Name()) == kCollectorName;
      })) {
    return;
  }
  factories.push_back(MakeWriteHybridTimeRangeCollectorFactory());
}

std::optional<WriteHybridTimeRange> GetWriteHybridTimeRange(
    const rocksdb::UserCollectedProperties& properties) {
  auto min = GetHybridTime(properties, WriteHybridTimeRangePropertyKeys::kFileMinWriteHt);
  auto max = GetHybridTime(properties, WriteHybridTimeRangePropertyKeys::kFileMaxWriteHt);
  if (!min || !max || *min > *max) {
    return std::nullopt;
  }
  return WriteHybridTimeRange{.min = *min, .max = *max};
}

}  // namespace yb::docdb
