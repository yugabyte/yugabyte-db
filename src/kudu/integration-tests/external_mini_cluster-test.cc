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

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "kudu/integration-tests/external_mini_cluster.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/util/metrics.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/test_util.h"

METRIC_DECLARE_entity(server);
METRIC_DECLARE_gauge_uint64(threads_running);

namespace kudu {

class EMCTest : public KuduTest {
 public:
  EMCTest() {
    // Hard-coded RPC ports for the masters. This is safe, as this unit test
    // runs under a resource lock (see CMakeLists.txt in this directory).
    // TODO we should have a generic method to obtain n free ports.
    master_peer_ports_ = { 11010, 11011, 11012 };
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
  for (int i = 0; i < opts.num_masters; i++) {
    SCOPED_TRACE(i);
    ExternalMaster* master = CHECK_NOTNULL(cluster.master(i));
    HostPort master_rpc = master->bound_rpc_hostport();
    EXPECT_TRUE(HasPrefixString(master_rpc.ToString(), "127.0.0.1:")) << master_rpc.ToString();

    HostPort master_http = master->bound_http_hostport();
    EXPECT_TRUE(HasPrefixString(master_http.ToString(), "127.0.0.1:")) << master_http.ToString();

    // Retrieve a thread metric, which should always be present on any master.
    int64_t value;
    ASSERT_OK(master->GetInt64Metric(&METRIC_ENTITY_server,
                                     "kudu.master",
                                     &METRIC_threads_running,
                                     "value",
                                     &value));
    EXPECT_GT(value, 0);
  }

  // Verify each of the tablet servers.
  for (int i = 0; i < opts.num_tablet_servers; i++) {
    SCOPED_TRACE(i);
    ExternalTabletServer* ts = CHECK_NOTNULL(cluster.tablet_server(i));
    HostPort ts_rpc = ts->bound_rpc_hostport();
    string expected_prefix = strings::Substitute("$0:", cluster.GetBindIpForTabletServer(i));
    EXPECT_NE(expected_prefix, "127.0.0.1") << "Should bind to unique per-server hosts";
    EXPECT_TRUE(HasPrefixString(ts_rpc.ToString(), expected_prefix)) << ts_rpc.ToString();

    HostPort ts_http = ts->bound_http_hostport();
    EXPECT_TRUE(HasPrefixString(ts_http.ToString(), expected_prefix)) << ts_http.ToString();

    // Retrieve a thread metric, which should always be present on any TS.
    int64_t value;
    ASSERT_OK(ts->GetInt64Metric(&METRIC_ENTITY_server,
                                 "kudu.tabletserver",
                                 &METRIC_threads_running,
                                 "value",
                                 &value));
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

} // namespace kudu
