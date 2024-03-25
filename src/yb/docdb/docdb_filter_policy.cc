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

#include "yb/docdb/docdb_filter_policy.h"

#include "yb/dockv/doc_key.h"

namespace yb::docdb {

namespace {

template<dockv::DocKeyPart doc_key_part>
class DocKeyComponentsExtractor : public rocksdb::FilterPolicy::KeyTransformer {
 public:
  DocKeyComponentsExtractor(const DocKeyComponentsExtractor&) = delete;
  DocKeyComponentsExtractor& operator=(const DocKeyComponentsExtractor&) = delete;

  static DocKeyComponentsExtractor& GetInstance() {
    static DocKeyComponentsExtractor<doc_key_part> instance;
    return instance;
  }

  // For encoded DocKey extracts specified part, for non-DocKey returns empty key, so they will
  // always match the filter (this is correct, but might be optimized for performance if/when
  // needed).
  // As of 2020-05-12 intents DB could contain keys in non-DocKey format.
  Slice Transform(Slice key) const override {
    auto size_result = dockv::DocKey::EncodedSize(key, doc_key_part);
    return size_result.ok() ? Slice(key.data(), *size_result) : Slice();
  }

 private:
  DocKeyComponentsExtractor() = default;
};

class HashedDocKeyUpToHashComponentsExtractor : public rocksdb::FilterPolicy::KeyTransformer {
 public:
  HashedDocKeyUpToHashComponentsExtractor(const HashedDocKeyUpToHashComponentsExtractor&) = delete;
  HashedDocKeyUpToHashComponentsExtractor& operator=(
      const HashedDocKeyUpToHashComponentsExtractor&) = delete;

  static HashedDocKeyUpToHashComponentsExtractor& GetInstance() {
    static HashedDocKeyUpToHashComponentsExtractor instance;
    return instance;
  }

  // For encoded DocKey with hash code present extracts prefix up to hashed components,
  // for non-DocKey or DocKey without hash code (for range-partitioned tables) returns empty key,
  // so they will always match the filter.
  Slice Transform(Slice key) const override {
    auto size_result = dockv::DocKey::EncodedSizeAndHashPresent(key, dockv::DocKeyPart::kUpToHash);
    return (size_result.ok() && size_result->second) ? Slice(key.data(), size_result->first)
                                                     : Slice();
  }

 private:
  HashedDocKeyUpToHashComponentsExtractor() = default;
};

} // namespace

void DocDbAwareFilterPolicyBase::CreateFilter(
    const rocksdb::Slice* keys, int n, std::string* dst) const {
  CHECK_GT(n, 0);
  return builtin_policy_->CreateFilter(keys, n, dst);
}

bool DocDbAwareFilterPolicyBase::KeyMayMatch(
    const rocksdb::Slice& key, const rocksdb::Slice& filter) const {
  return builtin_policy_->KeyMayMatch(key, filter);
}

rocksdb::FilterBitsBuilder* DocDbAwareFilterPolicyBase::GetFilterBitsBuilder() const {
  return builtin_policy_->GetFilterBitsBuilder();
}

rocksdb::FilterBitsReader* DocDbAwareFilterPolicyBase::GetFilterBitsReader(
    const rocksdb::Slice& contents) const {
  return builtin_policy_->GetFilterBitsReader(contents);
}

rocksdb::FilterPolicy::FilterType DocDbAwareFilterPolicyBase::GetFilterType() const {
  return builtin_policy_->GetFilterType();
}

const rocksdb::FilterPolicy::KeyTransformer*
DocDbAwareHashedComponentsFilterPolicy::GetKeyTransformer() const {
  return &DocKeyComponentsExtractor<dockv::DocKeyPart::kUpToHash>::GetInstance();
}

const rocksdb::FilterPolicy::KeyTransformer*
DocDbAwareV2FilterPolicy::GetKeyTransformer() const {
  // We want for DocDbAwareV2FilterPolicy to disable bloom filtering during read path for
  // range-partitioned tablets (see https://github.com/yugabyte/yugabyte-db/issues/6435,
  // https://github.com/yugabyte/yugabyte-db/issues/8731).
  return &HashedDocKeyUpToHashComponentsExtractor::GetInstance();
}

const rocksdb::FilterPolicy::KeyTransformer*
DocDbAwareV3FilterPolicy::GetKeyTransformer() const {
  return &DocKeyComponentsExtractor<dockv::DocKeyPart::kUpToHashOrFirstRange>::GetInstance();
}

Result<Slice> ExtractFilterPrefixFromKey(Slice key) {
  return key.Prefix(VERIFY_RESULT(dockv::DocKey::EncodedSize(
      key, dockv::DocKeyPart::kUpToHashOrFirstRange)));
}

}   // namespace yb::docdb
