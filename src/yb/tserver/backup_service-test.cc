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

#include <future>

#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"

#include "yb/rocksdb/db.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/outbound_call.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_retention_policy.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/backup.proxy.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_flusher.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server-test-base.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_admin.pb.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

DECLARE_bool(TEST_enable_sync_points);
DECLARE_bool(TEST_fail_flush_mem_table);
DECLARE_bool(enable_history_cutoff_propagation);
DECLARE_bool(snapshot_create_flush_before_submit);
DECLARE_int32(snapshot_preflush_concurrency);
DECLARE_int32(snapshot_preflush_timeout_ms);
DECLARE_int32(tablet_flush_concurrency);
DECLARE_int32(timestamp_history_retention_interval_sec);
METRIC_DECLARE_gauge_uint64(snapshot_preflush_active);
METRIC_DECLARE_gauge_uint64(tablet_flush_active);

namespace yb {
namespace tserver {

using std::string;
using namespace std::literals;

using yb::rpc::RpcController;

class BackupServiceTest : public TabletServerTestBase {
 public:
  BackupServiceTest() : TabletServerTestBase(TableType::YQL_TABLE_TYPE) {}

  Status WriteSingleRow(
      const std::string& tablet_id, int32_t key, int32_t int_val, const std::string& string_val);

  Status CreateSnapshot(
      const std::string& tablet_id, const TxnSnapshotId& snapshot_id,
      HybridTime snapshot_time = HybridTime::kInvalid);

  Status RestoreSnapshot(
      const std::string& tablet_id, const TxnSnapshotId& snapshot_id,
      const TxnSnapshotRestorationId& restoration_id);

 protected:
  void SetUp() override {
    TabletServerTestBase::SetUp();
    StartTabletServer();
  }
};

class SnapshotPreflushServiceTest : public BackupServiceTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_create_flush_before_submit) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
    BackupServiceTest::SetUp();
  }

  void PauseFlush(
      CountDownLatch& flushed, CountDownLatch& release, std::atomic<bool>* submitted = nullptr) {
    auto* sync = SyncPoint::GetInstance();
    sync->SetCallBack("TabletFlusher::Flushed", [&flushed, &release](void*) {
      flushed.CountDown();
      release.Wait();
    });
    if (submitted) {
      sync->SetCallBack("SnapshotPreflush::BeforeSubmit", [submitted](void*) {
        submitted->store(true, std::memory_order_release);
      });
    }
    sync->EnableProcessing();
  }

  Result<tablet::TabletPtr> AddTablet(const TabletId& id) {
    RETURN_NOT_OK(mini_server_->AddTestTablet(
        "test-namespace", "test-table", id, schema_, table_type_));
    RETURN_NOT_OK(WaitForTabletRunning(id.c_str()));
    return VERIFY_RESULT(mini_server_->server()->tablet_manager()->GetTablet(id))->shared_tablet();
  }

  void InitializeRetention(const tablet::TabletPtr& tablet) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_history_cutoff_propagation) = false;
    ASSERT_OK(tablet->metadata()->set_namespace_id("test-namespace"));
    ASSERT_OK(mini_server_->server()->XClusterHandleMasterHeartbeatResponse({}));
  }

  HybridTime HistoryCutoff(const tablet::TabletPtr& tablet) {
    return tablet->RetentionPolicy()->GetRetentionDirective().history_cutoff.primary_cutoff_ht;
  }

  uint64_t ActiveFlushes() {
    return mini_server_->server()->metric_entity()
        ->FindOrNull<AtomicGauge<uint64_t>>(METRIC_tablet_flush_active)->value();
  }

  void TestPartialFlushFailure(bool fail_wait) {
    auto first = ASSERT_RESULT(tablet_peer_->shared_tablet());
    auto second = ASSERT_RESULT(AddTablet("second-tablet"));
    ASSERT_OK(WriteSingleRow(first->tablet_id(), 1, 11, "value"));
    ASSERT_OK(WriteSingleRow(second->tablet_id(), 1, 11, "value"));
    auto* blocked_db = (fail_wait ? second : first)->regular_db();
    auto* failing_tablet = (fail_wait ? first : second).get();
    CountDownLatch started(1), injected(1), release(1);
    auto* sync = SyncPoint::GetInstance();
    sync->SetCallBack("DBImpl::BackgroundCallFlush:Start", [&](void* arg) {
      if (arg == blocked_db) {
        started.CountDown();
        release.Wait();
      }
    });
    sync->SetCallBack(fail_wait ? "TabletFlusher::BeforeWait" : "TabletFlusher::BeforeSuperblock",
        [&](void* arg) {
      auto& hook = *static_cast<std::pair<tablet::Tablet*, Status>*>(arg);
      if (hook.first == failing_tablet) {
        hook.second = STATUS(IOError, "injected flush failure");
        injected.CountDown();
      }
    });
    sync->EnableProcessing();
    auto result = std::make_shared<std::promise<std::pair<Status, TabletId>>>();
    auto future = result->get_future();
    bool accepted = false;
    auto cleanup = ScopeExit([&] {
      release.CountDown();
      if (accepted && future.valid()) {
        future.wait();
      }
      sync->DisableProcessing();
      sync->ClearAllCallBacks();
    });
    auto& flusher = mini_server_->server()->tablet_manager()->tablet_flusher();
    FlushTabletsRequestPB request;
    request.set_operation(FlushTabletsRequestPB::FLUSH);
    const auto deadline = CoarseMonoClock::Now() + 10s;
    ASSERT_OK(flusher.Submit({first, second}, request, deadline,
        [result](const Status& status, const TabletId& id) { result->set_value({status, id}); }));
    accepted = true;
    ASSERT_TRUE(started.WaitFor(5s));
    ASSERT_TRUE(injected.WaitFor(5s));
    ASSERT_EQ(ActiveFlushes(), 1);
    ASSERT_EQ(future.wait_for(20ms), std::future_status::timeout);
    ASSERT_TRUE(flusher.Submit({first}, request, deadline,
        [](const Status&, const TabletId&) {}).IsServiceUnavailable());
    release.CountDown();
    ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
    const auto [status, id] = future.get();
    ASSERT_TRUE(status.IsIOError()) << status;
    ASSERT_STR_CONTAINS(status.ToString(), "injected flush failure");
    ASSERT_EQ(id, failing_tablet->tablet_id());
    ASSERT_EQ(ActiveFlushes(), 0);
  }

  void WaitForRetiredPreflight() {
    ASSERT_OK(WaitFor([this] {
      return mini_server_->server()->metric_entity()
          ->FindOrNull<AtomicGauge<uint64_t>>(METRIC_snapshot_preflush_active)->value() == 0;
    }, 5s, "Retire preflight callbacks"));
  }
};

