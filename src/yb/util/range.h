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

#ifndef YB_UTIL_RANGE_H
#define YB_UTIL_RANGE_H

namespace yb {

template <class Int>
class RangeIterator : public std::iterator<std::random_access_iterator_tag, Int> {
 public:
  RangeIterator(Int pos, Int step) : pos_(pos), step_(step) {}

  Int operator*() const {
    return pos_;
  }

  Int operator++() {
    pos_ += step_;
    return pos_;
  }

  Int operator++(int) {
    auto result = pos_;
    pos_ += step_;
    return result;
  }

 private:
  friend bool operator==(const RangeIterator<Int>& lhs, const RangeIterator<Int>& rhs) {
    return lhs.pos_ == rhs.pos_;
  }

  friend bool operator!=(const RangeIterator<Int>& lhs, const RangeIterator<Int>& rhs) {
    return lhs.pos_ != rhs.pos_;
  }

  Int pos_;
  Int step_;
};

template <class Int>
class RangeObject {
 public:
  using const_iterator = RangeIterator<Int>;

  RangeObject(Int start, Int stop, Int step)
      : start_(start), stop_(start + (stop - start + step - 1) / step * step), step_(step) {}

  const_iterator begin() const {
    return const_iterator(start_, step_);
  }

  const_iterator end() const {
    return const_iterator(stop_, step_);
  }

 private:
  Int start_;
  Int stop_;
  Int step_;
};

// Useful to iterate over range of ints. Especially if we should repeat this iteration several
// times like we do in tests.
template<class Int>
RangeObject<Int> Range(Int stop) {
  return RangeObject<Int>(0, stop, 1);
}

template<class Int>
RangeObject<Int> Range(Int start, Int stop, Int step = 1) {
  return RangeObject<Int>(start, stop, step);
}

}  // namespace yb

#endif  // YB_UTIL_RANGE_H
