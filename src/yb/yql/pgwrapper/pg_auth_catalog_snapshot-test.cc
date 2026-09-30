// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.

#include "yb/common/wire_protocol.h"
#include "yb/common/ysql_auth_catalog_snapshot.h"

#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/service_pool.h"

#include "yb/server/rpc_server.h"
#include "yb/server/server_base.proxy.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_client.proxy.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_enable_pg_client_mock);
DECLARE_bool(TEST_enable_sync_points);
DECLARE_bool(enable_ysql_conn_mgr);
DECLARE_bool(ysql_enable_auth_catalog_follower_reads);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_enable_catalog_follower_read_reservation);

METRIC_DECLARE_counter(tserver_ysql_auth_snapshot_deadline_expirations);
METRIC_DECLARE_counter(tserver_ysql_auth_snapshot_task_limit_rejections);
METRIC_DECLARE_gauge_uint64(tserver_ysql_auth_snapshot_outstanding_tasks);

using namespace std::literals;

namespace yb::pgwrapper {

class PgAuthSnapshotPoolTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_pg_client_mock) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql_conn_mgr) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auth_catalog_follower_reads) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_catalog_follower_read_reservation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
    auto* sync = SyncPoint::GetInstance();
    sync->SetCallBack("PgClientService::AuthSnapshot::Limits", [](void* arg) {
      *static_cast<YsqlAuthSnapshotLimits*>(arg) = {1, 2};
    });
    sync->SetCallBack("MasterClusterService::AuthSnapshot::Timeout", [](void* arg) {
      *static_cast<std::chrono::milliseconds*>(arg) = 30s * kTimeMultiplier;
    });
    SetSnapshotTimeout(30s * kTimeMultiplier);
    sync->EnableProcessing();
    ASSERT_NO_FATAL_FAILURE(PgMiniTestBase::SetUp());

    auto proxy = ASSERT_RESULT(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
    master::ReserveYsqlCatalogFollowerReadsRequestPB req;
    req.set_acknowledge_permanent_pitr_exclusion(true);
    master::ReserveYsqlCatalogFollowerReadsResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(30s * kTimeMultiplier);
    ASSERT_OK(proxy.ReserveYsqlCatalogFollowerReads(req, &resp, &rpc));
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
  }

  void DoTearDown() override {
    SyncPoint::GetInstance()->DisableProcessing();
    SyncPoint::GetInstance()->ClearAllCallBacks();
    PgMiniTestBase::DoTearDown();
  }

  void SetSnapshotTimeout(std::chrono::milliseconds timeout) {
    SyncPoint::GetInstance()->SetCallBack(
        "PgClientService::AuthSnapshot::Timeout", [timeout](void* arg) {
          *static_cast<std::chrono::milliseconds*>(arg) = timeout;
        });
  }

  size_t NumMasters() override { return 1; }
  size_t NumTabletServers() override { return 1; }

  tserver::TabletServer* server() const {
    return cluster_->mini_tablet_server(0)->server();
  }

  struct SnapshotRpc {
    ~SnapshotRpc() {
      if (started) {
        done.Wait();
      }
    }

    void Start(tserver::PgClientServiceProxy& proxy) {
      started = true;
      rpc.set_timeout(60s * kTimeMultiplier);
      proxy.GetYsqlAuthCatalogReadTimeAsync(req, &resp, &rpc, [this] { done.CountDown(); });
    }

    tserver::PgGetYsqlAuthCatalogReadTimeRequestPB req;
    tserver::PgGetYsqlAuthCatalogReadTimeResponsePB resp;
    rpc::RpcController rpc;
    CountDownLatch done{1};
    bool started = false;
  };

  // Declare after RPCs and thread holders so failure cleanup releases the worker before joining.
  struct SnapshotPause {
    explicit SnapshotPause(master::Master* master) {
      auto* sync = SyncPoint::GetInstance();
      sync->SetCallBack("MasterClusterService::AuthSnapshot::AfterSafeTime",
                        [this, master](void* arg) {
        if (arg == master) {
          entered.CountDown();
          resume.Wait();
        }
      });
      sync->SetCallBack("PgClientService::AuthSnapshot::Enqueued",
                        [this](void*) { enqueued.CountDown(); });
      sync->EnableProcessing();
    }

    ~SnapshotPause() {
      resume.CountDown();
      auto* sync = SyncPoint::GetInstance();
      sync->DisableProcessing();
      // ClearAllCallBacks drains callbacks before their captured latches are destroyed.
      sync->ClearAllCallBacks();
    }

    CountDownLatch entered{1};
    CountDownLatch resume{1};
    CountDownLatch enqueued{2};
  };

  void CheckMetrics(uint64_t outstanding, uint64_t rejections, uint64_t expirations) {
    const auto& entity = server()->metric_entity();
    auto gauge = METRIC_tserver_ysql_auth_snapshot_outstanding_tasks.Instantiate(entity, 0);
    // The RPC response can arrive before Done releases the admission slot.
    ASSERT_OK(WaitFor([&] { return gauge->value() == outstanding; },
                      5s * kTimeMultiplier, "Wait for tserver snapshot admission slots"));
    ASSERT_EQ(
        METRIC_tserver_ysql_auth_snapshot_task_limit_rejections.Instantiate(entity)->value(),
        rejections);
    ASSERT_EQ(
        METRIC_tserver_ysql_auth_snapshot_deadline_expirations.Instantiate(entity)->value(),
        expirations);
  }

  void CheckSuccess(const SnapshotRpc& call) {
    ASSERT_TRUE(call.done.WaitFor(30s * kTimeMultiplier));
    ASSERT_OK(call.rpc.status());
    ASSERT_FALSE(call.resp.has_status()) << call.resp.ShortDebugString();
    ASSERT_FALSE(HybridTime(call.resp.read_time()).is_special()) << call.resp.ShortDebugString();
  }

  void CheckRejected(const SnapshotRpc& call) {
    ASSERT_TRUE(call.done.WaitFor(30s * kTimeMultiplier));
    ASSERT_OK(call.rpc.status());
    ASSERT_TRUE(call.resp.has_status());
    ASSERT_TRUE(StatusFromPB(call.resp.status()).IsServiceUnavailable())
        << call.resp.ShortDebugString();
    ASSERT_EQ(call.resp.read_time(), 0);
  }
};