TEST_F(SnapshotPreflushServiceTest, WritesContinueUntilPreflushCompletes) {
  CountDownLatch flushed(1), release(1);
  std::atomic<bool> submitted{false};
  auto* sync = SyncPoint::GetInstance();
  PauseFlush(flushed, release, &submitted);
  TestThreadHolder threads;
  auto cleanup = ScopeExit([&] {
    release.CountDown();
    threads.JoinAll();
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "value"));
  Status status;
  threads.AddThreadFunctor([&] {
    status = CreateSnapshot(kTabletId, TxnSnapshotId::GenerateRandom());
  });
  ASSERT_TRUE(flushed.WaitFor(5s));
  ASSERT_FALSE(submitted.load(std::memory_order_acquire));
  ASSERT_OK(WriteSingleRow(kTabletId, 2, 22, "value"));
  // Reject overlapping preflights rather than incorrectly reusing an earlier flush barrier.
  ASSERT_NOK(CreateSnapshot(kTabletId, TxnSnapshotId::GenerateRandom()));
  release.CountDown();
  threads.JoinAll();
  ASSERT_OK(status);
  ASSERT_TRUE(submitted.load(std::memory_order_acquire));
  ASSERT_NO_FATALS(WaitForRetiredPreflight());
}

TEST_F(SnapshotPreflushServiceTest, TimeoutDoesNotSubmitOnLateCompletion) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_preflush_timeout_ms) = 250;
  CountDownLatch flushed(1), release(1), completed(1);
  std::atomic<bool> submitted{false};
  auto* sync = SyncPoint::GetInstance();
  PauseFlush(flushed, release, &submitted);
  TestThreadHolder threads;
  auto cleanup = ScopeExit([&] {
    release.CountDown();
    threads.JoinAll();
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  Status status;
  threads.AddThreadFunctor([&] {
    status = CreateSnapshot(kTabletId, TxnSnapshotId::GenerateRandom());
    completed.CountDown();
  });
  ASSERT_TRUE(flushed.WaitFor(5s));
  ASSERT_TRUE(completed.WaitFor(5s));
  ASSERT_TRUE(status.IsTimedOut()) << status;
  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "value"));
  ASSERT_EQ(mini_server_->server()->metric_entity()
      ->FindOrNull<AtomicGauge<uint64_t>>(METRIC_snapshot_preflush_active)->value(), 1);
  release.CountDown();
  threads.JoinAll();
  ASSERT_NO_FATALS(WaitForRetiredPreflight());
  ASSERT_FALSE(submitted.load(std::memory_order_acquire));
}

