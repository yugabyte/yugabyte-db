// Copyright (c) YugabyteDB, Inc.
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
#include "yb/docdb/object_lock_shared_state.h"
#include "yb/docdb/object_lock_shared_state_manager.h"

#include "yb/rpc/thread_pool.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/tablet_server-test-base.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(TEST_assert_olm_empty_locks_map);
DECLARE_bool(TEST_olm_skip_scheduling_waiter_resumption);
DECLARE_bool(TEST_olm_skip_sending_wait_for_probes);
DECLARE_bool(enable_object_lock_fastpath);
DECLARE_bool(enable_ysql);

using namespace std::literals;

using yb::docdb::DocDBTableLocksConflictMatrixTest;
using yb::docdb::IntentTypeSetAdd;
using yb::docdb::LockState;
using yb::docdb::ObjectLockFastpathLockType;
using yb::docdb::ObjectLockOwner;
using yb::docdb::ObjectLockPrefix;

namespace yb::tserver {

using LockStateMap = std::unordered_map<ObjectLockPrefix, LockState>;

auto kTxn1 = ObjectLockOwner{TransactionId::GenerateRandom(), 1};
auto kTxn2 = ObjectLockOwner{TransactionId::GenerateRandom(), 1};

constexpr auto kDatabase1 = 1;
constexpr auto kDatabase2 = 2;
constexpr auto kObject1 = 1;
constexpr auto kObject2 = 2;
constexpr uint32_t kDefaultObjectId = 0;
constexpr uint32_t kDefaultObjectSubId = 0;
constexpr auto kDefaultTestStatusTabletId = "test_status_tablet";

class TSLocalLockManagerTest : public TabletServerTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_assert_olm_empty_locks_map) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_olm_skip_sending_wait_for_probes) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_lock_fastpath) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = true;
    TabletServerTestBase::SetUp();
    StartTabletServer();
    auto& server = *mini_server_->server();
    lm_ = CHECK_NOTNULL(server.ts_local_lock_manager()).get();
    BeforeSharedMemorySetup();
    lm_->TEST_MarkBootstrapped();
    // Skip shared mem negotiation since there is no pg supervisor managing conections,
    // and hence the negotiation callback never happens.
    ASSERT_OK(server.SkipSharedMemoryNegotiation());
    shared_mem_state_ = server.shared_mem_manager()->SharedData()->object_lock_state();
    shared_manager_ = server.ObjectLockSharedStateManager();
    lock_owner_registry_ = &shared_manager_->registry();
  }

  virtual void BeforeSharedMemorySetup() {}

  Status LockRelations(
      const ObjectLockOwner& owner, uint32_t database_id, const std::vector<uint32_t>& relation_ids,
      const std::vector<TableLockType>& lock_types,
      CoarseTimePoint deadline = CoarseTimePoint::max(), LockStateMap* state_map = nullptr,
      TransactionId bg_txn = TransactionId::Nil()) {
    SCHECK_EQ(relation_ids.size(), lock_types.size(), IllegalState, "Expected equal sizes");
    tserver::AcquireObjectLockRequestPB req;
    owner.PopulateLockRequest(&req);
    req.set_status_tablet(kDefaultTestStatusTabletId);
    for (size_t i = 0; i < relation_ids.size(); i++) {
      auto* lock = req.add_object_locks();
      lock->set_database_oid(database_id);
      lock->set_relation_oid(relation_ids[i]);
      lock->set_object_oid(kDefaultObjectId);
      lock->set_object_sub_oid(kDefaultObjectSubId);
      lock->set_lock_type(lock_types[i]);
    }
    req.set_propagated_hybrid_time(MonoTime::Now().ToUint64());
    if (!bg_txn.IsNil()) {
      req.set_background_transaction_id(bg_txn.data(), bg_txn.size());
    }
    Synchronizer synchronizer;
    lm_->AcquireObjectLocksAsync(req, deadline, synchronizer.AsStdStatusCallback());
    RETURN_NOT_OK(synchronizer.Wait());
    if (!state_map) {
      return Status::OK();
    }
    auto res = VERIFY_RESULT(DetermineObjectsToLock(req.object_locks()));
    bool is_lock_redundant = std::ranges::all_of(res.lock_batch, [&](auto lock_batch_entry) {
      return docdb::LockStateContains(
          (*state_map)[lock_batch_entry.key], IntentTypeSetAdd(lock_batch_entry.intent_types));
    });
    if (!is_lock_redundant) {
      for (auto& lock_batch_entry : res.lock_batch) {
        (*state_map)[lock_batch_entry.key] += IntentTypeSetAdd(lock_batch_entry.intent_types);
      }
    }
    return Status::OK();
  }

  Status LockRelation(
      const ObjectLockOwner& owner, uint32_t database_id, uint32_t relation_id,
      TableLockType lock_type, CoarseTimePoint deadline = CoarseTimePoint::max(),
      LockStateMap* state_map = nullptr, TransactionId bg_txn = TransactionId::Nil()) {
    return LockRelations(
        owner, database_id, {relation_id}, {lock_type}, deadline, state_map, bg_txn);
  }

  Status ReleaseLocksForSubtxn(
      const ObjectLockOwner& owner, CoarseTimePoint deadline = CoarseTimePoint::max()) {
    tserver::ReleaseObjectLockRequestPB req;
    owner.PopulateReleaseRequest(&req, false /* release all locks */);
    return ResultToStatus(lm_->ReleaseObjectLocks(req, deadline));
  }

  Status ReleaseLocksForOwner(
      const ObjectLockOwner& owner, CoarseTimePoint deadline = CoarseTimePoint::max()) {
    tserver::ReleaseObjectLockRequestPB req;
    owner.PopulateReleaseRequest(&req);
    return ResultToStatus(lm_->ReleaseObjectLocks(req, deadline));
  }

  Result<bool> LockRelationPgFastpath(
      docdb::SessionLockOwnerTag owner_tag, SubTransactionId subtxn_id,
      uint32_t database_id, uint32_t relation_id, ObjectLockFastpathLockType lock_type) {
    return shared_mem_state_->Lock({
        .owner = owner_tag,
        .subtxn_id = subtxn_id,
        .database_oid = database_id,
        .relation_oid = relation_id,
        .object_oid = kDefaultObjectId,
        .object_sub_oid = kDefaultObjectSubId,
        .lock_type = lock_type});
  }

  size_t GrantedLocksSize() {
    return lm_->TEST_GrantedLocksSize();
  }

  size_t WaitingLocksSize() {
    return lm_->TEST_WaitingLocksSize();
  }

  bool DoesLockTypeContainLock(TableLockType a, TableLockType b) {
    auto entries1 = docdb::GetEntriesForLockType(a);
    auto entries2 = docdb::GetEntriesForLockType(b);
    for (auto& [key2, intent_type2] : entries2) {
      bool contains = std::ranges::any_of(entries1, [&](auto key_and_intent) {
        return key_and_intent.first == key2 &&
               docdb::LockStateContains(IntentTypeSetAdd(key_and_intent.second),
                                        IntentTypeSetAdd(intent_type2));
      });
      if (!contains) {
        return false;
      }
    }
    return true;
  }

  tserver::TSLocalLockManager* lm_;
  tserver::SharedMemoryManager* shared_mem_manager_;
  docdb::ObjectLockSharedState* shared_mem_state_;
  docdb::ObjectLockSharedStateManager* shared_manager_;
  docdb::ObjectLockOwnerRegistry* lock_owner_registry_;
};

