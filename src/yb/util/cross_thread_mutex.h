// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_SHARED_MUTEX_H_
#define YB_UTIL_SHARED_MUTEX_H_

#include <mutex>
#include <condition_variable>

namespace yb {

// This is a wrapper around std::mutex which can be locked and unlocked from different threads.
class CrossThreadMutex {

private:
  std::mutex mutex;
  std::condition_variable condition_variable;
  bool has_lock;
public:
  CrossThreadMutex() : mutex(), condition_variable(), has_lock(false) {}

  void lock();

  void unlock();
};
} // namespace yb


#endif //PROJECT_SHARED_MUTEX_H
