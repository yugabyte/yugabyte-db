// Copyright (c) YugaByte, Inc.

#include "cross_thread_mutex.h"

namespace yb {

void CrossThreadMutex::lock() {
  std::unique_lock<std::mutex> lk(mutex);
  condition_variable.wait(lk, [this]{return has_lock == false;});
  has_lock = true;
}

void CrossThreadMutex::unlock() {
  {
    std::lock_guard<std::mutex> lk(mutex);
    has_lock = false;
  }
  condition_variable.notify_one();
}

} // namespace yb
