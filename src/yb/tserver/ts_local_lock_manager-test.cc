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

DECLARE_bool(TEST_enable_object_locking_for_table_locks);
DECLARE_bool(TEST_assert_olm_empty_locks_map);
DECLARE_bool(TEST_olm_skip_scheduling_waiter_resumption);
DECLARE_bool(TEST_olm_skip_sending_wait_for_probes);

using namespace std::literals;

using yb::docdb::IntentTypeSetAdd;
using yb::docdb::LockState;
using yb::docdb::ObjectLockOwner;
using yb::docdb::ObjectLockPrefix;

namespace yb::tserver {

using LockStateMap = std::unordered_map<ObjectLockPrefix, LockState>;

auto kTxn1 = ObjectLockOwner{TransactionId::GenerateRandom(), 1};
auto kTxn2 = ObjectLockOwner{TransactionId::GenerateRandom(), 1};

constexpr auto kDatabase1 = 1;
constexpr auto kDatabase2 = 2;
constexpr auto kObject1 = 1;

class TSLocalLockManagerTest : public TabletServerTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_object_locking_for_table_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_assert_olm_empty_locks_map) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_olm_skip_sending_wait_for_probes) = true;
    TabletServerTestBase::SetUp();
    StartTabletServer();
    lm_ = CHECK_NOTNULL(mini_server_->server()->ts_local_lock_manager());
    lm_->TEST_MarkBootstrapped();
  }

  Status LockObjects(
      const ObjectLockOwner& owner, uint64_t database_id, const std::vector<uint64_t>& object_ids,
      const std::vector<TableLockType>& lock_types,
      CoarseTimePoint deadline = CoarseTimePoint::max(), LockStateMap* state_map = nullptr) {
    SCHECK_EQ(object_ids.size(), lock_types.size(), IllegalState, "Expected equal sizes");
    tserver::AcquireObjectLockRequestPB req;
    owner.PopulateLockRequest(&req);
    for (size_t i = 0; i < object_ids.size(); i++) {
      auto* lock = req.add_object_locks();
      lock->set_database_oid(database_id);
      lock->set_object_oid(object_ids[i]);
      lock->set_lock_type(lock_types[i]);
    }
    req.set_propagated_hybrid_time(MonoTime::Now().ToUint64());
    auto s = lm_->AcquireObjectLocks(req, deadline);
    if (!state_map || !s.ok()) {
      return s;
    }
    auto res = VERIFY_RESULT(DetermineObjectsToLock(req.object_locks()));
    for (auto& lock_batch_entry : res.lock_batch) {
      (*state_map)[lock_batch_entry.key] += IntentTypeSetAdd(lock_batch_entry.intent_types);
    }
    return s;
  }

  Status LockObject(
      const ObjectLockOwner& owner, uint64_t database_id, uint64_t object_id,
      TableLockType lock_type, CoarseTimePoint deadline = CoarseTimePoint::max(),
      LockStateMap* state_map = nullptr) {
    return LockObjects(owner, database_id, {object_id}, {lock_type}, deadline, state_map);
  }

  Status ReleaseLocksForSubtxn(
      const ObjectLockOwner& owner, CoarseTimePoint deadline = CoarseTimePoint::max()) {
    tserver::ReleaseObjectLockRequestPB req;
    owner.PopulateReleaseRequest(&req, false /* release all locks */);
    return lm_->ReleaseObjectLocks(req, deadline);
  }

  Status ReleaseAllLocksForTxn(
      const ObjectLockOwner& owner, CoarseTimePoint deadline = CoarseTimePoint::max()) {
    tserver::ReleaseObjectLockRequestPB req;
    owner.PopulateReleaseRequest(&req);
    return lm_->ReleaseObjectLocks(req, deadline);
  }

  size_t GrantedLocksSize() const {
    return lm_->TEST_GrantedLocksSize();
  }

  size_t WaitingLocksSize() const {
    return lm_->TEST_WaitingLocksSize();
  }

  std::shared_ptr<tserver::TSLocalLockManager> lm_;
};