TEST_F(TSLocalLockManagerTest, TestLockAndRelease) {
  for (auto l = TableLockType_MIN + 1; l <= TableLockType_MAX; l++) {
    ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, TableLockType(l)));
    ASSERT_GE(GrantedLocksSize(), 1);
    ASSERT_EQ(WaitingLocksSize(), 0);

    ASSERT_OK(ReleaseLocksForOwner(kTxn1));
    ASSERT_EQ(GrantedLocksSize(), 0);
    ASSERT_EQ(WaitingLocksSize(), 0);
  }
}

TEST_F(TSLocalLockManagerTest, TestFastpathLockAndRelease) {
  auto txn1 = lock_owner_registry_->Register(kTxn1.txn_id, TabletId());
  for (auto l = TableLockType_MIN + 1; l <= TableLockType_MAX; l++) {
    auto lock_type = docdb::MakeObjectLockFastpathLockType(TableLockType(l));
    if (!lock_type) {
      continue;
    }

    ASSERT_TRUE(ASSERT_RESULT(LockRelationPgFastpath(
        txn1.tag(), kTxn1.subtxn_id, kDatabase1, kObject1, *lock_type)));
    ASSERT_GE(GrantedLocksSize(), 1);
    ASSERT_EQ(WaitingLocksSize(), 0);

    ASSERT_OK(ReleaseLocksForOwner(kTxn1));
    ASSERT_EQ(GrantedLocksSize(), 0);
    ASSERT_EQ(WaitingLocksSize(), 0);
  }
  ASSERT_OK(ReleaseLocksForOwner(kTxn1));
}

