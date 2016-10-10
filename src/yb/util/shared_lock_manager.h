// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_SHARED_LOCK_MANAGER_H_
#define YB_UTIL_SHARED_LOCK_MANAGER_H_

#include <string>
#include <unordered_map>
#include <mutex>
#include "yb/gutil/spinlock.h"
#include "yb/util/cross_thread_mutex.h"

namespace yb {
namespace util {

enum class LockType {
  EXCLUSIVE,
  SHARED
};

const char* LockTypeToStr(LockType lock_type);

// This class manages exclusive and shared locks on string keys.
// On each key, either there is one exclusive lock, or some shared locks.
// Shared locks don't block each other, but exclusive locks block everything else.
class SharedLockManager {
 public:
  // Attempt to lock the key with certain type. The call my be blocked waiting for other
  // locks to be released. If LockEntry doesn't exist, it creates a LockEntry.
  void Lock(std::string key, LockType lock_type);

  // If there is any shared lock, then releases a shared lock, else exclusive lock is released.
  // The type of lock that was released is returned. If all locks are released, then the LockEntry
  // is deallocated.
  LockType Unlock(std::string key);

  // TODO: Add lock and unlock functions that take slice as input.

 private:

  struct LockEntry {

    // The mutex is locked by the exclusive locker or the first shared locker.
    // Waiting on the mutex is not allowed while the global mutex is taken.
    CrossThreadMutex mutex;

    // Number of shared locks held.
    // This variable can be modified only if global mutex is taken.
    size_t num_shared;

    // Number of waiting (exclusive) lockers.
    // Also protected by the global mutex.
    size_t num_waiters;

    LockEntry() : mutex(), num_shared(0), num_waiters(0) {}

    void Lock(LockType lock_type, std::mutex* global_mutex);

    LockType Unlock();

  };

  // The global mutex should be taken only for very short duration, with no blocking wait.
  std::mutex global_mutex_;
  // Insertions or deletions of entries in the map can only be done if
  // the global mutex is acquired.
  std::unordered_map<std::string, LockEntry> locks_;
};

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_SHARED_LOCK_MANAGER_H_
