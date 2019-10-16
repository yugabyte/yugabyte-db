// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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

#include "yb/tablet/lock_manager.h"

#include <semaphore.h>

#include <atomic>
#include <mutex>
#include <string>

#include <boost/thread/shared_mutex.hpp>
#include <glog/logging.h>
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/hash/city.h"
#include "yb/util/locks.h"
#include "yb/util/semaphore.h"
#include "yb/util/shared_lock.h"

using std::atomic;

namespace yb {
namespace tablet {

class OperationState;

// ============================================================================
//  LockTable
// ============================================================================

// The entry returned to a thread which has taken a lock.
// Callers should generally use ScopedRowLock (see below).
class LockEntry {
 public:
  explicit LockEntry(const Slice& key)
  : sem(1),
    recursion_(0) {
    key_hash_ = util_hash::CityHash64(reinterpret_cast<const char *>(key.data()), key.size());
    key_ = key;
    refs_ = 1;
  }

  bool Equals(const Slice& key, uint64_t hash) const {
    return key_hash_ == hash && key_ == key;
  }

  std::string ToString() const {
    return key_.ToDebugString();
  }

  // Mutex used by the LockManager
  Semaphore sem;
  int recursion_;

 private:
  friend class LockTable;
  friend class LockManager;

  void CopyKey() {
    key_buf_.assign_copy(key_.data(), key_.size());
    key_ = Slice(key_buf_);
  }

  // Pointer to the next entry in the same hash table bucket
  LockEntry *ht_next_;

  // Hash of the key, used to lookup the hash table bucket
  uint64_t key_hash_;

  // key of the entry, used to compare the entries
  Slice key_;

  // number of users that are referencing this object
  uint64_t refs_;

  // buffer of the key, allocated on insertion by CopyKey()
  faststring key_buf_;

  // The transaction currently holding the lock
  const OperationState* holder_;
};

class LockTable {
 private:
  struct Bucket {
    simple_spinlock lock;
    // First entry chained from this bucket, or NULL if the bucket is empty.
    LockEntry *chain_head;
    Bucket() : chain_head(nullptr) {}
  };

 public:
  LockTable() : mask_(0), size_(0) {
    item_count_.store(0);
    Resize();
  }

  ~LockTable() {
    // Sanity checks: The table shouldn't be destructed when there are any entries in it.
    DCHECK_EQ(0, item_count_.load()) << "There are some unreleased locks";
    for (size_t i = 0; i < size_; ++i) {
      for (LockEntry *p = buckets_[i].chain_head; p != nullptr; p = p->ht_next_) {
        DCHECK(p == nullptr) << "The entry " << p->ToString() << " was not released";
      }
    }
  }

  LockEntry *GetLockEntry(const Slice &key);
  void ReleaseLockEntry(LockEntry *entry);

 private:
  Bucket *FindBucket(uint64_t hash) const {
    return &(buckets_[hash & mask_]);
  }

  // Return a pointer to slot that points to a lock entry that
  // matches key/hash. If there is no such lock entry, return a
  // pointer to the trailing slot in the corresponding linked list.
  LockEntry **FindSlot(Bucket *bucket, const Slice& key, uint64_t hash) const {
    LockEntry **node = &(bucket->chain_head);
    while (*node && !(*node)->Equals(key, hash)) {
      node = &((*node)->ht_next_);
    }
    return node;
  }

  // Return a pointer to slot that points to a lock entry that
  // matches the specified 'entry'.
  // If there is no such lock entry, NULL is returned.
  LockEntry **FindEntry(Bucket *bucket, LockEntry *entry) const {
    for (LockEntry **node = &(bucket->chain_head); *node != nullptr; node = &((*node)->ht_next_)) {
      if (*node == entry) {
        return node;
      }
    }
    return nullptr;
  }

  void Resize();

