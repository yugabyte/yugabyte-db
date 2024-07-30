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

#include "yb/tserver/ts_local_lock_manager.h"

#include "yb/rpc/thread_pool.h"
#include "yb/util/async_util.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

namespace yb::docdb {

constexpr auto kSession1 = 1;
constexpr auto kSession2 = 2;
constexpr auto kObject1 = 1;

class TSLocalLockManagerTest : public YBTest {
 protected:
  TSLocalLockManagerTest() = default;

  tablet::TSLocalLockManager lm_;

  Status LockObjects(
      uint64_t session_id, const std::vector<uint64_t>& object_ids,
      const std::vector<TableLockType>& lock_types,
      CoarseTimePoint deadline = CoarseTimePoint::max()) {
    SCHECK_EQ(object_ids.size(), lock_types.size(), IllegalState, "Expected equal sizes");
    tserver::AcquireObjectLockRequestPB req;
    req.set_session_id(session_id);
    req.set_session_host_uuid("localhost");
    for (size_t i = 0; i < object_ids.size(); i++) {
      auto* lock = req.add_object_locks();
      lock->set_id(object_ids[i]);
      lock->set_lock_type(lock_types[i]);
    }
    return lm_.AcquireObjectLocks(req, deadline);
  }

  Status LockObject(uint64_t session_id, uint64_t object_id, TableLockType lock_type,
                    CoarseTimePoint deadline = CoarseTimePoint::max()) {
    return LockObjects(session_id, {object_id}, {lock_type}, deadline);
  }

  Status ReleaseObjectLock(uint64_t session_id, uint64_t object_id) {
    tserver::ReleaseObjectLockRequestPB req;
    req.set_session_id(session_id);
    req.set_session_host_uuid("localhost");
    req.add_object_ids(object_id);
    return lm_.ReleaseObjectLocks(req);
  }

  Status ReleaseAllLocksForSession(uint64_t session_id) {
    tserver::ReleaseObjectLockRequestPB req;
    req.set_session_id(session_id);
    req.set_session_host_uuid("localhost");
    req.set_release_all_locks(true);
    return lm_.ReleaseObjectLocks(req);
  }

  size_t GrantedLocksSize() const {
    return lm_.TEST_GrantedLocksSize();
  }

