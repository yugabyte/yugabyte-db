// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_SHARED_LOCK_MANAGER_H_
#define YB_UTIL_SHARED_LOCK_MANAGER_H_

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/gutil/spinlock.h"
#include "yb/util/shared_lock_manager_fwd.h"
#include "yb/util/cross_thread_mutex.h"

namespace yb {
namespace util {

const char* ToString(LockType lock_type);

// This class manages six types of locks on string keys. On each key, the possibilities are:
// - No locks
// - A single SI_WRITE_STRONG
// - Multiple SR_READ_STRONG and SR_READ_WEAK
// - Multiple SR_WRITE_STRONG and SR_WRITE_WEAK
// - Multiple SI_WRITE_WEAK, SR_READ_WEAK, and SR_WRITE_WEAK
class SharedLockManager {
 public:

  // Attempt to lock the key with certain type. The call may be blocked waiting for other
  // locks to be released. If LockEntry doesn't exist, it creates a LockEntry.
  void Lock(std::string key, LockType lock_type) {
    Lock({ {std::move(key), lock_type} });
  }

  // Attempt to lock a batch of keys. The call may be blocked waiting for other locks to
  // be released. If the entries don't exist, they are created.
  void Lock(const LockBatch& batch);

  // Release the batch of locks. Requires that the locks are held.
  void Unlock(const LockBatch& batch);

  // Release one lock of the specified type. Requires that the lock is held by the specified
  // type. If all locks are released, the LockEntry is deallocated.
  void Unlock(std::string key, LockType lock_type) {
    Unlock({ {std::move(key), lock_type} });
  }

  // Whether or not the state is possible
  static bool VerifyState(LockState state);

 private:

  struct LockEntry {
    // Taken only for short duration, with no blocking wait.
    std::mutex mutex;

    std::condition_variable cond_var;

    // Refcounting for garbage collection. Can only be used while the global lock is held.
    size_t num_using = 0;

    // Number of holders for each type
    size_t num_holding[NUM_LOCK_TYPES] {0};
    LockState state = 0;

    void Lock(LockType lock_type);

    void Unlock(LockType lock_type);

  };

  typedef std::unordered_map<std::string, std::unique_ptr<LockEntry>> LockEntryMap;

  // Make sure the entries exist in the locks_ map and return pointers so we can access
  // them without holding the global lock. Returns a vector with pointers in the same order
  // as the keys in the batch.
  std::vector<LockEntry*> Reserve(const LockBatch& batch);

  // Update refcounts and maybe collect garbage.
  void Cleanup(const LockBatch& batch);

  // The global mutex should be taken only for very short duration, with no blocking wait.
  std::mutex global_mutex_;

  // Can only be modified if the global mutex is held.
  LockEntryMap locks_;
};

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_SHARED_LOCK_MANAGER_H_
