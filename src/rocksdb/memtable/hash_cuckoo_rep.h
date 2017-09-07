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
#ifndef ROCKSDB_LITE
#include "port/port.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/memtablerep.h"

namespace rocksdb {

class HashCuckooRepFactory : public MemTableRepFactory {
 public:
  // maxinum number of hash functions used in the cuckoo hash.
  static const unsigned int kMaxHashCount = 10;

  explicit HashCuckooRepFactory(size_t write_buffer_size,
                                size_t average_data_size,
                                unsigned int hash_function_count)
      : write_buffer_size_(write_buffer_size),
        average_data_size_(average_data_size),
        hash_function_count_(hash_function_count) {}

  virtual ~HashCuckooRepFactory() {}

  virtual MemTableRep* CreateMemTableRep(
      const MemTableRep::KeyComparator& compare, MemTableAllocator* allocator,
      const SliceTransform* transform, Logger* logger) override;

  virtual const char* Name() const override { return "HashCuckooRepFactory"; }

 private:
  size_t write_buffer_size_;
  size_t average_data_size_;
  const unsigned int hash_function_count_;
};
}  // namespace rocksdb
#endif  // ROCKSDB_LITE
