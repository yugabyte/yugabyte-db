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

#include "yb/rocksdb/memtable/hash_skiplist_rep.h"

#include <atomic>

#include "yb/rocksdb/memtablerep.h"
#include "yb/rocksdb/util/arena.h"
#include "yb/util/slice.h"
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/util/murmurhash.h"
#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/db/skiplist.h"

namespace rocksdb {
namespace {

class HashSkipListRep : public MemTableRep {
 public:
  HashSkipListRep(const MemTableRep::KeyComparator& compare,
                  MemTableAllocator* allocator, const SliceTransform* transform,
                  size_t bucket_size, int32_t skiplist_height,
                  int32_t skiplist_branching_factor);

  void Insert(KeyHandle handle) override;

  bool Contains(const char* key) const override;

  size_t ApproximateMemoryUsage() override;

  virtual void Get(const LookupKey& k, void* callback_args,
                   bool (*callback_func)(void* arg,
                                         const char* entry)) override;

  virtual ~HashSkipListRep();

  MemTableRep::Iterator* GetIterator(Arena* arena = nullptr) override;

  virtual MemTableRep::Iterator* GetDynamicPrefixIterator(
      Arena* arena = nullptr) override;

 private:
  friend class DynamicIterator;
  typedef SkipList<const char*, const MemTableRep::KeyComparator&> Bucket;

  size_t bucket_size_;

  const int32_t skiplist_height_;
  const int32_t skiplist_branching_factor_;

  // Maps slices (which are transformed user keys) to buckets of keys sharing
  // the same transform.
  std::atomic<Bucket*>* buckets_;

  // The user-supplied transform whose domain is the user keys.
  const SliceTransform* transform_;

  const MemTableRep::KeyComparator& compare_;
  // immutable after construction
  MemTableAllocator* const allocator_;

  inline size_t GetHash(const Slice& slice) const {
    return MurmurHash(slice.data(), static_cast<int>(slice.size()), 0) %
           bucket_size_;
  }
  inline Bucket* GetBucket(size_t i) const {
    return buckets_[i].load(std::memory_order_acquire);
  }
  inline Bucket* GetBucket(const Slice& slice) const {
    return GetBucket(GetHash(slice));
  }
  // Get a bucket from buckets_. If the bucket hasn't been initialized yet,
  // initialize it before returning.
  Bucket* GetInitializedBucket(const Slice& transformed);

  class Iterator : public MemTableRep::Iterator {
   public:
    explicit Iterator(Bucket* list, bool own_list = true,
                      Arena* arena = nullptr)
        : list_(list), iter_(list), own_list_(own_list), arena_(arena) {}

    virtual ~Iterator() {
      // if we own the list, we should also delete it
      if (own_list_) {
        assert(list_ != nullptr);
        delete list_;
      }
    }

    // Return current entry, NULL if iterator is invalid.
    const char* Entry() const override {
      return list_ != nullptr ? iter_.Entry() : nullptr;
    }

    // Advances to the next position.
    // REQUIRES: Valid()
    // Returns the same value as would be returned by Entry after this method is invoked.
    const char* Next() override {
      assert(Entry() != nullptr);
      return iter_.Next();
    }

    // Advances to the previous position.
    // REQUIRES: Valid()
    const char* Prev() override {
      assert(Entry() != nullptr);
      return iter_.Prev();
    }

    // Advance to the first entry with a key >= target
    const char* Seek(Slice internal_key) override {
      if (!list_) {
        return nullptr;
      }
      return iter_.Seek(EncodeKey(&tmp_, internal_key));
    }

    const char* SeekMemTableKey(Slice key, const char* memtable_key) override {
      if (!list_) {
        return nullptr;
      }
      return iter_.Seek(memtable_key);
    }

    // Position at the first entry in collection.
    // Final state of iterator is Valid() iff collection is not empty.
    const char* SeekToFirst() override {
      if (!list_) {
        return nullptr;
      }
      return iter_.SeekToFirst();
    }

    // Position at the last entry in collection.
    // Final state of iterator is Valid() iff collection is not empty.
    const char* SeekToLast() override {
      if (!list_) {
        return nullptr;
      }
      return iter_.SeekToLast();
    }

   protected:
    void Reset(Bucket* list) {
      if (own_list_) {
        assert(list_ != nullptr);
        delete list_;
      }
      list_ = list;
      iter_.SetList(list);
      own_list_ = false;
    }
   private:
    // if list_ is nullptr, we should NEVER call any methods on iter_
    // if list_ is nullptr, this Iterator is not Valid()
    Bucket* list_;
    Bucket::Iterator iter_;
    // here we track if we own list_. If we own it, we are also
    // responsible for it's cleaning. This is a poor man's shared_ptr
    bool own_list_;
    std::unique_ptr<Arena> arena_;
    std::string tmp_;       // For passing to EncodeKey
  };

  class DynamicIterator : public HashSkipListRep::Iterator {
   public:
    explicit DynamicIterator(const HashSkipListRep& memtable_rep)
      : HashSkipListRep::Iterator(nullptr, false),
        memtable_rep_(memtable_rep) {}

    // Advance to the first entry with a key >= target
    const char* Seek(Slice k) override {
      PrepareSeek(k);
      return HashSkipListRep::Iterator::Seek(k);
    }

    const char* SeekMemTableKey(Slice k, const char* memtable_key) override {
      PrepareSeek(k);
      return HashSkipListRep::Iterator::SeekMemTableKey(k, memtable_key);
    }

    // Position at the first entry in collection.
    // Final state of iterator is Valid() iff collection is not empty.
    const char* SeekToFirst() override {
      // Prefix iterator does not support total order.
      // We simply set the iterator to invalid state
      Reset(nullptr);
      return Entry();
    }

