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

#include "yb/rocksdb/db/inlineskiplist.h"
#include "yb/rocksdb/db/skiplist.h"

#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/memtablerep.h"
#include "yb/rocksdb/util/arena.h"

namespace rocksdb {
namespace {

template <class SkipListImpl>
class SkipListRep : public MemTableRep {
  SkipListImpl skip_list_;
  const MemTableRep::KeyComparator& cmp_;
  const SliceTransform* transform_;
  const size_t lookahead_;

  friend class LookaheadIterator;
 public:
  explicit SkipListRep(const MemTableRep::KeyComparator& compare,
                       MemTableAllocator* allocator,
                       const SliceTransform* transform, const size_t lookahead)
    : MemTableRep(allocator), skip_list_(compare, allocator), cmp_(compare),
      transform_(transform), lookahead_(lookahead) {
  }

  KeyHandle Allocate(const size_t len, char** buf) override {
    *buf = skip_list_.AllocateKey(len);
    return static_cast<KeyHandle>(*buf);
  }

  // Insert key into the list.
  // REQUIRES: nothing that compares equal to key is currently in the list.
  void Insert(KeyHandle handle) override {
    skip_list_.Insert(static_cast<char*>(handle));
  }

  void InsertConcurrently(KeyHandle handle) override {
    skip_list_.InsertConcurrently(static_cast<char*>(handle));
  }

  bool Erase(KeyHandle handle, const MemTableRep::KeyComparator& comparator) override {
    return skip_list_.Erase(static_cast<char*>(handle), comparator);
  }

  // Returns true iff an entry that compares equal to key is in the list.
  bool Contains(const char* key) const override {
    return skip_list_.Contains(key);
  }

  size_t ApproximateMemoryUsage() override {
    // All memory is allocated through allocator; nothing to report here
    return 0;
  }

  virtual void Get(const LookupKey& k, void* callback_args,
                   bool (*callback_func)(void* arg,
                                         const char* entry)) override {
    SkipListRep::Iterator iter(&skip_list_);
    for (iter.SeekMemTableKey(Slice(), k.memtable_key().cdata());; iter.Next()) {
      auto entry = iter.Entry();
      if (!entry || !callback_func(callback_args, entry)) {
        break;
      }
    }
  }

  uint64_t ApproximateNumEntries(const Slice& start_ikey,
                                 const Slice& end_ikey) override {
    std::string tmp;
    uint64_t start_count =
        skip_list_.EstimateCount(EncodeKey(&tmp, start_ikey));
    uint64_t end_count = skip_list_.EstimateCount(EncodeKey(&tmp, end_ikey));
    return (end_count >= start_count) ? (end_count - start_count) : 0;
  }

  ~SkipListRep() override { }

  // Iteration over the contents of a skip list
  class Iterator final : public MemTableRep::Iterator {
    typename SkipListImpl::Iterator iter_;

   public:
    // Initialize an iterator over the specified list.
    // The returned iterator is not valid.
    explicit Iterator(const SkipListImpl* list)
        : iter_(list) {}

    ~Iterator() override { }

    const char* Entry() const override {
      return iter_.Entry();
    }

    // Advances to the next position.
    // REQUIRES: Valid()
    // Returns the same value as would be returned by Entry after this method is invoked.
    const char* Next() override {
      return iter_.Next();
    }

    // Advances to the previous position.
    // REQUIRES: Valid()
    const char* Prev() override {
      return iter_.Prev();
    }

    // Advance to the first entry with a key >= target
    const char* Seek(Slice user_key) override {
      return iter_.Seek(EncodeKey(&tmp_, user_key));
    }

    const char* SeekMemTableKey(Slice user_key, const char* memtable_key) override {
      return iter_.Seek(memtable_key);
    }

    // Position at the first entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    const char* SeekToFirst() override {
      return iter_.SeekToFirst();
    }

    // Position at the last entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    const char* SeekToLast() override {
      return iter_.SeekToLast();
    }

   protected:
    std::string tmp_;       // For passing to EncodeKey
  };

