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

#include "yb/docdb/object_lock_data.h"

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

auto kTxn1 = ObjectLockOwner{VersionedTransaction{TransactionId::GenerateRandom(), 1}, 1};
auto kTxn2 = ObjectLockOwner{VersionedTransaction{TransactionId::GenerateRandom(), 1}, 1};

constexpr auto kDatabase1 = 1;
constexpr auto kDatabase2 = 2;
constexpr auto kObject1 = 1;

class TSLocalLockManagerTest : public YBTest {
 protected:
  TSLocalLockManagerTest() {
    lm_.TEST_MarkBootstrapped();
  }

  tablet::TSLocalLockManager lm_;

  Status LockObjects(
      const ObjectLockOwner& owner, uint64_t database_id, const std::vector<uint64_t>& object_ids,
      const std::vector<TableLockType>& lock_types,
      CoarseTimePoint deadline = CoarseTimePoint::max()) {
    SCHECK_EQ(object_ids.size(), lock_types.size(), IllegalState, "Expected equal sizes");
    tserver::AcquireObjectLockRequestPB req;
    owner.PopulateLockRequest(&req);
    for (size_t i = 0; i < object_ids.size(); i++) {
      auto* lock = req.add_object_locks();
      lock->set_database_oid(database_id);
      lock->set_object_oid(object_ids[i]);
      lock->set_lock_type(lock_types[i]);
    }
    return lm_.AcquireObjectLocks(req, deadline);
  }

  Status LockObject(
      const ObjectLockOwner& owner, uint64_t database_id, uint64_t object_id,
      TableLockType lock_type, CoarseTimePoint deadline = CoarseTimePoint::max()) {
    return LockObjects(owner, database_id, {object_id}, {lock_type}, deadline);
  }

  Status ReleaseObjectLock(
      const ObjectLockOwner& owner, uint64_t database_id, uint64_t object_id) {
    tserver::ReleaseObjectLockRequestPB req;
    owner.PopulateLockRequest(&req);
    auto* lock = req.add_object_locks();
    lock->set_database_oid(database_id);
    lock->set_object_oid(object_id);
    return lm_.ReleaseObjectLocks(req);
  }

  Status ReleaseAllLocksForTxn(const ObjectLockOwner& owner) {
    tserver::ReleaseObjectLockRequestPB req;
    req.set_txn_id(owner.versioned_txn.txn_id.data(), owner.versioned_txn.txn_id.size());
    req.set_txn_reuse_version(owner.versioned_txn.txn_version);
    req.set_subtxn_id(owner.subtxn_id);
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
    ASSERT_OK(LockObject(kTxn1, kDatabase1, kObject1, TableLockType(l)));
    ASSERT_GE(GrantedLocksSize(), 1);
    ASSERT_EQ(WaitingLocksSize(), 0);

    ASSERT_OK(ReleaseObjectLock(kTxn1, kDatabase1, kObject1));
    ASSERT_EQ(GrantedLocksSize(), 0);
    ASSERT_EQ(WaitingLocksSize(), 0);
  }
}

TEST_F(TSLocalLockManagerTest, TestReleaseAllLocksForTxn) {
  for (int i = 0; i < 5; i++) {
    ASSERT_OK(LockObject(kTxn1, kDatabase1, i, TableLockType::ACCESS_SHARE));
  }

  TestThreadHolder thread_holder;
  CountDownLatch blocker_pending{1};
  thread_holder.AddThreadFunctor([this, &blocker_pending] {
    for (int i = 0; i < 5; i++) {
      ASSERT_OK(LockObject(kTxn2, kDatabase1, i, TableLockType::ACCESS_EXCLUSIVE));
    }
    blocker_pending.CountDown();
  });
  SleepFor(2s * kTimeMultiplier);
  ASSERT_EQ(blocker_pending.count(), 1);

  ASSERT_OK(ReleaseAllLocksForTxn(kTxn1));
  ASSERT_TRUE(blocker_pending.WaitFor(2s * kTimeMultiplier));
  thread_holder.WaitAndStop(2s * kTimeMultiplier);
  ASSERT_OK(ReleaseAllLocksForTxn(kTxn2));
}

TEST_F(TSLocalLockManagerTest, TestWaitersAndBlocker) {
  constexpr auto kNumReaders = 5;

  CountDownLatch blocker_started{1};
  CountDownLatch waiters_started{kNumReaders};
  CountDownLatch waiters_blocked{kNumReaders};

  std::vector<ObjectLockOwner> reader_txns;
  for (uint64_t i = 0; i < kNumReaders; i++) {
    reader_txns.push_back(
        ObjectLockOwner{VersionedTransaction{TransactionId::GenerateRandom(), i}, 1});
    ASSERT_OK(LockObject(
        reader_txns[i],
        kDatabase1, kObject1, TableLockType::ACCESS_SHARE));
  }
  ASSERT_EQ(GrantedLocksSize(), kNumReaders);
  ASSERT_EQ(WaitingLocksSize(), 0);

  auto status_future = std::async(std::launch::async, [&]() {
    blocker_started.CountDown();
    return LockObject(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_EXCLUSIVE);
  });

  ASSERT_TRUE(blocker_started.WaitFor(2s * kTimeMultiplier));
  SleepFor(2s * kTimeMultiplier);
  ASSERT_GE(WaitingLocksSize(), 1);

  for (int i = 0; i < kNumReaders; i++) {
    ASSERT_OK(ReleaseObjectLock(reader_txns[i], kDatabase1, kObject1));
    if (i + 1 < kNumReaders) {
      ASSERT_NE(status_future.wait_for(0s), std::future_status::ready);
    }
  }
  ASSERT_OK(status_future.get());

  TestThreadHolder thread_holder;
  for (int i = 0; i < kNumReaders; i++) {
    thread_holder.AddThreadFunctor([&, i] {
      waiters_started.CountDown();
      ASSERT_OK(LockObject(reader_txns[i], kDatabase1, kObject1, TableLockType::ACCESS_SHARE));
      waiters_blocked.CountDown();
    });
  }
  ASSERT_EQ(waiters_blocked.count(), 5);

  ASSERT_OK(ReleaseObjectLock(kTxn1, kDatabase1, kObject1));
  ASSERT_TRUE(waiters_blocked.WaitFor(2s * kTimeMultiplier));
  ASSERT_EQ(GrantedLocksSize(), kNumReaders);
  thread_holder.WaitAndStop(2s * kTimeMultiplier);
  for (int i = 0; i < kNumReaders; i++) {
    ASSERT_OK(ReleaseObjectLock(reader_txns[i], kDatabase1, kObject1));
  }
}

