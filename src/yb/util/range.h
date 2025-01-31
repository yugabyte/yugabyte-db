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

#include <iterator>
#include <type_traits>

namespace yb {

template <class It, class Container>
class ContainerRangeIterator {
 public:
  ContainerRangeIterator(It iterator, const Container& container)
      : iterator_(iterator), container_(&container) {}

  auto& operator*() const {
    return (*container_)[*iterator_];
  }

  ContainerRangeIterator& operator++() {
    ++iterator_;
    return *this;
  }

  ContainerRangeIterator operator++(int) {
    ContainerRangeIterator result = *this;
    ++iterator_;
    return result;
  }

 private:
  friend bool operator==(const ContainerRangeIterator<It, Container>& lhs,
                         const ContainerRangeIterator<It, Container>& rhs) {
    return lhs.iterator_ == rhs.iterator_;
  }

  friend bool operator!=(const ContainerRangeIterator<It, Container>& lhs,
                         const ContainerRangeIterator<It, Container>& rhs) {
    return lhs.iterator_ != rhs.iterator_;
  }

  It iterator_;
  const Container* container_;
};

template <class Range, class Container>
class ContainerRangeObject {
 public:
  ContainerRangeObject(const Range& range, const Container& container)
      : range_(range), container_(&container) {}

  using const_iterator = ContainerRangeIterator<typename Range::const_iterator, Container>;

  const_iterator begin() const {
    return const_iterator(range_.begin(), *container_);
  }

  const_iterator end() const {
    return const_iterator(range_.end(), *container_);
  }

 private:
  Range range_;
  const Container* container_;
};

template <class Int>
class RangeIterator : public std::iterator<std::random_access_iterator_tag, Int> {
 public:
  RangeIterator(Int pos, Int step) : pos_(pos), step_(step) {}

  Int operator*() const {
    return pos_;
  }

  RangeIterator& operator++() {
    pos_ += step_;
    return *this;
  }

  RangeIterator operator++(int) {
    RangeIterator result = *this;
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
class RangeObject;

template <class Int>
class RangeObjectToContainerHelper {
 public:
  explicit RangeObjectToContainerHelper(const RangeObject<Int>& range) : range_(range) {}

  template <class Out>
  operator Out() const {
    return Out(range_.begin(), range_.end());
  }

 private:
  const RangeObject<Int>& range_;
};

template <class Int>
class RangeObject {
 public:
  using const_iterator = RangeIterator<Int>;

  RangeObject(Int start, Int stop, Int step)
      : start_(start), stop_(NormalizedStop(start, stop, step)), step_(step) {
  }

  [[nodiscard]] size_t size() const {
    return (stop_ - start_) / step_;
  }

  [[nodiscard]] const_iterator begin() const {
    return const_iterator(start_, step_);
  }

  [[nodiscard]] const_iterator end() const {
    return const_iterator(stop_, step_);
  }

  template <class T>
  [[nodiscard]] auto operator[](const T& container) const {
    return ContainerRangeObject<RangeObject<Int>, T>(*this, container);
  }

  [[nodiscard]] RangeObject<Int> Reversed() const {
    static_assert(std::is_signed_v<Int>);
    const auto new_step = -step_;
    auto new_start = stop_ - (new_step > 0 ? -1 : 1);
    auto new_stop = new_start + static_cast<Int>(size()) * new_step;
    return {new_start, new_stop, new_step};
  }

  RangeObjectToContainerHelper<Int> ToContainer() const {
    return RangeObjectToContainerHelper(*this);
  }

 private:
  [[nodiscard]] static Int NormalizedStop(Int start, Int stop, Int step) {
    auto diff = stop - start;
    auto step_count = diff / step;
    if (diff % step) {
      ++step_count;
    }
    return start + step_count * step;
  }

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

template<class Int>
RangeObject<Int> RangeOfSize(Int start, Int size, Int step = 1) {
  return RangeObject<Int>(start, start + size, step);
}

}  // namespace yb
