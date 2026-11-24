// Copyright (c) YugabyteDB, Inc.
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

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "yb/gutil/thread_annotations.h"

#include "yb/util/status.h"
#include "yb/util/unique_lock.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb::pgwrapper {

class PGConnPool;

// Connection acquired from a PGConnPool, returned to the pool on destruction.
class PooledPGConn {
 public:
  PooledPGConn() = default;
  PooledPGConn(PGConnPool* pool, std::unique_ptr<PGConn> conn)
      : pool_(pool), conn_(std::move(conn)) {}
  ~PooledPGConn() { Release(); }

  PooledPGConn(PooledPGConn&& other) noexcept
      : pool_(other.pool_), conn_(std::move(other.conn_)) {
    other.pool_ = nullptr;
  }
  PooledPGConn& operator=(PooledPGConn&& other) noexcept {
    if (this != &other) {
      Release();
      pool_ = other.pool_;
      conn_ = std::move(other.conn_);
      other.pool_ = nullptr;
    }
    return *this;
  }

  PooledPGConn(const PooledPGConn&) = delete;
  PooledPGConn& operator=(const PooledPGConn&) = delete;

  PGConn* operator->() const { return conn_.get(); }
  PGConn& operator*() const { return *conn_; }

 private:
  void Release();

  PGConnPool* pool_ = nullptr;
  std::unique_ptr<PGConn> conn_;
};

// Thread-safe fixed-size pool of PGConn instances.
class PGConnPool {
 public:
  PGConnPool() = default;
  PGConnPool(const PGConnPool&) = delete;
  PGConnPool& operator=(const PGConnPool&) = delete;

  // Opens `size` connections.
  Status Init(const PGConnBuilder& builder, size_t size) {
    std::lock_guard lock(mutex_);
    available_.reserve(size);
    for (size_t i = 0; i < size; ++i) {
      auto conn = VERIFY_RESULT(builder.Connect(/* simple_query_protocol= */ false));
      available_.push_back(std::make_unique<PGConn>(std::move(conn)));
    }
    return Status::OK();
  }

  // Blocks until a connection is available.
  PooledPGConn Acquire() {
    UniqueLock lock(mutex_);
    WaitOnConditionVariable(&cv_, &lock, [this]() REQUIRES(mutex_) { return !available_.empty(); });
    auto conn = std::move(available_.back());
    available_.pop_back();
    return PooledPGConn(this, std::move(conn));
  }

 private:
  friend class PooledPGConn;

  void Release(std::unique_ptr<PGConn> conn) {
    std::lock_guard lock(mutex_);
    available_.push_back(std::move(conn));
    cv_.notify_one();
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::unique_ptr<PGConn>> available_ GUARDED_BY(mutex_);
};

inline void PooledPGConn::Release() {
  if (pool_ && conn_) {
    pool_->Release(std::move(conn_));
  }
}

}  // namespace yb::pgwrapper
