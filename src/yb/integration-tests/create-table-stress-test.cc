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
#include <thread>

#include "yb/util/logging.h"
#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table_creator.h"

#include "yb/dockv/partition.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.proxy.h"

#include "yb/fs/fs_manager.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_loaders.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master-test-util.h"
#include "yb/master/master.h"
#include "yb/master/master_client.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_heartbeat.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_test_util.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/hdr_histogram.h"
#include "yb/util/metrics.h"
#include "yb/util/scope_exit.h"
#include "yb/util/spinlock_profiling.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"
#include "yb/util/flags.h"

using yb::client::YBClient;
using yb::client::YBClientBuilder;
using yb::client::YBSchema;
using yb::client::YBSchemaBuilder;
using yb::client::YBTableCreator;
using yb::client::YBTableName;
using yb::itest::CreateTabletServerMap;
using yb::itest::TabletServerMap;
using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;

DECLARE_int32(heartbeat_interval_ms);
DECLARE_bool(log_preallocate_segments);
DECLARE_bool(TEST_enable_remote_bootstrap);
DECLARE_int32(tserver_unresponsive_timeout_ms);
DECLARE_int32(max_create_tablets_per_ts);
DECLARE_int32(tablet_report_limit);
DECLARE_uint64(TEST_inject_latency_during_tablet_report_ms);
DECLARE_int32(heartbeat_rpc_timeout_ms);
DECLARE_int32(catalog_manager_report_batch_size);
DECLARE_int32(tablet_report_limit);

DEFINE_NON_RUNTIME_int32(num_test_tablets, 60, "Number of tablets for stress test");
DEFINE_NON_RUNTIME_int32(benchmark_runtime_secs, 5, "Number of seconds to run the benchmark");
DEFINE_NON_RUNTIME_int32(benchmark_num_threads, 16, "Number of threads to run the benchmark");
// Increase this for actually using this as a benchmark test.
DEFINE_NON_RUNTIME_int32(benchmark_num_tablets, 8, "Number of tablets to create");

METRIC_DECLARE_histogram(handler_latency_yb_master_MasterClient_GetTableLocations);

using std::string;
using std::vector;
using std::thread;
using std::unique_ptr;
using strings::Substitute;

namespace yb {

class CreateTableStressTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  CreateTableStressTest() {
    YBSchemaBuilder b;
    b.AddColumn("key")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    b.AddColumn("v1")->Type(DataType::INT64)->NotNull();
    b.AddColumn("v2")->Type(DataType::STRING)->NotNull();
    CHECK_OK(b.Build(&schema_));
  }

  void SetUp() override {
    // Make heartbeats faster to speed test runtime.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = 10;

    // Don't preallocate log segments, since we're creating thousands
    // of tablets here. If each preallocates 64M or so, we use
    // a ton of disk space in this test, and it fails on normal
    // sized /tmp dirs.
    // TODO: once we collapse multiple tablets into shared WAL files,
    // this won't be necessary.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_preallocate_segments) = false;

    // Workaround KUDU-941: without this, it's likely that while shutting
    // down tablets, they'll get resuscitated by their existing leaders.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_remote_bootstrap) = false;

    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    client_ = ASSERT_RESULT(YBClientBuilder()
        .add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str())
        .Build());

    messenger_ = ASSERT_RESULT(
        MessengerBuilder("stress-test-msgr").set_num_reactors(1).Build());
    rpc::ProxyCache proxy_cache(messenger_.get());
    master::MasterClusterProxy proxy(&proxy_cache, cluster_->mini_master()->bound_rpc_addr());
    ts_map_ = ASSERT_RESULT(CreateTabletServerMap(proxy, &proxy_cache));
  }

  void DoTearDown() override {
    messenger_->Shutdown();
    client_.reset();
    cluster_->Shutdown();
    ts_map_.clear();
  }

  void CreateBigTable(const YBTableName& table_name, int num_tablets);

 protected:
  std::unique_ptr<YBClient> client_;
  YBSchema schema_;
  std::unique_ptr<Messenger> messenger_;
  std::unique_ptr<master::MasterClusterProxy> master_proxy_;
  TabletServerMap ts_map_;
};

void CreateTableStressTest::CreateBigTable(const YBTableName& table_name, int num_tablets) {
  ASSERT_OK(client_->CreateNamespaceIfNotExists(table_name.namespace_name(),
                                                table_name.namespace_type()));
  std::unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
  ASSERT_OK(table_creator->table_name(table_name)
            .schema(&schema_)
            .num_tablets(num_tablets)
            .wait(false)
            .Create());
}

