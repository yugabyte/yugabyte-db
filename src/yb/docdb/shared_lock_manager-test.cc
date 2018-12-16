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

#include <atomic>
#include <mutex>
#include <random>
#include <stack>
#include <thread>

#include "yb/docdb/shared_lock_manager.h"

#include "yb/util/random_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using namespace std::literals;

using std::string;
using std::vector;
using std::stack;
using std::thread;

namespace yb {
namespace docdb {

class SharedLockManagerTest : public YBTest {
 protected:
  SharedLockManagerTest();

  void RandomLockOps(int thread_id, size_t num_keys);

  void Run(size_t num_keys, int num_threads);

 protected:
  SharedLockManager lm_;

  static constexpr size_t kMaxNumKeys = 10;

  // Total duration for the test run
  static constexpr int kTestDurationInSeconds = 10;

  // Maximum amount of random delay when the delay function(s) are called.
  static constexpr double kMaxDelayInMs = 10.0;

  // Used for vals_ and locks_
  std::mutex mutex_;

  // lock_batches_[k][t] stores the lock batches of locks of type t on the key k.
  std::vector<LockBatch> lock_batches_[kMaxNumKeys][kIntentTypeMapSize];

  // Attempt to take the lock, log the attempt and result
  void Lock(int thread_id, int op_id, int key, IntentType lt);

  // Release the lock and print to log associated info.
  void Unlock(int thread_id, int op_id, int key, IntentType lt);

  // This function verifies that the vals_[] array has permissible numbers, e.g.
  // there's no key with 2 exclusive locks, etc.
  void Verify();

  // Sleeps current thread for a number of milliseconds.
  void SleepDelay(int duration_ms);

  // Keeps the current thread running for a number of milliseconds.
  void SpinDelay(int duration_ms);

  // rand is a double between 0 and 1. Based on the input, picks a duration and a delay type
  // between SleepDelay or SpinDelay.
  void RandomDelay(double rand);

  LockBatch TestLockBatch() {
    return LockBatch(&lm_, {
        {RefCntPrefix("foo"), IntentType::kStrongSnapshotWrite},
        {RefCntPrefix("bar"), IntentType::kStrongSnapshotWrite}});
  }
};

SharedLockManagerTest::SharedLockManagerTest() {
}

void SharedLockManagerTest::Lock(int thread_id, int op_id, int key, IntentType lt) {
  string type(ToString(lt));
  type.resize(24, ' ');
  VLOG(1) << "\t" << thread_id << "\t" << op_id << "\t" << type << "\t" << key << "\tattempt";
  LockBatch lock_batch(&lm_, {{RefCntPrefix("key" + std::to_string(key)), lt}});
  VLOG(1) << "\t" << thread_id << "\t" << op_id << "\t" << type << "\t" << key << "\ttaken";

  std::lock_guard<std::mutex> lock(mutex_);
  lock_batches_[key][static_cast<int>(lt)].push_back(std::move(lock_batch));
}

void SharedLockManagerTest::Unlock(int thread_id, int op_id, int key, IntentType lt) {
  {
    LockBatch lock_batch;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto& batches = lock_batches_[key][static_cast<int>(lt)];
      size_t size = batches.size();
      ASSERT_GT(size, 0);
      size_t idx = RandomUniformInt<size_t>(0, size - 1);
      std::swap(batches[idx], batches.back());
      lock_batch = std::move(batches.back());
      batches.pop_back();
    }
  }

  string type(ToString(lt));
  type.resize(16, ' ');
  VLOG(1) << "\t" << thread_id << "\t" << op_id << "\t" << type << "\t" << key << "\treleased";
}

void SharedLockManagerTest::SleepDelay(int duration_ms) {
  if (duration_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
}

void SharedLockManagerTest::SpinDelay(int duration_ms) {
  auto begin = std::chrono::steady_clock::now();
  char c = 0;
  while (std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - begin).count() < duration_ms) {
    c++;
  }
}

void SharedLockManagerTest::RandomDelay(double rand) {
  if (rand < 0.5) {
    rand *= 2.0;
    SleepDelay(static_cast<int> (rand * kMaxDelayInMs));
  } else {
    rand = rand * 2.0 - 1.0;
    SpinDelay(static_cast<int> (rand * kMaxDelayInMs));
  }
}

void SharedLockManagerTest::Verify() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (int i = 0; i < kMaxNumKeys; i++) {
    LockState state = 0;
    for (auto intent_type : kIntentTypeList) {
      size_t j = static_cast<size_t>(intent_type);
      if (lock_batches_[i][j].size() > 0) {
        state |= kIntentMask[j];
      }
    }
    ASSERT_TRUE(SharedLockManager::IsStatePossible(state))
        << "KEY " << i << " INVALID STATE: " << SharedLockManager::ToString(state);
  }
}

