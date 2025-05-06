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

#include "yb/docdb/docdb-test.h"
#include "yb/docdb/lock_util.h"
#include "yb/docdb/object_lock_data.h"

#include "yb/rpc/thread_pool.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/tablet_server-test-base.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

using yb::docdb::ObjectLockOwner;

namespace yb::tserver {

auto kTxn1 = ObjectLockOwner{TransactionId::GenerateRandom(), 1};
auto kTxn2 = ObjectLockOwner{TransactionId::GenerateRandom(), 1};

constexpr auto kDatabase1 = 1;
constexpr auto kDatabase2 = 2;
constexpr auto kObject1 = 1;

class TSLocalLockManagerTest : public TabletServerTestBase {
 protected:
  TSLocalLockManagerTest() {
    auto ts = TabletServerTestBase::CreateMiniTabletServer();
    CHECK_OK(ts);
    mini_server_.reset(ts->release());
    lm_ = std::make_unique<tserver::TSLocalLockManager>(
        new server::HybridClock(), mini_server_->server());
    lm_->TEST_MarkBootstrapped();
  }

  std::unique_ptr<tserver::MiniTabletServer> mini_server_;
  std::unique_ptr<tserver::TSLocalLockManager> lm_;

  void SetUp() override {
    YBTest::SetUp();
    ASSERT_OK(lm_->clock()->Init());
  }

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
    return lm_->AcquireObjectLocks(req, deadline);
  }

  Status LockObject(
      const ObjectLockOwner& owner, uint64_t database_id, uint64_t object_id,
      TableLockType lock_type, CoarseTimePoint deadline = CoarseTimePoint::max()) {
    return LockObjects(owner, database_id, {object_id}, {lock_type}, deadline);
  }

  Status ReleaseObjectLock(
      const ObjectLockOwner& owner, CoarseTimePoint deadline = CoarseTimePoint::max()) {
    tserver::ReleaseObjectLockRequestPB req;
    owner.PopulateLockRequest(&req);
    return lm_->ReleaseObjectLocks(req, deadline);
  }

  Status ReleaseAllLocksForTxn(
      const ObjectLockOwner& owner, CoarseTimePoint deadline = CoarseTimePoint::max()) {
    tserver::ReleaseObjectLockRequestPB req;
    req.set_txn_id(owner.txn_id.data(), owner.txn_id.size());
    req.set_subtxn_id(owner.subtxn_id);
    return lm_->ReleaseObjectLocks(req, deadline);
  }

  size_t GrantedLocksSize() const {
    return lm_->TEST_GrantedLocksSize();
  }

  size_t WaitingLocksSize() const {
    return lm_->TEST_WaitingLocksSize();
  }
};

