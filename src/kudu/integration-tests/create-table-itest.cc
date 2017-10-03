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

#include <gflags/gflags.h>
#include <glog/stl_logging.h>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "kudu/client/client-test-util.h"
#include "kudu/common/wire_protocol-test-util.h"
#include "kudu/integration-tests/external_mini_cluster-itest-base.h"
#include "kudu/util/metrics.h"

using std::multimap;
using std::set;
using std::string;
using std::vector;

METRIC_DECLARE_entity(server);
METRIC_DECLARE_histogram(handler_latency_kudu_tserver_TabletServerAdminService_CreateTablet);
METRIC_DECLARE_histogram(handler_latency_kudu_tserver_TabletServerAdminService_DeleteTablet);

namespace kudu {

const char* const kTableName = "test-table";

class CreateTableITest : public ExternalMiniClusterITestBase {
};

// Regression test for an issue seen when we fail to create a majority of the
// replicas in a tablet. Previously, we'd still consider the tablet "RUNNING"
// on the master and finish the table creation, even though that tablet would
// be stuck forever with its minority never able to elect a leader.
TEST_F(CreateTableITest, TestCreateWhenMajorityOfReplicasFailCreation) {
  const int kNumReplicas = 3;
  vector<string> ts_flags;
  vector<string> master_flags;
  master_flags.push_back("--tablet_creation_timeout_ms=1000");
  NO_FATALS(StartCluster(ts_flags, master_flags, kNumReplicas));

  // Shut down 2/3 of the tablet servers.
  cluster_->tablet_server(1)->Shutdown();
  cluster_->tablet_server(2)->Shutdown();

  // Try to create a single-tablet table.
  // This won't succeed because we can't create enough replicas to get
  // a quorum.
  gscoped_ptr<client::KuduTableCreator> table_creator(client_->NewTableCreator());
  client::KuduSchema client_schema(client::KuduSchemaFromSchema(GetSimpleTestSchema()));
  ASSERT_OK(table_creator->table_name(kTableName)
            .schema(&client_schema)
            .num_replicas(3)
            .wait(false)
            .Create());

  // Sleep until we've seen a couple retries on our live server.
  int64_t num_create_attempts = 0;
  while (num_create_attempts < 3) {
    SleepFor(MonoDelta::FromMilliseconds(100));
    ASSERT_OK(cluster_->tablet_server(0)->GetInt64Metric(
        &METRIC_ENTITY_server,
        "kudu.tabletserver",
        &METRIC_handler_latency_kudu_tserver_TabletServerAdminService_CreateTablet,
        "total_count",
        &num_create_attempts));
    LOG(INFO) << "Waiting for the master to retry creating the tablet 3 times... "
              << num_create_attempts << " RPCs seen so far";

    // The CreateTable operation should still be considered in progress, even though
    // we'll be successful at creating a single replica.
    bool in_progress = false;
    ASSERT_OK(client_->IsCreateTableInProgress(kTableName, &in_progress));
    ASSERT_TRUE(in_progress);
  }

  // Once we restart the servers, we should succeed at creating a healthy
  // replicated tablet.
  ASSERT_OK(cluster_->tablet_server(1)->Restart());
  ASSERT_OK(cluster_->tablet_server(2)->Restart());

  // We should eventually finish the table creation we started earlier.
  bool in_progress = false;
  while (in_progress) {
    LOG(INFO) << "Waiting for the master to successfully create the table...";
    ASSERT_OK(client_->IsCreateTableInProgress(kTableName, &in_progress));
    SleepFor(MonoDelta::FromMilliseconds(100));
  }

  // The server that was up from the beginning should be left with only
  // one tablet, eventually, since the tablets which failed to get created
  // properly should get deleted.
  vector<string> tablets;
  int wait_iter = 0;
  while (tablets.size() != 1 && wait_iter++ < 100) {
    LOG(INFO) << "Waiting for only one tablet to be left on TS 0. Currently have: "
              << tablets;
    SleepFor(MonoDelta::FromMilliseconds(100));
    tablets = inspect_->ListTabletsWithDataOnTS(0);
  }
  ASSERT_EQ(1, tablets.size()) << "Tablets on TS0: " << tablets;
}

// Regression test for KUDU-1317. Ensure that, when a table is created,
// the tablets are well spread out across the machines in the cluster and
// that recovery from failures will be well parallelized.
TEST_F(CreateTableITest, TestSpreadReplicasEvenly) {
  const int kNumServers = 10;
  const int kNumTablets = 20;
  vector<string> ts_flags;
  vector<string> master_flags;
  ts_flags.push_back("--never_fsync"); // run faster on slow disks
  NO_FATALS(StartCluster(ts_flags, master_flags, kNumServers));

  gscoped_ptr<client::KuduTableCreator> table_creator(client_->NewTableCreator());
  client::KuduSchema client_schema(client::KuduSchemaFromSchema(GetSimpleTestSchema()));
  ASSERT_OK(table_creator->table_name(kTableName)
            .schema(&client_schema)
            .num_replicas(3)
            .add_hash_partitions({ "key" }, kNumTablets)
            .Create());

  // Check that the replicas are fairly well spread by computing the standard
  // deviation of the number of replicas per server.
  const double kMeanPerServer = kNumTablets * 3.0 / kNumServers;
  double sum_squared_deviation = 0;
  vector<int> tablet_counts;
  for (int ts_idx = 0; ts_idx < kNumServers; ts_idx++) {
    int num_replicas = inspect_->ListTabletsOnTS(ts_idx).size();
    LOG(INFO) << "TS " << ts_idx << " has " << num_replicas << " tablets";
    double deviation = static_cast<double>(num_replicas) - kMeanPerServer;
    sum_squared_deviation += deviation * deviation;
  }
  double stddev = sqrt(sum_squared_deviation / (kMeanPerServer - 1));
  LOG(INFO) << "stddev = " << stddev;
  // In 1000 runs of the test, only one run had stddev above 2.0. So, 3.0 should
  // be a safe non-flaky choice.
  ASSERT_LE(stddev, 3.0);

  // Construct a map from tablet ID to the set of servers that each tablet is hosted on.
  multimap<string, int> tablet_to_servers;
  for (int ts_idx = 0; ts_idx < kNumServers; ts_idx++) {
    vector<string> tablets = inspect_->ListTabletsOnTS(ts_idx);
    for (const string& tablet_id : tablets) {
      tablet_to_servers.insert(std::make_pair(tablet_id, ts_idx));
    }
  }

  // For each server, count how many other servers it shares tablets with.
  // This is highly correlated to how well parallelized recovery will be
  // in the case the server crashes.
  int sum_num_peers = 0;
  for (int ts_idx = 0; ts_idx < kNumServers; ts_idx++) {
    vector<string> tablets = inspect_->ListTabletsOnTS(ts_idx);
    set<int> peer_servers;
    for (const string& tablet_id : tablets) {
      auto peer_indexes = tablet_to_servers.equal_range(tablet_id);
      for (auto it = peer_indexes.first; it != peer_indexes.second; ++it) {
        peer_servers.insert(it->second);
      }
    }

    peer_servers.erase(ts_idx);
    LOG(INFO) << "Server " << ts_idx << " has " << peer_servers.size() << " peers";
    sum_num_peers += peer_servers.size();
  }

  // On average, servers should have at least half the other servers as peers.
  double avg_num_peers = static_cast<double>(sum_num_peers) / kNumServers;
  LOG(INFO) << "avg_num_peers = " << avg_num_peers;
  ASSERT_GE(avg_num_peers, kNumServers / 2);
}

} // namespace kudu