TEST_F(TSLocalLockManagerTest, TestFastpathConflictMatrix) {
  google::SetVLOGLevel("object_lock_shared*", 1);
  auto inner_txn = lock_owner_registry_->Register(kTxn2.txn_id, TabletId());
  for (auto l = TableLockType_MIN + 1; l <= TableLockType_MAX; l++) {
    auto outer_lock = TableLockType(l);
    auto outer_entries = docdb::GetEntriesForLockType(outer_lock);
    ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, outer_lock));
    for (auto fastpath_lock : docdb::ObjectLockFastpathLockTypeList()) {
      auto inner_lock = FastpathLockTypeToTableLockType(fastpath_lock);
      auto inner_entries = docdb::GetEntriesForLockType(inner_lock);
      LOG(INFO) << "Checking fastpath for " << TableLockType_Name(inner_lock)
                << " with existing lock " << TableLockType_Name(outer_lock);
      auto is_conflicting = ASSERT_RESULT(
          DocDBTableLocksConflictMatrixTest::ObjectLocksConflict(outer_entries, inner_entries));
      auto lock_acquired = ASSERT_RESULT(LockRelationPgFastpath(
          inner_txn.tag(), kTxn2.subtxn_id, kDatabase1, kObject1, fastpath_lock));
      ASSERT_TRUE(is_conflicting ^ lock_acquired)
          << "lock type " << TableLockType_Name(outer_lock)
          << ", " << TableLockType_Name(inner_lock)
          << " - is_conflicting: " << is_conflicting
          << ", fastpath_lock_acquired: " << lock_acquired;
      ASSERT_OK(ReleaseLocksForOwner(kTxn2));
    }
    ASSERT_OK(ReleaseLocksForOwner(kTxn1));
  }
}

TEST_F(TSLocalLockManagerTest, TestFastpathConflictWithExisting) {
  auto txn1 = lock_owner_registry_->Register(kTxn1.txn_id, TabletId());

  ASSERT_OK(LockRelation(kTxn2, kDatabase1, kObject1, TableLockType::EXCLUSIVE));

  ASSERT_FALSE(ASSERT_RESULT(LockRelationPgFastpath(
      txn1.tag(), kTxn1.subtxn_id, kDatabase1, kObject1,
      ObjectLockFastpathLockType::kRowExclusive)));

  ASSERT_OK(ReleaseLocksForOwner(kTxn1));

  ASSERT_EQ(GrantedLocksSize(), 2);
  ASSERT_EQ(WaitingLocksSize(), 0);
  ASSERT_OK(ReleaseLocksForOwner(kTxn2));
}

TEST_F(TSLocalLockManagerTest, TestFastpathBlockLaterConflicting) {
  auto txn1 = lock_owner_registry_->Register(kTxn1.txn_id, TabletId());

  ASSERT_TRUE(ASSERT_RESULT(LockRelationPgFastpath(
      txn1.tag(), kTxn1.subtxn_id, kDatabase1, kObject1,
      ObjectLockFastpathLockType::kRowExclusive)));

  auto status_future = std::async(std::launch::async, [&]() {
    return LockRelation(kTxn2, kDatabase1, kObject1, TableLockType::EXCLUSIVE);
  });

  SleepFor(1s * kTimeMultiplier);
  ASSERT_GE(WaitingLocksSize(), 1);

  ASSERT_OK(ReleaseLocksForOwner(kTxn1));

  ASSERT_OK(status_future.get());

  ASSERT_EQ(GrantedLocksSize(), 2);
  ASSERT_EQ(WaitingLocksSize(), 0);
  ASSERT_OK(ReleaseLocksForOwner(kTxn2));
}

TEST_F(TSLocalLockManagerTest, TestFastpathBlockLaterConflictingTimeout) {
  auto txn1 = lock_owner_registry_->Register(kTxn1.txn_id, TabletId());

  ASSERT_TRUE(ASSERT_RESULT(LockRelationPgFastpath(
      txn1.tag(), kTxn1.subtxn_id, kDatabase1, kObject1,
      ObjectLockFastpathLockType::kRowExclusive)));

  ASSERT_NOK(LockRelation(
      kTxn2, kDatabase1, kObject1, TableLockType::EXCLUSIVE,
      CoarseMonoClock::Now() + 1s * kTimeMultiplier));

  ASSERT_TRUE(ASSERT_RESULT(LockRelationPgFastpath(
      txn1.tag(), kTxn1.subtxn_id, kDatabase1, kObject1,
      ObjectLockFastpathLockType::kRowExclusive)));

  ASSERT_OK(ReleaseLocksForOwner(kTxn1));

  ASSERT_EQ(GrantedLocksSize(), 0);
  ASSERT_EQ(WaitingLocksSize(), 0);
}

TEST_F(TSLocalLockManagerTest, TestFastpathReleaseDuplicateExclusiveIntents) {
  auto txn2 = lock_owner_registry_->Register(kTxn2.txn_id, TabletId());
  // Test that exclusive lock intents from repeated locks on the same object are properly released.
  ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, TableLockType::EXCLUSIVE));
  ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, TableLockType::EXCLUSIVE));
  ASSERT_OK(ReleaseLocksForOwner(kTxn1));

  ASSERT_TRUE(ASSERT_RESULT(LockRelationPgFastpath(
      txn2.tag(), kTxn2.subtxn_id, kDatabase1, kObject1,
      ObjectLockFastpathLockType::kRowExclusive)));
  ASSERT_OK(ReleaseLocksForOwner(kTxn2));
}

