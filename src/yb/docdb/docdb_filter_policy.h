// Copyright (c) YugaByte, Inc.
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

#include "yb/rocksdb/filter_policy.h"

namespace yb::docdb {

class DocDbAwareFilterPolicyBase : public rocksdb::FilterPolicy {
 public:
  explicit DocDbAwareFilterPolicyBase(size_t filter_block_size_bits, rocksdb::Logger* logger) {
    builtin_policy_.reset(rocksdb::NewFixedSizeFilterPolicy(
        filter_block_size_bits, rocksdb::FilterPolicy::kDefaultFixedSizeFilterErrorRate, logger));
  }

  void CreateFilter(const Slice* keys, int n, std::string* dst) const override;

  bool KeyMayMatch(const Slice& key, const Slice& filter) const override;

  rocksdb::FilterBitsBuilder* GetFilterBitsBuilder() const override;

  rocksdb::FilterBitsReader* GetFilterBitsReader(const Slice& contents) const override;

  FilterType GetFilterType() const override;

 private:
  std::unique_ptr<const rocksdb::FilterPolicy> builtin_policy_;
};

// This filter policy only takes into account hashed components of keys for filtering.
class DocDbAwareHashedComponentsFilterPolicy : public DocDbAwareFilterPolicyBase {
 public:
  DocDbAwareHashedComponentsFilterPolicy(size_t filter_block_size_bits, rocksdb::Logger* logger)
      : DocDbAwareFilterPolicyBase(filter_block_size_bits, logger) {}

  const char* Name() const override { return "DocKeyHashedComponentsFilter"; }

  const KeyTransformer* GetKeyTransformer() const override;
};

// Together with the fix for BlockBasedTableBuild::Add
// (https://github.com/yugabyte/yugabyte-db/issues/6435) we also disable DocKeyV2Filter
// for range-partitioned tablets. For hash-partitioned tablets it will be supported during read
// path and will work the same way as DocDbAwareV3FilterPolicy.
class DocDbAwareV2FilterPolicy : public DocDbAwareFilterPolicyBase {
 public:
  DocDbAwareV2FilterPolicy(size_t filter_block_size_bits, rocksdb::Logger* logger)
      : DocDbAwareFilterPolicyBase(filter_block_size_bits, logger) {}

  const char* Name() const override { return "DocKeyV2Filter"; }

  const KeyTransformer* GetKeyTransformer() const override;
};

// This filter policy takes into account following parts of keys for filtering:
// - For range-based partitioned tables (such tables have 0 hashed components):
// use all hash components of the doc key.
// - For hash-based partitioned tables (such tables have >0 hashed components):
// use first range component of the doc key.
class DocDbAwareV3FilterPolicy : public DocDbAwareFilterPolicyBase {
 public:
  DocDbAwareV3FilterPolicy(size_t filter_block_size_bits, rocksdb::Logger* logger)
      : DocDbAwareFilterPolicyBase(filter_block_size_bits, logger) {}

  const char* Name() const override { return "DocKeyV3Filter"; }

  const KeyTransformer* GetKeyTransformer() const override;
};

Result<Slice> ExtractFilterPrefixFromKey(Slice key);

}  // namespace yb::docdb