void SharedLockManagerTest::RandomLockOps(int thread_id, size_t num_keys) {
  std::mt19937 gen;
  gen.seed(thread_id + 863540); // Some arbitrary random seed.
  std::uniform_real_distribution<double> dis(0.0, 1.0);
  std::vector<IntentType> intents = kIntentTypeList;
  std::uniform_int_distribution<> int_dis(0, intents.size() - 1);

  // Keep a stack of locks currently taken.
  // These two stacks are always pushed to and popped from in sync.
  stack<int> locked_keys;
  stack<IntentType> lock_types;
  // Guaranteed to be sorted with top() being largest.

  int op_id = 0;

  auto begin = std::chrono::steady_clock::now();

  while(std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - begin).count() < kTestDurationInSeconds) {
    ASSERT_NO_FATALS(Verify());
    RandomDelay(dis(gen));
    ASSERT_NO_FATALS(Verify());
    // Randomly try to take a new lock or unlock an existing lock.
    if (!locked_keys.empty() && locked_keys.top() == num_keys - 1) {
      // If the last key is taken, there's no choice but to release it.
      const int last_key = locked_keys.top();
      const IntentType lock_type = lock_types.top();

      locked_keys.pop();
      lock_types.pop();

      Unlock(thread_id, op_id, last_key, lock_type);
    } else if (locked_keys.empty() || dis(gen) < 0.5) {
      // If no lock is taken, no choice but to take a lock.
      // Otherwise take a new lock with 50% probability.
      int new_key = 0;
      if (!locked_keys.empty()) new_key = locked_keys.top() + 1;
      // Randomly choose a key which is more than all the existing locks taken.
      while (new_key < num_keys - 1 && dis(gen) < 0.5) new_key++;

      // Randomly pick if shared or exclusive lock is to be taken.
      const IntentType lock_type = intents[int_dis(gen)];

      locked_keys.push(new_key);
      lock_types.push(lock_type);

      Lock(thread_id, op_id, new_key, lock_type);
    } else {
      // We get here only if the lock stack is non-empty.
      // Release the last lock taken with remaining 50% probability.
      const int last_key = locked_keys.top();
      const IntentType lock_type = lock_types.top();

      locked_keys.pop();
      lock_types.pop();

      Unlock(thread_id, op_id, last_key, lock_type);
    }
    op_id++;
  }
  // Release all the locks at the end.
  while (!locked_keys.empty()) {
    ASSERT_NO_FATALS(Verify());
    RandomDelay(dis(gen));
    ASSERT_NO_FATALS(Verify());
    const int key = locked_keys.top();
    const IntentType lock_type = lock_types.top();

    locked_keys.pop();
    lock_types.pop();

    Unlock(thread_id, op_id, key, lock_type);
    op_id++;
  }
}

void SharedLockManagerTest::Run(size_t num_keys, int num_threads) {
  VLOG(1) << "\tthread\top\ttype\tkey";
  vector<thread> t;
  for (int i = 0; i < num_threads; i++) {
    t.emplace_back([this, i, num_keys](){
      RandomLockOps(i, num_keys);
    });
  }
  for (int i = 0; i < num_threads; i++) {
    t[i].join();
  }
}

TEST_F(SharedLockManagerTest, RandomLockUnlockTest) {
  Run(2, 3);
}

TEST_F(SharedLockManagerTest, RandomLockUnlockManyKeysTest) {
  Run(10, 3);
}

TEST_F(SharedLockManagerTest, RandomLockUnlockManyThreadsTest) {
  Run(2, 8);
}

namespace {

// Returns true if c1 covers (is superset of) c2.
inline bool Covers(const LockState& c1, const LockState& c2) {
  return (c1 & c2) == c2;
}

} // namespace

