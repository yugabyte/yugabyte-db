// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <memory>
#include <vector>

#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/mathlimits.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/integration-tests/test_workload.h"

#include "yb/master/master_rpc.h"

#include "yb/rpc/rpc.h"

#include "yb/util/metrics.h"
#include "yb/util/pstack_watcher.h"
#include "yb/util/random.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"

METRIC_DECLARE_entity(tablet);
METRIC_DECLARE_counter(leader_memory_pressure_rejections);
METRIC_DECLARE_counter(follower_memory_pressure_rejections);

using strings::Substitute;
using std::vector;
using namespace std::literals; // NOLINT

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBTable;
using client::YBTableName;

class ClientStressTest : public YBMiniClusterTestBase<ExternalMiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    ExternalMiniClusterOptions opts = default_opts();
    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());
  }

  void DoTearDown() override {
    alarm(0);
    YBMiniClusterTestBase::DoTearDown();
  }

 protected:
  virtual ExternalMiniClusterOptions default_opts() {
    ExternalMiniClusterOptions result;
    result.num_tablet_servers = 3;
    return result;
  }
};

// Stress test a case where most of the operations are expected to time out.
// This is a regression test for various bugs we've seen in timeout handling,
// especially with concurrent requests.
TEST_F(ClientStressTest, TestLookupTimeouts) {
  const int kSleepMillis = AllowSlowTests() ? 5000 : 100;

  TestWorkload work(cluster_.get());
  work.set_num_write_threads(64);
  work.set_write_timeout_millis(10);
  work.set_timeout_allowed(true);
  work.Setup();
  work.Start();
  SleepFor(MonoDelta::FromMilliseconds(kSleepMillis));
}

// Override the base test to run in multi-master mode.
class ClientStressTest_MultiMaster : public ClientStressTest {
 protected:
  ExternalMiniClusterOptions default_opts() override {
    ExternalMiniClusterOptions result;
    result.num_masters = 3;
    result.master_rpc_ports = {0, 0, 0};
    result.num_tablet_servers = 3;
    return result;
  }
};

// Stress test a case where most of the operations are expected to time out.
// This is a regression test for KUDU-614 - it would cause a deadlock prior
// to fixing that bug.
TEST_F(ClientStressTest_MultiMaster, TestLeaderResolutionTimeout) {
  TestWorkload work(cluster_.get());
  work.set_num_write_threads(64);

  // This timeout gets applied to the master requests. It's lower than the
  // amount of time that we sleep the masters, to ensure they timeout.
  work.set_client_default_rpc_timeout_millis(250);
  // This is the time budget for the whole request. It has to be longer than
  // the above timeout so that the client actually attempts to resolve
  // the leader.
  work.set_write_timeout_millis(280);
  work.set_timeout_allowed(true);
  work.Setup();

  work.Start();

  ASSERT_OK(cluster_->tablet_server(0)->Pause());
  ASSERT_OK(cluster_->tablet_server(1)->Pause());
  ASSERT_OK(cluster_->tablet_server(2)->Pause());
  ASSERT_OK(cluster_->master(0)->Pause());
  ASSERT_OK(cluster_->master(1)->Pause());
  ASSERT_OK(cluster_->master(2)->Pause());
  SleepFor(MonoDelta::FromMilliseconds(300));
  ASSERT_OK(cluster_->tablet_server(0)->Resume());
  ASSERT_OK(cluster_->tablet_server(1)->Resume());
  ASSERT_OK(cluster_->tablet_server(2)->Resume());
  ASSERT_OK(cluster_->master(0)->Resume());
  ASSERT_OK(cluster_->master(1)->Resume());
  ASSERT_OK(cluster_->master(2)->Resume());
  SleepFor(MonoDelta::FromMilliseconds(100));

  // Set an explicit timeout. This test has caused deadlocks in the past.
  // Also make sure to dump stacks before the alarm goes off.
  PstackWatcher watcher(MonoDelta::FromSeconds(30));
  alarm(60);
}

