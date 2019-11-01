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

#include "yb/docdb/key_bytes.h"

#include "yb/common/doc_hybrid_time.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"

using strings::Substitute;
using std::string;

namespace yb {
namespace docdb {

// Assuming the key bytes currently end with a hybrid time, replace that hybrid time with a
// different one.
Status KeyBytes::ReplaceLastHybridTimeForSeek(HybridTime hybrid_time) {
  int encoded_ht_size = 0;
  RETURN_NOT_OK(DocHybridTime::CheckAndGetEncodedSize(data_, &encoded_ht_size));
  data_.resize(data_.size() - encoded_ht_size);
  AppendHybridTimeForSeek(hybrid_time);
  return Status::OK();
}

Status KeyBytes::OnlyLacksHybridTimeFrom(const rocksdb::Slice& other_slice, bool* result) const {
  *result = false;

  DOCDB_DEBUG_LOG("other_slice=$0", FormatSliceAsStr(other_slice));

  if (other_slice.empty()) {
    return STATUS(Corruption, "Empty key not expected");
  }
  const size_t prefix_size = size();
  const size_t other_encoded_key_size = other_slice.size();
  int other_encoded_ht_size = 0;
  RETURN_NOT_OK(CheckHybridTimeSizeAndValueType(other_slice, &other_encoded_ht_size));

  // The difference should only consist of one byte for ValueType and other_encoded_ht_size bytes
  // for the timestamp.
  *result = prefix_size + other_encoded_ht_size + 1 == other_encoded_key_size &&
            other_slice.starts_with(AsSlice());
  return Status::OK();
}

void AppendDocHybridTime(const DocHybridTime& doc_ht, KeyBytes* key) {
  key->AppendValueType(ValueType::kHybridTime);
  doc_ht.AppendEncodedInDocDbFormat(key->mutable_data());
}

}  // namespace docdb
}  // namespace yb
