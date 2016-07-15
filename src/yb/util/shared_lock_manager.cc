// Copyright (c) YugaByte, Inc.

#include "shared_lock_manager.h"

using std::string;

namespace yb {
namespace util {

void SharedLockManager::LockEntry::Lock(LockType lock_type, std::mutex* global_mutex) {
  if (lock_type == LockType::EXCLUSIVE || num_shared == 0) {
    num_waiters++;
    global_mutex->unlock();
    mutex.lock();
    global_mutex->lock();
    num_waiters--;
  }
  if (lock_type == LockType::SHARED) {
    num_shared++;
  }
}

LockType SharedLockManager::LockEntry::Unlock() {
  if (num_shared > 0) {
    num_shared--;
    if (num_shared == 0) mutex.unlock();
    return LockType::SHARED;
  }
  mutex.unlock();
  return LockType::EXCLUSIVE;
}

void SharedLockManager::Lock(string key, LockType lock_type) {
  std::lock_guard<std::mutex> lock(global_mutex_);
  // If the key doesn't exist, a LockEntry is created using the default constructor.
  locks_[key].Lock(lock_type, &global_mutex_);
}

LockType SharedLockManager::Unlock(string key) {
  std::lock_guard<std::mutex> lock(global_mutex_);
  const bool no_waiters = locks_[key].num_waiters == 0;
  const LockType lock_type = locks_[key].Unlock();
  if (no_waiters && locks_[key].num_shared == 0) {
    // Deallocate LockEntry.
    locks_.erase(key);
  }
  return lock_type;
}

}
}
