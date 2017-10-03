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

#ifndef KUDU_CFILE_BLOCK_HANDLE_H
#define KUDU_CFILE_BLOCK_HANDLE_H

#include "kudu/cfile/block_cache.h"

namespace kudu {

namespace cfile {

// When blocks are read, they are sometimes resident in the block cache, and sometimes skip the
// block cache. In the case that they came from the cache, we just need to dereference them when
// they stop being used. In the case that they didn't come from cache, we need to actually free
// the underlying data.
class BlockHandle {
  MOVE_ONLY_TYPE_FOR_CPP_03(BlockHandle, RValue);
 public:
  static BlockHandle WithOwnedData(const Slice& data) {
    return BlockHandle(data);
  }

  static BlockHandle WithDataFromCache(BlockCacheHandle *handle) {
    return BlockHandle(handle);
  }

  // Constructor to use to Pass to.
  BlockHandle()
    : is_data_owner_(false) { }

  // Emulated Move constructor
  BlockHandle(RValue other) { // NOLINT(runtime/explicit)
    TakeState(other.object);
  }
  BlockHandle& operator=(RValue other) {
    TakeState(other.object);
    return *this;
  }

  ~BlockHandle() {
    if (is_data_owner_) {
      delete [] data_.data();
    }
  }

  const Slice &data() const {
    if (is_data_owner_) {
      return data_;
    } else {
      return dblk_data_.data();
    }
  }

 private:
  BlockCacheHandle dblk_data_;
  Slice data_;
  bool is_data_owner_;

  explicit BlockHandle(Slice data)
      : data_(std::move(data)),
        is_data_owner_(true) {
  }

  explicit BlockHandle(BlockCacheHandle *dblk_data)
    : is_data_owner_(false) {
    dblk_data_.swap(dblk_data);
  }

  void TakeState(BlockHandle* other) {
    is_data_owner_ = other->is_data_owner_;
    if (is_data_owner_) {
      data_ = other->data_;
      other->is_data_owner_ = false;
    } else {
      dblk_data_.swap(&other->dblk_data_);
    }
  }
};

} // namespace cfile
} // namespace kudu
#endif