TEST_F(TSLocalLockManagerTest, TestFastpathWeakStrongNoConflict) {
  auto txn2 = lock_owner_registry_->Register(kTxn2.txn_id, TabletId());
  ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, TableLockType::SHARE));
  ASSERT_TRUE(ASSERT_RESULT(LockRelationPgFastpath(
      txn2.tag(), kTxn2.subtxn_id, kDatabase1, kObject1, ObjectLockFastpathLockType::kRowShare)));
  ASSERT_OK(ReleaseLocksForOwner(kTxn1));
  ASSERT_OK(ReleaseLocksForOwner(kTxn2));
}

TEST_F(TSLocalLockManagerTest, TestReleaseLocksForOwner) {
  for (int i = 0; i < 5; i++) {
    ASSERT_OK(LockRelation(kTxn1, kDatabase1, i, TableLockType::ACCESS_SHARE));
  }

  TestThreadHolder thread_holder;
  CountDownLatch blocker_pending{1};
  thread_holder.AddThreadFunctor([this, &blocker_pending] {
    for (int i = 0; i < 5; i++) {
      ASSERT_OK(LockRelation(kTxn2, kDatabase1, i, TableLockType::ACCESS_EXCLUSIVE));
    }
    blocker_pending.CountDown();
  });
  SleepFor(2s * kTimeMultiplier);
  ASSERT_EQ(blocker_pending.count(), 1);

  ASSERT_OK(ReleaseLocksForOwner(kTxn1));
  ASSERT_TRUE(blocker_pending.WaitFor(2s * kTimeMultiplier));
  thread_holder.WaitAndStop(2s * kTimeMultiplier);
  ASSERT_OK(ReleaseLocksForOwner(kTxn2));
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
    ASSERT_OK(LockRelation(
        reader_txns[i],
        kDatabase1, kObject1, TableLockType::ACCESS_SHARE));
  }
  ASSERT_EQ(GrantedLocksSize(), kNumReaders);
  ASSERT_EQ(WaitingLocksSize(), 0);

  auto status_future = std::async(std::launch::async, [&]() {
    blocker_started.CountDown();
    return LockRelation(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_EXCLUSIVE);
  });

  ASSERT_TRUE(blocker_started.WaitFor(2s * kTimeMultiplier));
  SleepFor(2s * kTimeMultiplier);
  ASSERT_GE(WaitingLocksSize(), 1);

  for (int i = 0; i < kNumReaders; i++) {
    ASSERT_OK(ReleaseLocksForOwner(reader_txns[i]));
    if (i + 1 < kNumReaders) {
      ASSERT_NE(status_future.wait_for(0s), std::future_status::ready);
    }
  }
  ASSERT_OK(status_future.get());

  TestThreadHolder thread_holder;
  for (int i = 0; i < kNumReaders; i++) {
    thread_holder.AddThreadFunctor([&, i] {
      waiters_started.CountDown();
      ASSERT_OK(LockRelation(reader_txns[i], kDatabase1, kObject1, TableLockType::ACCESS_SHARE));
      waiters_blocked.CountDown();
    });
  }
  ASSERT_EQ(waiters_blocked.count(), 5);

  ASSERT_OK(ReleaseLocksForOwner(kTxn1));
  ASSERT_TRUE(waiters_blocked.WaitFor(2s * kTimeMultiplier));
  ASSERT_EQ(GrantedLocksSize(), kNumReaders);
  thread_holder.WaitAndStop(2s * kTimeMultiplier);
  for (int i = 0; i < kNumReaders; i++) {
    ASSERT_OK(ReleaseLocksForOwner(reader_txns[i]));
  }
}

TEST_F(TSLocalLockManagerTest, TestLockTypeNoneErrors) {
  ASSERT_NOK(LockRelation(kTxn1, kDatabase1, kObject1, TableLockType::NONE));
  ASSERT_GE(GrantedLocksSize(), 0);
}

TEST_F(TSLocalLockManagerTest, TestSessionIgnoresLockConflictWithSelf) {
  for (auto l = TableLockType_MIN + 1; l <= TableLockType_MAX; l++) {
    ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, TableLockType(l)));
  }
  // The above lock requests would lead to 2 entries in the granted locks map for the following keys
  // {1, kWeakObjectLock}
  // {1, kStrongObjectLock}
  ASSERT_EQ(GrantedLocksSize(), 2);
  ASSERT_OK(ReleaseLocksForOwner(kTxn1));
}

