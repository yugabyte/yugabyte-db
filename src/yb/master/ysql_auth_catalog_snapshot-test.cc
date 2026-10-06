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

#include <atomic>
#include <functional>

#include "yb/common/wire_protocol.h"
#include "yb/common/ysql_auth_catalog_snapshot.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_cluster.service.h"
#include "yb/master/mini_master.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/rpc/service_if.h"

#include "yb/server/clock.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/memory/arena.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

DECLARE_bool(TEST_enable_sync_points);
DECLARE_bool(ysql_enable_auth_catalog_follower_reads);

METRIC_DECLARE_counter(master_ysql_auth_snapshot_deadline_expirations);
METRIC_DECLARE_counter(master_ysql_auth_snapshot_task_limit_rejections);
METRIC_DECLARE_gauge_uint64(master_ysql_auth_snapshot_outstanding_tasks);
METRIC_DECLARE_counter(ysql_auth_catalog_snapshot_acquisitions);

using namespace std::chrono_literals;

namespace yb::master {

class MasterAuthSnapshotTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    ConfigureSnapshotLimits();
    MiniClusterOptions options;
    options.num_masters = 3;
    cluster_ = std::make_unique<MiniCluster>(options);
    ASSERT_OK(cluster_->Start());
  }

  void DoTearDown() override {
    SyncPoint::GetInstance()->DisableProcessing();
    SyncPoint::GetInstance()->ClearAllCallBacks();
    YBMiniClusterTestBase::DoTearDown();
  }

 protected:
  virtual void ConfigureSnapshotLimits() {}
  Result<MiniMaster*> RestartFollower() {
    auto* leader = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    for (size_t i = 0; i != cluster_->num_masters(); ++i) {
      auto* follower = cluster_->mini_master(i);
      if (follower != leader) {
        RETURN_NOT_OK(follower->Restart());
        return follower;
      }
    }
    return STATUS(NotFound, "No master follower");
  }

  Result<HybridTime> AuthReadTime(
      MiniMaster* master, HybridTime propagated = HybridTime::kInvalid) {
    MasterClusterProxy proxy(&cluster_->proxy_cache(), master->bound_rpc_addr());
    GetYsqlAuthCatalogReadTimeRequestPB req;
    if (propagated) {
      req.set_propagated_hybrid_time(propagated.ToUint64());
    }
    GetYsqlAuthCatalogReadTimeResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(30s * kTimeMultiplier);
    RETURN_NOT_OK(proxy.GetYsqlAuthCatalogReadTime(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    SCHECK(resp.has_read_time(), IllegalState, "Missing authentication read time");
    return HybridTime(resp.read_time());
  }

};

class MasterAuthSnapshotTaskPoolTest : public MasterAuthSnapshotTest {
 public:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(MasterAuthSnapshotTest::SetUp());
    ASSERT_OK(SET_FLAG(ysql_enable_auth_catalog_follower_reads, true));
  }

 protected:
  void SetSnapshotTimeout(std::chrono::milliseconds timeout) {
    SyncPoint::GetInstance()->SetCallBack(
        "MasterClusterService::AuthSnapshot::Timeout", [timeout](void* arg) {
          *static_cast<std::chrono::milliseconds*>(arg) = timeout;
        });
  }

  void ConfigureSnapshotLimits() override {
    ASSERT_OK(SET_FLAG(TEST_enable_sync_points, true));
    SyncPoint::GetInstance()->SetCallBack(
        "MasterClusterService::AuthSnapshot::Limits", [](void* arg) {
          *static_cast<YsqlAuthSnapshotLimits*>(arg) = {1, 2};
        });
    SetSnapshotTimeout(30s * kTimeMultiplier);
    SyncPoint::GetInstance()->EnableProcessing();
  }

  struct SnapshotRpc {
    ~SnapshotRpc() { Wait(); }

    void Start(MasterClusterProxy& proxy) {
      started = true;
      rpc.set_timeout(30s * kTimeMultiplier);
      proxy.GetYsqlAuthCatalogReadTimeAsync(
          req, &resp, &rpc, [this] { done.CountDown(); });
    }

    void Wait() {
      if (started) {
        done.Wait();
      }
    }

    GetYsqlAuthCatalogReadTimeRequestPB req;
    GetYsqlAuthCatalogReadTimeResponsePB resp;
    rpc::RpcController rpc;
    CountDownLatch done{1};
    bool started = false;
  };

  void PauseSnapshots(
      Master* master, const char* point, CountDownLatch& entered, CountDownLatch& resume,
      CountDownLatch& enqueued) {
    auto* sync = SyncPoint::GetInstance();
    CaptureSnapshotService();
    sync->SetCallBack(point, [master, &entered, &resume](void* arg) {
      if (arg == master) {
        entered.CountDown();
        resume.Wait();
      }
    });
    sync->SetCallBack("MasterClusterService::AuthSnapshot::Enqueued",
                      [master, &enqueued](void* arg) {
      if (arg == master) {
        enqueued.CountDown();
      }
    });
    sync->EnableProcessing();
  }

  void ClearSnapshotCallbacks() {
    auto* sync = SyncPoint::GetInstance();
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  }

  void CaptureSnapshotService() {
    // Master services share a wire service name; TEST_service_pool cannot distinguish them.
    SyncPoint::GetInstance()->SetCallBack(
        "MasterClusterService::AuthSnapshot::Service", [this](void* arg) {
          snapshot_service_.store(static_cast<MasterClusterIf*>(arg));
        });
  }

  void CheckSnapshotMetrics(
      Master* master, uint64_t outstanding, uint64_t rejections, uint64_t expirations,
      uint64_t acquisitions = 0) {
    const auto& entity = master->metric_entity();
    auto gauge = METRIC_master_ysql_auth_snapshot_outstanding_tasks.Instantiate(entity, 0);
    // The client can receive the response before Done releases its admission slot.
    ASSERT_OK(WaitFor([&] { return gauge->value() == outstanding; },
                      5s * kTimeMultiplier, "Wait for snapshot outstanding-task gauge"));
    ASSERT_EQ(METRIC_master_ysql_auth_snapshot_task_limit_rejections.Instantiate(entity)->value(),
              rejections);
    ASSERT_EQ(METRIC_master_ysql_auth_snapshot_deadline_expirations.Instantiate(entity)->value(),
              expirations);
    ASSERT_EQ(METRIC_ysql_auth_catalog_snapshot_acquisitions.Instantiate(entity)->value(),
              acquisitions);
  }

  void CheckClockWaitDeadline(bool shutdown) {
    SetSnapshotTimeout(2s * kTimeMultiplier);
    auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
    auto* master = leader->master();
    MasterClusterProxy proxy(&cluster_->proxy_cache(), leader->bound_rpc_addr());
    CountDownLatch waiting(1), resume(1), enqueued(2), shutdown_done(1);
    SnapshotRpc running, queued;
    TestThreadHolder threads;
    std::atomic<int64_t> snapshot_thread_id{0};
    auto cleanup = ScopeExit([&] {
      resume.CountDown();
      running.Wait();
      queued.Wait();
      threads.JoinAll();
      ClearSnapshotCallbacks();
    });
    CaptureSnapshotService();
    auto* sync = SyncPoint::GetInstance();
    sync->SetCallBack("MasterClusterService::AuthSnapshot::ReadTime", [&](void* arg) {
      snapshot_thread_id.store(Thread::UniqueThreadId());
      auto* time = static_cast<HybridTime*>(arg);
      *time = time->AddSeconds(10 * kTimeMultiplier);
    });
    sync->SetCallBack("Clock::WaitUntil::BeforeSleep", [&](void* arg) {
      if (arg == static_cast<ClockBase*>(master->clock()) &&
          snapshot_thread_id.load() == Thread::UniqueThreadId()) {
        waiting.CountDown();
        resume.Wait();
      }
    });
    sync->SetCallBack("MasterClusterService::AuthSnapshot::Enqueued", [&](void* arg) {
      if (arg == master) {
        enqueued.CountDown();
      }
    });
    sync->EnableProcessing();
    const auto start = CoarseMonoClock::Now();
    running.Start(proxy);
    ASSERT_TRUE(waiting.WaitFor(30s * kTimeMultiplier));
    ASSERT_NO_FATALS(CheckSnapshotMetrics(master, 1, 0, 0));
    if (shutdown) {
      ASSERT_NE(snapshot_service_.load(), nullptr);
      SetSnapshotTimeout(0ms);
      queued.Start(proxy);
      ASSERT_TRUE(enqueued.WaitFor(30s * kTimeMultiplier));
      ASSERT_NO_FATALS(CheckSnapshotMetrics(master, 2, 0, 0));
      threads.AddThreadFunctor([&] {
        snapshot_service_.load()->Shutdown();
        shutdown_done.CountDown();
      });
      ASSERT_TRUE(queued.done.WaitFor(30s * kTimeMultiplier));
      ASSERT_OK(queued.rpc.status());
      ASSERT_TRUE(queued.resp.has_error());
      ASSERT_TRUE(StatusFromPB(queued.resp.error().status()).IsServiceUnavailable());
      ASSERT_FALSE(queued.resp.has_read_time());
      ASSERT_NO_FATALS(CheckSnapshotMetrics(master, 1, 0, 0));
      ASSERT_EQ(shutdown_done.count(), 1);
    }
    ASSERT_LT(CoarseMonoClock::Now() - start, 2s * kTimeMultiplier);
    resume.CountDown();
    ASSERT_TRUE(running.done.WaitFor(3s * kTimeMultiplier));
    if (shutdown) {
      ASSERT_TRUE(shutdown_done.WaitFor(3s * kTimeMultiplier));
    }
    ASSERT_LT(CoarseMonoClock::Now() - start, 3s * kTimeMultiplier);
    ASSERT_OK(running.rpc.status());
    ASSERT_TRUE(running.resp.has_error());
    ASSERT_TRUE(StatusFromPB(running.resp.error().status()).IsTimedOut());
    ASSERT_FALSE(running.resp.has_read_time());
    ASSERT_NO_FATALS(CheckSnapshotMetrics(master, 0, 0, 1));
  }

  std::atomic<MasterClusterIf*> snapshot_service_{nullptr};
};