namespace {

class ClientStressTestSlowMultiMaster : public ClientStressTest {
 protected:
  ExternalMiniClusterOptions default_opts() override {
    ExternalMiniClusterOptions result;
    result.num_masters = 3;
    result.master_rpc_ports = { 0, 0, 0 };
    result.extra_master_flags = { "--master_slow_get_registration_probability=0.2"s };
    result.num_tablet_servers = 0;
    return result;
  }
};

void LeaderMasterCallback(Synchronizer* sync,
                          const Status& status,
                          const HostPort& result) {
  LOG_IF(INFO, status.ok()) << "Leader master host port: " << result.ToString();
  sync->StatusCB(status);
}

void RepeatGetLeaderMaster(ExternalMiniCluster* cluster) {
  server::MasterAddresses master_addrs;
  for (auto i = 0; i != cluster->num_masters(); ++i) {
    master_addrs.push_back({cluster->master(i)->bound_rpc_addr()});
  }
  auto stop_time = std::chrono::steady_clock::now() + 60s;
  std::vector<std::thread> threads;
  for (auto i = 0; i != 10; ++i) {
    threads.emplace_back([cluster, stop_time, master_addrs]() {
      while (std::chrono::steady_clock::now() < stop_time) {
        rpc::Rpcs rpcs;
        Synchronizer sync;
        auto deadline = CoarseMonoClock::Now() + 20s;
        auto rpc = rpc::StartRpc<master::GetLeaderMasterRpc>(
            Bind(&LeaderMasterCallback, &sync),
            master_addrs,
            deadline,
            cluster->messenger(),
            &cluster->proxy_cache(),
            &rpcs);
        auto status = sync.Wait();
        LOG_IF(INFO, !status.ok()) << "Get leader master failed: " << status;
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

} // namespace

TEST_F_EX(ClientStressTest, SlowLeaderResolution, ClientStressTestSlowMultiMaster) {
  DontVerifyClusterBeforeNextTearDown();

  RepeatGetLeaderMaster(cluster_.get());
}

namespace {

class ClientStressTestSmallQueueMultiMaster : public ClientStressTest {
 protected:
  ExternalMiniClusterOptions default_opts() override {
    ExternalMiniClusterOptions result;
    result.num_masters = 3;
    result.master_rpc_ports = { 0, 0, 0 };
    result.extra_master_flags = { "--master_svc_queue_length=5"s };
    result.num_tablet_servers = 0;
    return result;
  }
};

} // namespace

TEST_F_EX(ClientStressTest, RetryLeaderResolution, ClientStressTestSmallQueueMultiMaster) {
  DontVerifyClusterBeforeNextTearDown();

  RepeatGetLeaderMaster(cluster_.get());
}

// Override the base test to start a cluster with a low memory limit.
class ClientStressTest_LowMemory : public ClientStressTest {
 protected:
  ExternalMiniClusterOptions default_opts() override {
    // There's nothing scientific about this number; it must be low enough to
    // trigger memory pressure request rejection yet high enough for the
    // servers to make forward progress.
    //
    // Note that if this number is set too low, the test will fail in a CHECK in TestWorkload
    // after retries are exhausted when writing an entry. This happened e.g. when a substantial
    // upfront memory overhead was introduced by adding a large lock-free queue in Preparer.
    const int kMemLimitBytes = RegularBuildVsSanitizers(64_MB, 2_MB);
    ExternalMiniClusterOptions opts;

    opts.extra_tserver_flags = { Substitute("--memory_limit_hard_bytes=$0", kMemLimitBytes),
                                 "--memory_limit_soft_percentage=0"s };

    opts.num_tablet_servers = 3;
    return opts;
  }
};

// Stress test where, due to absurdly low memory limits, many client requests
// are rejected, forcing the client to retry repeatedly.
TEST_F(ClientStressTest_LowMemory, TestMemoryThrottling) {
  // Sanitized tests run much slower, so we don't want to wait for as many
  // rejections before declaring the test to be passed.
  const int64_t kMinRejections = 15;

  const MonoDelta kMaxWaitTime = MonoDelta::FromSeconds(60);

  TestWorkload work(cluster_.get());
  work.set_write_batch_size(RegularBuildVsSanitizers(25, 5));

  work.Setup();
  work.Start();

  // Wait until we've rejected some number of requests.
  MonoTime deadline = MonoTime::Now() + kMaxWaitTime;
  while (true) {
    int64_t total_num_rejections = 0;

    // It can take some time for the tablets (and their metric entities) to
    // appear on every server. Rather than explicitly wait for that above,
    // we'll just treat the lack of a metric as non-fatal. If the entity
    // or metric is truly missing, we'll eventually timeout and fail.
    for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
      for (const auto* metric : { &METRIC_leader_memory_pressure_rejections,
                                  &METRIC_follower_memory_pressure_rejections }) {
        auto result = cluster_->tablet_server(i)->GetInt64Metric(
            &METRIC_ENTITY_tablet, nullptr, metric, "value");
        if (result.ok()) {
          total_num_rejections += *result;
        } else {
          ASSERT_TRUE(result.status().IsNotFound()) << result.status();
        }
      }
    }
    if (total_num_rejections >= kMinRejections) {
      break;
    } else if (deadline.ComesBefore(MonoTime::Now())) {
      FAIL() << "Ran for " << kMaxWaitTime.ToString() << ", deadline expired and only saw "
             << total_num_rejections << " memory rejections";
    }
    SleepFor(MonoDelta::FromMilliseconds(200));
  }
}

namespace {

class ClientStressTestSmallQueueMultiMasterWithTServers : public ClientStressTest {
 protected:
  ExternalMiniClusterOptions default_opts() override {
    ExternalMiniClusterOptions result;
    result.num_masters = 3;
    result.master_rpc_ports = { 0, 0, 0 };
    result.extra_master_flags = {
        "--master_svc_queue_length=5"s, "--master_inject_latency_on_tablet_lookups_ms=50"s };
    result.num_tablet_servers = 3;
    return result;
  }
};

} // namespace

// Check behaviour of meta cache in case of server queue is full.
TEST_F_EX(ClientStressTest, MasterQueueFull, ClientStressTestSmallQueueMultiMasterWithTServers) {
  TestWorkload workload(cluster_.get());
  workload.Setup();

  struct Item {
    std::unique_ptr<client::YBClient> client;
    std::unique_ptr<client::TableHandle> table;
    client::YBSessionPtr session;
    std::future<Status> future;
  };

  std::vector<Item> items;
  constexpr size_t kNumRequests = 40;
  while (items.size() != kNumRequests) {
    Item item;
    item.client = ASSERT_RESULT(cluster_->CreateClient());
    item.table = std::make_unique<client::TableHandle>();
    ASSERT_OK(item.table->Open(TestWorkloadOptions::kDefaultTableName, item.client.get()));
    item.session = std::make_shared<client::YBSession>(item.client.get());
    items.push_back(std::move(item));
  }

  int32_t key = 0;
  const std::string kStringValue("string value");
  for (auto& item : items) {
    auto op = item.table->NewInsertOp();
    auto req = op->mutable_request();
    QLAddInt32HashValue(req, ++key);
    item.table->AddInt32ColumnValue(req, item.table->schema().columns()[1].name(), -key);
    item.table->AddStringColumnValue(req, item.table->schema().columns()[2].name(), kStringValue);
    ASSERT_OK(item.session->Apply(op));
    item.future = item.session->FlushFuture();
  }

  for (auto& item : items) {
    ASSERT_OK(item.future.get());
  }
}

class RF1ClientStressTest : public ClientStressTest {
 public:
  ExternalMiniClusterOptions default_opts() override {
    ExternalMiniClusterOptions result;
    result.num_tablet_servers = 1;
    result.extra_master_flags = { "--replication_factor=1" };
    return result;
  }
};

// Test that config change works while running a workload.
TEST_F_EX(ClientStressTest, IncreaseReplicationFactorUnderLoad, RF1ClientStressTest) {
  TestWorkload work(cluster_.get());
  work.set_num_write_threads(1);
  work.set_num_tablets(6);
  work.Setup();
  work.Start();

  // Fill table with some records.
  std::this_thread::sleep_for(1s);

  ASSERT_OK(cluster_->AddTabletServer(/* start_cql_proxy= */ false, {"--time_source=skewed,-500"}));

  master::ReplicationInfoPB replication_info;
  replication_info.mutable_live_replicas()->set_num_replicas(2);
  ASSERT_OK(work.client().SetReplicationInfo(replication_info));

  LOG(INFO) << "Replication factor changed";

  auto deadline = CoarseMonoClock::now() + 3s;
  while (CoarseMonoClock::now() < deadline) {
    ASSERT_NO_FATALS(cluster_->AssertNoCrashes());
    std::this_thread::sleep_for(100ms);
  }

  work.StopAndJoin();

  LOG(INFO) << "Written rows: " << work.rows_inserted();
}

}  // namespace yb