// The below test asserts that the lock manager signals the corresponding condition variable on
// every release call so as to unblock potential waiters.
TEST_F(TSLocalLockManagerTest, TestWaitersSignaledOnEveryRelease) {
  ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));
  ASSERT_OK(LockRelation(kTxn2, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));

  auto status_future = std::async(std::launch::async, [&]() {
    return LockRelation(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_EXCLUSIVE);
  });
  ASSERT_NE(status_future.wait_for(1s * kTimeMultiplier), std::future_status::ready);

  ASSERT_OK(ReleaseLocksForOwner(kTxn2));
  ASSERT_OK(status_future.get());
  ASSERT_OK(ReleaseLocksForOwner(kTxn1));
}

#ifndef NDEBUG
// When a lock rpc fails while locking the k'th key, only locks acquired as part of that rpc
// i.e (1 -> k-1) are rolled back/released. Previous locks acquired by the txn still remain
// valid until an explicit unlock request is executed. The below test asserts this behavior.
TEST_F(TSLocalLockManagerTest, TestFailedLockRpcSemantics) {
  ASSERT_OK(LockRelations(
      kTxn1, kDatabase1, {kObject1, kObject2},
      {TableLockType::ACCESS_SHARE, TableLockType::SHARE_UPDATE_EXCLUSIVE}));

  ASSERT_OK(LockRelation(kTxn2, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));
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
    return LockRelation(
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

  ASSERT_OK(ReleaseLocksForOwner(kTxn1));
  ASSERT_EQ(GrantedLocksSize(), 1);
  ASSERT_OK(ReleaseLocksForOwner(kTxn2));
}

TEST_F(TSLocalLockManagerTest, TestReleaseWaitingLocks) {
  ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));

  SyncPoint::GetInstance()->LoadDependency(
      {{"ObjectLockManagerImpl::DoLockSingleEntry", "TestReleaseWaitingLocks"}});
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();

  auto status_future = std::async(std::launch::async, [&]() {
    // txn2 + {1, kWeakObjectLock} -> would be granted
    // txn2 + {1, kStrongObjectLock} -> would end up waiting
    return LockRelation(
        kTxn2, kDatabase1, kObject1, TableLockType::ACCESS_EXCLUSIVE, CoarseMonoClock::Now() + 5s);
  });
  DEBUG_ONLY_TEST_SYNC_POINT("TestReleaseWaitingLocks");
  ASSERT_EQ(WaitingLocksSize(), 1);
  ASSERT_TRUE(status_future.valid());
  ASSERT_OK(ReleaseLocksForOwner(kTxn2));
  ASSERT_NOK(status_future.get());
  ASSERT_OK(ReleaseLocksForOwner(kTxn2));
  ASSERT_OK(ReleaseLocksForOwner(kTxn1));
}
#endif // NDEBUG

TEST_F(TSLocalLockManagerTest, TestLockAgainstDifferentDbsDontConflict) {
  ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_EXCLUSIVE));
  ASSERT_GE(GrantedLocksSize(), 1);
  ASSERT_EQ(WaitingLocksSize(), 0);

  ASSERT_OK(LockRelation(kTxn2, kDatabase2, kObject1, TableLockType::ACCESS_EXCLUSIVE));
  ASSERT_GE(GrantedLocksSize(), 2);
  ASSERT_EQ(WaitingLocksSize(), 0);

  ASSERT_OK(ReleaseLocksForOwner(kTxn1));
  ASSERT_GE(GrantedLocksSize(), 1);
  ASSERT_OK(ReleaseLocksForOwner(kTxn2));
}

TEST_F(TSLocalLockManagerTest, TestDowngradeDespiteExclusiveLockWaiter) {
  for (auto l1 = TableLockType_MIN + 1; l1 <= TableLockType_MAX; l1++) {
    auto lock_type_1 = TableLockType(l1);
    auto entries1 = docdb::GetEntriesForLockType(lock_type_1);
    for (auto l2 = TableLockType_MIN + 1; l2 <= TableLockType_MAX; l2++) {
      auto lock_type_2 = TableLockType(l2);
      auto entries2 = docdb::GetEntriesForLockType(lock_type_2);
      const auto is_conflicting = ASSERT_RESULT(
          DocDBTableLocksConflictMatrixTest::ObjectLocksConflict(entries1, entries2));

      if (is_conflicting) {
        SyncPoint::GetInstance()->LoadDependency({
            {"ObjectLockManagerImpl::DoLockSingleEntry",
                 "TestDowngradeDespiteExclusiveLockWaiter"}});
        SyncPoint::GetInstance()->ClearTrace();
        SyncPoint::GetInstance()->EnableProcessing();

        ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, lock_type_1));
        auto status_future = std::async(std::launch::async, [&]() {
        // Queue a waiter waiting for a conflicting lock w.r.t lock_type_1.
        return LockRelation(
            kTxn2, kDatabase1, kObject1, lock_type_2, CoarseMonoClock::Now() + 60s);
        });
        DEBUG_ONLY_TEST_SYNC_POINT("TestDowngradeDespiteExclusiveLockWaiter");

        for (auto l3 = l1; l3 >= TableLockType_MIN + 1; l3--) {
          // Despite a waiter waiting on a conflicting lock, the blocker should be able to
          // downgrade its lock without being blocked.
          const auto deadline = CoarseMonoClock::Now() + 2s * kTimeMultiplier;
          const auto lock_type_3 = TableLockType(l3);
          ASSERT_OK_PREPEND(
              LockRelation(kTxn1, kDatabase1, kObject1, lock_type_3, deadline),
              Format("Couldn't acquire $0 despite already having $1 when waiter waiting on $2",
                     TableLockType_Name(lock_type_3), TableLockType_Name(lock_type_1),
                     TableLockType_Name(lock_type_2)));
        }

        ASSERT_OK(ReleaseLocksForOwner(kTxn1));
        EXPECT_OK(WaitFor([&]() {
          return status_future.wait_for(0s) == std::future_status::ready;
        }, 2s * kTimeMultiplier, "Timed out waiting for unblocker waiter to acquire lock"));
        ASSERT_OK(status_future.get());
        ASSERT_OK(ReleaseLocksForOwner(kTxn2));
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
          failed = !LockRelation(
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
          ASSERT_OK(ReleaseLocksForOwner(owner, deadline));
          state_map.clear();
          owner.subtxn_id = 1;
        }
      }
      ASSERT_OK(ReleaseLocksForOwner(owner, CoarseTimePoint::max()));
    });
  }
  thread_holder.WaitAndStop(45s);
}

