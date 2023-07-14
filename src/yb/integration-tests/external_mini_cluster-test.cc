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

#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/util/metrics.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/test_util.h"

using std::string;

METRIC_DECLARE_entity(server);
METRIC_DECLARE_gauge_uint64(threads_running);

namespace yb {

class EMCTest : public YBTest {
 public:
  EMCTest() {
    master_peer_ports_ = { 0, 0, 0 };
  }

 protected:
  std::vector<uint16_t> master_peer_ports_;
};

TEST_F(EMCTest, TestBasicOperation) {
  ExternalMiniClusterOptions opts;
  opts.num_masters = master_peer_ports_.size();
  opts.num_tablet_servers = 3;
  opts.master_rpc_ports = master_peer_ports_;

  ExternalMiniCluster cluster(opts);
  ASSERT_OK(cluster.Start());

  // Verify each of the masters.
  for (size_t i = 0; i < opts.num_masters; i++) {
    SCOPED_TRACE(i);
    ExternalMaster* master = CHECK_NOTNULL(cluster.master(i));
    HostPort master_rpc = master->bound_rpc_hostport();
    EXPECT_TRUE(HasPrefixString(master_rpc.ToString(), "127.0.0.1:")) << master_rpc.ToString();

    HostPort master_http = master->bound_http_hostport();
    EXPECT_TRUE(HasPrefixString(master_http.ToString(), "127.0.0.1:")) << master_http.ToString();

    // Retrieve a thread metric, which should always be present on any master.
    int64_t value = ASSERT_RESULT(master->GetMetric<int64>(
        &METRIC_ENTITY_server,
        "yb.master",
        &METRIC_threads_running,
        "value"));
    LOG(INFO) << "Master " << i << ": " << METRIC_threads_running.name() << '=' << value;
    EXPECT_GT(value, 0);
  }

  // Verify each of the tablet servers.
  for (size_t i = 0; i < opts.num_tablet_servers; i++) {
    SCOPED_TRACE(i);
    const ExternalTabletServer* const ts = CHECK_NOTNULL(cluster.tablet_server(i));
    const HostPort ts_rpc = ts->bound_rpc_hostport();
    const HostPort ts_http = ts->bound_http_hostport();
    const string expected_prefix = strings::Substitute("$0:", cluster.GetBindIpForTabletServer(i));

    // Let TS 0 be on 127.0.0.1 address on MAC.
    if (opts.bind_to_unique_loopback_addresses && i > 0) {
      EXPECT_NE(expected_prefix, "127.0.0.1:") << "Should bind to unique per-server hosts";
    }

    EXPECT_TRUE(HasPrefixString(ts_rpc.ToString(), expected_prefix)) << ts_rpc.ToString();
    EXPECT_TRUE(HasPrefixString(ts_http.ToString(), expected_prefix)) << ts_http.ToString();

    // Retrieve a thread metric, which should always be present on any TS.
    int64_t value = ASSERT_RESULT(ts->GetMetric<int64>(
        &METRIC_ENTITY_server, "yb.tabletserver", &METRIC_threads_running, "value"));
    LOG(INFO) << "TServer " << i << ": " << METRIC_threads_running.name() << '=' << value;
    EXPECT_GT(value, 0);
  }

  // Restart a master and a tablet server. Make sure they come back up with the same ports.
  ExternalMaster* master = cluster.master(0);
  HostPort master_rpc = master->bound_rpc_hostport();
  HostPort master_http = master->bound_http_hostport();

  master->Shutdown();
  ASSERT_OK(master->Restart());

  ASSERT_EQ(master_rpc.ToString(), master->bound_rpc_hostport().ToString());
  ASSERT_EQ(master_http.ToString(), master->bound_http_hostport().ToString());

  ExternalTabletServer* ts = cluster.tablet_server(0);

  HostPort ts_rpc = ts->bound_rpc_hostport();
  HostPort ts_http = ts->bound_http_hostport();

  ts->Shutdown();
  ASSERT_OK(ts->Restart());

  ASSERT_EQ(ts_rpc.ToString(), ts->bound_rpc_hostport().ToString());
  ASSERT_EQ(ts_http.ToString(), ts->bound_http_hostport().ToString());

  cluster.Shutdown();
}

TEST_F(EMCTest, TestUniquePorts) {
  ExternalMiniClusterOptions opts;
  ExternalMiniCluster cluster(opts);

  std::set<uint16_t> ports;
  for (int idx = 0; idx < 100; idx++) {
    uint16_t port = cluster.AllocateFreePort();
    if (ports.count(port) == 0) {
      LOG(INFO) << "allocated port: " << port;
      ports.insert(port);
    } else {
      FAIL() << "port: " << port << " already allocated.";
    }
  }
}

TEST_F(EMCTest, TestYSQLShutdown) {
  ExternalMiniClusterOptions opts;
  opts.num_masters = master_peer_ports_.size();
  opts.num_tablet_servers = 3;
  opts.master_rpc_ports = master_peer_ports_;
  opts.enable_ysql = true;

  ExternalMiniCluster cluster(opts);
  ASSERT_OK(cluster.Start());

  cluster.Shutdown();
  for (const auto& server : cluster.daemons()) {
    if (server) {
      ASSERT_FALSE(server->WasUnsafeShutdown());
    }
  }
}

TEST_F(EMCTest, TestCallHomeCrash) {
  ExternalMiniClusterOptions opts;
  opts.num_masters = 1;
  opts.num_tablet_servers = 1;
  for (auto* server_flags : {&opts.extra_master_flags, &opts.extra_tserver_flags}) {
    server_flags->push_back("--callhome_interval_secs=1");
    server_flags->push_back("--callhome_url=dummy_url");
    server_flags->push_back("--callhome_enabled=true");
    server_flags->push_back("--TEST_callhome_destructor_sleep_ms=10000");
  }

  ExternalMiniCluster cluster(opts);
  ASSERT_OK(cluster.Start());

  // Require exit code 0 from Shutdown to assert that we did not crash.
  ASSERT_NO_FATALS(cluster.Shutdown(
      ExternalMiniCluster::NodeSelectionMode::ALL, RequireExitCode0::kTrue));
}

} // namespace yb
