//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once

#include <memory>

#include "yb/rocksdb/rocksdb_fwd.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/iterator_wrapper.h"

namespace rocksdb {

class Comparator;
class InternalIterator;
class Env;
class Arena;

// Return an iterator that provided the union of the data in
// children[0,n-1].  Takes ownership of the child iterators and
// will delete them when the result iterator is deleted.
//
// The result does no duplicate suppression.  I.e., if a particular
// key is present in K child iterators, it will be yielded K times.
//
// REQUIRES: n >= 0
InternalIterator* NewMergingIterator(
    const Comparator* comparator, InternalIterator** children, int n, Arena* arena = nullptr);

constexpr size_t kMergeIteratorNumReserved = 4;

// A builder class to build a merging iterator by adding iterators one by one.
template <typename IteratorWrapperType>
class MergeIteratorBuilderBase {
 public:
  using IteratorType = typename IteratorWrapperType::IteratorType;
  // comparator: the comparator used in merging comparator
  // arena: where the merging iterator needs to be allocated from.
  explicit MergeIteratorBuilderBase(const Comparator* comparator, Arena* arena);
  ~MergeIteratorBuilderBase() {}

  // Add iter to the merging iterator.
  void AddIterator(InternalIterator* iter);

  // Get arena used to build the merging iterator. It is called one a child
  // iterator needs to be allocated.
  Arena* GetArena() { return arena_; }

  // Return the result merging iterator.
  InternalIterator* Finish();

  void SetupIteratorFilter(
      const IteratorFilter* filter, const QueryOptions& filter_options) {
    filter_ = filter;
    filter_options_ = filter_options;
  }

 private:
  const Comparator* const comparator_;
  Arena* const arena_;
  boost::container::small_vector<IteratorWrapperType, kMergeIteratorNumReserved> iterators_;
  const IteratorFilter* filter_ = nullptr;
  QueryOptions filter_options_;
};

template <typename IteratorType>
class MergingIterator : public InternalIterator {
 public:
  virtual IteratorType* GetCurrentIterator() = 0;
};

// Same as MergeIteratorBuilder but uses heap instead of arena.
// DO NOT USE for critical code paths.
template <typename IteratorWrapperType>
class MergeIteratorInHeapBuilder {
 public:
  using IteratorType = typename IteratorWrapperType::IteratorType;
  explicit MergeIteratorInHeapBuilder(const Comparator* comparator);
  ~MergeIteratorInHeapBuilder();

  // Add iter to the merging iterator.
  void AddIterator(IteratorType* iter);

  // Return the result merging iterator.
  std::unique_ptr<MergingIterator<IteratorType>> Finish();

 private:
  const Comparator* const comparator_;
  boost::container::small_vector<IteratorWrapperType, kMergeIteratorNumReserved> iterators_;
};

}  // namespace rocksdb