TEST_F(TSLocalLockManagerTest, TestLockAndRelease) {
  for (auto l = TableLockType_MIN + 1; l <= TableLockType_MAX; l++) {
    ASSERT_OK(LockObject(kTxn1, kDatabase1, kObject1, TableLockType(l)));
    ASSERT_GE(GrantedLocksSize(), 1);
    ASSERT_EQ(WaitingLocksSize(), 0);

    ASSERT_OK(ReleaseAllLocksForTxn(kTxn1));
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
    ASSERT_OK(ReleaseAllLocksForTxn(reader_txns[i]));
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

  ASSERT_OK(ReleaseAllLocksForTxn(kTxn1));
  ASSERT_TRUE(waiters_blocked.WaitFor(2s * kTimeMultiplier));
  ASSERT_EQ(GrantedLocksSize(), kNumReaders);
  thread_holder.WaitAndStop(2s * kTimeMultiplier);
  for (int i = 0; i < kNumReaders; i++) {
    ASSERT_OK(ReleaseAllLocksForTxn(reader_txns[i]));
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
  ASSERT_OK(ReleaseAllLocksForTxn(kTxn1));
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

  ASSERT_OK(ReleaseAllLocksForTxn(kTxn2));
  ASSERT_OK(status_future.get());
  ASSERT_OK(ReleaseAllLocksForTxn(kTxn1));
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
    {"ObjectLockManagerImpl::DoLockSingleEntry", "TestFailedLockRpcSemantics"}});
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
  ASSERT_OK(ReleaseAllLocksForTxn(kTxn2));
}

TEST_F(TSLocalLockManagerTest, TestReleaseWaitingLocks) {
  ASSERT_OK(LockObject(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));

  SyncPoint::GetInstance()->LoadDependency(
      {{"ObjectLockManagerImpl::DoLockSingleEntry", "TestReleaseWaitingLocks"}});
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
  ASSERT_OK(ReleaseAllLocksForTxn(kTxn2));
  ASSERT_OK(ReleaseAllLocksForTxn(kTxn1));
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
        SyncPoint::GetInstance()->LoadDependency({
            {"ObjectLockManagerImpl::DoLockSingleEntry",
                 "TestDowngradeDespiteExclusiveLockWaiter"}});
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

TEST_F(TSLocalLockManagerTest, TestSanity) {
  const auto kNumConns = 30;
  const auto kNumbObjects = 5;
  TestThreadHolder thread_holder;
  for (int i = 0; i < kNumConns; i++) {
    thread_holder.AddThreadFunctor([&, &stop = thread_holder.stop_flag()]() {
      LockStateMap state_map;
      auto owner = ObjectLockOwner{TransactionId::GenerateRandom(), 1};
      while (!stop) {
        LockStateMap prev = state_map;
        auto deadline = CoarseMonoClock::Now() + 2s;
        unsigned int seed = SeedRandom();
        auto lock_type = TableLockType((rand_r(&seed) % TableLockType_MAX) + 1);
        auto failed = false;
        while(!failed && rand_r(&seed) % 3) {
          failed = !LockObject(
              owner, kDatabase1, rand_r(&seed) % kNumbObjects, lock_type, deadline,
              &state_map).ok();
        }
        if (failed) {
          ASSERT_OK(ReleaseLocksForSubtxn(owner, deadline));
          state_map = prev;
        }
        owner.subtxn_id++;
        auto actual_state_map = lm_->TEST_GetLockStateMapForTxn(owner.txn_id);
        for (auto& [key, state] : state_map) {
          auto it = actual_state_map.find(key);
          ASSERT_TRUE(it != actual_state_map.end());
          ASSERT_EQ(it->second, state);
          actual_state_map.erase(it);
        }
        for (auto& [_, state] : actual_state_map) {
          ASSERT_EQ(state, 0);
        }
        if (rand_r(&seed) % 3) {
          ASSERT_OK(ReleaseAllLocksForTxn(owner, deadline));
          state_map.clear();
          owner.subtxn_id = 1;
        }
      }
      ASSERT_OK(ReleaseAllLocksForTxn(owner, CoarseTimePoint::max()));
    });
  }
  thread_holder.WaitAndStop(45s);
}

#ifndef NDEBUG
TEST_F(TSLocalLockManagerTest, TestWaiterResetsStateDuringShutdown) {
  ASSERT_OK(LockObject(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));

  SyncPoint::GetInstance()->LoadDependency(
      {{"ObjectLockManagerImpl::DoLockSingleEntry", "TestWaiterResetsStateDuringShutdown"}});
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();

  auto status_future = std::async(std::launch::async, [&]() {
    return LockObject(
        kTxn2, kDatabase1, kObject1, TableLockType::ACCESS_EXCLUSIVE, CoarseMonoClock::Now() + 10s);
  });
  DEBUG_ONLY_TEST_SYNC_POINT("TestWaiterResetsStateDuringShutdown");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_olm_skip_scheduling_waiter_resumption) = true;
  ASSERT_OK(ReleaseAllLocksForTxn(kTxn1, CoarseTimePoint::max()));
  mini_server_->Shutdown();
  auto status = status_future.get();
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Object Lock Manager shutting down");
}
#endif

} // namespace yb::tserver
