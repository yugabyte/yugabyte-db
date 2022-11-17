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

#include <deque>

#include "yb/util/result.h"
#include "yb/util/std_util.h"

namespace yb {

// NumberedDeque maintains a deque with sequentially numbered (numbering is not necessarily 0-based
// as for std::deque or std::vector) elements without gaps in sequence numbers.
template <class SequenceNumber, class Element>
class NumberedDeque {
 public:
  typedef typename std::deque<Element>::value_type value_type;

  // We only provide const iterators to minimize risk of mixing up sequence numbers inside elements
  // after insertion into deque.
  typedef typename std::deque<Element>::const_iterator const_iterator;

  typedef typename std::deque<Element>::const_reverse_iterator const_reverse_iterator;

  typedef typename std::deque<Element>::const_reference const_reference;

  NumberedDeque() {}

  void clear() {
    inner_deque_.clear();
  }

  Status push_back(const SequenceNumber seq_num, const Element& element) {
    if (PREDICT_FALSE(inner_deque_.empty())) {
      first_seq_num_ = seq_num;
    } else {
      SCHECK_EQ(
          seq_num, first_seq_num_ + inner_deque_.size(), InvalidArgument,
          Format(
              "push_back: unexpected sequence number (first_seq_num: $0, size: $1)", first_seq_num_,
              inner_deque_.size()));
    }
    inner_deque_.push_back(element);
    return Status::OK();
  }

  Status push_front(const SequenceNumber seq_num, const Element& element) {
    if (PREDICT_FALSE(inner_deque_.empty())) {
      first_seq_num_ = seq_num;
    } else {
      if (std::is_unsigned<SequenceNumber>()) {
        SCHECK_GT(first_seq_num_, 0, IllegalState, "push_front: sequence number underflow");
      }
      SCHECK_EQ(
          seq_num, first_seq_num_ - 1, InvalidArgument,
          Format("push_front: unexpected sequence number (first_seq_num: $0)", first_seq_num_));
      --first_seq_num_;
    }
    inner_deque_.push_front(element);
    return Status::OK();
  }

  void assign(const NumberedDeque<SequenceNumber, Element>& other) {
    inner_deque_.assign(other.inner_deque_.begin(), other.inner_deque_.end());
    first_seq_num_ = other.first_seq_num_;
  }

  bool empty() const { return inner_deque_.empty(); }

  const_iterator begin() const { return inner_deque_.begin(); }
  const_iterator end() const { return inner_deque_.end(); }

  const_iterator cbegin() const { return inner_deque_.cbegin(); }
  const_iterator cend() const { return inner_deque_.cend(); }

  const_reverse_iterator rbegin() const { return inner_deque_.rbegin(); }
  const_reverse_iterator rend() const { return inner_deque_.rend(); }

  Result<const_reference> back() const {
    SCHECK(!inner_deque_.empty(), IllegalState, "back(): deque shouldn't be empty");
    return inner_deque_.back();
  }

  Result<const_reference> front() const {
    SCHECK(!inner_deque_.empty(), IllegalState, "front(): deque shouldn't be empty");
    return inner_deque_.front();
  }

  Status pop_back() {
    SCHECK(!inner_deque_.empty(), IllegalState, "pop_back(): deque shouldn't be empty");
    inner_deque_.pop_back();
    return Status::OK();
  }

  Status pop_front() {
    SCHECK(!inner_deque_.empty(), IllegalState, "pop_front(): deque shouldn't be empty");
    inner_deque_.pop_front();
    ++first_seq_num_;
    return Status::OK();
  }

  size_t size() const { return inner_deque_.size(); }

  void truncate(const size_t new_size) {
    if (PREDICT_FALSE(new_size >= inner_deque_.size())) {
      return;
    }
    inner_deque_.erase(inner_deque_.begin() + new_size , inner_deque_.end());
  }

  void truncate(const const_iterator& cut_from_included) {
    if (PREDICT_FALSE(std_util::cmp_greater_equal(
            cut_from_included - inner_deque_.begin(), inner_deque_.size()))) {
      return;
    }
    inner_deque_.erase(cut_from_included, inner_deque_.end());
  }

  Result<const_iterator> iter_at(const SequenceNumber seq_num) const {
    if (inner_deque_.empty()) {
      return STATUS(NotFound, "Empty");
    }

    // We always have a contiguous set of elements, so we can find the requested element by its
    // offset relative to the first element.
    if (seq_num < first_seq_num_) {
      return STATUS_FORMAT(
          NotFound, "Sequence number $0 is earlier than first_seq_num ($1)", seq_num,
          first_seq_num_);
    }

    const auto idx = seq_num - first_seq_num_;
    if (std_util::cmp_greater_equal(idx, inner_deque_.size())) {
      return STATUS_FORMAT(
          NotFound, "Sequence number $0 is later than last available ($1)", seq_num,
          first_seq_num_ + inner_deque_.size() - 1);
    }
    return begin() + idx;
  }

  Result<const Element&> Get(const SequenceNumber seq_num) const {
    return *VERIFY_RESULT(iter_at(seq_num));
  }

 private:
  SequenceNumber first_seq_num_;
  std::deque<Element> inner_deque_;
};

} // namespace yb
