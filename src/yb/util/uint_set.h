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

#include <boost/icl/discrete_interval.hpp>
#include <boost/icl/interval_set.hpp>
#include <google/protobuf/repeated_field.h>

#include "yb/gutil/strings/join.h"

#include "yb/util/memory/arena_fwd.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

namespace yb {

// Tracks the on/off state of a set of unsigned integers. Initially, all integers are in the "off"
// state, and ranges of integers can be set to "on". Individual integers can then be tested. This
// class currently does not support turning indexes "off".
//
// This class is designed to support efficient encoding/decoding of state into any valid numeric
// google::protobuf::RepeatedField<T>.
//
// Under the hood, this class tracks the state of integer indexes in both an interval_set to enable
// efficient serialization into a numeric RepeatedField.
template <class T>
class UnsignedIntSet {
 public:
  UnsignedIntSet() {}

  // Set the indexes of this set in [lo, hi] to "on". It is perfectly valid to call SetRange with
  // lo = hi.
  Status SetRange(T lo, T hi) {
    SCHECK_LE(lo, hi, InvalidArgument, Format("Called SetRange with lo ($0) > hi ($1).", lo, hi));
    interval_set_ += ElementRange::closed(lo, hi);
    return Status::OK();
  }

  // Return true if index at val is "on".
  bool Test(T val) const {
    return interval_set_.find(val) != interval_set_.end();
  }

  // Returns true if this set is empty.
  bool IsEmpty() const {
    return interval_set_.empty();
  }

  template <class Container>
  static Result<UnsignedIntSet<T>> FromPB(const Container& container) {
    UnsignedIntSet set;

    auto run_length_size = container.size();

    if (run_length_size == 0) {
      return set;
    }

    if (run_length_size % 2 != 0) {
      return STATUS(
        InvalidArgument,
        Format("Expect even number of run lengths in container, got $0", run_length_size));
    }

    uint32_t prev = 0;
    for (auto run = container.begin(); run != container.end();) {
      auto start = prev += *run++;
      auto finish = (prev += *run++) - 1;
      RETURN_NOT_OK(set.SetRange(start, finish));
    }

    return set;
  }

  void ToPB(ArenaVector<T>* out) const {
    uint32_t last_unset = 0;
    for (const auto& elem : interval_set_) {
      out->push_back(elem.lower() - last_unset);
      out->push_back(elem.upper() - elem.lower() + 1);
      last_unset = elem.upper() + 1;
    }
  }

  void ToPB(google::protobuf::RepeatedField<T>* mutable_container) const {
    uint32_t last_unset = 0;
    for (const auto& elem : interval_set_) {
      mutable_container->Add(elem.lower() - last_unset);
      mutable_container->Add(elem.upper() - elem.lower() + 1);
      last_unset = elem.upper() + 1;
    }
  }

  std::string ToString() const {
    std::vector<std::string> parts;
    for (const auto& elem : interval_set_) {
      parts.push_back(Format("[$0, $1]", elem.lower(), elem.upper()));
    }
    return JoinStrings(parts, ", ");
  }

  bool operator==(const UnsignedIntSet<T>& other) const {
    return boost::icl::is_element_equal(interval_set_, other.interval_set_);
  }

  // Returns true if this set is a super set of the other set.
  bool Contains(const UnsignedIntSet<T>& other) const {
    ElementRangeSet set_difference = other.interval_set_ - this->interval_set_;
    return set_difference.empty();
  }

 private:
  using ElementType = uint32_t;
  using ElementRange = boost::icl::discrete_interval<ElementType>;
  using ElementRangeSet = boost::icl::interval_set<ElementType>;
  ElementRangeSet interval_set_;
};

} // namespace yb