TEST_F(SnapshotPreflushServiceTest, ShutdownAbortsPendingPreflight) {
  CountDownLatch flushed(1), release(1), completed(1);
  auto* sync = SyncPoint::GetInstance();
  PauseFlush(flushed, release);
  TestThreadHolder threads;
  auto cleanup = ScopeExit([&] {
    release.CountDown();
    threads.JoinAll();
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  Status status;
  threads.AddThreadFunctor([&] {
    status = CreateSnapshot(kTabletId, TxnSnapshotId::GenerateRandom());
    completed.CountDown();
  });
  ASSERT_TRUE(flushed.WaitFor(5s));
  threads.AddThreadFunctor([&] { mini_server_->Shutdown(); });
  ASSERT_TRUE(completed.WaitFor(5s));
  ASSERT_NOK(status);
  release.CountDown();
  threads.JoinAll();
}

TEST_F(SnapshotPreflushServiceTest, SnapshotDataIncludesWritesAfterPreflush) {
  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "value"));
  auto* sync = SyncPoint::GetInstance();
  Status write_status;
  CountDownLatch write_completed(1);
  sync->SetCallBack("SnapshotPreflush::BeforeSubmit", [&](void*) {
    write_status = WriteSingleRow(kTabletId, 2, 22, "value");
    write_completed.CountDown();
  });
  sync->EnableProcessing();
  auto cleanup = ScopeExit([&] {
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  auto snapshot_id = TxnSnapshotId::GenerateRandom();
  ASSERT_OK(CreateSnapshot(kTabletId, snapshot_id));
  ASSERT_TRUE(write_completed.WaitFor(5s));
  ASSERT_OK(write_status);
  ASSERT_OK(WriteSingleRow(kTabletId, 3, 33, "value"));
  ASSERT_OK(RestoreSnapshot(kTabletId, snapshot_id, TxnSnapshotRestorationId::GenerateRandom()));
  VerifyRows(schema_, {KeyValue(1, 11), KeyValue(2, 22)});
}

TEST_F(SnapshotPreflushServiceTest, HistoryRemainsPinnedThroughPreflight) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_history_cutoff_propagation) = false;
  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "value"));
  auto peer = ASSERT_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  auto tablet = ASSERT_RESULT(peer->shared_tablet());
  ASSERT_NO_FATALS(InitializeRetention(tablet));
  const auto snapshot_time = peer->clock().Now();
  std::atomic<bool> history_pinned{false};
  auto* sync = SyncPoint::GetInstance();
  sync->SetCallBack("SnapshotPreflush::BeforeSubmit", [&](void*) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
    history_pinned.store(tablet->RetentionPolicy()->GetRetentionDirective()
        .history_cutoff.primary_cutoff_ht <= snapshot_time, std::memory_order_release);
  });
  sync->EnableProcessing();
  auto cleanup = ScopeExit([&] {
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  ASSERT_OK(CreateSnapshot(kTabletId, TxnSnapshotId::GenerateRandom(), snapshot_time));
  ASSERT_TRUE(history_pinned.load(std::memory_order_acquire));
  ASSERT_OK(WaitFor([&] {
    return tablet->RetentionPolicy()->GetRetentionDirective().history_cutoff.primary_cutoff_ht >
        snapshot_time;
  }, 5s, "Snapshot history guard released after submission"));
}

TEST_F(SnapshotPreflushServiceTest, EqualTimestampReadersUnregisterIndependently) {
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_NO_FATALS(InitializeRetention(tablet));
  const auto time = tablet_peer_->clock().Now();
  auto first = ASSERT_RESULT(tablet::ScopedReadOperation::Create(
      tablet.get(), tablet::RequireLease::kTrue, ReadHybridTime::SingleTime(time)));
  auto second = ASSERT_RESULT(tablet::ScopedReadOperation::Create(
      tablet.get(), tablet::RequireLease::kTrue, ReadHybridTime::SingleTime(time)));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_EQ(HistoryCutoff(tablet), time);
  first.Reset();
  ASSERT_EQ(HistoryCutoff(tablet), time);
  second.Reset();
  ASSERT_GT(HistoryCutoff(tablet), time);
}

TEST_F(SnapshotPreflushServiceTest, RejectedOverlapKeepsOriginalHistoryPin) {
  Status status;
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_NO_FATALS(InitializeRetention(tablet));
  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "value"));
  const auto time = tablet_peer_->clock().Now();
  CountDownLatch flushed(1), release(1);
  auto* sync = SyncPoint::GetInstance();
  PauseFlush(flushed, release);
  TestThreadHolder threads;
  auto cleanup = ScopeExit([&] {
    release.CountDown();
    threads.JoinAll();
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  threads.AddThreadFunctor([&] {
    status = CreateSnapshot(kTabletId, TxnSnapshotId::GenerateRandom(), time);
  });
  ASSERT_TRUE(flushed.WaitFor(5s));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_EQ(HistoryCutoff(tablet), time);
  auto rejected = CreateSnapshot(kTabletId, TxnSnapshotId::GenerateRandom(), time);
  ASSERT_EQ(rpc::RpcError(rejected), rpc::ErrorStatusPB::ERROR_SERVER_TOO_BUSY) << rejected;
  // Rejection releases its guard before responding, so this checks the surviving registration.
  ASSERT_EQ(HistoryCutoff(tablet), time);
  release.CountDown();
  threads.JoinAll();
  ASSERT_OK(status);
  ASSERT_OK(WaitFor([&] { return HistoryCutoff(tablet) > time; }, 5s, "Release history pin"));
}

TEST_F(SnapshotPreflushServiceTest, StepdownAfterFlushPreventsSubmission) {
  auto peer = ASSERT_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  auto consensus = ASSERT_RESULT(peer->GetConsensus());
  auto* sync = SyncPoint::GetInstance();
  Status stepdown_status;
  CountDownLatch stepped_down(1);
  sync->SetCallBack("SnapshotPreflush::BeforeSubmit", [&](void*) {
    consensus::LeaderStepDownRequestPB request;
    consensus::LeaderStepDownResponsePB response;
    request.set_tablet_id(kTabletId);
    request.set_disable_graceful_transition(true);
    stepdown_status = consensus->StepDown(&request, &response);
    if (stepdown_status.ok() && response.has_error()) {
      stepdown_status = StatusFromPB(response.error().status());
    }
    stepped_down.CountDown();
  });
  sync->EnableProcessing();
  auto cleanup = ScopeExit([&] {
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  const auto snapshot_id = TxnSnapshotId::GenerateRandom();
  ASSERT_NOK(CreateSnapshot(kTabletId, snapshot_id));
  ASSERT_TRUE(stepped_down.WaitFor(5s));
  ASSERT_OK(stepdown_status);
  ASSERT_NO_FATALS(WaitForRetiredPreflight());
  ASSERT_FALSE(peer->tablet_metadata()->fs_manager()->env()->FileExists(JoinPathSegments(
      peer->tablet_metadata()->snapshots_dir(), snapshot_id.ToString())));
}

TEST_F(SnapshotPreflushServiceTest, ReceiverRejectsOverlapAndInvalidFlags) {
  auto peer = ASSERT_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  auto tablet = ASSERT_RESULT(peer->shared_tablet());
  auto& flusher = mini_server_->server()->tablet_manager()->tablet_flusher();
  FlushTabletsRequestPB request;
  request.set_operation(FlushTabletsRequestPB::FLUSH);
  request.set_flags(tablet::FLUSH_COMPACT_VECTOR_INDEX_EXCLUDED);
  const auto deadline = CoarseMonoClock::Now() + 5s;
  const auto ignored = [](const Status&, const TabletId&) {};
  ASSERT_TRUE(flusher.Submit({tablet}, request, deadline, ignored).IsInvalidArgument());
  request.set_flags(tablet::FLUSH_COMPACT_ALL);
  ASSERT_TRUE(flusher.Submit({tablet}, request, CoarseMonoClock::Now(), ignored).IsTimedOut());

  CountDownLatch flushed(1), release(1);
  auto* sync = SyncPoint::GetInstance();
  PauseFlush(flushed, release);
  auto cleanup = ScopeExit([&] {
    release.CountDown();
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  auto result = std::make_shared<std::promise<Status>>();
  auto future = result->get_future();
  ASSERT_OK(flusher.Submit({tablet, tablet}, request, deadline,
      [result](const Status& status, const TabletId&) { result->set_value(status); }));
  ASSERT_TRUE(flushed.WaitFor(5s));
  ASSERT_TRUE(flusher.Submit({tablet}, request, deadline, ignored).IsServiceUnavailable());
  release.CountDown();
  ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
  ASSERT_OK(future.get());
}

TEST_F(SnapshotPreflushServiceTest, GuardFailureDoesNotLaunchEarlierFlush) {
  auto first = ASSERT_RESULT(tablet_peer_->shared_tablet());
  auto second = ASSERT_RESULT(AddTablet("second-tablet"));
  ASSERT_OK(WriteSingleRow(first->tablet_id(), 1, 11, "value"));
  auto second_peer = ASSERT_RESULT(
      mini_server_->server()->tablet_manager()->GetTablet(second->tablet_id()));
  ASSERT_OK(second_peer->TEST_Shutdown(
      tablet::ShouldAbortActiveTransactions::kFalse, tablet::DisableFlushOnShutdown::kTrue));
  auto started = std::make_shared<std::atomic<bool>>(false);
  auto* sync = SyncPoint::GetInstance();
  sync->SetCallBack("DBImpl::BackgroundCallFlush:Start",
      [started, db = first->regular_db()](void* arg) {
    if (arg == db) {
      started->store(true, std::memory_order_release);
    }
  });
  sync->EnableProcessing();
  auto cleanup = ScopeExit([&] {
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  auto result = std::make_shared<std::promise<std::pair<Status, TabletId>>>();
  auto future = result->get_future();
  FlushTabletsRequestPB request;
  ASSERT_OK(mini_server_->server()->tablet_manager()->tablet_flusher().Submit(
      {first, second}, request, CoarseMonoClock::Now() + 5s,
      [result](const Status& status, const TabletId& id) { result->set_value({status, id}); }));
  ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
  const auto [status, id] = future.get();
  ASSERT_TRUE(status.IsShutdownInProgress()) << status;
  ASSERT_EQ(id, second->tablet_id());
  ASSERT_OK(first->regular_db()->WaitForFlush());
  first->regular_db()->WaitForFlushJobs();
  ASSERT_FALSE(started->load(std::memory_order_acquire));
  ASSERT_EQ(ActiveFlushes(), 0);
}

TEST_F(SnapshotPreflushServiceTest, SuperblockFailureDrainsEarlierFlush) {
  TestPartialFlushFailure(false);
}

TEST_F(SnapshotPreflushServiceTest, WaitFailureDrainsLaterFlush) {
  TestPartialFlushFailure(true);
}

TEST_F(SnapshotPreflushServiceTest, BackgroundErrorRetainsAdmissionUntilCleanup) {
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "value"));
  CountDownLatch failed(1), release(1);
  auto* sync = SyncPoint::GetInstance();
  sync->SetCallBack("DBImpl::WaitAfterBackgroundError", [&](void* arg) {
    if (arg == tablet->regular_db()) {
      failed.CountDown();
      release.Wait();
    }
  });
  sync->EnableProcessing();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_flush_mem_table) = true;
  auto result = std::make_shared<std::promise<Status>>();
  auto future = result->get_future();
  bool accepted = false;
  auto cleanup = ScopeExit([&] {
    release.CountDown();
    if (accepted && future.valid()) {
      future.wait();
    }
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_flush_mem_table) = false;
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  auto& flusher = mini_server_->server()->tablet_manager()->tablet_flusher();
  FlushTabletsRequestPB request;
  request.set_operation(FlushTabletsRequestPB::FLUSH);
  ASSERT_OK(flusher.Submit({tablet}, request, CoarseMonoClock::Now() + 5s,
      [result](const Status& status, const TabletId&) { result->set_value(status); }));
  accepted = true;
  ASSERT_TRUE(failed.WaitFor(5s));
  ASSERT_NOK(tablet->regular_db()->WaitForFlush());
  ASSERT_EQ(ActiveFlushes(), 1);
  ASSERT_EQ(future.wait_for(20ms), std::future_status::timeout);
  ASSERT_TRUE(flusher.Submit({tablet}, request, CoarseMonoClock::Now() + 5s,
      [](const Status&, const TabletId&) {}).IsServiceUnavailable());
  release.CountDown();
  ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
  ASSERT_TRUE(future.get().IsIOError());
  ASSERT_EQ(ActiveFlushes(), 0);
}

TEST_F(SnapshotPreflushServiceTest, ReceiverRpcTimeoutRetainsAdmission) {
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "value"));
  CountDownLatch started(1), release(1);
  auto* sync = SyncPoint::GetInstance();
  sync->SetCallBack("DBImpl::BackgroundCallFlush:Start", [&](void* arg) {
    if (arg == tablet->regular_db()) {
      started.CountDown();
      release.Wait();
    }
  });
  sync->EnableProcessing();
  FlushTabletsRequestPB request;
  request.set_dest_uuid(mini_server_->server()->permanent_uuid());
  request.add_tablet_ids(kTabletId);
  FlushTabletsResponsePB response;
  RpcController controller;
  controller.set_timeout(250ms);
  CountDownLatch replied(1);
  auto cleanup = ScopeExit([&] {
    release.CountDown();
    replied.Wait();
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
    ASSERT_OK(WaitFor([&] { return ActiveFlushes() == 0; }, 5s, "Receiver flush retired"));
  });
  admin_proxy_->FlushTabletsAsync(request, &response, &controller, [&] { replied.CountDown(); });
  ASSERT_TRUE(started.WaitFor(5s));
  ASSERT_TRUE(replied.WaitFor(5s));
  ASSERT_TRUE(controller.status().IsTimedOut()) << controller.status();
  ASSERT_EQ(ActiveFlushes(), 1);
  RpcController retry_controller;
  retry_controller.set_timeout(5s);
  FlushTabletsResponsePB retry_response;
  const auto rejected = admin_proxy_->FlushTablets(request, &retry_response, &retry_controller);
  ASSERT_EQ(rpc::RpcError(rejected), rpc::ErrorStatusPB::ERROR_SERVER_TOO_BUSY) << rejected;
  release.CountDown();
  ASSERT_OK(WaitFor([&] { return ActiveFlushes() == 0; }, 5s, "Receiver flush retired"));
  retry_controller.Reset();
  retry_controller.set_timeout(5s);
  retry_response.Clear();
  ASSERT_OK(admin_proxy_->FlushTablets(request, &retry_response, &retry_controller));
  ASSERT_FALSE(retry_response.has_error()) << retry_response.DebugString();
}

TEST_F(SnapshotPreflushServiceTest, LocalFlushFailureDoesNotSubmit) {
  mini_server_->server()->tablet_manager()->tablet_flusher().StartShutdown();
  auto status = CreateSnapshot(kTabletId, TxnSnapshotId::GenerateRandom());
  ASSERT_TRUE(status.IsShutdownInProgress()) << status;
  ASSERT_NO_FATALS(WaitForRetiredPreflight());
  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "value"));
}

class SnapshotPreflushLimitServiceTest : public SnapshotPreflushServiceTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_preflush_concurrency) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_flush_concurrency) = 1;
    SnapshotPreflushServiceTest::SetUp();
  }

  void TestCapacity(bool snapshot) {
    Status first_status;
    auto second = ASSERT_RESULT(AddTablet("second-tablet"));
    auto send = [&](const TabletId& id) -> Status {
      if (snapshot) {
        return CreateSnapshot(id, TxnSnapshotId::GenerateRandom());
      }
      FlushTabletsRequestPB request;
      request.set_dest_uuid(mini_server_->server()->permanent_uuid());
      request.add_tablet_ids(id);
      FlushTabletsResponsePB response;
      RpcController controller;
      controller.set_timeout(10s);
      RETURN_NOT_OK(admin_proxy_->FlushTablets(request, &response, &controller));
      return response.has_error() ? StatusFromPB(response.error().status()) : Status::OK();
    };
    CountDownLatch flushed(1), release(1);
    auto* sync = SyncPoint::GetInstance();
    PauseFlush(flushed, release);
    TestThreadHolder threads;
    auto cleanup = ScopeExit([&] {
      release.CountDown();
      threads.JoinAll();
      sync->DisableProcessing();
      sync->ClearAllCallBacks();
    });
    threads.AddThreadFunctor([&] { first_status = send(kTabletId); });
    ASSERT_TRUE(flushed.WaitFor(5s));
    auto rejected = send(second->tablet_id());
    ASSERT_EQ(rpc::RpcError(rejected), rpc::ErrorStatusPB::ERROR_SERVER_TOO_BUSY) << rejected;
    ASSERT_STR_CONTAINS(rejected.ToString(), snapshot ? "Snapshot preflight capacity exhausted" :
                                                       "Tablet flush capacity exhausted");
    release.CountDown();
    threads.JoinAll();
    ASSERT_OK(first_status);
    ASSERT_NO_FATALS(WaitForRetiredPreflight());
    ASSERT_OK(send(second->tablet_id()));
  }
};

