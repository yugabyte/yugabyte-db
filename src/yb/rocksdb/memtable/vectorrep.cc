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
#include <unordered_set>
#include <set>
#include <memory>
#include <algorithm>
#include <type_traits>

#include "yb/rocksdb/memtablerep.h"

#include "yb/rocksdb/util/arena.h"
#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/memtable/stl_wrappers.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/util/mutexlock.h"

namespace rocksdb {
namespace {

using stl_wrappers::Compare;

class VectorRep : public MemTableRep {
 public:
  VectorRep(const KeyComparator& compare, MemTableAllocator* allocator,
            size_t count);

  // Insert key into the collection. (The caller will pack key and value into a
  // single buffer and pass that in as the parameter to Insert)
  // REQUIRES: nothing that compares equal to key is currently in the
  // collection.
  void Insert(KeyHandle handle) override;

  // Returns true iff an entry that compares equal to key is in the collection.
  bool Contains(const char* key) const override;

  void MarkReadOnly() override;

  size_t ApproximateMemoryUsage() override;

  virtual void Get(const LookupKey& k, void* callback_args,
                   bool (*callback_func)(void* arg,
                                         const char* entry)) override;

  ~VectorRep() override { }

  class Iterator final : public MemTableRep::Iterator {
    class VectorRep* vrep_;
    std::shared_ptr<std::vector<const char*>> bucket_;
    std::vector<const char*>::const_iterator mutable cit_;
    const KeyComparator& compare_;
    std::string tmp_;       // For passing to EncodeKey
    bool mutable sorted_;
    void DoSort() const;
   public:
    explicit Iterator(class VectorRep* vrep,
      std::shared_ptr<std::vector<const char*>> bucket,
      const KeyComparator& compare);

    // Initialize an iterator over the specified collection.
    // The returned iterator is not valid.
    // explicit Iterator(const MemTableRep* collection);
    ~Iterator() override = default;

    const char* Entry() const override;

    // Advances to the next position.
    // REQUIRES: Valid()
    // Returns the same value as would be returned by Entry after this method is invoked.
    const char* Next() override;

    // Advances to the previous position.
    // REQUIRES: Valid()
    const char* Prev() override;

    // Advance to the first entry with a key >= target
    const char* Seek(Slice user_key) override;

    // Advance to the first entry with a key >= target, using externally specified memtable_key.
    const char* SeekMemTableKey(Slice user_key, const char* memtable_key) override;

    // Position at the first entry in collection.
    // Final state of iterator is Valid() iff collection is not empty.
    const char* SeekToFirst() override;

    // Position at the last entry in collection.
    // Final state of iterator is Valid() iff collection is not empty.
    const char* SeekToLast() override;
  };

  // Return an iterator over the keys in this representation.
  MemTableRep::Iterator* GetIterator(Arena* arena) override;

