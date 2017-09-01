// Copyright (c) YugaByte, Inc.
#ifndef ROCKSDB_INCLUDE_ROCKSDB_MEMORY_MONITOR_H
#define ROCKSDB_INCLUDE_ROCKSDB_MEMORY_MONITOR_H

#include <atomic>
#include <functional>
#include <utility>

#include "port/likely.h"

namespace rocksdb {

// Counts the total memory of the registered write_buffers, and notifies the
// callback if the limit is exceeded.
class MemoryMonitor {
 public:
  explicit MemoryMonitor(size_t limit, std::function<void()> exceeded_callback)
    : limit_(limit), exceeded_callback_(std::move(exceeded_callback)) {}

  ~MemoryMonitor() {}

  size_t memory_usage() const {
    return memory_used_.load(std::memory_order_relaxed);
  }

  size_t limit() const { return limit_; }

  bool Exceeded() const {
    return Exceeded(memory_usage());
  }

  void ReservedMem(size_t mem) {
    auto new_value = memory_used_.fetch_add(mem, std::memory_order_release) + mem;
    if (UNLIKELY(Exceeded(new_value))) {
      exceeded_callback_();
    }
  }

  void FreedMem(size_t mem) {
    memory_used_.fetch_sub(mem, std::memory_order_relaxed);
  }

  // No copying allowed
  MemoryMonitor(const MemoryMonitor&) = delete;
  void operator=(const MemoryMonitor&) = delete;

 private:

  bool Exceeded(size_t size) const {
    return limit() > 0 && size >= limit();
  }

  const size_t limit_;
  const std::function<void()> exceeded_callback_;
  std::atomic<size_t> memory_used_ {0};

};

}  // namespace rocksdb

#endif // ROCKSDB_INCLUDE_ROCKSDB_MEMORY_MONITOR_H
