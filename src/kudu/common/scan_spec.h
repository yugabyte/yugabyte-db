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
#ifndef KUDU_COMMON_SCAN_SPEC_H
#define KUDU_COMMON_SCAN_SPEC_H

#include <string>
#include <vector>

#include "kudu/common/scan_predicate.h"
#include "kudu/common/encoded_key.h"

namespace kudu {

using std::vector;

class ScanSpec {
 public:
  ScanSpec()
    : lower_bound_key_(NULL),
      exclusive_upper_bound_key_(NULL),
      lower_bound_partition_key_(),
      exclusive_upper_bound_partition_key_(),
      cache_blocks_(true) {
  }

  typedef vector<ColumnRangePredicate> PredicateList;

  void AddPredicate(const ColumnRangePredicate &pred);

  // Set the lower bound (inclusive) primary key for the scan.
  // Does not take ownership of 'key', which must remain valid.
  // If called multiple times, the most restrictive key will be used.
  void SetLowerBoundKey(const EncodedKey* key);

  // Set the upper bound (exclusive) primary key for the scan.
  // Does not take ownership of 'key', which must remain valid.
  // If called multiple times, the most restrictive key will be used.
  void SetExclusiveUpperBoundKey(const EncodedKey* key);

  // Sets the lower bound (inclusive) partition key for the scan.
  //
  // The scan spec makes a copy of 'slice'; the caller may free it afterward.
  //
  // Only used in the client.
  void SetLowerBoundPartitionKey(const Slice& slice);

  // Sets the upper bound (exclusive) partition key for the scan.
  //
  // The scan spec makes a copy of 'slice'; the caller may free it afterward.
  //
  // Only used in the client.
  void SetExclusiveUpperBoundPartitionKey(const Slice& slice);

  const vector<ColumnRangePredicate> &predicates() const {
    return predicates_;
  }

  // Return a pointer to the list of predicates in this scan spec.
  //
  // Callers may use this during predicate pushdown to remove predicates
  // from their caller if they're able to apply them lower down the
  // iterator tree.
  vector<ColumnRangePredicate> *mutable_predicates() {
    return &predicates_;
  }

  const EncodedKey* lower_bound_key() const {
    return lower_bound_key_;
  }
  const EncodedKey* exclusive_upper_bound_key() const {
    return exclusive_upper_bound_key_;
  }

  const string& lower_bound_partition_key() const {
    return lower_bound_partition_key_;
  }
  const string& exclusive_upper_bound_partition_key() const {
    return exclusive_upper_bound_partition_key_;
  }

  bool cache_blocks() const {
    return cache_blocks_;
  }

  void set_cache_blocks(bool cache_blocks) {
    cache_blocks_ = cache_blocks;
  }

  std::string ToString() const;
  std::string ToStringWithSchema(const Schema& s) const;

 private:
  // Helper for the ToString*() methods. 's' may be NULL.
  std::string ToStringWithOptionalSchema(const Schema* s) const;

  vector<ColumnRangePredicate> predicates_;
  const EncodedKey* lower_bound_key_;
  const EncodedKey* exclusive_upper_bound_key_;
  std::string lower_bound_partition_key_;
  std::string exclusive_upper_bound_partition_key_;
  bool cache_blocks_;
};

} // namespace kudu

#endif