TEST_F(PgAuthSnapshotPoolTest, SaturationDoesNotBlockUnrelatedService) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto proxy = cluster_->GetTServerProxy<tserver::PgClientServiceProxy>(0);
  SnapshotRpc running, queued, rejected;
  SnapshotPause pause(leader->master());
  running.Start(proxy);
  ASSERT_TRUE(pause.entered.WaitFor(30s * kTimeMultiplier));
  queued.Start(proxy);
  ASSERT_TRUE(pause.enqueued.WaitFor(30s * kTimeMultiplier));
  ASSERT_EQ(running.done.count(), 1);
  ASSERT_EQ(queued.done.count(), 1);
  ASSERT_NO_FATAL_FAILURE(CheckMetrics(2, 0, 0));

  rejected.Start(proxy);
  ASSERT_NO_FATAL_FAILURE(CheckRejected(rejected));
  ASSERT_STR_CONTAINS(rejected.resp.status().message(), "task limit");
  ASSERT_NO_FATAL_FAILURE(CheckMetrics(2, 1, 0));

  auto status_proxy = cluster_->GetTServerProxy<server::GenericServiceProxy>(0);
  server::GetStatusRequestPB req;
  server::GetStatusResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(5s * kTimeMultiplier);
  ASSERT_OK(status_proxy.GetStatus(req, &resp, &rpc));
  ASSERT_EQ(resp.status().node_instance().permanent_uuid(), server()->permanent_uuid());

  pause.resume.CountDown();
  ASSERT_NO_FATAL_FAILURE(CheckSuccess(running));
  ASSERT_NO_FATAL_FAILURE(CheckSuccess(queued));
  ASSERT_NO_FATAL_FAILURE(CheckMetrics(0, 1, 0));
}