TEST_F(TSLocalLockManagerTest, TestLockTypeNoneErrors) {
  ASSERT_NOK(LockObject(kTxn1, kDatabase1, kObject1, TableLockType::NONE));
  ASSERT_GE(GrantedLocksSize(), 0);
}

TEST_F(TSLocalLockManagerTest, TestSessionIgnoresLockConflictWithSelf) {
  for (auto l = TableLockType_MIN + 1; l <= TableLockType_MAX; l++) {
    ASSERT_OK(LockObject(kTxn1, kDatabase1, kObject1, TableLockType(l)));
  }
  // The above lock requests would lead to 2 entries in the granted locks map for the following keys
  // {1, kWeakObjectLock}
  // {1, kStrongObjectLock}
  ASSERT_EQ(GrantedLocksSize(), 2);
}

// The below test asserts that the lock manager signals the corresponding condition variable on
// every release call so as to unblock potential waiters.
TEST_F(TSLocalLockManagerTest, TestWaitersSignaledOnEveryRelease) {
  ASSERT_OK(LockObject(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));
  ASSERT_OK(LockObject(kTxn2, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));

  auto status_future = std::async(std::launch::async, [&]() {
    return LockObject(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_EXCLUSIVE);
  });
  ASSERT_NE(status_future.wait_for(1s * kTimeMultiplier), std::future_status::ready);

  ASSERT_OK(ReleaseObjectLock(kTxn2, kDatabase1, kObject1));
  ASSERT_OK(status_future.get());
}

#ifndef NDEBUG
// When a lock rpc fails while locking the k'th key, only locks acquired as part of that rpc
// i.e (1 -> k-1) are rolled back/released. Previous locks acquired by the txn still remain
// valid until an explicit unlock request is executed. The below test asserts this behavior.
TEST_F(TSLocalLockManagerTest, TestFailedLockRpcSemantics) {
  constexpr auto kObject2 = 2;
  ASSERT_OK(LockObjects(
      kTxn1, kDatabase1, {kObject1, kObject2},
      {TableLockType::ACCESS_SHARE, TableLockType::ACCESS_EXCLUSIVE}));

  ASSERT_OK(LockObject(kTxn2, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));
  // Granted locks map would have the following keys
  // txn1 + {1, kWeakObjectLock}
  // txn1 + {2, kWeakObjectLock}
  // txn1 + {2, kStrongObjectLock}
  // txn2 + {1, kWeakObjectLock}
  ASSERT_EQ(GrantedLocksSize(), 4);

  SyncPoint::GetInstance()->LoadDependency({
    {"LockedBatchEntry<T>::Lock", "TestFailedLockRpcSemantics"}});
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();

  auto status_future = std::async(std::launch::async, [&]() {
    // txn2 + {2, kWeakObjectLock} -> would be granted
    // txn2 + {2, kStrongObjectLock} -> would end up waiting
    return LockObject(
        kTxn2, kDatabase1, kObject2, TableLockType::ACCESS_EXCLUSIVE,
        CoarseMonoClock::Now() + 5s);
  });
  DEBUG_ONLY_TEST_SYNC_POINT("TestFailedLockRpcSemantics");

  ASSERT_EQ(GrantedLocksSize(), 5);
  ASSERT_EQ(WaitingLocksSize(), 1);
  ASSERT_NOK(status_future.get());
  // Assert that all successfull previous locks belonging to txn2 (and txn1) still exist.
  ASSERT_EQ(GrantedLocksSize(), 4);
  ASSERT_EQ(WaitingLocksSize(), 0);

  ASSERT_OK(ReleaseAllLocksForTxn(kTxn1));
  ASSERT_EQ(GrantedLocksSize(), 1);
}
#endif // NDEBUG

TEST_F(TSLocalLockManagerTest, TestLockAgainstDifferentDbsDontConflict) {
  ASSERT_OK(LockObject(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_EXCLUSIVE));
  ASSERT_GE(GrantedLocksSize(), 1);
  ASSERT_EQ(WaitingLocksSize(), 0);

  ASSERT_OK(LockObject(kTxn2, kDatabase2, kObject1, TableLockType::ACCESS_EXCLUSIVE));
  ASSERT_GE(GrantedLocksSize(), 2);
  ASSERT_EQ(WaitingLocksSize(), 0);

  ASSERT_OK(ReleaseAllLocksForTxn(kTxn1));
  ASSERT_GE(GrantedLocksSize(), 1);
  ASSERT_OK(ReleaseAllLocksForTxn(kTxn2));
}

} // namespace yb::docdb