TEST_F(CreateTableStressTest, GetTableLocationsBenchmark) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_create_tablets_per_ts) = FLAGS_benchmark_num_tablets;
  DontVerifyClusterBeforeNextTearDown();
  YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 1. Creating big table "
            << table_name.ToString() << " ...";
  LOG_TIMING(INFO, "creating big table") {
    ASSERT_NO_FATALS(CreateBigTable(table_name, FLAGS_benchmark_num_tablets));
  }

  // Make sure the table is completely created before we start poking.
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 2. Waiting for creation of big table "
            << table_name.ToString() << " to complete...";
  master::GetTableLocationsResponsePB create_resp;
  LOG_TIMING(INFO, "waiting for creation of big table") {
    ASSERT_OK(WaitForRunningTabletCount(cluster_->mini_master(), table_name,
                                        FLAGS_benchmark_num_tablets, &create_resp));
  }
  // Sleep for a while to let all TS heartbeat to master.
  SleepFor(MonoDelta::FromSeconds(10));
  const int kNumThreads = FLAGS_benchmark_num_threads;
  const auto kRuntime = MonoDelta::FromSeconds(FLAGS_benchmark_runtime_secs);

  // Make one proxy per thread, so each thread gets its own messenger and
  // reactor. If there were only one messenger, then only one reactor thread
  // would be used for the connection to the master, so this benchmark would
  // probably be testing the serialization and network code rather than the
  // master GTL code.
  vector<rpc::AutoShutdownMessengerHolder> messengers;
  vector<master::MasterClientProxy> proxies;
  vector<unique_ptr<rpc::ProxyCache>> caches;
  messengers.reserve(kNumThreads);
  proxies.reserve(kNumThreads);
  caches.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    messengers.emplace_back(
        ASSERT_RESULT(MessengerBuilder("Client").set_num_reactors(1).Build()).release());
    caches.emplace_back(new rpc::ProxyCache(messengers.back().get()));
    proxies.emplace_back(caches.back().get(), cluster_->mini_master()->bound_rpc_addr());
  }

  std::atomic<bool> stop { false };
  vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&, i]() {
        while (!stop) {
          master::GetTableLocationsRequestPB req;
          master::GetTableLocationsResponsePB resp;
          RpcController controller;
          // Silence errors.
          controller.set_timeout(MonoDelta::FromSeconds(10));
          table_name.SetIntoTableIdentifierPB(req.mutable_table());
          req.set_max_returned_locations(1000);
          CHECK_OK(proxies[i].GetTableLocations(req, &resp, &controller));
          CHECK_EQ(resp.tablet_locations_size(), FLAGS_benchmark_num_tablets);
        }
      });
  }

  std::stringstream profile;
  StartSynchronizationProfiling();
  SleepFor(kRuntime);
  stop = true;
  for (auto& t : threads) {
    t.join();
  }
  StopSynchronizationProfiling();
  int64_t discarded_samples = 0;
  FlushSynchronizationProfile(&profile, &discarded_samples);

  const auto& ent = cluster_->mini_master()->master()->metric_entity();
  auto hist = METRIC_handler_latency_yb_master_MasterClient_GetTableLocations
      .Instantiate(ent);

  cluster_->Shutdown();

  LOG(INFO) << "LOCK PROFILE\n" << profile.str();
  LOG(INFO) << "BENCHMARK HISTOGRAM:";
  hist->underlying()->DumpHumanReadable(&LOG(INFO));
}

class CreateMultiHBTableStressTest : public CreateTableStressTest,
                                     public testing::WithParamInterface<bool /* is_multiHb */> {
  void SetUp() override {
    // "MultiHB" Tables are too large to be reported in a single heartbeat from a TS.
    // Setup so all 3 TS will have to break tablet report updates into multiple chunks.
    bool is_multiHb = GetParam();
    if (is_multiHb) {
      // 90 Tablets * 3 TS < 300 Tablets
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_report_limit) = 90;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_test_tablets) = 300;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_create_tablets_per_ts) = FLAGS_num_test_tablets;
      // 1000 ms deadline / 20 ms wait/batch ~= 40 Tablets processed before Master hits deadline
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_latency_during_tablet_report_ms) = 20;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_rpc_timeout_ms) = 1000;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_report_batch_size) = 1;
    }
    CreateTableStressTest::SetUp();
  }
};
INSTANTIATE_TEST_CASE_P(MultiHeartbeat, CreateMultiHBTableStressTest, ::testing::Bool());