TEST_F(PgAuthSnapshotPoolTest, DeadlineExpiresQueuedRequest) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto proxy = cluster_->GetTServerProxy<tserver::PgClientServiceProxy>(0);
  SnapshotRpc running, queued;
  SnapshotPause pause(leader->master());
  running.Start(proxy);
  ASSERT_TRUE(pause.entered.WaitFor(30s * kTimeMultiplier));

  // The running task has captured its longer budget; only the queued task gets the short one.
  const auto queue_budget = 100ms * kTimeMultiplier;
  SetSnapshotTimeout(queue_budget);
  queued.Start(proxy);
  ASSERT_TRUE(pause.enqueued.WaitFor(30s * kTimeMultiplier));
  ASSERT_NO_FATAL_FAILURE(CheckMetrics(2, 0, 0));
  // Enqueue follows deadline construction, so this is an upper bound on its coarse deadline.
  const auto expired_by = CoarseMonoClock::Now() + queue_budget;
  ASSERT_OK(WaitFor([&] { return CoarseMonoClock::Now() >= expired_by; },
                    5s * kTimeMultiplier, "Expire the queued tserver snapshot budget"));
  ASSERT_EQ(running.done.count(), 1);
  ASSERT_EQ(queued.done.count(), 1);

  pause.resume.CountDown();
  ASSERT_TRUE(queued.done.WaitFor(30s * kTimeMultiplier));
  ASSERT_OK(queued.rpc.status());
  ASSERT_TRUE(queued.resp.has_status());
  ASSERT_TRUE(StatusFromPB(queued.resp.status()).IsTimedOut()) << queued.resp.ShortDebugString();
  ASSERT_STR_CONTAINS(queued.resp.status().message(), "expired in queue");
  ASSERT_EQ(queued.resp.read_time(), 0);

  ASSERT_NO_FATAL_FAILURE(CheckSuccess(running));
  ASSERT_NO_FATAL_FAILURE(CheckMetrics(0, 0, 1));
}

TEST_F(PgAuthSnapshotPoolTest, ShutdownRejectsQueuedAndJoinsRunningRequest) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto proxy = cluster_->GetTServerProxy<tserver::PgClientServiceProxy>(0);
  const auto* pool = server()->rpc_server()->TEST_service_pool(
      tserver::PgClientServiceIf::static_service_name());
  ASSERT_NE(pool, nullptr);
  const auto service = pool->TEST_get_service();
  ASSERT_NE(service, nullptr);
  ASSERT_EQ(service.get(), server()->TEST_GetPgClientService());

  CountDownLatch shutdown_done(1);
  SnapshotRpc running, queued, rejected;
  TestThreadHolder threads;
  SnapshotPause pause(leader->master());
  running.Start(proxy);
  ASSERT_TRUE(pause.entered.WaitFor(30s * kTimeMultiplier));
  queued.Start(proxy);
  ASSERT_TRUE(pause.enqueued.WaitFor(30s * kTimeMultiplier));
  ASSERT_NO_FATAL_FAILURE(CheckMetrics(2, 0, 0));
  threads.AddThreadFunctor([&] {
    service->Shutdown();
    shutdown_done.CountDown();
  });

  ASSERT_NO_FATAL_FAILURE(CheckRejected(queued));
  ASSERT_NO_FATAL_FAILURE(CheckMetrics(1, 0, 0));
  ASSERT_EQ(running.done.count(), 1);
  ASSERT_EQ(shutdown_done.count(), 1);
  rejected.Start(proxy);
  ASSERT_NO_FATAL_FAILURE(CheckRejected(rejected));

  pause.resume.CountDown();
  ASSERT_TRUE(shutdown_done.WaitFor(30s * kTimeMultiplier));
  ASSERT_NO_FATAL_FAILURE(CheckRejected(running));
  ASSERT_NO_FATAL_FAILURE(CheckMetrics(0, 0, 0));
  threads.JoinAll();
  service->Shutdown();
  ASSERT_NO_FATAL_FAILURE(CheckMetrics(0, 0, 0));
}

}  // namespace yb::pgwrapper
