// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/memtablerep.h"

namespace rocksdb {

class HashSkipListRepFactory : public MemTableRepFactory {
 public:
  explicit HashSkipListRepFactory(
    size_t bucket_count,
    int32_t skiplist_height,
    int32_t skiplist_branching_factor)
      : bucket_count_(bucket_count),
        skiplist_height_(skiplist_height),
        skiplist_branching_factor_(skiplist_branching_factor) { }

  virtual ~HashSkipListRepFactory() {}

  virtual MemTableRep* CreateMemTableRep(
      const MemTableRep::KeyComparator& compare, MemTableAllocator* allocator,
      const SliceTransform* transform, Logger* logger) override;

  virtual const char* Name() const override {
    return "HashSkipListRepFactory";
  }

 private:
  const size_t bucket_count_;
  const int32_t skiplist_height_;
  const int32_t skiplist_branching_factor_;
};

}