// Replaces itest version, which requires an External Mini Cluster.
Status ListRunningTabletIds(std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy,
                            const MonoDelta& timeout,
                            std::vector<string>* tablet_ids) {
  tserver::ListTabletsRequestPB req;
  tserver::ListTabletsResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout);

  RETURN_NOT_OK(ts_proxy->ListTablets(req, &resp, &rpc));
  tablet_ids->clear();
  for (const auto& t : resp.status_and_schema()) {
    if (t.tablet_status().state() == tablet::RUNNING) {
      tablet_ids->push_back(t.tablet_status().tablet_id());
    }
  }
  return Status::OK();
}

TEST_P(CreateMultiHBTableStressTest, CreateAndDeleteBigTable) {
  DontVerifyClusterBeforeNextTearDown();
  if (IsSanitizer()) {
    LOG(INFO) << "Skipping slow test";
    return;
  }
  YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  ASSERT_NO_FATALS(CreateBigTable(table_name, FLAGS_num_test_tablets));
  master::GetTableLocationsResponsePB resp;
  ASSERT_OK(WaitForRunningTabletCount(cluster_->mini_master(), table_name,
                                      FLAGS_num_test_tablets, &resp));
  LOG(INFO) << "Created table successfully!";
  // Use std::cout instead of log, since these responses are large and log
  // messages have a max size.
  std::cout << "Response:\n" << resp.DebugString();
  std::cout << "CatalogManager state:\n";
  ASSERT_OK(cluster_->mini_master()->catalog_manager().DumpState(&std::cerr));

  // Store all relevant tablets for this big table we've created.
  std::vector<string> big_table_tablets;
  for (const auto & loc : resp.tablet_locations()) {
    big_table_tablets.push_back(loc.tablet_id());
  }
  std::sort(big_table_tablets.begin(), big_table_tablets.end());

  LOG(INFO) << "Deleting table...";
  ASSERT_OK(client_->DeleteTable(table_name));

  // The actual removal of the tablets is asynchronous, so we loop for a bit
  // waiting for them to get removed.
  LOG(INFO) << "Waiting for tablets to be removed on TS#1";
  std::vector<string> big_tablet_left, tablet_ids;
  auto ts_proxy = cluster_->mini_tablet_server(0)->server()->proxy();
  for (int i = 0; i < 1000; i++) {
    ASSERT_OK(ListRunningTabletIds(ts_proxy, 10s, &tablet_ids));
    std::sort(tablet_ids.begin(), tablet_ids.end());
    big_tablet_left.clear();
    std::set_intersection(big_table_tablets.begin(), big_table_tablets.end(),
                          tablet_ids.begin(), tablet_ids.end(),
                          big_tablet_left.begin());
    if (big_tablet_left.empty()) return;
    SleepFor(MonoDelta::FromMilliseconds(100));
  }
  ASSERT_TRUE(big_tablet_left.empty()) << "Tablets remaining: " << big_tablet_left.size()
                                       << " : " << big_tablet_left;
}

TEST_P(CreateMultiHBTableStressTest, RestartServersAfterCreation) {
  DontVerifyClusterBeforeNextTearDown();
  if (IsSanitizer()) {
    LOG(INFO) << "Skipping slow test";
    return;
  }
  YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  ASSERT_NO_FATALS(CreateBigTable(table_name, FLAGS_num_test_tablets));

  for (int i = 0; i < 3; i++) {
    SleepFor(MonoDelta::FromMicroseconds(500));
    LOG(INFO) << "Restarting master...";
    ASSERT_OK(cluster_->mini_master()->Restart());
    ASSERT_OK(cluster_->mini_master()->master()->
        WaitUntilCatalogManagerIsLeaderAndReadyForTests());
    LOG(INFO) << "Master restarted.";
  }

  // Restart TS#2, which forces a full tablet report on TS #2 and incremental updates on the others.
  ASSERT_OK(cluster_->mini_tablet_server(1)->Restart());

  master::GetTableLocationsResponsePB resp;
  Status s = WaitForRunningTabletCount(cluster_->mini_master(), table_name,
                                       FLAGS_num_test_tablets, &resp);
  if (!s.ok()) {
    ASSERT_OK(cluster_->mini_master()->catalog_manager().DumpState(&std::cerr));
    CHECK_OK(s);
  }
}

