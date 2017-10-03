// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/common/scan_spec.h"

#include <string>
#include <vector>

#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/escaping.h"

using std::vector;
using std::string;

namespace kudu {

void ScanSpec::AddPredicate(const ColumnRangePredicate &pred) {
  predicates_.push_back(pred);
}

void ScanSpec::SetLowerBoundKey(const EncodedKey* key) {
  if (lower_bound_key_ == nullptr ||
      key->encoded_key().compare(lower_bound_key_->encoded_key()) > 0) {
    lower_bound_key_ = key;
  }
}
void ScanSpec::SetExclusiveUpperBoundKey(const EncodedKey* key) {
  if (exclusive_upper_bound_key_ == nullptr ||
      key->encoded_key().compare(exclusive_upper_bound_key_->encoded_key()) < 0) {
    exclusive_upper_bound_key_ = key;
  }
}

void ScanSpec::SetLowerBoundPartitionKey(const Slice& partitionKey) {
  if (partitionKey.compare(lower_bound_partition_key_) > 0) {
    lower_bound_partition_key_ = partitionKey.ToString();
  }
}

void ScanSpec::SetExclusiveUpperBoundPartitionKey(const Slice& partitionKey) {
  if (exclusive_upper_bound_partition_key_.empty() ||
      (!partitionKey.empty() && partitionKey.compare(exclusive_upper_bound_partition_key_) < 0)) {
    exclusive_upper_bound_partition_key_ = partitionKey.ToString();
  }
}

string ScanSpec::ToString() const {
  return ToStringWithOptionalSchema(nullptr);
}

string ScanSpec::ToStringWithSchema(const Schema& s) const {
  return ToStringWithOptionalSchema(&s);
}

string ScanSpec::ToStringWithOptionalSchema(const Schema* s) const {
  vector<string> preds;

  if (lower_bound_key_ || exclusive_upper_bound_key_) {
    if (s) {
      preds.push_back(EncodedKey::RangeToStringWithSchema(
                          lower_bound_key_,
                          exclusive_upper_bound_key_,
                          *s));
    } else {
      preds.push_back(EncodedKey::RangeToString(
                          lower_bound_key_,
                          exclusive_upper_bound_key_));
    }
  }

  for (const ColumnRangePredicate& pred : predicates_) {
    preds.push_back(pred.ToString());
  }
  return JoinStrings(preds, "\n");
}

} // namespace kudu