TEST_F(MasterAuthSnapshotTest, AuthSnapshotRequiresLeaderAndRoutingFlag) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto* follower = ASSERT_RESULT(RestartFollower());
  ASSERT_NOK(AuthReadTime(follower));
  ASSERT_NOK_STR_CONTAINS(AuthReadTime(leader), "disabled");
  ASSERT_OK(SET_FLAG(ysql_enable_auth_catalog_follower_reads, true));
  const auto propagated = leader->Now().AddMilliseconds(100);
  const auto read_time = ASSERT_RESULT(AuthReadTime(leader, propagated));
  ASSERT_GE(read_time, propagated);
  ASSERT_FALSE(read_time.is_special());
  auto tablet = ASSERT_RESULT(leader->tablet_peer()->shared_tablet());
  ASSERT_GE(ASSERT_RESULT(tablet->SafeTime(tablet::RequireLease::kTrue)), read_time);
  ASSERT_EQ(METRIC_ysql_auth_catalog_snapshot_acquisitions.Instantiate(
      leader->master()->metric_entity())->value(), 1);
  ASSERT_NOK(AuthReadTime(follower));
}

TEST_F(MasterAuthSnapshotTaskPoolTest, SaturationAndQueuedRoutingFlagRecheck) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  MasterClusterProxy proxy(&cluster_->proxy_cache(), leader->bound_rpc_addr());
  CountDownLatch entered(1), resume(1), enqueued(2);
  SnapshotRpc running, queued;
  auto cleanup = ScopeExit([&] {
    resume.CountDown();
    running.Wait();
    queued.Wait();
    ClearSnapshotCallbacks();
  });
  PauseSnapshots(leader->master(), "MasterClusterService::AuthSnapshot::Execute",
                 entered, resume, enqueued);
  running.Start(proxy);
  ASSERT_TRUE(entered.WaitFor(30s * kTimeMultiplier));
  queued.Start(proxy);
  ASSERT_TRUE(enqueued.WaitFor(30s * kTimeMultiplier));
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 2, 0, 0));

  const auto overloaded = AuthReadTime(leader);
  ASSERT_NOK(overloaded);
  ASSERT_TRUE(overloaded.status().IsServiceUnavailable()) << overloaded.status();
  ASSERT_STR_CONTAINS(overloaded.status().ToString(), "task limit");
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 2, 1, 0));

  IsMasterLeaderReadyRequestPB ready_req;
  IsMasterLeaderReadyResponsePB ready_resp;
  rpc::RpcController ready_rpc;
  ready_rpc.set_timeout(5s * kTimeMultiplier);
  ASSERT_OK(proxy.IsMasterLeaderServiceReady(ready_req, &ready_resp, &ready_rpc));
  ASSERT_FALSE(ready_resp.has_error()) << ready_resp.ShortDebugString();

  ASSERT_OK(SET_FLAG(ysql_enable_auth_catalog_follower_reads, false));
  resume.CountDown();
  for (auto* call : {&running, &queued}) {
    call->Wait();
    ASSERT_OK(call->rpc.status());
    ASSERT_TRUE(call->resp.has_error());
    ASSERT_TRUE(StatusFromPB(call->resp.error().status()).IsNotSupported());
    ASSERT_FALSE(call->resp.has_read_time());
  }
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 0, 1, 0));
  ASSERT_OK(SET_FLAG(ysql_enable_auth_catalog_follower_reads, true));
  ASSERT_RESULT(AuthReadTime(leader));
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 0, 1, 0, 1));
}

