// Copyright (c) YugaByte, Inc.

#include <atomic>
#include <mutex>
#include <random>
#include <stack>
#include <thread>

#include "yb/util/shared_lock_manager.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;
using std::stack;
using std::thread;

namespace yb {
namespace util {

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
  int vals_[kMaxNumKeys][NUM_LOCK_TYPES];

  // Attempt to take the lock, log the attempt and result
  void Lock(int thread_id, int op_id, int key, LockType lt);

  // Release the lock and print to log associated info.
  LockType Unlock(int thread_id, int op_id, int key, LockType lt);

  // Increments atomic ints for correctness verification
  void Increment(size_t key, LockType lt);

  // Decrements atomic ints for correctness verification
  void Decrement(size_t key, LockType lt);

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

void SharedLockManagerTest::Lock(int thread_id, int op_id, int key, LockType lt) {
  string type(ToString(lt));
  type.resize(16, ' ');
  if (kLogOps)
    LOG(INFO) << "\t" << thread_id << "\t" << op_id << "\t" << type << "\t" << key << "\tattempt";
  lm_.Lock("key" + std::to_string(key), lt);
  if (kLogOps)
    LOG(INFO) << "\t" << thread_id << "\t" << op_id << "\t" << type << "\t" << key << "\ttaken";
}

LockType SharedLockManagerTest::Unlock(int thread_id, int op_id, int key, LockType lt) {
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

void SharedLockManagerTest::Increment(size_t key, LockType lt) {
  std::lock_guard<std::mutex> lock(mutex_);
  vals_[key][static_cast<int>(lt)]++;
}

void SharedLockManagerTest::Decrement(size_t key, LockType lt) {
  std::lock_guard<std::mutex> lock(mutex_);
  vals_[key][static_cast<int>(lt)]--;
}

void SharedLockManagerTest::Verify() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (int i = 0; i < kMaxNumKeys; i++) {
    LockState state = 0;
    for (int j = 0; j < NUM_LOCK_TYPES; j++) {
      ASSERT_GE(vals_[i][j], 0) << "KEY " << i << " INVALID STATE";
      if (vals_[i][j] > 0) {
        state |= (1 << j);
      }
    }
    ASSERT_TRUE(SharedLockManager::VerifyState(state)) << "KEY " << i << " INVALID STATE";
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
  stack<LockType> lock_types;
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
      const LockType lock_type = lock_types.top();

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
      const LockType lock_type = static_cast<LockType>(int_dis(gen));

      locked_keys.push(new_key);
      lock_types.push(lock_type);

      Lock(thread_id, op_id, new_key, lock_type);
      Increment(new_key, lock_type);
    } else {
      // We get here only if the lock stack is non-empty.
      // Release the last lock taken with remaining 50% probability.
      const int last_key = locked_keys.top();
      const LockType lock_type = lock_types.top();

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
    const LockType lock_type = lock_types.top();

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

} // namespace util
} // namespace yb
