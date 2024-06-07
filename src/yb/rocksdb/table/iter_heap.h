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

#pragma once

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/table/iterator_wrapper.h"

namespace rocksdb {

// When used with std::priority_queue, this comparison functor puts the
// iterator with the max/largest key on top.
template <typename IteratorType>
class MaxIteratorComparator {
 public:
  explicit MaxIteratorComparator(const Comparator* comparator) :
    comparator_(comparator) {}

  bool operator()(IteratorType* a, IteratorType* b) const {
    return comparator_->Compare(a->key(), b->key()) < 0;
  }
 private:
  const Comparator* comparator_;
};

// When used with std::priority_queue, this comparison functor puts the
// iterator with the min/smallest key on top.
template <typename IteratorType>
class MinIteratorComparator {
 public:
  explicit MinIteratorComparator(const Comparator* comparator) :
    comparator_(comparator) {}

  bool operator()(IteratorType* a, IteratorType* b) const {
    return comparator_->Compare(a->key(), b->key()) > 0;
  }
 private:
  const Comparator* comparator_;
};

}  // namespace rocksdb