  // Iterator over the contents of a skip list which also keeps track of the
  // previously visited node. In Seek(), it examines a few nodes after it
  // first, falling back to O(log n) search from the head of the list only if
  // the target key hasn't been found.
  class LookaheadIterator final : public MemTableRep::Iterator {
   public:
    explicit LookaheadIterator(const SkipListRep& rep) :
        rep_(rep), iter_(&rep_.skip_list_), prev_(iter_) {}

    ~LookaheadIterator() override {}

    const char *Entry() const override {
      return iter_.Entry();
    }

    const char* Next() override {
      auto entry = iter_.Entry();
      assert(entry != nullptr);

      bool advance_prev = true;
      auto prev_key = prev_.Entry();
      if (prev_key) {
        auto k1 = rep_.UserKey(prev_key);
        auto k2 = rep_.UserKey(entry);

        if (k1.compare(k2) == 0) {
          // same user key, don't move prev_
          advance_prev = false;
        } else if (rep_.transform_) {
          // only advance prev_ if it has the same prefix as iter_
          auto t1 = rep_.transform_->Transform(k1);
          auto t2 = rep_.transform_->Transform(k2);
          advance_prev = t1.compare(t2) == 0;
        }
      }

      if (advance_prev) {
        prev_ = iter_;
      }
      iter_.Next();
      return Entry();
    }

    const char* Prev() override {
      assert(Entry() != nullptr);
      iter_.Prev();
      prev_ = iter_;
      return Entry();
    }

    const char* Seek(Slice internal_key) override {
      return SeekMemTableKey(internal_key, EncodeKey(&tmp_, internal_key));
    }

    const char* SeekMemTableKey(Slice key, const char* encoded_key) override {
      auto prev_entry = prev_.Entry();
      if (prev_entry && rep_.cmp_(encoded_key, prev_entry) >= 0) {
        // prev_.key() is smaller or equal to our target key; do a quick
        // linear search (at most lookahead_ steps) starting from prev_
        iter_ = prev_;

        size_t cur = 0;
        while (cur++ <= rep_.lookahead_) {
          auto entry = iter_.Entry();
          if (!entry) {
            break;
          }
          if (rep_.cmp_(encoded_key, entry) <= 0) {
            return Entry();
          }
          Next();
        }
      }

      iter_.Seek(encoded_key);
      prev_ = iter_;
      return Entry();
    }

    const char* SeekToFirst() override {
      iter_.SeekToFirst();
      prev_ = iter_;
      return Entry();
    }

    const char* SeekToLast() override {
      iter_.SeekToLast();
      prev_ = iter_;
      return Entry();
    }

   protected:
    std::string tmp_;       // For passing to EncodeKey

   private:
    const SkipListRep& rep_;
    typename SkipListImpl::Iterator iter_;
    typename SkipListImpl::Iterator prev_;
  };

  MemTableRep::Iterator* GetIterator(Arena* arena = nullptr) override {
    if (lookahead_ > 0) {
      void *mem =
        arena ? arena->AllocateAligned(sizeof(SkipListRep::LookaheadIterator))
              : operator new(sizeof(SkipListRep::LookaheadIterator));
      return new (mem) SkipListRep::LookaheadIterator(*this);
    } else {
      void *mem =
        arena ? arena->AllocateAligned(sizeof(SkipListRep::Iterator))
              : operator new(sizeof(SkipListRep::Iterator));
      return new (mem) SkipListRep::Iterator(&skip_list_);
    }
  }
};
} // namespace

MemTableRep* SkipListFactory::CreateMemTableRep(
    const MemTableRep::KeyComparator& compare, MemTableAllocator* allocator,
    const SliceTransform* transform, Logger* logger) {
  if (concurrent_writes_) {
    return new SkipListRep<InlineSkipList<const MemTableRep::KeyComparator&>>(
        compare, allocator, transform, lookahead_);
  } else {
    return new SkipListRep<SingleWriterInlineSkipList<const MemTableRep::KeyComparator&>>(
        compare, allocator, transform, lookahead_);
  }
}

} // namespace rocksdb
