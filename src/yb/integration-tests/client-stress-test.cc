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
#include <regex>
#include <vector>

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/common/schema.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/mathlimits.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/test_workload.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/master_rpc.h"

#include "yb/rpc/rpc.h"

#include "yb/util/curl_util.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/pstack_watcher.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

DECLARE_int32(memory_limit_soft_percentage);

METRIC_DECLARE_entity(tablet);
METRIC_DECLARE_counter(leader_memory_pressure_rejections);
METRIC_DECLARE_counter(follower_memory_pressure_rejections);

using strings::Substitute;
using std::vector;
using namespace std::literals; // NOLINT
using namespace std::placeholders;

namespace yb {

using client::YBClient;

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
  work.set_num_write_threads(RegularBuildVsSanitizers(64, 8));

  // This timeout gets applied to the master requests. It's lower than the
  // amount of time that we sleep the masters, to ensure they timeout.
  work.set_client_default_rpc_timeout_millis(250 * kTimeMultiplier);
  // This is the time budget for the whole request. It has to be longer than
  // the above timeout so that the client actually attempts to resolve
  // the leader.
  work.set_write_timeout_millis(280 * kTimeMultiplier);
  work.set_timeout_allowed(true);
  work.Setup();

  work.Start();

  ASSERT_OK(cluster_->tablet_server(0)->Pause());
  ASSERT_OK(cluster_->tablet_server(1)->Pause());
  ASSERT_OK(cluster_->tablet_server(2)->Pause());
  ASSERT_OK(cluster_->master(0)->Pause());
  ASSERT_OK(cluster_->master(1)->Pause());
  ASSERT_OK(cluster_->master(2)->Pause());
  SleepFor(MonoDelta::FromMilliseconds(300 * kTimeMultiplier));
  ASSERT_OK(cluster_->tablet_server(0)->Resume());
  ASSERT_OK(cluster_->tablet_server(1)->Resume());
  ASSERT_OK(cluster_->tablet_server(2)->Resume());
  ASSERT_OK(cluster_->master(0)->Resume());
  ASSERT_OK(cluster_->master(1)->Resume());
  ASSERT_OK(cluster_->master(2)->Resume());
  SleepFor(MonoDelta::FromMilliseconds(100 * kTimeMultiplier));