TEST_F(MasterAuthSnapshotTaskPoolTest, LeadershipChangeRejectsRunningAndQueuedSnapshots) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  MasterClusterProxy proxy(&cluster_->proxy_cache(), leader->bound_rpc_addr());
  CountDownLatch safe(1), resume(1), enqueued(2);
  SnapshotRpc running, queued;
  auto cleanup = ScopeExit([&] {
    resume.CountDown();
    running.Wait();
    queued.Wait();
    ClearSnapshotCallbacks();
  });
  PauseSnapshots(leader->master(), "MasterClusterService::AuthSnapshot::AfterSafeTime",
                 safe, resume, enqueued);
  running.Start(proxy);
  ASSERT_TRUE(safe.WaitFor(30s * kTimeMultiplier));
  queued.Start(proxy);
  ASSERT_TRUE(enqueued.WaitFor(30s * kTimeMultiplier));
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 2, 0, 0));

  MiniMaster* target = nullptr;
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    if (cluster_->mini_master(i) != leader) {
      target = cluster_->mini_master(i);
      break;
    }
  }
  ASSERT_NE(target, nullptr);
  ASSERT_RESULT(cluster_->StepDownMasterLeader(target->permanent_uuid()));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(cluster_->GetLeaderMiniMaster()) == target;
  }, 30s * kTimeMultiplier, "Wait for replacement master leader"));
  resume.CountDown();
  for (auto* call : {&running, &queued}) {
    call->Wait();
    ASSERT_OK(call->rpc.status());
    ASSERT_TRUE(call->resp.has_error());
    ASSERT_EQ(call->resp.error().code(), MasterErrorPB::NOT_THE_LEADER);
    ASSERT_FALSE(call->resp.has_read_time());
  }
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 0, 0, 0));
  ASSERT_RESULT(AuthReadTime(target));
  ASSERT_NO_FATALS(CheckSnapshotMetrics(target->master(), 0, 0, 0, 1));
}

