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

#include "yb/docdb/doc_ql_filefilter.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"

#include "yb/rocksdb/db/compaction.h"

namespace yb {
namespace docdb {
extern rocksdb::UserBoundaryTag TagForRangeComponent(size_t index);

std::vector<KeyBytes> EncodePrimitiveValues(const std::vector<KeyEntryValue>& source,
                                            size_t min_size) {
  size_t size = source.size();
  std::vector<KeyBytes> result(std::max(min_size, size));
  for (size_t i = 0; i != size; ++i) {
    source[i].AppendToKey(&result[i]);
  }
  return result;
}

std::vector<bool> ExtendBoolVector(const std::vector<bool>& source, size_t min_size, bool val) {
  std::vector<bool> vec(source);
  if (min_size > vec.size()) {
    for (size_t i = vec.size(); i < min_size; i++) {
      vec.push_back(val);
    }
  }
  return vec;
}

int Compare(const Slice *lhs, const Slice *rhs) {
  // TODO(neil) Need to double check this NULL-equals-all logic or make the code clearer.
  if (lhs == nullptr || rhs == nullptr) {
    return 0;
  }
  if (lhs->empty() || rhs->empty()) {
    return 0;
  }
  return lhs->compare(*rhs);
}


QLRangeBasedFileFilter::QLRangeBasedFileFilter(const std::vector<KeyEntryValue>& lower_bounds,
                                               const std::vector<bool>& lower_bounds_inclusive,
                                               const std::vector<KeyEntryValue>& upper_bounds,
                                               const std::vector<bool>& upper_bounds_inclusive)
    : lower_bounds_(EncodePrimitiveValues(lower_bounds, upper_bounds.size())),
      lower_bounds_inclusive_(ExtendBoolVector(lower_bounds_inclusive, upper_bounds.size(), true)),
      upper_bounds_(EncodePrimitiveValues(upper_bounds, lower_bounds.size())),
      upper_bounds_inclusive_(ExtendBoolVector(upper_bounds_inclusive, lower_bounds.size(), true)) {
  CHECK_EQ(lower_bounds_.size(), lower_bounds_inclusive_.size());
}

bool QLRangeBasedFileFilter::Filter(const rocksdb::FdWithBoundaries& file) const {

  for (size_t i = 0; i != lower_bounds_.size(); ++i) {
    const Slice lower_bound = lower_bounds_[i].AsSlice();
    bool lower_bound_incl = lower_bounds_inclusive_[i];
    const Slice upper_bound = upper_bounds_[i].AsSlice();
    bool upper_bound_incl = upper_bounds_inclusive_[i];

    rocksdb::UserBoundaryTag tag = TagForRangeComponent(i);
    const Slice *smallest = file.smallest.user_value_with_tag(tag);
    const Slice *largest = file.largest.user_value_with_tag(tag);

    bool lower_compare_min_value = lower_bound_incl ? 0 : 1;
    bool upper_compare_min_value = upper_bound_incl ? 0 : 1;

    if (Compare(&upper_bound, smallest) < lower_compare_min_value
        || Compare(largest, &lower_bound) < upper_compare_min_value) {
      return false;
    }
  }
  return true;
}

}  // namespace docdb
}  // namespace yb