TEST_F(SnapshotPreflushLimitServiceTest, OriginLimitAcrossDistinctTablets) {
  TestCapacity(true);
}

TEST_F(SnapshotPreflushLimitServiceTest, ReceiverLimitAcrossDistinctTablets) {
  TestCapacity(false);
}

TEST_F(BackupServiceTest, TestCreateTabletSnapshot) {
  // Verify that the tablet exists.
  auto tablet = ASSERT_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  FsManager* const fs = tablet->tablet_metadata()->fs_manager();

  const string snapshot_id = "00000000000000000000000000000000";
  const string rocksdb_dir = tablet->tablet_metadata()->rocksdb_dir();
  const string top_snapshots_dir = tablet->tablet_metadata()->snapshots_dir();
  const string snapshot_dir = JoinPathSegments(top_snapshots_dir, snapshot_id);

  TabletSnapshotOpRequestPB req;
  TabletSnapshotOpResponsePB resp;

  req.set_operation(TabletSnapshotOpRequestPB::CREATE_ON_TABLET);
  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_snapshot_id(snapshot_id);

  // Test empty tablet list - expected error.
  // Send the call.
  {
    RpcController rpc;
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(backup_proxy_->TabletSnapshotOp(req, &resp, &rpc));
    ASSERT_NOK(StatusFromPB(resp.error().status()));
  }

  req.add_tablet_id(kTabletId);

  ASSERT_TRUE(fs->Exists(rocksdb_dir));
  ASSERT_TRUE(fs->Exists(top_snapshots_dir));

  // Send the call.
  {
    RpcController rpc;
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(backup_proxy_->TabletSnapshotOp(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  ASSERT_TRUE(fs->Exists(rocksdb_dir));
  ASSERT_TRUE(fs->Exists(top_snapshots_dir));
  ASSERT_TRUE(fs->Exists(snapshot_dir));
  // Check existence of snapshot files:
  ASSERT_TRUE(fs->Exists(JoinPathSegments(snapshot_dir, "CURRENT")));
  ASSERT_TRUE(fs->Exists(JoinPathSegments(snapshot_dir, "MANIFEST-000001")));
}

TEST_F(BackupServiceTest, TestSnapshotData) {
  // Verify that the tablet exists.
  ASSERT_OK(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));

  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "key1"));
  VerifyRows(schema_, { KeyValue(1, 11) });

  auto snapshot_id = TxnSnapshotId::GenerateRandom();
  ASSERT_OK(CreateSnapshot(kTabletId, snapshot_id));
  SleepFor(MonoDelta::FromMilliseconds(500));
  LOG(INFO) << "CREATED SNAPSHOT. UPDATING THE TABLET DATA..";

  ASSERT_OK(WriteSingleRow(kTabletId, 2, 22, "key1"));
  VerifyRows(schema_, { KeyValue(1, 11), KeyValue(2, 22) });

  // Send the restore snapshot request.
  auto restoration_id = TxnSnapshotRestorationId::GenerateRandom();
  ASSERT_OK(RestoreSnapshot(kTabletId, snapshot_id, restoration_id));
  SleepFor(MonoDelta::FromMilliseconds(500));
  LOG(INFO) << "RESTORED SNAPSHOT. CHECKING THE TABLET DATA..";

  // Expected the first row only from the snapshot.
  VerifyRows(schema_, { KeyValue(1, 11) });

  LOG(INFO) << "THE TABLET DATA IS VALID. Test TestSnapshotData finished.";
}