class CreateSmallHBTableStressTest : public CreateTableStressTest {
  void SetUp() override {
    // 40 / 3 ~= 13 tablets / server.  2 / report >= 7 reports to finish a heartbeat
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_report_limit) = 2;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_test_tablets) = 40;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_create_tablets_per_ts) = FLAGS_num_test_tablets;

    CreateTableStressTest::SetUp();
  }
};
TEST_F(CreateSmallHBTableStressTest, TestRestartMasterDuringFullHeartbeat) {
  DontVerifyClusterBeforeNextTearDown();
  if (IsSanitizer()) {
    LOG(INFO) << "Skipping slow test";
    return;
  }
  YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  ASSERT_NO_FATALS(CreateBigTable(table_name, FLAGS_num_test_tablets));

  // 100 ms wait / tablet >= 1.3 sec to receive a full report
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_latency_during_tablet_report_ms) = 100;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_report_batch_size) = 1;

  // Restart Master #1.  Triggers Full Report from all TServers.
  ASSERT_OK(cluster_->mini_master()->Restart());
  ASSERT_OK(cluster_->mini_master()->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  // Wait until the Master is ~25% complete with getting the heartbeats from the TS.
  master::GetTableLocationsResponsePB resp;
  ASSERT_OK(WaitForRunningTabletCount(cluster_->mini_master(), table_name,
                                      FLAGS_num_test_tablets / 4, &resp));
  ASSERT_LT(resp.tablet_locations_size(), FLAGS_num_test_tablets / 2);
  LOG(INFO) << "Resetting Master after seeing table count: " << resp.tablet_locations_size();

  // Restart Master #2.  Re-triggers a Full Report from all TServers, even though they were in the
  // middle of sending a full report to the old master.
  ASSERT_OK(cluster_->mini_master()->Restart());
  ASSERT_OK(cluster_->mini_master()->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  // Speed up the test now...
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_latency_during_tablet_report_ms) = 0;

  // The TS should send a full report.  If they just sent the remainder from their original
  // Full Report, this test will fail.
  Status s = WaitForRunningTabletCount(cluster_->mini_master(), table_name,
                                       FLAGS_num_test_tablets, &resp);
  if (!s.ok()) {
    ASSERT_OK(cluster_->mini_master()->catalog_manager().DumpState(&std::cerr));
    CHECK_OK(s);
  }
}

TEST_F(CreateTableStressTest, YB_DISABLE_TEST_ON_MACOS(TestHeartbeatDeadline)) {
  DontVerifyClusterBeforeNextTearDown();

  // 500ms deadline / 50 ms wait ~= 10 Tablets processed before Master hits deadline
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_report_batch_size) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_latency_during_tablet_report_ms)
      = 50 * kTimeMultiplier;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_rpc_timeout_ms) = 500 * kTimeMultiplier;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_test_tablets) = 60;

  // Create a Table with 60 tablets, so ~20 per TS.
  YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  ASSERT_NO_FATALS(CreateBigTable(table_name, FLAGS_num_test_tablets));
  master::GetTableLocationsResponsePB resp;
  ASSERT_OK(WaitForRunningTabletCount(cluster_->mini_master(), table_name,
    FLAGS_num_test_tablets, &resp));

  master::SysClusterConfigEntryPB config =
      ASSERT_RESULT(cluster_->mini_master()->catalog_manager().GetClusterConfig());
  auto universe_uuid = config.universe_uuid();

  // Grab TS#1 and Generate a Full Report for it.
  master::TSHeartbeatRequestPB hb_req;
  {
    // The TabletServer pointer is invalid after the Shutdown call below. So introduce a scope to
    // explicitly contain the ts_server lifetime to when it is valid.
    auto ts_server = cluster_->mini_tablet_server(0)->server();
    hb_req.mutable_common()->mutable_ts_instance()->CopyFrom(ts_server->instance_pb());
    hb_req.set_universe_uuid(universe_uuid);
    ts_server->tablet_manager()->StartFullTabletReport(hb_req.mutable_tablet_report());
    ASSERT_GT(
        hb_req.tablet_report().updated_tablets_size(),
        FLAGS_heartbeat_rpc_timeout_ms / FLAGS_TEST_inject_latency_during_tablet_report_ms);
    ASSERT_EQ(ts_server->tablet_manager()->GetReportLimit(), FLAGS_tablet_report_limit);
    ASSERT_LE(hb_req.tablet_report().updated_tablets_size(), FLAGS_tablet_report_limit);
  }

  // Stop TS#1 so their heartbeat doesn't interfere with our fake one.
  cluster_->mini_tablet_server(0)->Shutdown();
  rpc::ProxyCache proxy_cache(messenger_.get());
  master::MasterHeartbeatProxy proxy(&proxy_cache, cluster_->mini_master()->bound_rpc_addr());

  // Grab Master and Process this Tablet Report.
  // This should go over the deadline and get truncated.
  master::TSHeartbeatResponsePB hb_resp;
  hb_req.mutable_tablet_report()->set_is_incremental(true);
  // Bump sequence number because the TS' heartbeat thread continues to run after we grab the
  // sequence number so it may have already invalidated the sequence number we grabbed.
  hb_req.mutable_tablet_report()->set_sequence_number(
      hb_req.tablet_report().sequence_number() + 100);
  hb_req.set_universe_uuid(universe_uuid);
  Status heartbeat_status;
  // Regression testbed often has stalls at this timing granularity.  Allow a couple hiccups.
  for (int tries = 0; tries < 3; ++tries) {
    RpcController rpc;
    rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_heartbeat_rpc_timeout_ms));
    heartbeat_status = proxy.TSHeartbeat(hb_req, &hb_resp, &rpc);
    if (heartbeat_status.ok()) break;
    ASSERT_TRUE(heartbeat_status.IsTimedOut())
        << Format("Heartbeat status is: $0", heartbeat_status);
  }
  ASSERT_OK(heartbeat_status);
  ASSERT_TRUE(hb_resp.tablet_report().processing_truncated());
  ASSERT_LE(hb_resp.tablet_report().tablets_size(),
            FLAGS_heartbeat_rpc_timeout_ms / FLAGS_TEST_inject_latency_during_tablet_report_ms);
}