TEST_F(TSLocalLockManagerTest, TestLockAndRelease) {
  for (auto l = TableLockType_MIN + 1; l <= TableLockType_MAX; l++) {
    ASSERT_OK(LockObject(kTxn1, kDatabase1, kObject1, TableLockType(l)));
    ASSERT_GE(GrantedLocksSize(), 1);
    ASSERT_EQ(WaitingLocksSize(), 0);

    ASSERT_OK(ReleaseObjectLock(kTxn1));
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
        ObjectLockOwner{TransactionId::GenerateRandom(), 1});
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
    ASSERT_OK(ReleaseObjectLock(reader_txns[i]));
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

  ASSERT_OK(ReleaseObjectLock(kTxn1));
  ASSERT_TRUE(waiters_blocked.WaitFor(2s * kTimeMultiplier));
  ASSERT_EQ(GrantedLocksSize(), kNumReaders);
  thread_holder.WaitAndStop(2s * kTimeMultiplier);
  for (int i = 0; i < kNumReaders; i++) {
    ASSERT_OK(ReleaseObjectLock(reader_txns[i]));
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

  ASSERT_OK(ReleaseObjectLock(kTxn2));
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
      {TableLockType::ACCESS_SHARE, TableLockType::SHARE_UPDATE_EXCLUSIVE}));

  ASSERT_OK(LockObject(kTxn2, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));
  // Granted locks map would have the following keys
  // txn1 + {1, kWeakObjectLock}
  // txn1 + {2, kWeakObjectLock}
  // txn1 + {2, kStrongObjectLock}
  // txn2 + {1, kWeakObjectLock}
  ASSERT_EQ(GrantedLocksSize(), 4);

  SyncPoint::GetInstance()->LoadDependency({
    {"ObjectLockedBatchEntry::Lock", "TestFailedLockRpcSemantics"}});
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();

  auto status_future = std::async(std::launch::async, [&]() {
    // txn2 + {2, kWeakObjectLock} -> would be granted
    // txn2 + {2, kStrongObjectLock} -> would end up waiting
    return LockObject(
        kTxn2, kDatabase1, kObject2, TableLockType::SHARE,
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

TEST_F(TSLocalLockManagerTest, TestReleaseWaitingLocks) {
  ASSERT_OK(LockObject(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));

  SyncPoint::GetInstance()->LoadDependency(
      {{"ObjectLockedBatchEntry::Lock", "TestReleaseWaitingLocks"}});
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();

  auto status_future = std::async(std::launch::async, [&]() {
    // txn2 + {1, kWeakObjectLock} -> would be granted
    // txn2 + {1, kStrongObjectLock} -> would end up waiting
    return LockObject(
        kTxn2, kDatabase1, kObject1, TableLockType::ACCESS_EXCLUSIVE, CoarseMonoClock::Now() + 5s);
  });
  DEBUG_ONLY_TEST_SYNC_POINT("TestReleaseWaitingLocks");
  ASSERT_EQ(WaitingLocksSize(), 1);
  ASSERT_TRUE(status_future.valid());
  ASSERT_OK(ReleaseAllLocksForTxn(kTxn2));
  ASSERT_NOK(status_future.get());
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

TEST_F(TSLocalLockManagerTest, TestDowngradeDespiteExclusiveLockWaiter) {
  for (auto l1 = TableLockType_MIN + 1; l1 <= TableLockType_MAX; l1++) {
    auto lock_type_1 = TableLockType(l1);
    auto entries1 = docdb::GetEntriesForLockType(lock_type_1);
    for (auto l2 = TableLockType_MIN + 1; l2 <= TableLockType_MAX; l2++) {
      auto lock_type_2 = TableLockType(l2);
      auto entries2 = docdb::GetEntriesForLockType(lock_type_2);
      const auto is_conflicting = ASSERT_RESULT(
          docdb::DocDBTableLocksConflictMatrixTest::ObjectLocksConflict(entries1, entries2));

      if (is_conflicting) {
        SyncPoint::GetInstance()->LoadDependency(
            {{"ObjectLockedBatchEntry::Lock", "TestDowngradeDespiteExclusiveLockWaiter"}});
        SyncPoint::GetInstance()->ClearTrace();
        SyncPoint::GetInstance()->EnableProcessing();

        ASSERT_OK(LockObject(kTxn1, kDatabase1, kObject1, lock_type_1));
        auto status_future = std::async(std::launch::async, [&]() {
        // Queue a waiter waiting for a conflicting lock w.r.t lock_type_1.
        return LockObject(
            kTxn2, kDatabase1, kObject1, lock_type_2, CoarseMonoClock::Now() + 60s);
        });
        DEBUG_ONLY_TEST_SYNC_POINT("TestDowngradeDespiteExclusiveLockWaiter");

        for (auto l3 = l1; l3 >= TableLockType_MIN + 1; l3--) {
          // Despite a waiter waiting on a conflicting lock, the blocker should be able to
          // downgrade its lock without being blocked.
          const auto deadline = CoarseMonoClock::Now() + 2s * kTimeMultiplier;
          const auto lock_type_3 = TableLockType(l3);
          ASSERT_OK_PREPEND(
              LockObject(kTxn1, kDatabase1, kObject1, lock_type_3, deadline),
              Format("Couldn't acquire $0 despite already having $1 when waiter waiting on $2",
                     TableLockType_Name(lock_type_3), TableLockType_Name(lock_type_1),
                     TableLockType_Name(lock_type_2)));
        }

        ASSERT_OK(ReleaseAllLocksForTxn(kTxn1));
        EXPECT_OK(WaitFor([&]() {
          return status_future.wait_for(0s) == std::future_status::ready;
        }, 2s * kTimeMultiplier, "Timed out waiting for unblocker waiter to acquire lock"));
        ASSERT_OK(status_future.get());
        ASSERT_OK(ReleaseAllLocksForTxn(kTxn2));
      }
    }
  }
}

} // namespace yb::tserver