TEST_F(BackupServiceTest, RepeatedRestoreRequest) {
  // Verify that the tablet exists.
  ASSERT_OK(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));

  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "key1"));
  VerifyRows(schema_, { KeyValue(1, 11) });

  auto snapshot_id = TxnSnapshotId::GenerateRandom();
  ASSERT_OK(CreateSnapshot(kTabletId, snapshot_id));
  SleepFor(MonoDelta::FromMilliseconds(500));
  LOG(INFO) << "CREATED SNAPSHOT. UPDATING THE TABLET DATA..";

  ASSERT_OK(WriteSingleRow(kTabletId, 2, 22, "key1"));
  VerifyRows(schema_, { KeyValue(1, 11), KeyValue(2, 22) });

  // Send the restore snapshot request.
  auto restoration_id = TxnSnapshotRestorationId::GenerateRandom();
  ASSERT_OK(RestoreSnapshot(kTabletId, snapshot_id, restoration_id));
  SleepFor(MonoDelta::FromMilliseconds(500));

  // Repeat the restoration attempt with the same restoration_id.
  ASSERT_OK(RestoreSnapshot(kTabletId, snapshot_id, restoration_id));
  SleepFor(MonoDelta::FromMilliseconds(500));
  LOG(INFO) << "SENT SNAPSHOT RESTORATION REQUEST TWICE. CHECKING TABLET METADATA..";

  // Verify there is only a single active restoration id in the metadata.
  tablet::RaftGroupReplicaSuperBlockPB super_block;
  ASSERT_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId))
      ->tablet_metadata()
      ->ToSuperBlock(&super_block);
  ASSERT_EQ(super_block.active_restorations_size(), 1);
  auto recorded_restoration_id =
      ASSERT_RESULT(FullyDecodeTxnSnapshotRestorationId(super_block.active_restorations(0)));
  ASSERT_EQ(recorded_restoration_id, restoration_id);

  // Expected only the first row only from the snapshot.
  VerifyRows(schema_, { KeyValue(1, 11) });
}

