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
#include <stack>
#include <thread>

#include "yb/docdb/lock_batch.h"
#include "yb/docdb/shared_lock_manager.h"

#include "yb/rpc/thread_pool.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using namespace std::literals;

using std::string;
using std::vector;
using std::thread;

DECLARE_bool(dump_lock_keys);

namespace yb {
namespace docdb {

using dockv::IntentType;
using dockv::IntentTypeSet;

const RefCntPrefix kKey1("foo"s);
const RefCntPrefix kKey2("bar"s);

class SharedLockManagerTest : public YBTest {
 protected:
  SharedLockManagerTest();

 protected:
  SharedLockManager lm_;

  LockBatch TestLockBatch(CoarseTimePoint deadline = CoarseTimePoint::max()) {
    return LockBatch(&lm_, {
        {kKey1, IntentTypeSet({IntentType::kStrongWrite, IntentType::kStrongRead})},
        {kKey2, IntentTypeSet({IntentType::kStrongWrite, IntentType::kStrongRead})}},
        deadline);
  }
};

SharedLockManagerTest::SharedLockManagerTest() {
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
  ASSERT_OK(lb.status());

  LockBatch lb_fail = TestLockBatch(CoarseMonoClock::now() + 10ms);
  ASSERT_FALSE(lb_fail.status().ok());
  ASSERT_TRUE(lb_fail.empty());

  LockBatch lb2(std::move(lb));
  EXPECT_EQ(2, lb2.size());
  EXPECT_FALSE(lb2.empty());
  ASSERT_OK(lb2.status());

  // lb has been moved from and is now empty.
  EXPECT_EQ(0, lb.size()); // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(lb.empty()); // NOLINT(bugprone-use-after-move)
  ASSERT_OK(lb.status()); // NOLINT(bugprone-use-after-move)

  LockBatch lb_fail2(std::move(lb_fail));
  ASSERT_FALSE(lb_fail2.status().ok());
  ASSERT_TRUE(lb_fail2.empty());
}

TEST_F(SharedLockManagerTest, LockBatchMoveAssignment) {
  LockBatch lb = TestLockBatch();

  LockBatch lb_fail = TestLockBatch(CoarseMonoClock::now() + 10ms);
  ASSERT_FALSE(lb_fail.status().ok());
  ASSERT_TRUE(lb_fail.empty());

  LockBatch lb2 = std::move(lb);
  EXPECT_EQ(2, lb2.size());
  EXPECT_FALSE(lb2.empty());
  ASSERT_OK(lb2.status());

  // lb has been moved from and is now empty.
  EXPECT_EQ(0, lb.size()); // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(lb.empty()); // NOLINT(bugprone-use-after-move)

  LockBatch lb_fail2 = std::move(lb_fail);
  ASSERT_FALSE(lb_fail2.status().ok());
  ASSERT_TRUE(lb_fail2.empty());
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
        LockBatch lb(&lm_,
                     {{key, IntentTypeSet({IntentType::kStrongWrite, IntentType::kStrongRead})}},
                     CoarseTimePoint::max());
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

TEST_F(SharedLockManagerTest, LockConflicts) {
  rpc::ThreadPool tp(rpc::ThreadPoolOptions{
    .name = "test_pool"s,
    .max_workers = 1,
  });

  for (size_t idx1 = 0; idx1 != dockv::kIntentTypeSetMapSize; ++idx1) {
    IntentTypeSet set1(idx1);
    SCOPED_TRACE(Format("Set1: $0", set1));
    for (size_t idx2 = 0; idx2 != dockv::kIntentTypeSetMapSize; ++idx2) {
      IntentTypeSet set2(idx2);
      SCOPED_TRACE(Format("Set2: $0", set2));
      LockBatch lb1(&lm_, {{kKey1, set1}}, CoarseTimePoint::max());
      ASSERT_OK(lb1.status());
      LockBatch lb2(&lm_, {{kKey1, set2}}, CoarseMonoClock::now());
      if (lb2.status().ok()) {
        // Lock on set2 was taken fast enough, it means that sets should NOT conflict.
        ASSERT_FALSE(IntentTypeSetsConflict(set1, set2));
      } else {
        // Lock on set2 was taken not taken for too long, it means that sets should conflict.
        ASSERT_TRUE(IntentTypeSetsConflict(set1, set2));
      }
    }
  }

  tp.Shutdown();
}

TEST_F(SharedLockManagerTest, DumpKeys) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_dump_lock_keys) = true;

  auto lb1 = TestLockBatch();
  ASSERT_OK(lb1.status());
  auto lb2 = TestLockBatch(CoarseMonoClock::now() + 10ms);
  ASSERT_NOK(lb2.status());
  ASSERT_STR_CONTAINS(
      lb2.status().ToString(),
      "[{ key: 666F6F intent_types: [kStrongRead, kStrongWrite] existing_state: 0 }, "
      "{ key: 626172 intent_types: [kStrongRead, kStrongWrite] existing_state: 0 }]");
}

} // namespace docdb
} // namespace yb
