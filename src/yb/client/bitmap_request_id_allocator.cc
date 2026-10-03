// Copyright (c) YugabyteDB, Inc.
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

#include "yb/client/bitmap_request_id_allocator.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <vector>

#include "yb/gutil/bits.h"

#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"

DEFINE_RUNTIME_uint32(client_request_id_num_words, 4096,
    "Number of words of the bitmap of the finished retryable request ids, rounded up to a power "
    "of two. They cover 64 times as many ids starting at min_running_request_id, and the ids "
    "beyond them are kept in a map guarded by a spinlock. So this only has to cover the ids that "
    "a client can have in flight at once.");

namespace yb::client::internal {

class BitmapRequestIdAllocatorImpl {
 public:
  BitmapRequestIdAllocatorImpl()
      : num_words_(
            1ULL << Bits::Log2Ceiling64(std::max<uint32_t>(1, FLAGS_client_request_id_num_words))),
        words_(num_words_) {
    // The vector zeroes the atomics.
  }

  ~BitmapRequestIdAllocatorImpl() {
    DCHECK(!running_.load());
  }

  AtomicRequestIdAllocation Next() {
    auto id = next_id_.fetch_add(1);
    // min_running only moves past finished ids, so it stays below the id allocated above.
    return AtomicRequestIdAllocation { .id = id, .min_running = MinRunning() };
  }

  void Finish(RetryableRequestId id) {
    auto word = WordOf(id);
    auto bit = BitOf(id);
    // A stale min_word could only send the bit to the overflow map, which is safe.
    auto min_word = min_running_word_.load();
    if (PREDICT_FALSE(word - min_word >= num_words_)) {
      AddOverflow(word, bit);
      return;
    }
    // Adding the bit sets it, since it is set exactly once, and returns the previous word. The
    // word that shared this slot was zeroed before min_running_word_ left it, so it is clean.
    auto previous = words_[Slot(word)].fetch_add(bit);
    if (PREDICT_FALSE(previous & bit)) {
      // The addition has already carried into the neighbor bits.
      LOG(DFATAL) << "Request id " << id << " finished twice";
      return;
    }
    if ((previous | bit) != kAllFinished) {
      // Some ids of this word are still running.
      return;
    }
    ElectAndAdvanceMinRunningWord();
  }

  RetryableRequestId MinRunning() const {
    WordIndex word_index = min_running_word_.load();
    // The bits may belong to the word that reused this slot. That only lowers the result: the
    // slot is reused after every id of this word has finished, i.e. after the minimum left it.
    auto bits = words_[Slot(word_index)].load();
    if (PREDICT_FALSE(bits == kAllFinished)) {
      // Every id has finished, but min_running_word_ has not left the word yet. Its end is
      // still below every running id. FindLSBSetNonZero64 needs a non zero value anyway.
      return static_cast<RetryableRequestId>((word_index + 1) * kBitsPerWord);
    }
    // The bits below min_running are set, so the lowest unset one is min_running or a later id.
    return static_cast<RetryableRequestId>(word_index * kBitsPerWord) +
           Bits::FindLSBSetNonZero64(~bits);
  }

  int64_t num_overflows() const {
    return num_overflows_.load();
  }

 private:
  using Word = std::atomic<uint64_t>;
  // Unsigned, so that the distance wraps instead of going negative in the overflow check.
  using WordIndex = uint64_t;

  static constexpr size_t kBitsPerWord = 64;
  static constexpr uint64_t kAllFinished = std::numeric_limits<uint64_t>::max();

  // The words sharing a slot are num_words_ apart, which the overflow check relies on.
  // Scattering them over the ring was measured to make no difference.
  size_t Slot(WordIndex word) const {
    return word & (num_words_ - 1);
  }

  static WordIndex WordOf(RetryableRequestId id) {
    return static_cast<WordIndex>(id) / kBitsPerWord;
  }

  static uint64_t BitOf(RetryableRequestId id) {
    return 1ULL << (id % kBitsPerWord);
  }