TEST_F(MasterAuthSnapshotTaskPoolTest, ShutdownRejectsPendingAndJoinsRunningSnapshots) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  MasterClusterProxy proxy(&cluster_->proxy_cache(), leader->bound_rpc_addr());
  CountDownLatch safe(1), resume(1), enqueued(2), shutdown_done(1);
  SnapshotRpc running, queued;
  TestThreadHolder threads;
  auto cleanup = ScopeExit([&] {
    resume.CountDown();
    running.Wait();
    queued.Wait();
    threads.JoinAll();
    ClearSnapshotCallbacks();
  });
  PauseSnapshots(leader->master(), "MasterClusterService::AuthSnapshot::AfterSafeTime",
                 safe, resume, enqueued);
  running.Start(proxy);
  ASSERT_TRUE(safe.WaitFor(30s * kTimeMultiplier));
  auto* service = snapshot_service_.load();
  ASSERT_NE(service, nullptr);
  // Cancellation must not count as a deadline expiration, even for an already-expired task.
  SetSnapshotTimeout(0ms);
  queued.Start(proxy);
  ASSERT_TRUE(enqueued.WaitFor(30s * kTimeMultiplier));
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 2, 0, 0));
  threads.AddThreadFunctor([&] {
    service->Shutdown();
    shutdown_done.CountDown();
  });

  ASSERT_TRUE(queued.done.WaitFor(30s * kTimeMultiplier));
  ASSERT_OK(queued.rpc.status());
  ASSERT_TRUE(queued.resp.has_error());
  ASSERT_TRUE(StatusFromPB(queued.resp.error().status()).IsServiceUnavailable());
  ASSERT_FALSE(queued.resp.has_read_time());
  ASSERT_EQ(shutdown_done.count(), 1);
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 1, 0, 0));
  const auto rejected = AuthReadTime(leader);
  ASSERT_NOK(rejected);
  ASSERT_TRUE(rejected.status().IsServiceUnavailable()) << rejected.status();

  resume.CountDown();
  ASSERT_TRUE(shutdown_done.WaitFor(30s * kTimeMultiplier));
  running.Wait();
  ASSERT_OK(running.rpc.status());
  ASSERT_TRUE(running.resp.has_error());
  ASSERT_TRUE(StatusFromPB(running.resp.error().status()).IsServiceUnavailable());
  ASSERT_FALSE(running.resp.has_read_time());
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 0, 0, 0));
  threads.JoinAll();
  service->Shutdown();
}