#ifndef NDEBUG
TEST_F(TSLocalLockManagerTest, TestWaiterResetsStateDuringShutdown) {
  ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_SHARE));

  SyncPoint::GetInstance()->LoadDependency(
      {{"ObjectLockManagerImpl::DoLockSingleEntry", "TestWaiterResetsStateDuringShutdown"}});
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();

  auto status_future = std::async(std::launch::async, [&]() {
    return LockRelation(
        kTxn2, kDatabase1, kObject1, TableLockType::ACCESS_EXCLUSIVE, CoarseMonoClock::Now() + 10s);
  });
  DEBUG_ONLY_TEST_SYNC_POINT("TestWaiterResetsStateDuringShutdown");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_olm_skip_scheduling_waiter_resumption) = true;
  ASSERT_OK(ReleaseLocksForOwner(kTxn1, CoarseTimePoint::max()));
  mini_server_->Shutdown();
  auto status = status_future.get();
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Object Lock Manager shutting down");
}

// Below test asserts that waiters are resumed despite other active waiters being amidst resumption
// when they aren't conflicting with the in progress set.
// - txn1 holds ACCESS_EXCLUSIVE lock
// - txn2, txn3 & txn4 wait for EXCLUSIVE, ROW_EXCLUSIVE & ACCESS_SHARE respectively on the same key
// - txn1 finishes, releasing just txn2, the EXCLUSIVE waiter
// - txn2 acquires EXCLUSIVE lock on the same key for another subtxn, and then releases it
//   signaling the wait-queue
// - Both txn3 & txn4 are resumed from the queue, txn4 (ACCESS_SHARE) is purposefully held back from
//   acquiring the locks for asserting the signaling mechanism.
// - Now txn3 re-enters the wait-queue since it conflicts with EXCLUSIVE
// - txn2 finishes, signaling the wait-queue.
// - txn3 is now resumed despite non-zero waiters_in_resuming_state because it is non-conflicting
TEST_F(TSLocalLockManagerTest, TestWaiterResumptionStateLogic) {
  ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, TableLockType::ACCESS_EXCLUSIVE));
  google::SetVLOGLevel("object_lock_manager*", 1);

  const size_t kTotalNumWaiters = 3;
  const size_t kNumExclusiveWaiters = 1;
  const size_t kNumRowExclusiveWaiters = 1;
  const size_t kNumAccessShareWaiters = 1;
  ASSERT_EQ(kTotalNumWaiters,
            kNumExclusiveWaiters + kNumRowExclusiveWaiters + kNumAccessShareWaiters);

  std::atomic<bool> hold_waiter_being_resumed{false};
  std::atomic<uint32_t> resumed_waiters{0};
  std::vector<std::future<Status>> status_futures;
  status_futures.reserve(kTotalNumWaiters);
  std::vector<ObjectLockOwner> lock_owners;
  lock_owners.reserve(kTotalNumWaiters);
  for (size_t i = 0 ; i < kTotalNumWaiters; i++) {
    lock_owners.push_back(ObjectLockOwner{TransactionId::GenerateRandom(), 1});
  }
  auto add_waiter_fn = [&](TableLockType lock_type, size_t idx) {
    auto log_waiter = StringWaiterLogSink("added to wait-queue on");
    status_futures.push_back(std::async(std::launch::async, [&]() {
      return LockRelation(
          lock_owners[idx], kDatabase1, kObject1,
          lock_type, CoarseMonoClock::Now() + 60s);
    }));
    ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromMilliseconds(5 * kTimeMultiplier)));
  };

  yb::SyncPoint::GetInstance()->SetCallBack(
      "WaiterEntry::Resume", [&](void* arg) {
        ++resumed_waiters;
        auto txn_id = *(static_cast<TransactionId*>(arg));
        while (hold_waiter_being_resumed && txn_id == lock_owners[2].txn_id) {
          SleepFor(100ms);
          LOG(INFO) << "Waiting for hold_waiter_being_resumed to be set to false";
        }
      });
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();

  add_waiter_fn(TableLockType::EXCLUSIVE, 0);
  add_waiter_fn(TableLockType::ROW_EXCLUSIVE, 1);
  add_waiter_fn(TableLockType::ACCESS_SHARE, 2);

  ASSERT_OK(ReleaseLocksForOwner(kTxn1, CoarseTimePoint::max()));
  ASSERT_OK(WaitFor([&]() {
    return resumed_waiters >= kNumExclusiveWaiters;
  }, 5s * kTimeMultiplier, "Expected 1 waiter to be scheduled for resumption"));
  ASSERT_OK(status_futures[0].get());
  ASSERT_OK(LockRelation(
      ObjectLockOwner{lock_owners[0].txn_id, 2}, kDatabase1, kObject1,
      TableLockType::EXCLUSIVE, CoarseMonoClock::Now() + 60s));

  hold_waiter_being_resumed = true;
  {
    auto log_waiter = StringWaiterLogSink("added to wait-queue on");
    ASSERT_OK(ReleaseLocksForSubtxn(
        ObjectLockOwner{lock_owners[0].txn_id, 2}, CoarseTimePoint::max()));
    ASSERT_OK(WaitFor([&]() {
      return resumed_waiters >= kTotalNumWaiters;
    }, 5s * kTimeMultiplier, "Expected 2 more waiters to be scheduled for resumption"));
    ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(5 * kTimeMultiplier)));
  }

  auto log_waiter =
      StringWaiterLogSink("Resuming waiter despite active waiters since they don't conflict");
  const auto old_resumed_waiters_count = resumed_waiters.load();
  ASSERT_OK(ReleaseLocksForOwner(lock_owners[0], CoarseTimePoint::max()));
  ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(5 * kTimeMultiplier)));
  ASSERT_OK(WaitFor([&]() {
    return resumed_waiters >= old_resumed_waiters_count + 1;
  }, 5s * kTimeMultiplier, "Expected another waiter to be scheduled for resumption"));
  ASSERT_OK(WaitFor([&]() {
    return status_futures[1].wait_for(0s) == std::future_status::ready;
  }, 5s * kTimeMultiplier, "Expected ROW_EXCLUSIVE waiter to have been resumed"));

  hold_waiter_being_resumed = false;
  ASSERT_OK(status_futures[2].get());
  ASSERT_OK(status_futures[1].get());
  ASSERT_OK(ReleaseLocksForOwner(lock_owners[1], CoarseTimePoint::max()));
  ASSERT_OK(ReleaseLocksForOwner(lock_owners[2], CoarseTimePoint::max()));
}
#endif