TEST_F(CreateTableStressTest, TestGetTableLocationsOptions) {
DontVerifyClusterBeforeNextTearDown();
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipping slow test";
    return;
  }

  YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 1. Creating big table "
            << table_name.ToString() << " ...";
  LOG_TIMING(INFO, "creating big table") {
    ASSERT_NO_FATALS(CreateBigTable(table_name, FLAGS_num_test_tablets));
  }

  master::GetTableLocationsRequestPB req;
  master::GetTableLocationsResponsePB resp;

  // Make sure the table is completely created before we start poking.
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 2. Waiting for creation of big table "
            << table_name.ToString() << " to complete...";
  LOG_TIMING(INFO, "waiting for creation of big table") {
    ASSERT_OK(WaitForRunningTabletCount(cluster_->mini_master(), table_name,
                                       FLAGS_num_test_tablets, &resp));
  }

  // Test asking for 0 tablets, should fail
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 3. Asking for zero tablets...";
  LOG_TIMING(INFO, "asking for zero tablets") {
    req.Clear();
    resp.Clear();
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(0);
    Status s = cluster_->mini_master()->catalog_manager().GetTableLocations(&req, &resp);
    ASSERT_STR_CONTAINS(s.ToString(), "must be greater than 0");
  }

  // Ask for one, get one, verify
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 4. Asking for one tablet...";
  LOG_TIMING(INFO, "asking for one tablet") {
    req.Clear();
    resp.Clear();
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(1);
    ASSERT_OK(cluster_->mini_master()->catalog_manager().GetTableLocations(&req, &resp));
    ASSERT_EQ(resp.tablet_locations_size(), 1);
    // empty since it's the first
    ASSERT_EQ(resp.tablet_locations(0).partition().partition_key_start(), "");
    ASSERT_EQ(resp.tablet_locations(0).partition().partition_key_end(), string("\x80\0\0\1", 4));
  }

  int half_tablets = FLAGS_num_test_tablets / 2;
  // Ask for half of them, get that number back
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 5. Asking for half the tablets...";
  LOG_TIMING(INFO, "asking for half the tablets") {
    req.Clear();
    resp.Clear();
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(half_tablets);
    ASSERT_OK(cluster_->mini_master()->catalog_manager().GetTableLocations(&req, &resp));
    ASSERT_EQ(half_tablets, resp.tablet_locations_size());
  }

  // Ask for all of them, get that number back
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 6. Asking for all the tablets...";
  LOG_TIMING(INFO, "asking for all the tablets") {
    req.Clear();
    resp.Clear();
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(FLAGS_num_test_tablets);
    ASSERT_OK(cluster_->mini_master()->catalog_manager().GetTableLocations(&req, &resp));
    ASSERT_EQ(FLAGS_num_test_tablets, resp.tablet_locations_size());
  }

  LOG(INFO) << "========================================================";
  LOG(INFO) << "Tables and tablets:";
  LOG(INFO) << "========================================================";
  auto tables = cluster_->mini_master()->catalog_manager().GetTables(
      master::GetTablesMode::kAll);
  for (const scoped_refptr<master::TableInfo>& table_info : tables) {
    LOG(INFO) << "Table: " << table_info->ToString();
    auto tablets = ASSERT_RESULT(table_info->GetTablets());
    for (const auto& tablet_info : tablets) {
      auto l_tablet = tablet_info->LockForRead();
      const master::SysTabletsEntryPB& metadata = l_tablet->pb;
      LOG(INFO) << "  Tablet: " << tablet_info->ToString()
                << " { start_key: "
                << ((metadata.partition().has_partition_key_start())
                    ? metadata.partition().partition_key_start() : "<< none >>")
                << ", end_key: "
                << ((metadata.partition().has_partition_key_end())
                    ? metadata.partition().partition_key_end() : "<< none >>")
                << ", running = " << tablet_info->metadata().state().is_running() << " }";
    }
    ASSERT_EQ(FLAGS_num_test_tablets, tablets.size());
  }
  LOG(INFO) << "========================================================";
}