  size_t WaitingLocksSize() const {
    return lm_.TEST_WaitingLocksSize();
  }
};

TEST_F(TSLocalLockManagerTest, TestLockAndRelease) {
  for (auto l = TableLockType_MIN + 1; l <= TableLockType_MAX; l++) {
    ASSERT_OK(LockObject(kSession1, kObject1, TableLockType(l)));
    ASSERT_GE(GrantedLocksSize(), 1);
    ASSERT_EQ(WaitingLocksSize(), 0);

    ASSERT_OK(ReleaseObjectLock(kSession1, kObject1));
    ASSERT_EQ(GrantedLocksSize(), 0);
    ASSERT_EQ(WaitingLocksSize(), 0);
  }
}

TEST_F(TSLocalLockManagerTest, TestReleaseAllLocksForSession) {
  constexpr auto kReaderSession = 1;
  constexpr auto kWriterSession = 2;
  for (int i = 0; i < 5; i++) {
    ASSERT_OK(LockObject(kReaderSession, i, TableLockType::ACCESS_SHARE));
  }

  TestThreadHolder thread_holder;
  CountDownLatch blocker_pending{1};
  thread_holder.AddThreadFunctor([this, &blocker_pending] {
    for (int i = 0; i < 5; i++) {
      ASSERT_OK(LockObject(kWriterSession, i, TableLockType::ACCESS_EXCLUSIVE));
    }
    blocker_pending.CountDown();
  });
  SleepFor(2s * kTimeMultiplier);
  ASSERT_EQ(blocker_pending.count(), 1);

  ASSERT_OK(ReleaseAllLocksForSession(kReaderSession));
  ASSERT_TRUE(blocker_pending.WaitFor(2s * kTimeMultiplier));
  thread_holder.WaitAndStop(2s * kTimeMultiplier);
  ASSERT_OK(ReleaseAllLocksForSession(kWriterSession));
}

TEST_F(TSLocalLockManagerTest, TestWaitersAndBlocker) {
  constexpr auto kNumReaders = 5;
  constexpr auto kWriterSession = 100;

  CountDownLatch blocker_started{1};
  CountDownLatch waiters_started{kNumReaders};
  CountDownLatch waiters_blocked{kNumReaders};

  for (int i = 0; i < kNumReaders; i++) {
    ASSERT_OK(LockObject(i, kObject1, TableLockType::ACCESS_SHARE));
  }
  ASSERT_EQ(GrantedLocksSize(), kNumReaders);
  ASSERT_EQ(WaitingLocksSize(), 0);

  auto status_future = std::async(std::launch::async, [&]() {
    blocker_started.CountDown();
    return LockObject(kWriterSession, kObject1, TableLockType::ACCESS_EXCLUSIVE);
  });

  ASSERT_TRUE(blocker_started.WaitFor(2s * kTimeMultiplier));
  SleepFor(2s * kTimeMultiplier);
  ASSERT_GE(WaitingLocksSize(), 1);

  for (int i = 0; i < kNumReaders; i++) {
    ASSERT_OK(ReleaseObjectLock(i, kObject1));
    if (i + 1 < kNumReaders) {
      ASSERT_NE(status_future.wait_for(0s), std::future_status::ready);
    }
  }
  ASSERT_OK(status_future.get());

  TestThreadHolder thread_holder;
  for (int i = 0; i < kNumReaders; i++) {
    thread_holder.AddThreadFunctor([this, i, &waiters_started, &waiters_blocked] {
      waiters_started.CountDown();
      ASSERT_OK(LockObject(i, kObject1, TableLockType::ACCESS_SHARE));
      waiters_blocked.CountDown();
    });
  }
  ASSERT_EQ(waiters_blocked.count(), 5);

  ASSERT_OK(ReleaseObjectLock(kWriterSession, kObject1));
  ASSERT_TRUE(waiters_blocked.WaitFor(2s * kTimeMultiplier));
  ASSERT_EQ(GrantedLocksSize(), kNumReaders);
  thread_holder.WaitAndStop(2s * kTimeMultiplier);
  for (int i = 0; i < kNumReaders; i++) {
    ASSERT_OK(ReleaseObjectLock(i, kObject1));
  }
}

TEST_F(TSLocalLockManagerTest, TestLockTypeNoneErrors) {
  ASSERT_NOK(LockObject(1, 1, TableLockType::NONE));
  ASSERT_GE(GrantedLocksSize(), 0);
}

TEST_F(TSLocalLockManagerTest, TestSessionIgnoresLockConflictWithSelf) {
  for (auto l = TableLockType_MIN + 1; l <= TableLockType_MAX; l++) {
    ASSERT_OK(LockObject(kSession1, kObject1, TableLockType(l)));
  }
  // The above lock requests would lead to 2 entries in the granted locks map for the following keys
  // {1, kWeakObjectLock}
  // {1, kStrongObjectLock}
  ASSERT_EQ(GrantedLocksSize(), 2);
}

// The below test asserts that the lock manager signals the corresponding condition variable on
// every release call so as to unblock potential waiters.
TEST_F(TSLocalLockManagerTest, TestWaitersSignaledOnEveryRelease) {
  ASSERT_OK(LockObject(kSession1, kObject1, TableLockType::ACCESS_SHARE));
  ASSERT_OK(LockObject(kSession2, kObject1, TableLockType::ACCESS_SHARE));

  auto status_future = std::async(std::launch::async, [&]() {
    return LockObject(kSession1, kObject1, TableLockType::ACCESS_EXCLUSIVE);
  });
  ASSERT_NE(status_future.wait_for(1s * kTimeMultiplier), std::future_status::ready);

  ASSERT_OK(ReleaseObjectLock(kSession2, kObject1));
  ASSERT_OK(status_future.get());
}

#ifndef NDEBUG
// When a lock rpc fails while locking the k'th key, only locks acquired as part of that rpc
// i.e (1 -> k-1) are rolled back/released. Previous locks acquired by the session still remain
// valid until an explicit unlock request is executed. The below test asserts this behavior.
TEST_F(TSLocalLockManagerTest, TestFailedLockRpcSemantics) {
  constexpr auto kObject2 = 2;
  ASSERT_OK(LockObjects(
      kSession1, {kObject1, kObject2},
      {TableLockType::ACCESS_SHARE, TableLockType::ACCESS_EXCLUSIVE}));

  ASSERT_OK(LockObject(kSession2, kObject1, TableLockType::ACCESS_SHARE));
  // Granted locks map would have the following keys
  // session1 + {1, kWeakObjectLock}
  // session1 + {2, kWeakObjectLock}
  // session1 + {2, kStrongObjectLock}
  // session2 + {1, kWeakObjectLock}
  ASSERT_EQ(GrantedLocksSize(), 4);

  SyncPoint::GetInstance()->LoadDependency({
    {"LockedBatchEntry<T>::Lock", "TestFailedLockRpcSemantics"}});
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();

  auto status_future = std::async(std::launch::async, [&]() {
    // session2 + {2, kWeakObjectLock} -> would be granted
    // session2 + {2, kStrongObjectLock} -> would end up waiting
    return LockObject(
        kSession2, kObject2, TableLockType::ACCESS_EXCLUSIVE, CoarseMonoClock::Now() + 5s);
  });
  DEBUG_ONLY_TEST_SYNC_POINT("TestFailedLockRpcSemantics");

  ASSERT_EQ(GrantedLocksSize(), 5);
  ASSERT_EQ(WaitingLocksSize(), 1);
  ASSERT_NOK(status_future.get());
  // Assert that all successfull previous locks belonging to session2 (and session1) still exist.
  ASSERT_EQ(GrantedLocksSize(), 4);
  ASSERT_EQ(WaitingLocksSize(), 0);

  ASSERT_OK(ReleaseAllLocksForSession(kSession1));
  ASSERT_EQ(GrantedLocksSize(), 1);
}
#endif // NDEBUG

} // namespace yb::docdb
