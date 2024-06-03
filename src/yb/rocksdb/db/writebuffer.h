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
//
// WriteBuffer is for managing memory allocation for one or more MemTables.

#pragma once

#include <atomic>
#include <memory>

#include "yb/rocksdb/memory_monitor.h"

namespace rocksdb {

class WriteBuffer {
 public:
  WriteBuffer(size_t _buffer_size,
              std::shared_ptr<MemoryMonitor> memory_monitor = nullptr)
    : buffer_size_(_buffer_size), memory_monitor_(memory_monitor) {}

  ~WriteBuffer() {}

  size_t memory_usage() const {
    return memory_used_.load(std::memory_order_relaxed);
  }
  size_t buffer_size() const { return buffer_size_; }

  // Should only be called from write thread
  bool ShouldFlush() const {
    return buffer_size() > 0 && memory_usage() >= buffer_size();
  }

  // Should only be called from write thread
  void ReserveMem(size_t mem) {
    memory_used_.fetch_add(mem, std::memory_order_relaxed);
    if (memory_monitor_) {
      memory_monitor_->ReservedMem(mem);
    }
  }
  void FreeMem(size_t mem) {
    memory_used_.fetch_sub(mem, std::memory_order_relaxed);
    if (memory_monitor_) {
      memory_monitor_->FreedMem(mem);
    }
  }

 private:
  const size_t buffer_size_;
  std::atomic<size_t> memory_used_{0};
  std::shared_ptr<MemoryMonitor> memory_monitor_;

  // No copying allowed
  WriteBuffer(const WriteBuffer&);
  void operator=(const WriteBuffer&);
};

}  // namespace rocksdb
