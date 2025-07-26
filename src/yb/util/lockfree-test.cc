//
// Copyright (c) YugaByte, Inc.
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
//

#include <atomic>
#include <regex>
#include <string>
#include <thread>

#include <boost/lockfree/queue.hpp>
#include <gtest/gtest.h>

#include "yb/util/concurrent_queue.h"
#include "yb/util/flags.h"
#include "yb/util/lockfree.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"

DEFINE_test_flag(string, queue_name_regex, "",
    "Regex to filter queue by name in LockfreeTest.QueuePerformance test");

using namespace std::literals;

namespace yb {

struct TestEntry : public MPSCQueueEntry<TestEntry> {
  size_t thread_index;
  size_t index;
};

template<class Queue, class NoneValue>
void TestQueueSimple(const NoneValue& none_value) {
  const size_t kTotalEntries = 10;
  std::vector<TestEntry> entries(kTotalEntries);
  for (size_t i = 0; i != entries.size(); ++i) {
    entries[i].index = i;
  }
  Queue queue;

  // Push pop 1 entry
  queue.Push(&entries[0]);
  ASSERT_EQ(&entries[0], queue.Pop());

  // Push pop multiple entries
  for (auto& entry : entries) {
    queue.Push(&entry);
  }

  for (auto& entry : entries) {
    ASSERT_EQ(&entry, queue.Pop());
  }

  for (auto& entry : entries) {
    queue.Push(&entry);
  }

  queue.Clear();
  ASSERT_EQ(none_value, queue.Pop());

  // Mixed push and pop
  queue.Push(&entries[0]);
  queue.Push(&entries[1]);
  ASSERT_EQ(&entries[0], queue.Pop());
  queue.Push(&entries[2]);
  queue.Push(&entries[3]);
  ASSERT_EQ(&entries[1], queue.Pop());
  ASSERT_EQ(&entries[2], queue.Pop());
  queue.Push(&entries[4]);
  ASSERT_EQ(&entries[3], queue.Pop());
  ASSERT_EQ(&entries[4], queue.Pop());
  queue.Push(&entries[5]);
  queue.Push(&entries[6]);
  queue.Push(&entries[7]);
  ASSERT_EQ(&entries[5], queue.Pop());
  ASSERT_EQ(&entries[6], queue.Pop());
  ASSERT_EQ(&entries[7], queue.Pop());
  ASSERT_EQ(none_value, queue.Pop());
  queue.Push(&entries[8]);
  queue.Push(&entries[9]);
  ASSERT_EQ(&entries[8], queue.Pop());
  ASSERT_EQ(&entries[9], queue.Pop());
  ASSERT_EQ(none_value, queue.Pop());
}

TEST(LockfreeTest, MPSCQueueSimple) {
  TestQueueSimple<MPSCQueue<TestEntry>>(nullptr);
}

TEST(LockfreeTest, RWQueueSimple) {
  TestQueueSimple<RWQueue<TestEntry*>>(std::nullopt);
}

TEST(LockfreeTest, MPSCQueueConcurrent) {
  constexpr size_t kThreads = 10;
  constexpr size_t kEntriesPerThread = 200000;

  std::vector<TestEntry> entries(kThreads * kEntriesPerThread);
  MPSCQueue<TestEntry> queue;

  auto start_time = MonoTime::Now();
  std::vector<std::thread> threads;
  while (threads.size() != kThreads) {
    size_t thread_index = threads.size();
    threads.emplace_back([&queue, thread_index, &entries] {
      size_t base = thread_index * kEntriesPerThread;
      for (size_t i = 0; i != kEntriesPerThread; ++i) {
        auto& entry = entries[base + i];
        entry.thread_index = thread_index;
        entry.index = i;
        queue.Push(&entry);
      }
    });
  }

  size_t threads_left = kThreads;
  std::vector<size_t> next_index(kThreads);
  while (threads_left > 0) {
    auto* entry = queue.Pop();
    if (!entry) {
      continue;
    }

    ASSERT_EQ(entry->index, next_index[entry->thread_index]);
    if (++next_index[entry->thread_index] == kEntriesPerThread) {
      --threads_left;
    }
  }

  auto passed = MonoTime::Now() - start_time;

  LOG(INFO) << "Passed: " << passed;

  for (auto i : next_index) {
    ASSERT_EQ(i, kEntriesPerThread);
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

#ifndef NDEBUG
constexpr auto kEntries = RegularBuildVsSanitizers(1000000, 1000);
#else
constexpr auto kEntries = 10000000;
#endif

template <class T, class Allocator = std::allocator<T>>
struct BlockAllocator {
  template <class U>
  struct rebind {
    using other = BlockAllocator<
        U, typename std::allocator_traits<Allocator>::template rebind_alloc<U>>;
  };

  using value_type = typename Allocator::value_type;
  using size_type = typename Allocator::size_type;

  void deallocate(T* p, size_type n) {
    BlockEntry* entry = OBJECT_FROM_MEMBER(BlockEntry, value, p);
    if (entry->counter->fetch_sub(1, std::memory_order_acq_rel) == 1) {
      Block* block = OBJECT_FROM_MEMBER(Block, counter, entry->counter);
      impl_.deallocate(block, 1);
    }
  }

  static constexpr size_t kBlockEntries = 0x80;

  struct BlockEntry {
    std::atomic<size_t>* counter;
    T value;
  };

  struct Block {
    std::atomic<size_t> counter;
    BlockEntry entries[kBlockEntries];
  };

  struct TSS {
    size_t idx = kBlockEntries;
    Block* block = nullptr;

    ~TSS() {
      auto delta = kBlockEntries - idx;
      if (delta != 0 && block->counter.fetch_sub(delta, std::memory_order_acq_rel) == delta) {
        Impl().deallocate(block, 1);
      }
    }
  };

  static thread_local std::unique_ptr<TSS> tss_;

  T* allocate(size_type n) {
    TSS* tss = tss_.get();
    if (PREDICT_FALSE(!tss)) {
      tss_.reset(new TSS);
      tss = tss_.get();
    }
    if (PREDICT_FALSE(tss->idx == kBlockEntries)) {
      tss->block = impl_.allocate(1);
      tss->block->counter.store(kBlockEntries, std::memory_order_release);
      tss->idx = 0;
    }
    auto& entry = tss->block->entries[tss->idx++];
    entry.counter = &tss->block->counter;
    return &entry.value;
  }

  using Impl = typename std::allocator_traits<Allocator>::template rebind_alloc<Block>;
  Impl impl_;
};

template <class T, class Allocator>
thread_local std::unique_ptr<typename BlockAllocator<T, Allocator>::TSS>
    BlockAllocator<T, Allocator>::tss_;

class QueuePerformanceHelper {
 public:
  void Warmup() {
    // Empty name would not be printed, so we use it for warmup.
    TestQueue<boost::lockfree::queue<ptrdiff_t>>("", 1000);
  }

  void Perform(size_t workers, bool mixed_mode) {
    Setup(workers, mixed_mode);
    RunAll();
  }

 private:
  void Setup(size_t workers, bool mixed_mode) {
    workers_ = workers;
    mixed_mode_ = mixed_mode;

    LOG(INFO) << "Setup, workers: " << workers << ", mixed mode: " << mixed_mode;
  }

  void RunAll() {
    TestQueue<boost::lockfree::queue<ptrdiff_t, boost::lockfree::fixed_sized<true>>>(
        "boost::lockfree::queue", 50000);
    TestQueue<RWQueue<ptrdiff_t>>("YBRWQueue");
  }
 private:
  template <class T, class... Args>
  void DoTestQueue(const std::string& name, T* queue) {
    std::atomic<size_t> pushes(0);
    std::atomic<size_t> pops(0);

    std::vector<std::thread> threads;
    threads.reserve(workers_);

    CountDownLatch start_latch(workers_);
    CountDownLatch finish_latch(workers_);

    enum class Role {
      kReader,
      kWriter,
      kBoth,
    };

    for (size_t i = 0; i != workers_; ++i) {
      Role role = mixed_mode_ ? Role::kBoth : (i & 1 ? Role::kReader : Role::kWriter);
      threads.emplace_back([queue, &start_latch, &finish_latch, &pushes, &pops, role] {
        start_latch.CountDown();
        start_latch.Wait();
        bool push_done = false;
        bool pop_done = false;
        int commands_left = 0;
        uint64_t commands = 0;
        std::mt19937_64& random = ThreadLocalRandom();
        if (role == Role::kWriter) {
          pop_done = true;
        } else if (role == Role::kReader) {
          push_done = true;
        }
        while (!push_done || !pop_done) {
          if (commands_left == 0) {
            switch (role) {
              case Role::kReader:
                commands = 0;
                break;
              case Role::kWriter:
                commands = std::numeric_limits<uint64_t>::max();
                break;
              case Role::kBoth:
                commands = random();;
                break;
            }
            commands_left = sizeof(commands) * 8;
          }
          bool push = (commands & 1) != 0;
          commands >>= 1;
          --commands_left;
          if (push) {
            auto entry = pushes.fetch_add(1, std::memory_order_acq_rel);
            if (entry > kEntries) {
              push_done = true;
              continue;
            }
            while (!queue->push(entry)) {}
          } else {
            if (pops.load(std::memory_order_acquire) >= kEntries) {
              pop_done = true;
              continue;
            }
            typename T::value_type entry;
            if (queue->pop(entry)) {
              if (pops.fetch_add(1, std::memory_order_acq_rel) == kEntries - 1) {
                pop_done = true;
                continue;
              }
            }
          }
        }
        finish_latch.CountDown();
      });
    }

    start_latch.Wait();
    auto start = MonoTime::Now();

    bool wait_result = finish_latch.WaitUntil(start + 30s);
    auto stop = MonoTime::Now();
    auto passed = stop - start;

    if (!wait_result) {
      pushes.fetch_add(kEntries, std::memory_order_acq_rel);
      pops.fetch_add(kEntries, std::memory_order_acq_rel);
      // Cleanup queue, since some of implementations could hang on queue overflow.
      while (!finish_latch.WaitFor(10ms)) {
        typename T::value_type entry;
        while (queue->pop(entry)) {}
      }
    }

    for (auto& thread : threads) {
      thread.join();
    }

    if (!name.empty()) {
      if (wait_result) {
        LOG(INFO) << name << ": " << passed;
      } else {
        LOG(INFO) << name << ": TIMED OUT";
      }
    }
  }

  template <class T, class... Args>
  void TestQueue(const std::string& name, Args&&... args) {
    if (!name.empty() && !FLAGS_TEST_queue_name_regex.empty()) {
      std::regex regex(FLAGS_TEST_queue_name_regex, std::regex::egrep);
      if (!regex_match(name, regex)) {
        return;
      }
    }
    T queue(std::forward<Args>(args)...);
    DoTestQueue(name, &queue);
  }

  size_t workers_ = 0x100;
  bool mixed_mode_ = false;
};

TEST(LockfreeTest, QueuePerformance) {
  InitGoogleLoggingSafeBasic("lockfree");
  InitThreading();

  QueuePerformanceHelper helper;
  helper.Warmup();
  helper.Perform(0x100, false);
  helper.Perform(0x100, true);
  helper.Perform(0x10, false);
  helper.Perform(0x10, true);
}

template <template<class> class Collection>
void TestIntrusive() {
  constexpr int kNumEntries = 100;
  constexpr int kNumThreads = 5;

  struct Entry : public MPSCQueueEntry<Entry> {
    int value;
  };

  Collection<Entry> collection;
  std::vector<Entry> entries(kNumEntries);
  for (int i = 0; i != kNumEntries; ++i) {
    entries[i].value = i;
    collection.Push(&entries[i]);
  }

  TestThreadHolder holder;
  for (int i = 0; i != kNumThreads; ++i) {
    // Each thread randomly does one of
    // 1) pull items from shared stack and store it to local set.
    // 2) push random item from local set to shared stack.
    holder.AddThread([&collection, &stop = holder.stop_flag()] {
      std::vector<Entry*> local;
      while (!stop.load(std::memory_order_acquire)) {
        bool push = !local.empty() && RandomUniformInt(0, 1);
        if (push) {
          size_t index = RandomUniformInt<size_t>(0, local.size() - 1);
          collection.Push(local[index]);
          local[index] = local.back();
          local.pop_back();
        } else {
          auto entry = collection.Pop();
          if (entry) {
            local.push_back(entry);
          }
        }
      }
      while (!local.empty()) {
        collection.Push(local.back());
        local.pop_back();
      }
    });
  }

  holder.WaitAndStop(5s);

  std::vector<int> content;
  while (content.size() <= kNumEntries) {
    auto entry = collection.Pop();
    if (!entry) {
      break;
    }
    content.push_back(entry->value);
  }

  LOG(INFO) << "Content: " << AsString(content);

  ASSERT_EQ(content.size(), kNumEntries);

  std::sort(content.begin(), content.end());
  for (int i = 0; i != kNumEntries; ++i) {
    ASSERT_EQ(content[i], i);
  }
}

TEST(LockfreeTest, Stack) {
  TestIntrusive<LockFreeStack>();
}

TEST(LockfreeTest, SemiFairQueue) {
  TestIntrusive<SemiFairQueue>();
}

TEST(LockfreeTest, WriteOnceWeakPtr) {
  std::shared_ptr<std::string> hello = std::make_shared<std::string>("Hello");
  std::shared_ptr<std::string> world = std::make_shared<std::string>("world");

  {
    WriteOnceWeakPtr<std::string> wowp(hello);
    ASSERT_TRUE(wowp.IsInitialized());
    ASSERT_FALSE(wowp.Set(world));
    auto* hello_ptr = hello.get();
    ASSERT_EQ(wowp.raw_ptr_for_logging(), hello_ptr);
    hello.reset();
    ASSERT_EQ(wowp.lock(), nullptr);
    // Still initialized, even though the object has been destroyed.
    ASSERT_TRUE(wowp.IsInitialized());
    // The weak pointer still stores the same raw pointer.
    ASSERT_EQ(wowp.raw_ptr_for_logging(), hello_ptr);
  }

  {
    WriteOnceWeakPtr<std::string> wowp;
    ASSERT_FALSE(wowp.IsInitialized());
    ASSERT_FALSE(wowp.Set(nullptr));
    ASSERT_TRUE(!wowp.IsInitialized());  // Setting to nullptr was a no-op.
    ASSERT_TRUE(wowp.Set(world));
    ASSERT_TRUE(wowp.IsInitialized());
    // Setting the pointer the second time, even to the same value, will fail.
    ASSERT_FALSE(wowp.Set(world));
    auto* world_ptr = world.get();
    ASSERT_EQ(wowp.raw_ptr_for_logging(), world_ptr);
    world.reset();
    ASSERT_TRUE(wowp.IsInitialized());
    ASSERT_EQ(wowp.raw_ptr_for_logging(), world_ptr);
  }
}

} // namespace yb