 private:
  // table rwlock used as write on resize
  percpu_rwlock lock_;
  // size - 1 used to lookup the bucket (hash & mask_)
  uint64_t mask_;
  // number of buckets in the table
  uint64_t size_;
  // table buckets
  gscoped_array<Bucket> buckets_;
  // number of items in the table
  atomic<int64> item_count_;
};

LockEntry *LockTable::GetLockEntry(const Slice& key) {
  auto new_entry = new LockEntry(key);
  LockEntry *old_entry;

  {
    SharedLock<rw_spinlock> table_rdlock(lock_.get_lock());
    Bucket *bucket = FindBucket(new_entry->key_hash_);
    {
      std::lock_guard<simple_spinlock> bucket_lock(bucket->lock);
      LockEntry **node = FindSlot(bucket, new_entry->key_, new_entry->key_hash_);
      old_entry = *node;
      if (old_entry != nullptr) {
        old_entry->refs_++;
      } else {
        new_entry->ht_next_ = nullptr;
        new_entry->CopyKey();
        *node = new_entry;
      }
    }
  }

  if (old_entry != nullptr) {
    delete new_entry;
    return old_entry;
  }

  if (item_count_.fetch_add(1) >= size_) {
    std::unique_lock<percpu_rwlock> table_wrlock(lock_, std::try_to_lock);
    // If we can't take the lock, means that someone else is resizing.
    // (The percpu_rwlock try_lock waits for readers to complete)
    if (table_wrlock.owns_lock()) {
      Resize();
    }
  }

  return new_entry;
}

void LockTable::ReleaseLockEntry(LockEntry *entry) {
  bool removed = false;
  {
    std::lock_guard<rw_spinlock> table_rdlock(lock_.get_lock());
    Bucket *bucket = FindBucket(entry->key_hash_);
    {
      std::lock_guard<simple_spinlock> bucket_lock(bucket->lock);
      LockEntry **node = FindEntry(bucket, entry);
      if (node != nullptr) {
        // ASSUMPTION: There are few updates, so locking the same row at the same time is rare
        // TODO: Move out this if we're going with the TryLock
        if (--entry->refs_ > 0)
          return;

        *node = entry->ht_next_;
        removed = true;
      }
    }
  }

  DCHECK(removed) << "Unable to find LockEntry on release";
  item_count_.fetch_add(-1);
  delete entry;
}

void LockTable::Resize() {
  // Calculate a new table size
  size_t new_size = 16;
  while (new_size < item_count_.load()) {
    new_size <<= 1;
  }

  if (PREDICT_FALSE(size_ >= new_size))
    return;

  // Allocate a new bucket list
  gscoped_array<Bucket> new_buckets(new Bucket[new_size]);
  size_t new_mask = new_size - 1;

  // Copy entries
  for (size_t i = 0; i < size_; ++i) {
    LockEntry *p = buckets_[i].chain_head;
    while (p != nullptr) {
      LockEntry *next = p->ht_next_;

      // Insert Entry
      Bucket *bucket = &(new_buckets[p->key_hash_ & new_mask]);
      p->ht_next_ = bucket->chain_head;
      bucket->chain_head = p;

      p = next;
    }
  }

  // Swap the bucket
  mask_ = new_mask;
  size_ = new_size;
  buckets_.swap(new_buckets);
}

// ============================================================================
//  ScopedRowLock
// ============================================================================

ScopedRowLock::ScopedRowLock(LockManager *manager,
                             const OperationState* tx,
                             const Slice &key,
                             LockManager::LockMode mode)
  : manager_(DCHECK_NOTNULL(manager)),
    acquired_(false) {
  ls_ = manager_->Lock(key, tx, mode, &entry_);

  if (ls_ == LockManager::LOCK_ACQUIRED) {
    acquired_ = true;
  } else {
    // the lock might already have been acquired by this transaction so
    // simply check that we didn't get a LOCK_BUSY status (we should have waited)
    CHECK_NE(ls_, LockManager::LOCK_BUSY);
  }
}

ScopedRowLock::ScopedRowLock(RValue other) {
  TakeState(other.object);
}

ScopedRowLock& ScopedRowLock::operator=(RValue other) {
  TakeState(other.object);
  return *this;
}

void ScopedRowLock::TakeState(ScopedRowLock* other) {
  manager_ = other->manager_;
  acquired_ = other->acquired_;
  entry_ = other->entry_;
  ls_ = other->ls_;

  other->acquired_ = false;
  other->entry_ = nullptr;
}

ScopedRowLock::~ScopedRowLock() {
  Release();
}

void ScopedRowLock::Release() {
  if (entry_) {
    manager_->Release(entry_, ls_);
    acquired_ = false;
    entry_ = nullptr;
  }
}

// ============================================================================
//  LockManager
// ============================================================================

LockManager::LockManager()
  : locks_(new LockTable()) {
}

LockManager::~LockManager() {
  delete locks_;
}

LockManager::LockStatus LockManager::Lock(const Slice& key,
                                          const OperationState* tx,
                                          LockManager::LockMode mode,
                                          LockEntry** entry) {
  *entry = locks_->GetLockEntry(key);

  // We expect low contention, so just try to try_lock first. This is faster
  // than a timed_lock, since we don't have to do a syscall to get the current
  // time.
  if (!(*entry)->sem.TryAcquire()) {
    // If the current holder of this lock is the same transaction just return
    // a LOCK_ALREADY_ACQUIRED status without actually acquiring the mutex.
    //
    //
    // NOTE: This is not a problem for the current way locks are managed since
    // they are obtained and released in bulk (all locks for a transaction are
    // obtained and released at the same time). If at any time in the future
    // we opt to perform more fine grained locking, possibly letting transactions
    // release a portion of the locks they no longer need, this no longer is OK.
    if (ANNOTATE_UNPROTECTED_READ((*entry)->holder_) == tx) {
      // TODO: this is likely to be problematic even today: if you issue two
      // UPDATEs for the same row in the same transaction, we can get:
      // "deltamemstore.cc:74] Check failed: !mutation.exists() Already have an entry ..."
      (*entry)->recursion_++;
      return LOCK_ACQUIRED;
    }

    // If we couldn't immediately acquire the lock, do a timed lock so we can
    // warn if it takes a long time.
    // TODO: would be nice to hook in some histogram metric about lock acquisition
    // time.
    int waited_seconds = 0;
    while (!(*entry)->sem.TimedAcquire(MonoDelta::FromSeconds(1))) {
      const OperationState* cur_holder = ANNOTATE_UNPROTECTED_READ((*entry)->holder_);
      LOG(WARNING) << "Waited " << (++waited_seconds) << " seconds to obtain row lock on key "
                   << key.ToDebugString() << " cur holder: " << cur_holder;
      // TODO: add RPC trace annotation here. Above warning should also include an RPC
      // trace ID.
      // TODO: would be nice to also include some info about the blocking transaction,
      // but it's a bit tricky to do in a non-racy fashion (the other transaction may
      // complete at any point)
    }
  }

  (*entry)->holder_ = tx;
  return LOCK_ACQUIRED;
}

LockManager::LockStatus LockManager::TryLock(const Slice& key,
                                             const OperationState* tx,
                                             LockManager::LockMode mode,
                                             LockEntry **entry) {
  *entry = locks_->GetLockEntry(key);
  bool locked = (*entry)->sem.TryAcquire();
  if (!locked) {
    locks_->ReleaseLockEntry(*entry);
    return LOCK_BUSY;
  }
  (*entry)->holder_ = tx;
  return LOCK_ACQUIRED;
}

void LockManager::Release(LockEntry *lock, LockStatus ls) {
  DCHECK_NOTNULL(lock)->holder_ = nullptr;
  if (ls == LOCK_ACQUIRED) {
    if (lock->recursion_ > 0) {
      lock->recursion_--;
    } else {
      lock->sem.Release();
    }
  }
  locks_->ReleaseLockEntry(lock);
}

} // namespace tablet
} // namespace yb