 private:
  friend class Iterator;
  typedef std::vector<const char*> Bucket;
  std::shared_ptr<Bucket> bucket_;
  mutable port::RWMutex rwlock_;
  bool immutable_;
  bool sorted_;
  const KeyComparator& compare_;
};

void VectorRep::Insert(KeyHandle handle) {
  auto* key = static_cast<char*>(handle);
  WriteLock l(&rwlock_);
  assert(!immutable_);
  bucket_->push_back(key);
}

// Returns true iff an entry that compares equal to key is in the collection.
bool VectorRep::Contains(const char* key) const {
  ReadLock l(&rwlock_);
  return std::find(bucket_->begin(), bucket_->end(), key) != bucket_->end();
}

void VectorRep::MarkReadOnly() {
  WriteLock l(&rwlock_);
  immutable_ = true;
}

size_t VectorRep::ApproximateMemoryUsage() {
  return
    sizeof(bucket_) + sizeof(*bucket_) +
    bucket_->size() *
    sizeof(
      std::remove_reference<decltype(*bucket_)>::type::value_type
    );
}

VectorRep::VectorRep(const KeyComparator& compare, MemTableAllocator* allocator,
                     size_t count)
  : MemTableRep(allocator),
    bucket_(new Bucket()),
    immutable_(false),
    sorted_(false),
    compare_(compare) { bucket_.get()->reserve(count); }

VectorRep::Iterator::Iterator(class VectorRep* vrep,
                   std::shared_ptr<std::vector<const char*>> bucket,
                   const KeyComparator& compare)
: vrep_(vrep),
  bucket_(bucket),
  cit_(bucket_->end()),
  compare_(compare),
  sorted_(false) { }

void VectorRep::Iterator::DoSort() const {
  // vrep is non-null means that we are working on an immutable memtable
  if (!sorted_ && vrep_ != nullptr) {
    WriteLock l(&vrep_->rwlock_);
    if (!vrep_->sorted_) {
      std::sort(bucket_->begin(), bucket_->end(), Compare(compare_));
      cit_ = bucket_->begin();
      vrep_->sorted_ = true;
    }
    sorted_ = true;
  }
  if (!sorted_) {
    std::sort(bucket_->begin(), bucket_->end(), Compare(compare_));
    cit_ = bucket_->begin();
    sorted_ = true;
  }
  assert(sorted_);
  assert(vrep_ == nullptr || vrep_->sorted_);
}

const char* VectorRep::Iterator::Entry() const {
  DoSort();
  return cit_ != bucket_->end() ? *cit_ : nullptr;
}

// Advances to the next position.
// REQUIRES: Valid()
// Returns the same value as would be returned by Entry after this method is invoked.
const char* VectorRep::Iterator::Next() {
  assert(sorted_);
  if (cit_ == bucket_->end()) {
    return nullptr;
  }
  ++cit_;
  return Entry();
}

// Advances to the previous position.
// REQUIRES: Valid()
const char* VectorRep::Iterator::Prev() {
  assert(sorted_);
  if (cit_ == bucket_->begin()) {
    // If you try to go back from the first element, the iterator should be
    // invalidated. So we set it to past-the-end. This means that you can
    // treat the container circularly.
    cit_ = bucket_->end();
  } else {
    --cit_;
  }
  return Entry();
}

// Advance to the first entry with a key >= target
const char* VectorRep::Iterator::Seek(Slice user_key) {
  return SeekMemTableKey(user_key, EncodeKey(&tmp_, user_key));
}

const char* VectorRep::Iterator::SeekMemTableKey(Slice user_key, const char* memtable_key) {
  DoSort();
  // Do binary search to find first value not less than the target
  cit_ = std::equal_range(bucket_->begin(),
                          bucket_->end(),
                          memtable_key,
                          [this] (const char* a, const char* b) {
                            return compare_(a, b) < 0;
                          }).first;
  return Entry();
}

// Position at the first entry in collection.
// Final state of iterator is Valid() iff collection is not empty.
const char* VectorRep::Iterator::SeekToFirst() {
  DoSort();
  cit_ = bucket_->begin();
  return Entry();
}

// Position at the last entry in collection.
// Final state of iterator is Valid() iff collection is not empty.
const char* VectorRep::Iterator::SeekToLast() {
  DoSort();
  cit_ = bucket_->end();
  if (bucket_->size() != 0) {
    --cit_;
  }
  return Entry();
}

void VectorRep::Get(const LookupKey& k, void* callback_args,
                    bool (*callback_func)(void* arg, const char* entry)) {
  rwlock_.ReadLock();
  VectorRep* vector_rep;
  std::shared_ptr<Bucket> bucket;
  if (immutable_) {
    vector_rep = this;
  } else {
    vector_rep = nullptr;
    bucket.reset(new Bucket(*bucket_));  // make a copy
  }
  VectorRep::Iterator iter(vector_rep, immutable_ ? bucket_ : bucket, compare_);
  rwlock_.ReadUnlock();

  for (iter.SeekMemTableKey(k.user_key(), k.memtable_key().cdata());; iter.Next()) {
    auto entry = iter.Entry();
    if (!entry || !callback_func(callback_args, entry)) {
      break;
    }
  }
}

MemTableRep::Iterator* VectorRep::GetIterator(Arena* arena) {
  char* mem = nullptr;
  if (arena != nullptr) {
    mem = arena->AllocateAligned(sizeof(Iterator));
  }
  ReadLock l(&rwlock_);
  // Do not sort here. The sorting would be done the first time
  // a Seek is performed on the iterator.
  if (immutable_) {
    if (arena == nullptr) {
      return new Iterator(this, bucket_, compare_);
    } else {
      return new (mem) Iterator(this, bucket_, compare_);
    }
  } else {
    std::shared_ptr<Bucket> tmp;
    tmp.reset(new Bucket(*bucket_)); // make a copy
    if (arena == nullptr) {
      return new Iterator(nullptr, tmp, compare_);
    } else {
      return new (mem) Iterator(nullptr, tmp, compare_);
    }
  }
}
} // anon namespace

MemTableRep* VectorRepFactory::CreateMemTableRep(
    const MemTableRep::KeyComparator& compare, MemTableAllocator* allocator,
    const SliceTransform*, Logger* logger) {
  return new VectorRep(compare, allocator, count_);
}
} // namespace rocksdb
