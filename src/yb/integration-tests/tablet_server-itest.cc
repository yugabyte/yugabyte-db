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

#include "yb/util/logging.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/ts_itest-base.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/util/status_log.h"
#include "yb/client/table.h"
#include "yb/consensus/log-test-base.h"
#include "yb/util/test_thread_holder.h"

DECLARE_bool(TEST_simulate_fs_create_with_empty_uuid);
DECLARE_int32(num_replicas);
DECLARE_int32(num_tablet_servers);
DECLARE_uint64(initial_log_segment_size_bytes);

using std::string;

namespace yb {
namespace tserver {

class TabletServerITest : public TabletServerIntegrationTestBase {
};

// Given a host:port string, return the host part as a string.
std::string GetHost(const std::string& val) {
  return CHECK_RESULT(HostPort::FromString(val, 0)).host();
}

TEST_F(TabletServerITest, TestCrashBeforeWritingWALHeader) {
  FLAGS_num_tablet_servers = 1;
  FLAGS_num_replicas = 1;
  BuildAndStart(std::vector<string>{"--log_async_preallocate_segments=false",
      "--log_preallocate_segments=false"});
  auto kTimeout = MonoDelta::FromSeconds(10);
  auto* ts = cluster_->tablet_server(0);
  auto* ts_details = tablet_servers_[ts->instance_id().permanent_uuid()].get();
  WaitForTSAndReplicas();
  ASSERT_OK(StartElection(ts_details, tablet_id_, kTimeout));
  ASSERT_OK(WaitUntilCommittedOpIdIndexIs(1,
                  ts_details, tablet_id_, kTimeout));

  // Trigger WAL rollover, so TServer can crash in Log::SwitchToAllocatedSegment().
  ASSERT_OK(cluster_->SetFlag(ts, "TEST_crash_before_wal_header_is_written", "true"));
  WriteRequestPB req;
  WriteResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(10000));
  req.set_tablet_id(tablet_id_);
  std::string test_payload(FLAGS_initial_log_segment_size_bytes+1, '0');
  AddTestRowInsert(0, 0, test_payload, &req);
  ASSERT_NOK(ts_details->tserver_proxy->Write(req, &resp, &rpc));
  ASSERT_OK(cluster_->WaitForTSToCrash(ts, kTimeout));
  // TServer should successfully finish the bootstrap,
  // even though a tablet's last WAL file doesn't have header.
  ASSERT_OK(ts->Restart(ExternalMiniClusterOptions::kDefaultStartCqlProxy,
      std::vector<std::pair<string, string>>{{"TEST_crash_before_wal_header_is_written",
                                              "false"}}));
  ASSERT_OK(WaitUntilTabletRunning(ts_details, tablet_id_, kTimeout));
}

TEST_F(TabletServerITest, TestNumberOfSegmentInCrashloop) {
  // Instead of safe shutdown, we will just kill the process to simulate crash.
  FLAGS_num_tablet_servers = 1;
  FLAGS_num_replicas = 1;
  // Enable reuse log, threshold will be maximized in testing mode.
  BuildAndStart(std::vector<string>{"--reuse_unclosed_segment_threshold_bytes=524288"});
  ExternalTabletServer* ts = cluster_->tablet_server(0);
  itest::TServerDetails* ts_details = tablet_servers_[ts->instance_id().permanent_uuid()].get();
  string wal_dir = JoinPathSegments(cluster_->data_root(), ts->id(), "yb-data", "tserver", "wals",
                      Substitute("table-$0", table_->id()), Substitute("tablet-$0", tablet_id_));

  // Simulate crashloop.
  int restart_num = 50;
  // Make sure segment number doesn't get increase in a crashloop
  for (int i = 1; i <= restart_num; i++) {
    WaitForTSAndReplicas();
    ASSERT_OK(StartElection(ts_details, tablet_id_, MonoDelta::FromSeconds(10)));
    ASSERT_OK(WaitUntilCommittedOpIdIndexIs(i,
                  ts_details, tablet_id_, MonoDelta::FromSeconds(10)));
    // The number of segments should remain 1.
    auto current_segment_size = ASSERT_RESULT(log::GetSegmentsCount(wal_dir));
    ASSERT_EQ(1, current_segment_size);

    ts->Shutdown();
    ASSERT_OK(cluster_->WaitForTSToCrash(ts, MonoDelta::FromSeconds(10)));
    // Restart the server.
    ASSERT_OK(ts->Restart());
  }
  // Every time server starts, it will Initiate a NO_OP operation.
  // So, we expect that the number of entries is equal to restart_num.
  auto num_entries = ASSERT_RESULT(log::GetEntries(wal_dir));
  ASSERT_EQ(restart_num, num_entries);
}

TEST_F(TabletServerITest, TestTServerCrashWithEmptyUUID) {
  // Testing tablet server crash at startup because of empty UUID
  FLAGS_TEST_simulate_fs_create_with_empty_uuid = true;
  auto mini_ts =
      MiniTabletServer::CreateMiniTabletServer(GetTestPath("TabletServerTest-fsroot"), 0);
  CHECK_OK(mini_ts);
  mini_server_ = std::move(*mini_ts);
  auto status = mini_server_->Start(tserver::WaitTabletsBootstrapped::kFalse);
  ASSERT_TRUE(status.IsCorruption());
}

TEST_F(TabletServerITest, TestTserverInitWaitsForMasterLeader) {
  FLAGS_num_tablet_servers = 1;
  FLAGS_num_replicas = 1;
  CreateCluster("tablet_server-itest-cluster");
  ASSERT_EQ(cluster_->num_tablet_servers(), 1);
  ASSERT_EQ(cluster_->num_masters(), 1);

  // shutdown master
  auto master = cluster_->master();
  auto ts = cluster_->tablet_server(0);
  master->Shutdown();
  ts->Shutdown();
  auto test_thread_holder = TestThreadHolder();

  test_thread_holder.AddThread([&]() { ASSERT_OK(ts->Restart()); });
  test_thread_holder.AddThread([&]() {
    std::this_thread::sleep_for(45s);
    ASSERT_OK(master->Start());
  });

  test_thread_holder.JoinAll();
}

TEST_F(TabletServerITest, TestProxyAddrs) {
  CreateCluster("tablet_server-itest-cluster");

  for (int i = 0; i < FLAGS_num_tablet_servers; i++) {
    auto tserver = cluster_->tablet_server(i);
    auto rpc_host = GetHost(ASSERT_RESULT(tserver->GetFlag("rpc_bind_addresses")));
    for (auto flag : {"cql_proxy_bind_address",
                      "redis_proxy_bind_address",
                      "pgsql_proxy_bind_address"}) {
      auto host = GetHost(ASSERT_RESULT(tserver->GetFlag(flag)));
      ASSERT_EQ(host, rpc_host);
    }
  }
}

TEST_F(TabletServerITest, TestProxyAddrsNonDefault) {
  std::vector<string> ts_flags, master_flags;
  ts_flags.push_back("--cql_proxy_bind_address=127.0.0.1${index}");
  std::unique_ptr<FileLock> redis_port_file_lock;
  auto redis_port = GetFreePort(&redis_port_file_lock);
  ts_flags.push_back("--redis_proxy_bind_address=127.0.0.2${index}:" + std::to_string(redis_port));
  std::unique_ptr<FileLock> pgsql_port_file_lock;
  auto pgsql_port = GetFreePort(&pgsql_port_file_lock);
  ts_flags.push_back("--pgsql_proxy_bind_address=127.0.0.3${index}:" + std::to_string(pgsql_port));

  CreateCluster("ts-itest-cluster-nd", ts_flags, master_flags);

  for (int i = 0; i < FLAGS_num_tablet_servers; i++) {
    auto tserver = cluster_->tablet_server(i);
    auto rpc_host = GetHost(ASSERT_RESULT(tserver->GetFlag("rpc_bind_addresses")));

    auto res = GetHost(ASSERT_RESULT(tserver->GetFlag("cql_proxy_bind_address")));
    ASSERT_EQ(res, Format("127.0.0.1$0", i));
    ASSERT_NE(res, rpc_host);

    res = GetHost(ASSERT_RESULT(tserver->GetFlag("redis_proxy_bind_address")));
    ASSERT_EQ(res, Format("127.0.0.2$0", i));
    ASSERT_NE(res, rpc_host);

    res = GetHost(ASSERT_RESULT(tserver->GetFlag("pgsql_proxy_bind_address")));
    ASSERT_EQ(res, Format("127.0.0.3$0", i));
    ASSERT_NE(res, rpc_host);
  }
}

}  // namespace tserver
}  // namespace yb
