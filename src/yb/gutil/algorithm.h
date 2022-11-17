// Copyright 2006 Google Inc.
//
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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// ---
//
//
// This file contains some Google extensions to the standard
// <algorithm> C++ header. Many of these algorithms were in the
// original STL before it was proposed for standardization.

#pragma once

#include <algorithm>
#include <functional>


namespace util {
namespace gtl {

// Returns true if [first, last) contains an element equal to value.
// Complexity: linear.
template<typename InputIterator, typename EqualityComparable>
bool contains(InputIterator first, InputIterator last,
              const EqualityComparable& value) {
  return std::find(first, last, value) != last;
}

// There is no contains_if().  Use any() instead.

template<typename InputIterator, typename ForwardIterator>
bool contains_some_of(InputIterator first1, InputIterator last1,
                      ForwardIterator first2, ForwardIterator last2) {
  return std::find_first_of(first1, last1, first2, last2) != last1;
}

template<typename InputIterator, typename ForwardIterator, typename Predicate>
bool contains_some_of(InputIterator first1, InputIterator last1,
                      ForwardIterator first2, ForwardIterator last2,
                      Predicate pred) {
  return std::find_first_of(first1, last1, first2, last2, pred) != last1;
}

template<typename InputIterator, typename EqualityComparable>
typename std::iterator_traits<InputIterator>::pointer
find_or_null(InputIterator first, InputIterator last,
             const EqualityComparable& value) {
  const InputIterator it = std::find(first, last, value);
  return it != last ? &*it : NULL;
}

template<typename InputIterator, typename Predicate>
typename std::iterator_traits<InputIterator>::pointer
find_if_or_null(InputIterator first, InputIterator last, Predicate pred) {
  const InputIterator it = std::find_if(first, last, pred);
  return it != last ? &*it : NULL;
}

// Copies all elements that satisfy the predicate pred from [first,
// last) to out. This is the complement of remove_copy_if. Complexity:
// exactly last-first applications of pred.
template <typename InputIterator, typename OutputIterator, typename Predicate>
OutputIterator copy_if(InputIterator first, InputIterator last,
                       OutputIterator out,
                       Predicate pred) {
  for (; first != last; ++first) {
    if (pred(*first))
      *out++ = *first;
  }
  return out;
}

// Copies n elements to out.  Equivalent to copy(first, first + n, out) for
// random access iterators and a longer code block for lesser iterators.
template <typename InputIterator, typename Size, typename OutputIterator>
OutputIterator copy_n(InputIterator first, Size n, OutputIterator out) {
  while (n > 0) {
    *out = *first;
    ++out;
    ++first;
    --n;
  }
  return out;
}

// Returns true if pred is true for every element in [first, last). Complexity:
// at most last-first applications of pred.
template <typename InputIterator, typename Predicate>
bool all(InputIterator first, InputIterator last, Predicate pred) {
  for (; first != last; ++first) {
    if (!pred(*first))
      return false;
  }
  return true;
}

// Returns true if pred is false for every element in [first,
// last). Complexity: at most last-first applications of pred.
template <typename InputIterator, typename Predicate>
bool none(InputIterator first, InputIterator last, Predicate pred) {
  return find_if(first, last, pred) == last;
}

// Returns true if pred is true for at least one element in [first, last).
// Complexity: at most last-first applications of pred.
template <typename InputIterator, typename Predicate>
bool any(InputIterator first, InputIterator last, Predicate pred) {
  return !none(first, last, pred);
}

// Returns a pair of iterators p such that p.first points to the
// minimum element in the range and p.second points to the maximum,
// ordered by comp. Complexity: at most floor((3/2) (N-1))
// comparisons. Postcondition: If the return value is <min, max>, min is
// the first iterator i in [first, last) such that comp(*j, *i) is
// false for every other iterator j, and max is the last iterator i
// such that comp(*i, *j) is false for every other iterator j. Or less
// formally, min is the first minimum and max is the last maximum.
template <typename ForwardIter, typename Compare>
std::pair<ForwardIter, ForwardIter> minmax_element(ForwardIter first,
                                                   const ForwardIter last,
                                                   Compare comp) {
  // Initialization: for N<2, set min=max=first. For N >= 2, set min
  // to be the smaller of the first two and max to be the larger. (Taking
  // care that min is the first, and max the second, if the two compare
  // equivalent.)
  ForwardIter min(first);
  ForwardIter max(first);

  if (first != last) {
    ++first;
  }

  if (first != last) {
    max = first;
    if (comp(*max, *min))
      swap(min, max);
    ++first;
  }

  while (first != last) {
    ForwardIter next(first);
    ++next;

    if (next != last) {
      // We have two elements to look at that we haven't seen already,
      // *first and *next. Compare the smaller of them with the
      // current min, and the larger against the current max. The
      // subtle point: write all of the comparisons so that, if the
      // two things being compared are equivalent, we take the first
      // one as the smaller and the last as the larger.
      if (comp(*next, *first)) {
        if (comp(*next, *min))
          min = next;
        if (!comp(*first, *max))
          max = first;
      } else {
        if (comp(*first, *min))
          min = first;
        if (!comp(*next, *max))
          max = next;
      }
    } else {
      // There is only one element left that we haven't seen already, *first.
      // Adjust min or max, as appropriate, and exit the loop.
      if (comp(*first, *min)) {
        min = first;
      } else if (!comp(*first, *max)) {
        max = first;
      }
      break;
    }

    first = next;
    ++first;
  }

  return make_pair(min, max);
}

// Returns a pair of iterators p such that p.first points to the first
// minimum element in the range and p.second points to the last
// maximum, ordered by operator<.
template <typename ForwardIter>
inline std::pair<ForwardIter, ForwardIter> minmax_element(ForwardIter first,
                                                          ForwardIter last) {
  typedef typename std::iterator_traits<ForwardIter>::value_type value_type;
  return util::gtl::minmax_element(first, last, std::less<value_type>());
}

// Returns true if [first, last) is partitioned by pred, i.e. if all
// elements that satisfy pred appear before those that do
// not. Complexity: linear.
template <typename ForwardIterator, typename Predicate>
inline bool is_partitioned(ForwardIterator first, ForwardIterator last,
                           Predicate pred) {
  for (; first != last; ++first) {
    if (!pred(*first)) {
      ++first;
      return util::gtl::none(first, last, pred);
    }
  }
  return true;
}

// Precondition: is_partitioned(first, last, pred). Returns: the
// partition point p, i.e. an iterator mid satisfying the conditions
// all(first, mid, pred) and none(mid, last, pred).  Complexity:
// O(log(last-first)) applications of pred.
template <typename ForwardIterator, typename Predicate>
ForwardIterator partition_point(ForwardIterator first, ForwardIterator last,
                                Predicate pred) {
  typedef typename std::iterator_traits<ForwardIterator>::difference_type diff;
  diff n = distance(first, last);

  // Loop invariant: n == distance(first, last)
  while (first != last) {
    diff half = n/2;
    ForwardIterator mid = first;
    advance(mid, half);
    if (pred(*mid)) {
      first = mid;
      ++first;
      n -= half+1;
    } else {
      n = half;
      last = mid;
    }
  }

  return first;
}

// Copies all elements that satisfy pred to out_true and all elements
// that don't satisfy it to out_false. Returns: a pair p such that
// p.first is the end of the range beginning at out_t and p.second is
// the end of the range beginning at out_f. Complexity: exactly
// last-first applications of pred.
template <typename InputIterator,
          typename OutputIterator1, typename OutputIterator2,
          typename Predicate>
std::pair<OutputIterator1, OutputIterator2>
partition_copy(InputIterator first, InputIterator last,
               OutputIterator1 out_true, OutputIterator2 out_false,
               Predicate pred) {
  for (; first != last; ++first) {
    if (pred(*first))
      *out_true++ = *first;
    else
      *out_false++ = *first;
  }
  return make_pair(out_true, out_false);
}

// Reorders elements in [first, last), so that for each consecutive group
// of duplicate elements (according to eq predicate) the first one is left and
// others are moved at the end of the range. Returns: iterator middle such that
// [first, middle) contains no two consecutive elements that are duplicates and
// [middle, last) contains elements removed from all groups. It's stable for
// range [first, middle) meaning the order of elements are the same as order of
// their corresponding groups in input, but the order in range [middle, last)
// is not preserved. Function is similar to std::unique, but ensures that
// removed elements are properly copied and accessible at the range's end.
// Complexity: exactly last-first-1 applications of eq; at most middle-first-1
// swap operations.
template <typename ForwardIterator, typename Equals>
ForwardIterator unique_partition(ForwardIterator first, ForwardIterator last,
                                 Equals eq) {
  first = adjacent_find(first, last, eq);
  if (first == last)
    return last;

  // Points to right-most element within range of unique elements being built.
  ForwardIterator result = first;

  // 'first' iterator goes through the sequence starting from element after
  // first equal elements pair (found by adjacent_find above).
  ++first;
  while (++first != last) {
    // If we encounter an element that isn't equal to right-most element in
    // result, then extend the range and swap this element into it.
    // Otherwise just continue incrementing 'first'.
    if (!eq(*result, *first)) {
      swap(*++result, *first);
    }
  }
  // Return past-the-end upper-bound of the resulting range.
  return ++result;
}

// Reorders elements in [first, last) range moving duplicates for each
// consecutive group of elements to the end. Equality is checked using ==.
template <typename ForwardIterator>
inline ForwardIterator unique_partition(ForwardIterator first,
                                        ForwardIterator last) {
  typedef typename std::iterator_traits<ForwardIterator>::value_type T;
  return unique_partition(first, last, std::equal_to<T>());
}

// Samples k elements from the next n.
// Elements have the same order in the output stream as they did on input.
//
// This is Algorithm S from section 3.4.2 of Knuth, TAOCP, 2nd edition.
// My k corresponds to Knuth n-m.
// My n corrsponds to Knuth N-t.
//
// RngFunctor is any functor that can be called as:
//   size_t RngFunctor(size_t x)
// The return value is an integral value in the half-open range [0, x)
// such that all values are equally likely.  Behavior is unspecified if x==0.
// (This function never calls RngFunctor with x==0).

template <typename InputIterator, typename OutputIterator, typename RngFunctor>
inline void sample_k_of_n(InputIterator in, size_t k, size_t n,
                          RngFunctor& rng, OutputIterator out) { // NOLINT
  if (k > n) {
    k = n;
  }
  while (k > 0) {
    if (rng(n) < k) {
      *out++ = *in;
      k--;
    }
    ++in;
    --n;
  }
}

// Finds the longest prefix of a range that is a binary max heap with
// respect to a given StrictWeakOrdering.  If first == last, returns last.
// Otherwise, return an iterator it such that [first,it) is a heap but
// no longer prefix is -- in other words, first + i for the lowest i
// such that comp(first[(i-1)/2], first[i]) returns true.
template <typename RandomAccessIterator, typename StrictWeakOrdering>
RandomAccessIterator gtl_is_binary_heap_until(RandomAccessIterator first,
                                              RandomAccessIterator last,
                                              StrictWeakOrdering comp) {
  if (last - first < 2) return last;
  RandomAccessIterator parent = first;
  bool is_right_child = false;
  for (RandomAccessIterator child = first + 1; child != last; ++child) {
    if (comp(*parent, *child)) return child;
    if (is_right_child) ++parent;
    is_right_child = !is_right_child;
  }
  return last;
}

// Special case of gtl_is_binary_heap_until where the order is std::less,
// i.e., where we're working with a simple max heap.
template <typename RandomAccessIterator>
RandomAccessIterator gtl_is_binary_heap_until(RandomAccessIterator first,
                                              RandomAccessIterator last) {
  typedef typename std::iterator_traits<RandomAccessIterator>::value_type T;
  return gtl_is_binary_heap_until(first, last, std::less<T>());
}

// Checks whether a range of values is a binary heap, i.e., checks that
// no node is less (as defined by a StrictWeakOrdering) than a child.
template <typename RandomAccessIterator, typename StrictWeakOrdering>
bool gtl_is_binary_heap(RandomAccessIterator begin,
                        RandomAccessIterator end,
                        StrictWeakOrdering comp) {
  return gtl_is_binary_heap_until(begin, end, comp) == end;
}

// Special case of gtl_is_binary_heap where the order is std::less (i.e.,
// where we're working on a simple max heap).
template <typename RandomAccessIterator>
bool gtl_is_binary_heap(RandomAccessIterator begin,
                        RandomAccessIterator end) {
  return gtl_is_binary_heap_until(begin, end) == end;
}

// Unqualified calls to is_heap are ambiguous with some build types,
// namespace that can clash with names that C++11 added to ::std.
// By calling util::gtl::is_heap, clients can avoid those errors,
// and by using the underlying is_heap call we ensure consistency
// with the standard library's heap implementation just in case a
// standard library ever uses anything other than a binary heap.
#if defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus > 199711L \
  || defined(LIBCXX) || _MSC_VER >= 1600 /* Visual Studio 2010 */
#elif defined __GNUC__
/* using __gnu_cxx::is_heap; */
#elif defined _MSC_VER
// For old versions of MSVC++, we know by inspection that make_heap()
// traffics in binary max heaps, so gtl_is_binary_heap is an acceptable
// implementation for is_heap.
template <typename RandomAccessIterator>
bool is_heap(RandomAccessIterator begin, RandomAccessIterator end) {
  return gtl_is_binary_heap(begin, end);
}

template <typename RandomAccessIterator, typename StrictWeakOrdering>
bool is_heap(RandomAccessIterator begin,
             RandomAccessIterator end,
             StrictWeakOrdering comp) {
  return gtl_is_binary_heap(begin, end, comp);
}
#else
// We need an implementation of is_heap that matches the library's
// implementation of make_heap() and friends.  gtl_is_binary_heap will
// *probably* work, but let's be safe and not make that assumption.
#error No implementation of is_heap defined for this toolchain.
#endif

}  // namespace gtl
}  // namespace util