  void ElectAndAdvanceMinRunningWord() {
    auto expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
      // Another thread is moving it, and re-checks the word before giving up the role.
      return;
    }
    for (;;) {
      auto word_index = AdvanceMinRunningWord();
      // As in PreparerImpl::Run, the check below must not be reordered before this store, or a
      // word completed meanwhile is left behind.
      if (PREDICT_TRUE(running_.exchange(false))) {
        if (words_[Slot(word_index)].load() == kAllFinished) {
          // Completed while we were giving up the role.
          expected = false;
          if (running_.compare_exchange_strong(expected, true)) {
            continue;
          }
          // Somebody else took the role.
        }
      } else {
        LOG(DFATAL) << "running_ is false while a thread is advancing min running request id";
      }
      return;
    }
  }

  // Moves min_running_word_ over the words whose ids have all finished. Returns where it stopped.
  WordIndex AdvanceMinRunningWord() {
    WordIndex word_index = min_running_word_.load();
    for (;;) {
      auto initial = word_index;
      while (words_[Slot(word_index)].load() == kAllFinished) {
        // Nobody writes the word anymore. Zeroing it before publishing keeps the slot clean for
        // the word that reuses it.
        words_[Slot(word_index)].store(0);
        ++word_index;
      }
      if (word_index != initial) {
        min_running_word_.store(word_index);
      }
      // The bits that did not fit into the ring may fit now, and let it move further.
      if (!DrainOverflow(word_index)) {
        return word_index;
      }
    }
  }

  void AddOverflow(WordIndex word, uint64_t bit) {
    num_overflows_.fetch_add(1);
    std::lock_guard lock(overflow_mutex_);
    overflow_[word] |= bit;
    has_overflow_.store(true);
  }

  // Moves the overflow words that fit into the ring now, if any.
  bool DrainOverflow(WordIndex min_word) {
    if (!has_overflow_.load()) {
      return false;
    }
    std::lock_guard lock(overflow_mutex_);
    // Ordered by word, so the ones that fit are a prefix.
    auto it = overflow_.begin();
    for (; it != overflow_.end() && it->first - min_word < num_words_; ++it) {
      // An id sets its bit either in the ring or in the map, never in both, so the bits are
      // disjoint and adding them sets them.
      words_[Slot(it->first)].fetch_add(it->second);
    }
    auto result = it != overflow_.begin();
    overflow_.erase(overflow_.begin(), it);
    has_overflow_.store(!overflow_.empty());
    return result;
  }

  const size_t num_words_;

  std::atomic<RetryableRequestId> next_id_{0};

  // Word that holds the minimum of the running ids. Every id below it has finished, so the
  // preceding words are zeroed and their slots are free to be reused.
  std::atomic<WordIndex> min_running_word_{0};

  // One bit per id, set when it has finished, cleared when min_running_word_ passes its word.
  std::vector<Word> words_;

  // Whether a thread is moving min_running_word_.
  std::atomic<bool> running_{false};

  // Whether overflow_ is not empty, so that the common path does not take the lock.
  std::atomic<bool> has_overflow_{false};

  std::atomic<int64_t> num_overflows_{0};

  simple_spinlock overflow_mutex_;
  std::map<WordIndex, uint64_t> overflow_ GUARDED_BY(overflow_mutex_);
};

BitmapRequestIdAllocator::BitmapRequestIdAllocator()
    : impl_(std::make_unique<BitmapRequestIdAllocatorImpl>()) {
}

BitmapRequestIdAllocator::~BitmapRequestIdAllocator() = default;

AtomicRequestIdAllocation BitmapRequestIdAllocator::Next() {
  return impl_->Next();
}

void BitmapRequestIdAllocator::Finish(RetryableRequestId id) {
  impl_->Finish(id);
}

RetryableRequestId BitmapRequestIdAllocator::TEST_min_running() const {
  return impl_->MinRunning();
}

int64_t BitmapRequestIdAllocator::TEST_num_overflows() const {
  return impl_->num_overflows();
}

} // namespace yb::client::internal
