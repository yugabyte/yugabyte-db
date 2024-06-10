//
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
//

#include "yb/rocksdb/db/dbformat.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

namespace yb {
namespace docdb {

rocksdb::UserBoundaryTag TagForRangeComponent(size_t index);

namespace {

// Here we reserve some tags for future use.
// Because Tag is persistent.
constexpr rocksdb::UserBoundaryTag kRangeComponentsStart = 10;

class DocBoundaryValuesExtractor : public rocksdb::BoundaryValuesExtractor {
 public:
  virtual ~DocBoundaryValuesExtractor() {}

  Status Extract(Slice user_key, rocksdb::UserBoundaryValueRefs* values) override {
    if (dockv::IsInternalRecordKeyType(dockv::DecodeKeyEntryType(user_key))) {
      // Skipping internal DocDB records.
      return Status::OK();
    }

    CHECK_NOTNULL(values);
    boost::container::small_vector<Slice, 20> slices;
    auto user_key_copy = user_key;
    RETURN_NOT_OK(dockv::SubDocKey::PartiallyDecode(&user_key_copy, &slices));
    size_t size = slices.size();
    if (size == 0) {
      return STATUS(Corruption, "Key does not contain hybrid time", user_key.ToDebugString());
    }
    // Last one contains Doc Hybrid Time, so number of range is less by 1.
    --size;

    for (uint32_t i = 0; i != size; ++i) {
      values->push_back(rocksdb::UserBoundaryValueRef {
        .tag = TagForRangeComponent(i),
        .value = slices[i],
      });
    }

    return Status::OK();
  }

  rocksdb::UserFrontierPtr CreateFrontier() override {
    return new docdb::ConsensusFrontier();
  }
};

} // namespace

std::shared_ptr<rocksdb::BoundaryValuesExtractor> DocBoundaryValuesExtractorInstance() {
  static std::shared_ptr<rocksdb::BoundaryValuesExtractor> instance =
      std::make_shared<DocBoundaryValuesExtractor>();
  return instance;
}

// Used in tests
Result<dockv::KeyEntryValue> TEST_GetKeyEntryValue(
    const rocksdb::UserBoundaryValues& values, size_t index) {
  auto value = rocksdb::TEST_UserValueWithTag(values, TagForRangeComponent(index));
  if (!value) {
    return STATUS_SUBSTITUTE(NotFound, "Not found value for index $0", index);
  }
  return dockv::KeyEntryValue::FullyDecodeFromKey(value->AsSlice());
}

rocksdb::UserBoundaryTag TagForRangeComponent(size_t index) {
  return static_cast<rocksdb::UserBoundaryTag>(kRangeComponentsStart + index);
}

} // namespace docdb
} // namespace yb