// Creates tables and reloads on-disk metadata concurrently to test for races
// between the two operations.
TEST_F(CreateTableStressTest, TestConcurrentCreateTableAndReloadMetadata) {
  // Because of sys catalog reload we could get into situation where tablet creation task was
  // lost. Since we don't retry creating tablet on catalog load, we are getting into situation where
  // table remains in PREPARING state forever. It breaks cluster verifier.
  verify_cluster_before_next_tear_down_ = false;

  // This test continuously reloads the sys catalog. These reloads cancel all inflight tasks, which
  // can cancel some create replica tasks for tablet replicas. Given this test uses RF=3, if two
  // create tablet requests succeed then a tablet leader will be elected and the master will
  // transition the tablet to RUNNING state, preventing further tasks to fix the tablet. From here
  // the tablet leader will initiate a remote bootstrap to create a tablet replica on the tablet
  // peer missing the tablet. We explicitly enable remote bootstraps here so the tablet leader
  // can successfully create the missing replica.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_remote_bootstrap) = true;
  AtomicBool stop(false);

  // Since this test constantly invokes VisitSysCatalog() which is the function
  // that runs after a new leader gets elected, during that period the leader rejects
  // tablet server heart-beats (because it holds the leader_lock_), and this leads
  // the master to mistakenly think that the tablet servers are dead. To avoid this
  // increase the TS unresponsive timeout so that the leader correctly thinks that
  // they are alive.
  SetAtomicFlag(5 * 60 * 1000, &FLAGS_tserver_unresponsive_timeout_ms);

  thread reload_metadata_thread([&]() {
    while (!stop.Load()) {
      master::SysCatalogLoadingState state{ master::LeaderEpoch(1) };
      CHECK_OK(cluster_->mini_master()->catalog_manager_impl().VisitSysCatalog(&state));
      // Give table creation a chance to run.
      SleepFor(MonoDelta::FromMilliseconds(10 * kTimeMultiplier));
    }
  });

  auto se = ScopeExit([&stop, &reload_metadata_thread] {
    stop.Store(true);
    reload_metadata_thread.join();
  });

  for (int num_tables_created = 0; num_tables_created < 20;) {
    YBTableName table_name(
        YQL_DATABASE_CQL, "my_keyspace", Substitute("test-$0", num_tables_created));
    LOG(INFO) << "Creating table " << table_name.ToString();
    Status s = client_->CreateNamespaceIfNotExists(table_name.namespace_name(),
                                                   table_name.namespace_type());
    if (s.ok()) {
      unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
      s = table_creator->table_name(table_name)
          .schema(&schema_)
          .hash_schema(dockv::YBHashSchema::kMultiColumnHash)
          .set_range_partition_columns({ "key" })
          .num_tablets(1)
          .wait(false)
          .Create();
    }
    if (s.IsServiceUnavailable()) {
      // The master was busy reloading its metadata. Try again.
      //
      // This is a purely synthetic case. In real life, it only manifests at
      // startup (single master) or during leader failover (multiple masters).
      // In the latter case, the client will transparently retry to another
      // master. That won't happen here as we've only got one master, so we
      // must handle retrying ourselves.
      continue;
    }
    ASSERT_OK(s);
    num_tables_created++;
    LOG(INFO) << "Total created: " << num_tables_created;
  }
}

}  // namespace yb
