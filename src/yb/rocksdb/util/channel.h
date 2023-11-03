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

#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>

#pragma once

namespace rocksdb {

template <class T>
class channel {
 public:
  explicit channel() : eof_(false) {}

  channel(const channel&) = delete;
  void operator=(const channel&) = delete;

  void sendEof() {
    std::lock_guard lk(lock_);
    eof_ = true;
    cv_.notify_all();
  }

  bool eof() {
    std::lock_guard lk(lock_);
    return buffer_.empty() && eof_;
  }

  size_t size() const {
    std::lock_guard lk(lock_);
    return buffer_.size();
  }

  // writes elem to the queue
  void write(T&& elem) {
    std::unique_lock<std::mutex> lk(lock_);
    buffer_.emplace(std::forward<T>(elem));
    cv_.notify_one();
  }

  /// Moves a dequeued element onto elem, blocking until an element
  /// is available.
  // returns false if EOF
  bool read(T& elem) {
    std::unique_lock<std::mutex> lk(lock_);
    cv_.wait(lk, [&] { return eof_ || !buffer_.empty(); });
    if (eof_ && buffer_.empty()) {
      return false;
    }
    elem = std::move(buffer_.front());
    buffer_.pop();
    cv_.notify_one();
    return true;
  }

 private:
  std::condition_variable cv_;
  std::mutex lock_;
  std::queue<T> buffer_;
  bool eof_;
};
}  // namespace rocksdb
