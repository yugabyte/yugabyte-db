// Copyright (c) YugaByte, Inc.

#include "yb/util/shared_lock_manager.h"

#include <glog/logging.h>

#include "yb/util/enums.h"
#include "yb/util/bytes_formatter.h"

using std::string;
using yb::util::to_underlying;

namespace yb {
namespace util {

const char* LockTypeToStr(LockType lock_type) {
  switch (lock_type) {
    case LockType::EXCLUSIVE:
      return "EXCLUSIVE";
    case LockType::SHARED:
      return "SHARED";
  }
  LOG(FATAL) << "Invalid LockType value: " << to_underlying(lock_type);
}

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
  VLOG(4) << "Locking " << LockTypeToStr(lock_type) << ": " << FormatBytesAsStr(key);
  std::lock_guard<std::mutex> lock(global_mutex_);
  // If the key doesn't exist, a LockEntry is created using the default constructor.
  locks_[key].Lock(lock_type, &global_mutex_);
}

LockType SharedLockManager::Unlock(string key) {
  VLOG(4) << "Unlocking " << FormatBytesAsStr(key);
  std::lock_guard<std::mutex> lock(global_mutex_);
  const bool no_waiters = locks_[key].num_waiters == 0;
  const LockType lock_type = locks_[key].Unlock();
  if (no_waiters && locks_[key].num_shared == 0) {
    // Deallocate LockEntry.
    locks_.erase(key);
  }
  return lock_type;
}

}  // namespace util
}  // namespace yb