TEST_F(MasterAuthSnapshotTaskPoolTest, DeadlineBoundsClockWait) {
  ASSERT_NO_FATALS(CheckClockWaitDeadline(false));
}

TEST_F(MasterAuthSnapshotTaskPoolTest, ShutdownDuringClockWaitRespectsDeadline) {
  ASSERT_NO_FATALS(CheckClockWaitDeadline(true));
}

TEST_F(MasterAuthSnapshotTaskPoolTest, DeadlineExpiresQueuedTasks) {
  SetSnapshotTimeout(100ms * kTimeMultiplier);
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  MasterClusterProxy proxy(&cluster_->proxy_cache(), leader->bound_rpc_addr());
  CountDownLatch entered(1), resume(1), enqueued(2);
  SnapshotRpc running, queued;
  auto cleanup = ScopeExit([&] {
    resume.CountDown();
    running.Wait();
    queued.Wait();
    ClearSnapshotCallbacks();
  });
  PauseSnapshots(leader->master(), "MasterClusterService::AuthSnapshot::Execute",
                 entered, resume, enqueued);
  running.Start(proxy);
  ASSERT_TRUE(entered.WaitFor(30s * kTimeMultiplier));
  queued.Start(proxy);
  ASSERT_TRUE(enqueued.WaitFor(30s * kTimeMultiplier));
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 2, 0, 0));
  SleepFor(150ms * kTimeMultiplier);
  resume.CountDown();
  for (auto* call : {&running, &queued}) {
    call->Wait();
    ASSERT_OK(call->rpc.status());
    ASSERT_TRUE(call->resp.has_error());
    ASSERT_TRUE(StatusFromPB(call->resp.error().status()).IsTimedOut());
    ASSERT_STR_CONTAINS(call->resp.error().status().message(), "expired in queue");
    ASSERT_FALSE(call->resp.has_read_time());
  }
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 0, 0, 2));
  SetSnapshotTimeout(30s * kTimeMultiplier);
  ASSERT_RESULT(AuthReadTime(leader));
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 0, 0, 2, 1));
}

TEST_F(MasterAuthSnapshotTaskPoolTest, DeadlineRecheckedBeforePublishingReadTime) {
  SetSnapshotTimeout(2s * kTimeMultiplier);
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  MasterClusterProxy proxy(&cluster_->proxy_cache(), leader->bound_rpc_addr());
  CountDownLatch safe(1), resume(1), enqueued(1);
  SnapshotRpc running;
  auto cleanup = ScopeExit([&] {
    resume.CountDown();
    running.Wait();
    ClearSnapshotCallbacks();
  });
  PauseSnapshots(leader->master(), "MasterClusterService::AuthSnapshot::AfterSafeTime",
                 safe, resume, enqueued);
  running.Start(proxy);
  ASSERT_TRUE(safe.WaitFor(30s * kTimeMultiplier));
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 1, 0, 0));
  SleepFor(2100ms * kTimeMultiplier);
  resume.CountDown();
  running.Wait();
  ASSERT_OK(running.rpc.status());
  ASSERT_TRUE(running.resp.has_error());
  ASSERT_TRUE(StatusFromPB(running.resp.error().status()).IsTimedOut());
  ASSERT_STR_CONTAINS(running.resp.error().status().message(), "before publishing read time");
  ASSERT_FALSE(running.resp.has_read_time());
  ASSERT_NO_FATALS(CheckSnapshotMetrics(leader->master(), 0, 0, 1));
}

}  // namespace yb::master