TEST_F(SharedLockManagerTest, CombineIntentsTest) {
  // Verify the lock type returned from SharedLockManager::CombineIntents() that combines two
  // intents satisfies the following rules:
  // - strong > weak
  // - snapshot isolation > serializable write
  // - snapshot isolation > serializable read
  // - serializable read + serializable write = snapshot isolation
  for (const auto i1 : kIntentTypeList) {
    for (const auto i2 : kIntentTypeList) {
      const auto combined = SharedLockManager::CombineIntents(i1, i2);

      // strong > weak:
      //   If any of the input intents is strong intent, the combined intent must be strong.
      //   Otherwise, it should be weak intent.
      if (IsStrongIntent(i1) || IsStrongIntent(i2)) {
        ASSERT_TRUE(IsStrongIntent(combined));
      } else {
        ASSERT_TRUE(IsWeakIntent(combined));
      }

      // snapshot isolation > serializable write
      // snapshot isolation > serializable read:
      //   If any of the inputs is snapshot isolation, the combined isolation must be snapshot also.
      //   Otherwise, it can be serializable or snapshot isolation.
      if (IsSnapshotIntent(i1) || IsSnapshotIntent(i1)) {
        ASSERT_TRUE(IsSnapshotIntent(combined));
      } else {
        ASSERT_TRUE(IsSerializableIntent(combined) || IsSnapshotIntent(combined));
      }

      // serializable read + serializable write = snapshot isolation
      if (IsSerializableIntent(i1) && IsSerializableIntent(i2) &&
          ((IsReadIntent(i1) && IsWriteIntent(i2)) || (IsReadIntent(i2) && IsWriteIntent(i1)))) {
        ASSERT_TRUE(IsSnapshotIntent(combined));
      }

      // If any of the input intent is write intent, combined intent must be write intent.
      if (IsWriteIntent(i1) || IsWriteIntent(i2)) {
        ASSERT_TRUE(IsWriteIntent(combined));
      }

      // Verify that the combined lock type covers the input intents' conflicts.
      const LockState c1 = kIntentConflicts[static_cast<size_t>(i1)];
      const LockState c2 = kIntentConflicts[static_cast<size_t>(i2)];
      const LockState combined_conflicts = kIntentConflicts[static_cast<size_t>(combined)];
      ASSERT_TRUE(Covers(combined_conflicts, c1)) << i1 << ", " << i2 << ", " << combined;
      ASSERT_TRUE(Covers(combined_conflicts, c2)) << i1 << ", " << i2 << ", " << combined;

      // Verify that the combined lock type's conflict set is the smallest among all matched
      // lock types.
      for (const auto i : kIntentTypeList) {
        const LockState c = kIntentConflicts[static_cast<size_t>(i)];
        if (Covers(c, c1) && Covers(c, c2)) {
          ASSERT_EQ(combined_conflicts & kIntentConflicts[static_cast<size_t>(i)],
                    combined_conflicts)
              << i1 << ", " << i2 << ", " << combined;
        }
      }
    }
  }
}

TEST_F(SharedLockManagerTest, LockBatchAutoUnlockTest) {
  for (int i = 0; i < 2; ++i) {
    auto lb = TestLockBatch();
    EXPECT_EQ(2, lb.size());
    EXPECT_FALSE(lb.empty());
    // The locks get unlocked on scope exit.
  }
}

TEST_F(SharedLockManagerTest, LockBatchMoveConstructor) {
  LockBatch lb = TestLockBatch();
  EXPECT_EQ(2, lb.size());
  EXPECT_FALSE(lb.empty());

  LockBatch lb2(std::move(lb));
  EXPECT_EQ(2, lb2.size());
  EXPECT_FALSE(lb2.empty());

  // lb has been moved from and is now empty
  EXPECT_EQ(0, lb.size());
  EXPECT_TRUE(lb.empty());
}

TEST_F(SharedLockManagerTest, LockBatchMoveAssignment) {
  LockBatch lb = TestLockBatch();

  LockBatch lb2 = std::move(lb);
  EXPECT_EQ(2, lb2.size());
  EXPECT_FALSE(lb2.empty());

  // lb has been moved from and is now empty
  EXPECT_EQ(0, lb.size());
  EXPECT_TRUE(lb.empty());
}

TEST_F(SharedLockManagerTest, LockBatchReset) {
  LockBatch lb = TestLockBatch();
  lb.Reset();

  EXPECT_EQ(0, lb.size());
  EXPECT_TRUE(lb.empty());
}

// Launch pairs of threads. Each pair tries to lock/unlock on the same key sequence.
// This catches bug in SharedLockManager when condition is waited incorrectly.
TEST_F(SharedLockManagerTest, QuickLockUnlock) {
  const auto kThreads = 2 * 32; // Should be even

  std::atomic<bool> stop_requested{false};
  std::vector<std::thread> threads;
  std::atomic<size_t> finished_threads{0};
  while (threads.size() != kThreads) {
    size_t pair_idx = threads.size() / 2;
    threads.emplace_back([this, &stop_requested, &finished_threads, pair_idx] {
      int i = 0;
      while (!stop_requested.load(std::memory_order_acquire)) {
        RefCntPrefix key(Format("key_$0_$1", pair_idx, i));
        LockBatch lb(&lm_, {{key, IntentType::kStrongSnapshotWrite}});
        ++i;
      }
      finished_threads.fetch_add(1, std::memory_order_acq_rel);
    });
  }

  std::this_thread::sleep_for(30s);
  LOG(INFO) << "Requesting stop";
  stop_requested.store(true, std::memory_order_release);

  ASSERT_OK(WaitFor(
      [&finished_threads] {
        return finished_threads.load(std::memory_order_acquire) == kThreads;
      },
      3s,
      "All threads finished"));

  for (auto& thread : threads) {
    thread.join();
  }
}

} // namespace docdb
} // namespace yb
