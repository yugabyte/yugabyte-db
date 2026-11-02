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

#include "yb/docdb/properties_collector/key_hybrid_time_range_collector.h"

#include <algorithm>

#include "yb/common/doc_hybrid_time.h"

#include "yb/util/format.h"
#include "yb/util/stol_utils.h"

namespace yb::docdb {

namespace {

constexpr const char* kCollectorName = "DocDbKeyHybridTimeRangeCollector";

class KeyHybridTimeRangeCollector : public rocksdb::TablePropertiesCollector {
 public:
  Status AddUserKey(
      const Slice& key, const Slice& value, rocksdb::EntryType type, rocksdb::SequenceNumber seq,
      uint64_t file_size) override {
    if (!valid_) {
      return Status::OK();
    }
    // A properties collector must never fail the table build. A key without a decodable hybrid time
    // leaves the file without a range, so nothing relies on it.
    auto doc_ht = DocHybridTime::DecodeFromEnd(key);
    if (!doc_ht.ok()) {
      valid_ = false;
      return Status::OK();
    }
    auto ht = doc_ht->hybrid_time();
    if (!min_.is_valid() || ht < min_) {
      min_ = ht;
    }
    if (!max_.is_valid() || ht > max_) {
      max_ = ht;
    }
    return Status::OK();
  }

  // Called more than once, see properties_collector/README.md.
  Status Finish(rocksdb::UserCollectedProperties* properties) override {
    if (valid_ && min_.is_valid()) {
      using K = KeyHybridTimeRangePropertyKeys;
      (*properties)[std::string(K::kMinKeyHt)] = std::to_string(min_.ToUint64());
      (*properties)[std::string(K::kMaxKeyHt)] = std::to_string(max_.ToUint64());
    }
    return Status::OK();
  }

  rocksdb::UserCollectedProperties GetReadableProperties() const override {
    if (!valid_ || !min_.is_valid()) {
      return {};
    }
    return {
      {std::string(KeyHybridTimeRangePropertyKeys::kMinKeyHt), min_.ToString()},
      {std::string(KeyHybridTimeRangePropertyKeys::kMaxKeyHt), max_.ToString()},
    };
  }

  const char* Name() const override { return kCollectorName; }

 private:
  bool valid_ = true;
  HybridTime min_;
  HybridTime max_;
};

class KeyHybridTimeRangeCollectorFactory : public rocksdb::TablePropertiesCollectorFactory {
 public:
  rocksdb::TablePropertiesCollector* CreateTablePropertiesCollector(
      rocksdb::TablePropertiesCollectorFactory::Context context) override {
    return new KeyHybridTimeRangeCollector();
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

std::string KeyHybridTimeRange::ToString() const {
  return YB_STRUCT_TO_STRING(min, max);
}

std::shared_ptr<rocksdb::TablePropertiesCollectorFactory>
    MakeKeyHybridTimeRangeCollectorFactory() {
  return std::make_shared<KeyHybridTimeRangeCollectorFactory>();
}

void AddKeyHybridTimeRangeCollector(rocksdb::Options* options) {
  auto& factories = options->table_properties_collector_factories;
  if (std::any_of(factories.begin(), factories.end(), [](const auto& factory) {
        return std::string_view(factory->Name()) == kCollectorName;
      })) {
    return;
  }
  factories.push_back(MakeKeyHybridTimeRangeCollectorFactory());
}

std::optional<KeyHybridTimeRange> GetKeyHybridTimeRange(
    const rocksdb::UserCollectedProperties& properties) {
  auto min = GetHybridTime(properties, KeyHybridTimeRangePropertyKeys::kMinKeyHt);
  auto max = GetHybridTime(properties, KeyHybridTimeRangePropertyKeys::kMaxKeyHt);
  if (!min || !max || *min > *max) {
    return std::nullopt;
  }
  return KeyHybridTimeRange{.min = *min, .max = *max};
}

}  // namespace yb::docdb