  // Set an explicit timeout. This test has caused deadlocks in the past.
  // Also make sure to dump stacks before the alarm goes off.
  PstackWatcher watcher(MonoDelta::FromSeconds(30 * kTimeMultiplier));
  alarm(60 * kTimeMultiplier);
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
  for (size_t i = 0; i != cluster->num_masters(); ++i) {
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
        auto rpc = std::make_shared<master::GetLeaderMasterRpc>(
            Bind(&LeaderMasterCallback, &sync),
            master_addrs,
            deadline,
            cluster->messenger(),
            &cluster->proxy_cache(),
            &rpcs);
        rpc->SendRpc();
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

    opts.extra_tserver_flags = {
        Substitute("--memory_limit_hard_bytes=$0", kMemLimitBytes),
        "--memory_limit_soft_percentage=0"s};
    // Turn off tablet guardrail otherwise we fail due to insufficient memory for tablets:
    opts.extra_master_flags = {"--tablet_replicas_per_gib_limit=0"s};

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
    for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
      for (const auto* metric : { &METRIC_leader_memory_pressure_rejections,
                                  &METRIC_follower_memory_pressure_rejections }) {
        auto result = cluster_->tablet_server(i)->GetMetric<int64>(
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
    std::future<client::FlushStatus> future;
  };

  std::vector<Item> items;
  constexpr size_t kNumRequests = 40;
  while (items.size() != kNumRequests) {
    Item item;
    item.client = ASSERT_RESULT(cluster_->CreateClient());
    item.table = std::make_unique<client::TableHandle>();
    ASSERT_OK(item.table->Open(TestWorkloadOptions::kDefaultTableName, item.client.get()));
    item.session = item.client.get()->NewSession(60s);
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
    item.session->Apply(op);
    item.future = item.session->FlushFuture();
  }

  for (auto& item : items) {
    ASSERT_OK(item.future.get().status);
  }
}

namespace {

// TODO: Add peak root mem tracker metric after https://github.com/yugabyte/yugabyte-db/issues/3442
// is implemented. Retrieve metric value using RPC instead of parsing HTML report.
Result<size_t> GetPeakRootConsumption(const ExternalTabletServer& ts) {
  EasyCurl c;
  faststring buf;
  EXPECT_OK(c.FetchURL(Format("http://$0/mem-trackers?raw=1", ts.bound_http_hostport().ToString()),
                       &buf));
  static const std::regex re(
      R"#(\s*<td><span class=\"toggle collapse\"></span>root</td>)#"
      R"#(<td>([0-9.]+\w)(\s+\([0-9.]+\w\))?</td>)#"
      R"#(<td>([0-9.]+\w)</td><td>([0-9.]+\w)</td>\s*)#");
  const auto str = buf.ToString();
  std::smatch match;
  if (std::regex_search(str, match, re)) {
    const auto consumption_str = match.str(3);
    int64_t consumption;
    if (HumanReadableNumBytes::ToInt64(consumption_str, &consumption)) {
      return consumption;
    } else {
      return STATUS_FORMAT(
          InvalidArgument,
          "Failed to parse memory consumption: $0", consumption_str);
    }
  } else {
    return STATUS_FORMAT(
        InvalidArgument,
        "Failed to parse root mem tracker consumption from: $0", str);
  }
}

class ThrottleLogCounter : public ExternalDaemon::StringListener {
 public:
  explicit ThrottleLogCounter(ExternalDaemon* daemon) : daemon_(daemon) {
    daemon_->SetLogListener(this);
  }

  ~ThrottleLogCounter() {
    daemon_->RemoveLogListener(this);
  }

  size_t rejected_call_messages() { return rejected_call_messages_; }

  size_t ignored_call_messages() { return ignored_call_messages_; }

 private:
  void Handle(const GStringPiece& s) override {
    if (s.contains("Rejecting RPC call")) {
      rejected_call_messages_.fetch_add(1);
    } else if (s.contains("Ignoring RPC call")) {
      ignored_call_messages_.fetch_add(1);
    }
  }

  ExternalDaemon* daemon_;
  std::atomic<size_t> rejected_call_messages_{0};
  std::atomic<size_t> ignored_call_messages_{0};
};

class ClientStressTest_FollowerOom : public ClientStressTest {
 protected:
  ExternalMiniClusterOptions default_opts() override {
    ExternalMiniClusterOptions opts;

    opts.extra_tserver_flags = {
        Format("--memory_limit_hard_bytes=$0", kHardLimitBytes),
        Format("--consensus_max_batch_size_bytes=$0", kConsensusMaxBatchSizeBytes),
        // Turn off exponential backoff and lagging follower threshold in order to hit soft memory
        // limit and check throttling.
        "--enable_consensus_exponential_backoff=false",
        "--consensus_lagging_follower_threshold=-1"
    };

    opts.num_tablet_servers = 3;
    return opts;
  }

  static constexpr size_t kHardLimitBytes = 100_MB * RegularBuildVsSanitizers(5, 1);
  const size_t kConsensusMaxBatchSizeBytes = 32_MB;
};

} // namespace

// Original scenario for reproducing the issue is the following:
// 1. Kill follower, wait some time, then restart.
// 2. That should lead to follower trying to catch up with leader, but due to big UpdateConsensus
// RPC requests and slow parsing, requests will be consuming more and more memory
// (https://github.com/yugabyte/yugabyte-db/issues/2563,
// https://github.com/yugabyte/yugabyte-db/issues/2564)
// 3. We expect in this scenario follower to hit soft memory limit and fix for #2563 should
// start throttling inbound RPCs.
//
// In this test we simulate slow inbound RPC requests parsing using
// TEST_yb_inbound_big_calls_parse_delay_ms flag.
TEST_F_EX(ClientStressTest, PauseFollower, ClientStressTest_FollowerOom) {
  constexpr int kNumRows = 20000 * RegularBuildVsSanitizers(5, 1);

  TestWorkload workload(cluster_.get());
  workload.set_write_timeout_millis(30000);
  workload.set_num_tablets(1);
  workload.set_num_write_threads(4);
  workload.set_write_batch_size(500);
  workload.set_payload_bytes(100);
  workload.Setup();
  workload.Start();

  while (workload.rows_inserted() < 500) {
    std::this_thread::sleep_for(10ms);
  }

  auto ts = cluster_->tablet_server(0);

  LOG(INFO) << "Peak root mem tracker consumption: "
            << HumanReadableNumBytes::ToString(ASSERT_RESULT(GetPeakRootConsumption(*ts)));

  LOG(INFO) << "Killing ts-1";
  ts->Shutdown();

  // Write enough data to guarantee large UpdateConsensus requests before restarting ts-1.
  while (workload.rows_inserted() < kNumRows) {
    LOG(INFO) << "Rows inserted: " << workload.rows_inserted();
    std::this_thread::sleep_for(1s);
  }

  LOG(INFO) << "Restarting ts-1";
  ts->mutable_flags()->push_back(
      Format("--TEST_yb_inbound_big_calls_parse_delay_ms=$0", 50000 * kTimeMultiplier));
  ts->mutable_flags()->push_back("--binary_call_parser_reject_on_mem_tracker_hard_limit=true");
  // Throttle requests with network size larger than 1 MB. Note that the amount of memory that is
  // counted against the memtracker is much larger after the param has been parsed, so it does not
  // take too many requests to hit the soft memory limit.
  ts->mutable_flags()->push_back(Format("--rpc_throttle_threshold_bytes=$0", 1_MB));
  // Read buffer should be large enough to accept the large RPCs.
  ts->mutable_flags()->push_back("--read_buffer_memory_limit=-50");
  ASSERT_OK(ts->Restart());

  ThrottleLogCounter log_counter(ts);
  for (;;) {
    const auto ignored_rejected_call_messages =
        log_counter.ignored_call_messages() + log_counter.rejected_call_messages();
    YB_LOG_EVERY_N_SECS(INFO, 5) << "Ignored/rejected call messages: "
                                 << ignored_rejected_call_messages;
    if (ignored_rejected_call_messages > 5) {
      break;
    }
    std::this_thread::sleep_for(1s);
  }

  const auto peak_consumption = ASSERT_RESULT(GetPeakRootConsumption(*ts));
  LOG(INFO) << "Peak root mem tracker consumption: "
            << HumanReadableNumBytes::ToString(peak_consumption);

  ASSERT_GT(peak_consumption, kHardLimitBytes * FLAGS_memory_limit_soft_percentage / 100);

  LOG(INFO) << "Stopping cluster";

  workload.StopAndJoin();

  cluster_->Shutdown();

  LOG(INFO) << "Done";
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