    // Position at the last entry in collection.
    // Final state of iterator is Valid() iff collection is not empty.
    const char* SeekToLast() override {
      // Prefix iterator does not support total order.
      // We simply set the iterator to invalid state
      Reset(nullptr);
      return Entry();
    }
   private:
    void PrepareSeek(Slice k) {
      auto transformed = memtable_rep_.transform_->Transform(ExtractUserKey(k));
      Reset(memtable_rep_.GetBucket(transformed));
    }

    // the underlying memtable
    const HashSkipListRep& memtable_rep_;
  };

  class EmptyIterator : public MemTableRep::Iterator {
    // This is used when there wasn't a bucket. It is cheaper than
    // instantiating an empty bucket over which to iterate.
   public:
    EmptyIterator() { }
    const char* Entry() const override {
      assert(false);
      return nullptr;
    }
    const char* Next() override { return nullptr; }
    const char* Prev() override { return nullptr; }
    const char* Seek(Slice internal_key) override { return nullptr; }
    const char* SeekMemTableKey(Slice key, const char* memtable_key) override { return nullptr; }
    const char* SeekToFirst() override { return nullptr; }
    const char* SeekToLast() override { return nullptr; }
  };
};

HashSkipListRep::HashSkipListRep(const MemTableRep::KeyComparator& compare,
                                 MemTableAllocator* allocator,
                                 const SliceTransform* transform,
                                 size_t bucket_size, int32_t skiplist_height,
                                 int32_t skiplist_branching_factor)
    : MemTableRep(allocator),
      bucket_size_(bucket_size),
      skiplist_height_(skiplist_height),
      skiplist_branching_factor_(skiplist_branching_factor),
      transform_(transform),
      compare_(compare),
      allocator_(allocator) {
  auto mem = allocator->AllocateAligned(
               sizeof(std::atomic<void*>) * bucket_size);
  buckets_ = new (mem) std::atomic<Bucket*>[bucket_size];

  for (size_t i = 0; i < bucket_size_; ++i) {
    buckets_[i].store(nullptr, std::memory_order_relaxed);
  }
}

HashSkipListRep::~HashSkipListRep() {
}

HashSkipListRep::Bucket* HashSkipListRep::GetInitializedBucket(
    const Slice& transformed) {
  size_t hash = GetHash(transformed);
  auto bucket = GetBucket(hash);
  if (bucket == nullptr) {
    auto addr = allocator_->AllocateAligned(sizeof(Bucket));
    bucket = new (addr) Bucket(compare_, allocator_, skiplist_height_,
                               skiplist_branching_factor_);
    buckets_[hash].store(bucket, std::memory_order_release);
  }
  return bucket;
}

void HashSkipListRep::Insert(KeyHandle handle) {
  auto* key = static_cast<char*>(handle);
  assert(!Contains(key));
  auto transformed = transform_->Transform(UserKey(key));
  auto bucket = GetInitializedBucket(transformed);
  bucket->Insert(key);
}

bool HashSkipListRep::Contains(const char* key) const {
  auto transformed = transform_->Transform(UserKey(key));
  auto bucket = GetBucket(transformed);
  if (bucket == nullptr) {
    return false;
  }
  return bucket->Contains(key);
}

size_t HashSkipListRep::ApproximateMemoryUsage() {
  return 0;
}

void HashSkipListRep::Get(const LookupKey& k, void* callback_args,
                          bool (*callback_func)(void* arg, const char* entry)) {
  auto transformed = transform_->Transform(k.user_key());
  auto bucket = GetBucket(transformed);
  if (bucket != nullptr) {
    Bucket::Iterator iter(bucket);
    for (iter.Seek(k.memtable_key().cdata());; iter.Next()) {
      const auto* entry = iter.Entry();
      if (!entry || !callback_func(callback_args, entry)) {
        break;
      }
    }
  }
}

MemTableRep::Iterator* HashSkipListRep::GetIterator(Arena* arena) {
  // allocate a new arena of similar size to the one currently in use
  Arena* new_arena = new Arena(allocator_->BlockSize());
  auto list = new Bucket(compare_, new_arena);
  for (size_t i = 0; i < bucket_size_; ++i) {
    auto bucket = GetBucket(i);
    if (bucket != nullptr) {
      Bucket::Iterator itr(bucket);
      for (itr.SeekToFirst();; itr.Next()) {
        const auto* entry = itr.Entry();
        if (!entry) {
          break;
        }
        list->Insert(entry);
      }
    }
  }
  if (arena == nullptr) {
    return new Iterator(list, true, new_arena);
  } else {
    auto mem = arena->AllocateAligned(sizeof(Iterator));
    return new (mem) Iterator(list, true, new_arena);
  }
}

MemTableRep::Iterator* HashSkipListRep::GetDynamicPrefixIterator(Arena* arena) {
  if (arena == nullptr) {
    return new DynamicIterator(*this);
  } else {
    auto mem = arena->AllocateAligned(sizeof(DynamicIterator));
    return new (mem) DynamicIterator(*this);
  }
}

} // anon namespace

MemTableRep* HashSkipListRepFactory::CreateMemTableRep(
    const MemTableRep::KeyComparator& compare, MemTableAllocator* allocator,
    const SliceTransform* transform, Logger* logger) {
  return new HashSkipListRep(compare, allocator, transform, bucket_count_,
                             skiplist_height_, skiplist_branching_factor_);
}

MemTableRepFactory* NewHashSkipListRepFactory(
    size_t bucket_count, int32_t skiplist_height,
    int32_t skiplist_branching_factor) {
  return new HashSkipListRepFactory(bucket_count, skiplist_height,
      skiplist_branching_factor);
}

} // namespace rocksdb