TEST_F(TSLocalLockManagerTest, YB_LINUX_DEBUG_ONLY_TEST(TestFastpathCrash)) {
  constexpr const char* kCrashPoints[] = {
    "ObjectLockSharedState::AddLockRequest:unfinalized",
    "ObjectLockSharedState::AddLockRequest:finalized",
  };

  ASSERT_EQ(GrantedLocksSize(), 0);
  ASSERT_EQ(WaitingLocksSize(), 0);

  auto txn1 = lock_owner_registry_->Register(kTxn1.txn_id, TabletId());

  for (uint32_t i = 0; i < arraysize(kCrashPoints); ++i) {
    ASSERT_OK(ForkAndRunToCrashPoint([&] {
      (void) LockRelationPgFastpath(
          txn1.tag(), kTxn1.subtxn_id, kDatabase1, i, ObjectLockFastpathLockType::kRowShare);
    }, kCrashPoints[i]));
  }

  ASSERT_EQ(GrantedLocksSize(), 1);
  ASSERT_EQ(WaitingLocksSize(), 0);

  ASSERT_OK(LockRelation(kTxn2, kDatabase1, kObject2, TableLockType::EXCLUSIVE));
  ASSERT_EQ(GrantedLocksSize(), 3);
  ASSERT_EQ(WaitingLocksSize(), 0);
  ASSERT_OK(ReleaseLocksForOwner(kTxn2));
  ASSERT_EQ(GrantedLocksSize(), 1);
  ASSERT_EQ(WaitingLocksSize(), 0);

  ASSERT_OK(ReleaseLocksForOwner(kTxn1));
}