Status BackupServiceTest::WriteSingleRow(
    const std::string& tablet_id, int32_t key, int32_t int_val, const std::string& string_val) {
  WriteRequestPB req;
  req.set_tablet_id(tablet_id);
  AddTestRowInsert(key, int_val, string_val, &req);
  RpcController rpc;
  SCOPED_TRACE(req.DebugString());
  WriteResponsePB resp;
  RETURN_NOT_OK(proxy_->Write(req, &resp, &rpc));
  SCOPED_TRACE(resp.DebugString());
  return ResponseStatus(resp);
}

Status BackupServiceTest::CreateSnapshot(
    const std::string& tablet_id, const TxnSnapshotId& snapshot_id, HybridTime snapshot_time) {
  TabletSnapshotOpRequestPB req;
  TabletSnapshotOpResponsePB resp;
  req.set_operation(TabletSnapshotOpRequestPB::CREATE_ON_TABLET);
  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  req.set_snapshot_hybrid_time(snapshot_time.ToUint64());
  req.add_tablet_id(tablet_id);
  RpcController rpc;
  SCOPED_TRACE(req.DebugString());
  RETURN_NOT_OK(backup_proxy_->TabletSnapshotOp(req, &resp, &rpc));
  SCOPED_TRACE(resp.DebugString());
  return ResponseStatus(resp);
}

Status BackupServiceTest::RestoreSnapshot(
    const std::string& tablet_id, const TxnSnapshotId& snapshot_id,
    const TxnSnapshotRestorationId& restoration_id) {
  TabletSnapshotOpRequestPB req;
  TabletSnapshotOpResponsePB resp;
  req.set_operation(TabletSnapshotOpRequestPB::RESTORE_ON_TABLET);
  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  req.add_tablet_id(tablet_id);
  req.set_restoration_id(restoration_id.data(), restoration_id.size());
  RpcController rpc;
  SCOPED_TRACE(req.DebugString());
  RETURN_NOT_OK(backup_proxy_->TabletSnapshotOp(req, &resp, &rpc));
  SCOPED_TRACE(resp.DebugString());
  return ResponseStatus(resp);
}

} // namespace tserver
} // namespace yb
