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


#include "yb/rocksdb/db/managed_iterator.h"

#include <limits>
#include <string>
#include <utility>

#include "yb/rocksdb/db/column_family.h"
#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/slice_transform.h"

using std::unique_ptr;

namespace rocksdb {

namespace {
// Helper class that locks a mutex on construction and unlocks the mutex when
// the destructor of the MutexLock object is invoked.
//
// Typical usage:
//
//   void MyClass::MyMethod() {
//     MILock l(&mu_);       // mu_ is an instance variable
//     ... some complex code, possibly with multiple return paths ...
//   }

class MILock {
 public:
  explicit MILock(std::mutex* mu, ManagedIterator* mi) : mu_(mu), mi_(mi) {
    this->mu_->lock();
  }
  ~MILock() {
    this->mu_->unlock();
  }
  ManagedIterator* GetManagedIterator() { return mi_; }

 private:
  std::mutex* const mu_;
  ManagedIterator* mi_;
  // No copying allowed
  MILock(const MILock&) = delete;
  void operator=(const MILock&) = delete;
};
}  // anonymous namespace

//
// Synchronization between modifiers, releasers, creators
// If iterator operation, wait till (!in_use), set in_use, do op, reset in_use
//  if modifying mutable_iter, atomically exchange in_use:
//  return if in_use set / otherwise set in use,
//  atomically replace new iter with old , reset in use
//  The releaser is the new operation and it holds a lock for a very short time
//  The existing non-const iterator operations are supposed to be single
//  threaded and hold the lock for the duration of the operation
//  The existing const iterator operations use the cached key/values
//  and don't do any locking.
ManagedIterator::ManagedIterator(DBImpl* db, const ReadOptions& read_options,
                                 ColumnFamilyData* cfd)
    : db_(db),
      read_options_(read_options),
      cfd_(cfd),
      svnum_(cfd->GetSuperVersionNumber()),
      mutable_iter_(nullptr),
      snapshot_created_(false),
      release_supported_(true) {
  read_options_.managed = false;
  if ((!read_options_.tailing) && (read_options_.snapshot == nullptr)) {
    assert(read_options_.snapshot = db_->GetSnapshot());
    snapshot_created_ = true;
  }
  cfh_.SetCFD(cfd);
  mutable_iter_ = unique_ptr<Iterator>(db->NewIterator(read_options_, &cfh_));
}

ManagedIterator::~ManagedIterator() {
  Lock();
  if (snapshot_created_) {
    db_->ReleaseSnapshot(read_options_.snapshot);
    snapshot_created_ = false;
    read_options_.snapshot = nullptr;
  }
  UnLock();
}

const KeyValueEntry& ManagedIterator::SeekToLast() {
  MILock l(&in_use_, this);
  if (NeedToRebuild()) {
    RebuildIterator();
  }
  assert(mutable_iter_ != nullptr);
  mutable_iter_->SeekToLast();
  if (mutable_iter_->status().ok()) {
    UpdateCurrent();
  }
  return entry_;
}

const KeyValueEntry& ManagedIterator::SeekToFirst() {
  MILock l(&in_use_, this);
  SeekInternal(Slice(), true);
  return entry_;
}

const KeyValueEntry& ManagedIterator::Seek(Slice user_key) {
  MILock l(&in_use_, this);
  SeekInternal(user_key, false);
  return entry_;
}

void ManagedIterator::SeekInternal(const Slice& user_key, bool seek_to_first) {
  if (NeedToRebuild()) {
    RebuildIterator();
  }
  assert(mutable_iter_ != nullptr);
  if (seek_to_first) {
    mutable_iter_->SeekToFirst();
  } else {
    mutable_iter_->Seek(user_key);
  }
  UpdateCurrent();
}

const KeyValueEntry& ManagedIterator::Prev() {
  if (!entry_) {
    status_ = STATUS(InvalidArgument, "Iterator value invalid");
    return entry_;
  }
  MILock l(&in_use_, this);
  if (NeedToRebuild()) {
    std::string current_key = key().ToString();
    Slice old_key(current_key);
    RebuildIterator();
    SeekInternal(old_key, false);
    UpdateCurrent();
    if (!entry_) {
      return entry_;
    }
    if (key().compare(old_key) != 0) {
      entry_.Reset();
      status_ = STATUS(Incomplete, "Cannot do Prev now");
      return entry_;
    }
  }
  mutable_iter_->Prev();
  if (mutable_iter_->status().ok()) {
    UpdateCurrent();
    status_ = Status::OK();
  } else {
    status_ = mutable_iter_->status();
  }
  return entry_;
}

const KeyValueEntry& ManagedIterator::Next() {
  if (!entry_) {
    status_ = STATUS(InvalidArgument, "Iterator value invalid");
    return entry_;
  }
  MILock l(&in_use_, this);
  if (NeedToRebuild()) {
    std::string current_key = key().ToString();
    Slice old_key(current_key.data(), cached_key_.Size());
    RebuildIterator();
    SeekInternal(old_key, false);
    UpdateCurrent();
    if (!entry_) {
      return entry_;
    }
    if (key().compare(old_key) != 0) {
      entry_.Reset();
      status_ = STATUS(Incomplete, "Cannot do Next now");
      return entry_;
    }
  }
  mutable_iter_->Next();
  UpdateCurrent();
  return entry_;
}

const KeyValueEntry& ManagedIterator::Entry() const {
  return entry_;
}

Status ManagedIterator::status() const { return status_; }

void ManagedIterator::RebuildIterator() {
  svnum_ = cfd_->GetSuperVersionNumber();
  mutable_iter_ = unique_ptr<Iterator>(db_->NewIterator(read_options_, &cfh_));
}

void ManagedIterator::UpdateCurrent() {
  assert(mutable_iter_ != nullptr);

  if (!mutable_iter_->Valid()) {
    entry_.Reset();
    status_ = mutable_iter_->status();
    return;
  }

  status_ = Status::OK();
  cached_key_.SetKey(mutable_iter_->key());
  cached_value_.SetKey(mutable_iter_->value());
  entry_.key = cached_key_.GetKey();
  entry_.value = cached_value_.GetKey();
}

void ManagedIterator::ReleaseIter(bool only_old) {
  if ((mutable_iter_ == nullptr) || (!release_supported_)) {
    return;
  }
  if (svnum_ != cfd_->GetSuperVersionNumber() || !only_old) {
    if (!TryLock()) {  // Don't release iter if in use
      return;
    }
    mutable_iter_ = nullptr;  // in_use for a very short time
    UnLock();
  }
}

bool ManagedIterator::NeedToRebuild() {
  if ((mutable_iter_ == nullptr) || (status_.IsIncomplete()) ||
      (!only_drop_old_ && (svnum_ != cfd_->GetSuperVersionNumber()))) {
    return true;
  }
  return false;
}

void ManagedIterator::Lock() {
  in_use_.lock();
  return;
}

bool ManagedIterator::TryLock() { return in_use_.try_lock(); }

void ManagedIterator::UnLock() {
  in_use_.unlock();
}

}  // namespace rocksdb
