// Copyright (c) YugaByte, Inc.

#include <thread>
#include <random>
#include <stack>
#include <atomic>

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
  static constexpr int kNumThreads = 3;
  static constexpr bool kLogOps = false;

  SharedLockManagerTest();

  void RandomLockOps(int thread_id);

 private:
  static constexpr int kNumKeys = 2;

  // Total duration for the test run
  static constexpr int kTestDurationInSeconds = 10;

  // Maximum amount of random delay when the delay function(s) are called.
  static constexpr double kMaxDelayInMs = 10.0;

  // Increment for each exclusive lock on the atomic test variables. For bookkeeping and testing.
  static constexpr uint32_t kExclusiveLockDelta = 1 << 16;

  SharedLockManager lm_;

  // vals_[k] stores the state of key k. If there are u exclusive locks and v shared locks,
  // the value of the test variable is u * kExclusiveLockDelta + v.
  std::atomic<int> vals_[kNumKeys];

  // Attempt to take the lock, log the attempt and result
  void Lock(int thread_id, int op_id, LockType lt, int key);

  // Release the lock and print to log associated info.
  LockType Unlock(int thread_id, int op_id, int key);

  // Increments atomic ints for correctness verification
  void Increment(int key, LockType lt);

  // Decrements atomic ints for correctness verification
  void Decrement(int key, LockType lt);

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
  for (int i = 0; i < kNumKeys; i++) {
    vals_[i] = 0;
  }
}

void SharedLockManagerTest::Lock(int thread_id, int op_id, LockType lt, int key) {
  string type = lt == LockType::SHARED ? "shared   " : "exclusive";
  if (kLogOps)
    LOG(INFO) << "\t" << thread_id << "\t" << op_id << "\t" << type << "\t" << key << "\tattempt";
  lm_.Lock("key" + std::to_string(key), lt);
  if (kLogOps)
    LOG(INFO) << "\t" << thread_id << "\t" << op_id << "\t" << type << "\t" << key << "\ttaken";
}

LockType SharedLockManagerTest::Unlock(int thread_id, int op_id, int key) {
  LockType lt = lm_.Unlock("key" + std::to_string(key));
  string type = lt == LockType::SHARED ? "shared   " : "exclusive";
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

void SharedLockManagerTest::Increment(int key, LockType lt) {
  vals_[key] += lt == LockType::EXCLUSIVE ? kExclusiveLockDelta : 1;
}

void SharedLockManagerTest::Decrement(int key, LockType lt) {
  vals_[key] -= lt == LockType::EXCLUSIVE ? kExclusiveLockDelta : 1;
}

void SharedLockManagerTest::Verify() {
  for (int i = 0; i < kNumKeys; i++) {
    int x = vals_[i];
    if (x != kExclusiveLockDelta && (x < 0 || x > kNumThreads)) {
      FAIL() << "KEY " << i << " INVALID STATE " << x;
    }
  }
}

void SharedLockManagerTest::RandomLockOps(int thread_id) {
  std::mt19937 gen;
  gen.seed(thread_id + 863540); // Some arbitrary random seed.
  std::uniform_real_distribution<double> dis(0.0, 1.0);

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
    if (!locked_keys.empty() && locked_keys.top() == kNumKeys - 1) {
      // If the last key is taken, there's no choice but to release it.
      const int last_key = locked_keys.top();
      const LockType lock_type = lock_types.top();

      locked_keys.pop();
      lock_types.pop();

      Decrement(last_key, lock_type);
      ASSERT_EQ(lock_type, Unlock(thread_id, op_id, last_key));
    } else if (locked_keys.empty() || dis(gen) < 0.5) {
      // If no lock is taken, no choice but to take a lock.
      // Otherwise take a new lock with 50% probability.
      int new_key = 0;
      if (!locked_keys.empty()) new_key = locked_keys.top() + 1;
      // Randomly choose a key which is more than all the existing locks taken.
      while (new_key < kNumKeys - 1 && dis(gen) < 0.5) new_key++;

      // Randomly pick if shared or exclusive lock is to be taken.
      const LockType lock_type = dis(gen) < 0.5 ? LockType::SHARED : LockType::EXCLUSIVE;

      locked_keys.push(new_key);
      lock_types.push(lock_type);

      Lock(thread_id, op_id, lock_type, new_key);
      Increment(new_key, lock_type);
    } else {
      // We get here only if the lock stack is non-empty.
      // Release the last lock taken with remaining 50% probability.
      const int last_key = locked_keys.top();
      const LockType lock_type = lock_types.top();

      locked_keys.pop();
      lock_types.pop();

      Decrement(last_key, lock_type);
      ASSERT_EQ(lock_type, Unlock(thread_id, op_id, last_key));
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
    Unlock(thread_id, op_id, key);
    op_id++;
  }
}

TEST_F(SharedLockManagerTest, RandomLockUnlockTest) {
  if (kLogOps)
    LOG(INFO) << "\tthread\top\ttype\tkey";
  vector<thread> t;
  for (int i = 0; i < kNumThreads; i++) {
    t.emplace_back([this, i](){
      RandomLockOps(i);
    });
  }
  for (int i = 0; i < kNumThreads; i++) {
    t[i].join();
  }
}

}
}
