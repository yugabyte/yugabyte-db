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

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;
using std::stack;
using std::thread;

namespace yb {
namespace docdb {

class SharedLockManagerTest : public YBTest {
 protected:
  static constexpr bool kLogOps = false;

  SharedLockManagerTest();

  void RandomLockOps(int thread_id, size_t num_keys);

  void Run(size_t num_keys, int num_threads);

 private:
  static constexpr size_t kMaxNumKeys = 10;

  // Total duration for the test run
  static constexpr int kTestDurationInSeconds = 10;

  // Maximum amount of random delay when the delay function(s) are called.
  static constexpr double kMaxDelayInMs = 10.0;

  SharedLockManager lm_;

  // Used for vals_
  std::mutex mutex_;

  // vals_[k][t] stores the number of locks of type t on the key k.
  int vals_[kMaxNumKeys][kIntentTypeMapSize];

  // Attempt to take the lock, log the attempt and result
  void Lock(int thread_id, int op_id, int key, IntentType lt);

  // Release the lock and print to log associated info.
  IntentType Unlock(int thread_id, int op_id, int key, IntentType lt);

  // Increments atomic ints for correctness verification
  void Increment(size_t key, IntentType lt);

  // Decrements atomic ints for correctness verification
  void Decrement(size_t key, IntentType lt);

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
};

SharedLockManagerTest::SharedLockManagerTest() {
  memset(vals_, 0, sizeof(vals_));
}

void SharedLockManagerTest::Lock(int thread_id, int op_id, int key, IntentType lt) {
  string type(ToString(lt));
  type.resize(16, ' ');
  if (kLogOps)
    LOG(INFO) << "\t" << thread_id << "\t" << op_id << "\t" << type << "\t" << key << "\tattempt";
  lm_.Lock("key" + std::to_string(key), lt);
  if (kLogOps)
    LOG(INFO) << "\t" << thread_id << "\t" << op_id << "\t" << type << "\t" << key << "\ttaken";
}

IntentType SharedLockManagerTest::Unlock(int thread_id, int op_id, int key, IntentType lt) {
  lm_.Unlock("key" + std::to_string(key), lt);
  string type(ToString(lt));
  type.resize(16, ' ');
  if (kLogOps)
    LOG(INFO) << "\t" << thread_id << "\t" << op_id << "\t" << type << "\t" << key << "\treleased";
  return lt;
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

void SharedLockManagerTest::Increment(size_t key, IntentType lt) {
  std::lock_guard<std::mutex> lock(mutex_);
  vals_[key][static_cast<int>(lt)]++;
}

void SharedLockManagerTest::Decrement(size_t key, IntentType lt) {
  std::lock_guard<std::mutex> lock(mutex_);
  vals_[key][static_cast<int>(lt)]--;
}

void SharedLockManagerTest::Verify() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (int i = 0; i < kMaxNumKeys; i++) {
    LockState state;
    for (auto intent_type : kIntentTypeList) {
      size_t j = static_cast<size_t>(intent_type);
      ASSERT_GE(vals_[i][j], 0) << "KEY " << i << " INVALID STATE";
      if (vals_[i][j] > 0) {
        state.set(j);
      }
    }
    ASSERT_TRUE(SharedLockManager::VerifyState(state))
        << "KEY " << i << " INVALID STATE: " << SharedLockManager::ToString(state);
  }
}

void SharedLockManagerTest::RandomLockOps(int thread_id, size_t num_keys) {
  std::mt19937 gen;
  gen.seed(thread_id + 863540); // Some arbitrary random seed.
  std::uniform_real_distribution<double> dis(0.0, 1.0);
  std::uniform_int_distribution<> int_dis(0, 5);

  // Keep a stack of locks currently taken.
  // These two stacks are always pushed to and popped from in sync.
  stack<int> locked_keys;
  stack<IntentType> lock_types;
  // Guaranteed to be sorted with top() being largest.

  int op_id = 0;

  auto begin = std::chrono::steady_clock::now();

  while(std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - begin).count() < kTestDurationInSeconds) {
    Verify();
    RandomDelay(dis(gen));
    Verify();
    // Randomly try to take a new lock or unlock an existing lock.
    if (!locked_keys.empty() && locked_keys.top() == num_keys - 1) {
      // If the last key is taken, there's no choice but to release it.
      const int last_key = locked_keys.top();
      const IntentType lock_type = lock_types.top();

      locked_keys.pop();
      lock_types.pop();

      Decrement(last_key, lock_type);
      Unlock(thread_id, op_id, last_key, lock_type);
    } else if (locked_keys.empty() || dis(gen) < 0.5) {
      // If no lock is taken, no choice but to take a lock.
      // Otherwise take a new lock with 50% probability.
      int new_key = 0;
      if (!locked_keys.empty()) new_key = locked_keys.top() + 1;
      // Randomly choose a key which is more than all the existing locks taken.
      while (new_key < num_keys - 1 && dis(gen) < 0.5) new_key++;

      // Randomly pick if shared or exclusive lock is to be taken.
      const IntentType lock_type = static_cast<IntentType>(int_dis(gen));

      locked_keys.push(new_key);
      lock_types.push(lock_type);

      Lock(thread_id, op_id, new_key, lock_type);
      Increment(new_key, lock_type);
    } else {
      // We get here only if the lock stack is non-empty.
      // Release the last lock taken with remaining 50% probability.
      const int last_key = locked_keys.top();
      const IntentType lock_type = lock_types.top();

      locked_keys.pop();
      lock_types.pop();

      Decrement(last_key, lock_type);
      Unlock(thread_id, op_id, last_key, lock_type);
    }
    op_id++;
  }
  // Release all the locks at the end.
  while (!locked_keys.empty()) {
    Verify();
    RandomDelay(dis(gen));
    Verify();
    const int key = locked_keys.top();
    const IntentType lock_type = lock_types.top();

    locked_keys.pop();
    lock_types.pop();

    Decrement(key, lock_type);
    Unlock(thread_id, op_id, key, lock_type);
    op_id++;
  }
}

void SharedLockManagerTest::Run(size_t num_keys, int num_threads) {
  if (kLogOps)
    LOG(INFO) << "\tthread\top\ttype\tkey";
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
      if (StrongIntent(i1) || StrongIntent(i2)) {
        ASSERT_TRUE(StrongIntent(combined));
      } else {
        ASSERT_TRUE(WeakIntent(combined));
      }

      // snapshot isolation > serializable write
      // snapshot isolation > serializable read:
      //   If any of the inputs is snapshot isolation, the combined isolation must be snapshot also.
      //   Otherwise, it can be serializable or snapshot isolation.
      if (SnapshotIntent(i1) || SnapshotIntent(i1)) {
        ASSERT_TRUE(SnapshotIntent(combined));
      } else {
        ASSERT_TRUE(SerializableIntent(combined) || SnapshotIntent(combined));
      }

      // serializable read + serializable write = snapshot isolation
      if (SerializableIntent(i1) && SerializableIntent(i2) &&
          (ReadIntent(i1) && WriteIntent(i2) || ReadIntent(i2) && WriteIntent(i1))) {
        ASSERT_TRUE(SnapshotIntent(combined));
      }

      // If any of the input intent is write intent, combined intent must be write intent.
      if (WriteIntent(i1) || WriteIntent(i2)) {
        ASSERT_TRUE(WriteIntent(combined));
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
          ASSERT_LE(combined_conflicts.count(), kIntentConflicts[static_cast<size_t>(i)].count())
              << i1 << ", " << i2 << ", " << combined;
        }
      }
    }
  }
}

} // namespace docdb
} // namespace yb