TEST_F(TSLocalLockManagerTest, RedundadntLockBecomesNoOp) {
  google::SetVLOGLevel("object_lock_manager*", 4);
  for (auto l1 = TableLockType_MIN + 1; l1 <= TableLockType_MAX; l1++) {
    auto lock_type_1 = TableLockType(l1);
    for (auto l2 = TableLockType_MIN + 1; l2 <= TableLockType_MAX; l2++) {
      auto lock_type_2 = TableLockType(l2);
      bool is_inner_lock_redundant = DoesLockTypeContainLock(lock_type_1, lock_type_2);
      auto deadline = CoarseMonoClock::Now() + 60s;
      LOG(INFO) << "Checking " << TableLockType_Name(lock_type_1)
                << ", " << TableLockType_Name(lock_type_2);
      ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, lock_type_1, deadline));
      auto log_waiter = is_inner_lock_redundant
          ? RegexWaiterLogSink(".*Ignoring redundant acquire.*")
          : RegexWaiterLogSink(".*Locking key :.*with existing state:.*");
      ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, lock_type_2, deadline));
      ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(5 * kTimeMultiplier)));
      ASSERT_OK(ReleaseLocksForOwner(kTxn1));
    }
  }
}

TEST_F(TSLocalLockManagerTest, TestConflictsWithBgTxnAreIgnored) {
  google::SetVLOGLevel("object_lock_manager*", 4);
  for (auto l1 = TableLockType_MIN + 1; l1 <= TableLockType_MAX; l1++) {
    auto lock_type_1 = TableLockType(l1);
    auto entries1 = docdb::GetEntriesForLockType(lock_type_1);
    for (auto l2 = TableLockType_MIN + 1; l2 <= TableLockType_MAX; l2++) {
      auto lock_type_2 = TableLockType(l2);
      auto entries2 = docdb::GetEntriesForLockType(lock_type_2);
      const auto is_conflicting = ASSERT_RESULT(
          DocDBTableLocksConflictMatrixTest::ObjectLocksConflict(entries1, entries2));
      if (!is_conflicting) {
        continue;
      }
      ASSERT_OK(LockRelation(kTxn1, kDatabase1, kObject1, lock_type_1));
      ASSERT_OK(LockRelation(
          kTxn2, kDatabase1, kObject1, lock_type_2, CoarseMonoClock::Now() + 1s * kTimeMultiplier,
          nullptr, kTxn1.txn_id));
      ASSERT_OK(LockRelation(
          kTxn2, kDatabase1, kObject1, lock_type_1, CoarseMonoClock::Now() + 1s * kTimeMultiplier,
          nullptr, kTxn1.txn_id));
      ASSERT_OK(LockRelation(
          kTxn1, kDatabase1, kObject1, lock_type_2, CoarseMonoClock::Now() + 1s * kTimeMultiplier,
          nullptr, kTxn2.txn_id));

      ASSERT_OK(ReleaseLocksForOwner(kTxn1));
      ASSERT_OK(ReleaseLocksForOwner(kTxn2));
      ASSERT_EQ(GrantedLocksSize(), 0);
      ASSERT_EQ(WaitingLocksSize(), 0);
    }
  }
}

class TSLocalLockManagerBootstrappedLocksTest : public TSLocalLockManagerTest {
 public:
  void BeforeSharedMemorySetup() override {
    DdlLockEntriesPB entries;
    auto* lock_request = entries.mutable_lock_entries()->Add();
    lock_request->set_txn_id(kTxn1.txn_id.data(), kTxn1.txn_id.size());
    lock_request->set_status_tablet(kDefaultTestStatusTabletId);
    lock_request->set_subtxn_id(kTxn1.subtxn_id);
    auto* lock = lock_request->mutable_object_locks()->Add();
    lock->set_database_oid(kDatabase1);
    lock->set_relation_oid(kObject1);
    lock->set_object_oid(kDefaultObjectId);
    lock->set_object_sub_oid(kDefaultObjectSubId);
    lock->set_lock_type(TableLockType::EXCLUSIVE);
    ASSERT_OK(lm_->BootstrapDdlObjectLocks(entries));
  }

  void TearDown() override {
    ASSERT_OK(ReleaseLocksForOwner(kTxn1));
    ASSERT_EQ(GrantedLocksSize(), 0);
    ASSERT_EQ(WaitingLocksSize(), 0);
    TSLocalLockManagerTest::TearDown();
  }
};

TEST_F(TSLocalLockManagerBootstrappedLocksTest, TestSimple) {
  ASSERT_GE(GrantedLocksSize(), 1);
  ASSERT_EQ(WaitingLocksSize(), 0);

  auto txn2 = lock_owner_registry_->Register(kTxn2.txn_id, TabletId());
  ASSERT_FALSE(ASSERT_RESULT(LockRelationPgFastpath(
      txn2.tag(), kTxn2.subtxn_id, kDatabase1, kObject1,
      ObjectLockFastpathLockType::kRowExclusive)));
}

} // namespace yb::tserver
