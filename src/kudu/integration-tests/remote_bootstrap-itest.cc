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

#include <boost/optional.hpp>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <string>
#include <unordered_map>

#include "kudu/client/client-test-util.h"
#include "kudu/client/client.h"
#include "kudu/common/wire_protocol-test-util.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/integration-tests/cluster_itest_util.h"
#include "kudu/integration-tests/cluster_verifier.h"
#include "kudu/integration-tests/external_mini_cluster.h"
#include "kudu/integration-tests/external_mini_cluster_fs_inspector.h"
#include "kudu/integration-tests/test_workload.h"
#include "kudu/tablet/tablet_bootstrap.h"
#include "kudu/tablet/tablet_metadata.h"
#include "kudu/tserver/remote_bootstrap_client.h"
#include "kudu/util/metrics.h"
#include "kudu/util/pstack_watcher.h"
#include "kudu/util/test_util.h"

DEFINE_int32(test_delete_leader_num_iters, 3,
             "Number of iterations to run in TestDeleteLeaderDuringRemoteBootstrapStressTest.");
DEFINE_int32(test_delete_leader_min_rows_per_iter, 20,
             "Number of writer threads in TestDeleteLeaderDuringRemoteBootstrapStressTest.");
DEFINE_int32(test_delete_leader_payload_bytes, 16 * 1024,
             "Payload byte size in TestDeleteLeaderDuringRemoteBootstrapStressTest.");
DEFINE_int32(test_delete_leader_num_writer_threads, 1,
             "Number of writer threads in TestDeleteLeaderDuringRemoteBootstrapStressTest.");

using kudu::client::KuduClient;
using kudu::client::KuduClientBuilder;
using kudu::client::KuduSchema;
using kudu::client::KuduSchemaFromSchema;
using kudu::client::KuduTableCreator;
using kudu::client::sp::shared_ptr;
using kudu::consensus::CONSENSUS_CONFIG_COMMITTED;
using kudu::itest::TServerDetails;
using kudu::tablet::TABLET_DATA_TOMBSTONED;
using kudu::tserver::ListTabletsResponsePB;
using kudu::tserver::RemoteBootstrapClient;
using std::string;
using std::unordered_map;
using std::vector;
using strings::Substitute;

METRIC_DECLARE_entity(server);
METRIC_DECLARE_histogram(handler_latency_kudu_consensus_ConsensusService_UpdateConsensus);
METRIC_DECLARE_counter(glog_info_messages);
METRIC_DECLARE_counter(glog_warning_messages);
METRIC_DECLARE_counter(glog_error_messages);

namespace kudu {

class RemoteBootstrapITest : public KuduTest {
 public:
  virtual void TearDown() OVERRIDE {
    if (HasFatalFailure()) {
      LOG(INFO) << "Found fatal failure";
      for (int i = 0; i < 3; i++) {
        if (!cluster_->tablet_server(i)->IsProcessAlive()) {
          LOG(INFO) << "Tablet server " << i << " is not running. Cannot dump its stacks.";
          continue;
        }
        LOG(INFO) << "Attempting to dump stacks of TS " << i
                  << " with UUID " << cluster_->tablet_server(i)->uuid()
                  << " and pid " << cluster_->tablet_server(i)->pid();
        WARN_NOT_OK(PstackWatcher::DumpPidStacks(cluster_->tablet_server(i)->pid()),
                    "Couldn't dump stacks");
      }
    }
    if (cluster_) cluster_->Shutdown();
    KuduTest::TearDown();
    STLDeleteValues(&ts_map_);
  }

 protected:
  void StartCluster(const vector<string>& extra_tserver_flags = vector<string>(),
                    const vector<string>& extra_master_flags = vector<string>(),
                    int num_tablet_servers = 3);

  gscoped_ptr<ExternalMiniCluster> cluster_;
  gscoped_ptr<itest::ExternalMiniClusterFsInspector> inspect_;
  shared_ptr<KuduClient> client_;
  unordered_map<string, TServerDetails*> ts_map_;
};

void RemoteBootstrapITest::StartCluster(const vector<string>& extra_tserver_flags,
                                        const vector<string>& extra_master_flags,
                                        int num_tablet_servers) {
  ExternalMiniClusterOptions opts;
  opts.num_tablet_servers = num_tablet_servers;
  opts.extra_tserver_flags = extra_tserver_flags;
  opts.extra_tserver_flags.push_back("--never_fsync"); // fsync causes flakiness on EC2.
  opts.extra_master_flags = extra_master_flags;
  cluster_.reset(new ExternalMiniCluster(opts));
  ASSERT_OK(cluster_->Start());
  inspect_.reset(new itest::ExternalMiniClusterFsInspector(cluster_.get()));
  ASSERT_OK(itest::CreateTabletServerMap(cluster_->master_proxy().get(),
                                          cluster_->messenger(),
                                          &ts_map_));
  KuduClientBuilder builder;
  ASSERT_OK(cluster_->CreateClient(builder, &client_));
}

// If a rogue (a.k.a. zombie) leader tries to remote bootstrap a tombstoned
// tablet, make sure its term isn't older than the latest term we observed.
// If it is older, make sure we reject the request, to avoid allowing old
// leaders to create a parallel universe. This is possible because config
// change could cause nodes to move around. The term check is reasonable
// because only one node can be elected leader for a given term.
//
// A leader can "go rogue" due to a VM pause, CTRL-z, partition, etc.
TEST_F(RemoteBootstrapITest, TestRejectRogueLeader) {
  // This test pauses for at least 10 seconds. Only run in slow-test mode.
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipping test in fast-test mode.";
    return;
  }

  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--enable_leader_failure_detection=false");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  NO_FATALS(StartCluster(ts_flags, master_flags));

  const MonoDelta timeout = MonoDelta::FromSeconds(30);
  const int kTsIndex = 0; // We'll test with the first TS.
  TServerDetails* ts = ts_map_[cluster_->tablet_server(kTsIndex)->uuid()];

  TestWorkload workload(cluster_.get());
  workload.Setup();

  // Figure out the tablet id of the created tablet.
  vector<ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  ASSERT_OK(WaitForNumTabletsOnTS(ts, 1, timeout, &tablets));
  string tablet_id = tablets[0].tablet_status().tablet_id();

  // Wait until all replicas are up and running.
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(i)->uuid()],
                                            tablet_id, timeout));
  }

  // Elect a leader for term 1, then run some data through the cluster.
  int zombie_leader_index = 1;
  string zombie_leader_uuid = cluster_->tablet_server(zombie_leader_index)->uuid();
  ASSERT_OK(itest::StartElection(ts_map_[zombie_leader_uuid], tablet_id, timeout));
  workload.Start();
  while (workload.rows_inserted() < 100) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  workload.StopAndJoin();

  ASSERT_OK(WaitForServersToAgree(timeout, ts_map_, tablet_id, workload.batches_completed()));

  // Come out of the blue and try to remotely bootstrap a running server while
  // specifying an old term. That running server should reject the request.
  // We are essentially masquerading as a rogue leader here.
  Status s = itest::StartRemoteBootstrap(ts, tablet_id, zombie_leader_uuid,
                                         HostPort(cluster_->tablet_server(1)->bound_rpc_addr()),
                                         0, // Say I'm from term 0.
                                         timeout);
  ASSERT_TRUE(s.IsInvalidArgument());
  ASSERT_STR_CONTAINS(s.ToString(), "term 0 lower than last logged term 1");

  // Now pause the actual leader so we can bring him back as a zombie later.
  ASSERT_OK(cluster_->tablet_server(zombie_leader_index)->Pause());

  // Trigger TS 2 to become leader of term 2.
  int new_leader_index = 2;
  string new_leader_uuid = cluster_->tablet_server(new_leader_index)->uuid();
  ASSERT_OK(itest::StartElection(ts_map_[new_leader_uuid], tablet_id, timeout));
  ASSERT_OK(itest::WaitUntilLeader(ts_map_[new_leader_uuid], tablet_id, timeout));

  unordered_map<string, TServerDetails*> active_ts_map = ts_map_;
  ASSERT_EQ(1, active_ts_map.erase(zombie_leader_uuid));

  // Wait for the NO_OP entry from the term 2 election to propagate to the
  // remaining nodes' logs so that we are guaranteed to reject the rogue
  // leader's remote bootstrap request when we bring it back online.
  int log_index = workload.batches_completed() + 2; // 2 terms == 2 additional NO_OP entries.
  ASSERT_OK(WaitForServersToAgree(timeout, active_ts_map, tablet_id, log_index));
  // TODO: Write more rows to the new leader once KUDU-1034 is fixed.

  // Now kill the new leader and tombstone the replica on TS 0.
  cluster_->tablet_server(new_leader_index)->Shutdown();
  ASSERT_OK(itest::DeleteTablet(ts, tablet_id, TABLET_DATA_TOMBSTONED, boost::none, timeout));

  // Zombies!!! Resume the rogue zombie leader.
  // He should attempt to remote bootstrap TS 0 but fail.
  ASSERT_OK(cluster_->tablet_server(zombie_leader_index)->Resume());

  // Loop for a few seconds to ensure that the tablet doesn't transition to READY.
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromSeconds(5));
  while (MonoTime::Now(MonoTime::FINE).ComesBefore(deadline)) {
    ASSERT_OK(itest::ListTablets(ts, timeout, &tablets));
    ASSERT_EQ(1, tablets.size());
    ASSERT_EQ(TABLET_DATA_TOMBSTONED, tablets[0].tablet_status().tablet_data_state());
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  // Force the rogue leader to step down.
  // Then, send a remote bootstrap start request from a "fake" leader that
  // sends an up-to-date term in the RB request but the actual term stored
  // in the bootstrap source's consensus metadata would still be old.
  LOG(INFO) << "Forcing rogue leader T " << tablet_id << " P " << zombie_leader_uuid
            << " to step down...";
  ASSERT_OK(itest::LeaderStepDown(ts_map_[zombie_leader_uuid], tablet_id, timeout));
  ExternalTabletServer* zombie_ets = cluster_->tablet_server(zombie_leader_index);
  // It's not necessarily part of the API but this could return faliure due to
  // rejecting the remote. We intend to make that part async though, so ignoring
  // this return value in this test.
  ignore_result(itest::StartRemoteBootstrap(ts, tablet_id, zombie_leader_uuid,
                                            HostPort(zombie_ets->bound_rpc_addr()),
                                            2, // Say I'm from term 2.
                                            timeout));

  // Wait another few seconds to be sure the remote bootstrap is rejected.
  deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromSeconds(5));
  while (MonoTime::Now(MonoTime::FINE).ComesBefore(deadline)) {
    ASSERT_OK(itest::ListTablets(ts, timeout, &tablets));
    ASSERT_EQ(1, tablets.size());
    ASSERT_EQ(TABLET_DATA_TOMBSTONED, tablets[0].tablet_status().tablet_data_state());
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
}

// Start remote bootstrap session and delete the tablet in the middle.
// It should actually be possible to complete bootstrap in such a case, because
// when a remote bootstrap session is started on the "source" server, all of
// the relevant files are either read or opened, meaning that an in-progress
// remote bootstrap can complete even after a tablet is officially "deleted" on
// the source server. This is also a regression test for KUDU-1009.
TEST_F(RemoteBootstrapITest, TestDeleteTabletDuringRemoteBootstrap) {
  MonoDelta timeout = MonoDelta::FromSeconds(10);
  const int kTsIndex = 0; // We'll test with the first TS.
  NO_FATALS(StartCluster());

  // Populate a tablet with some data.
  TestWorkload workload(cluster_.get());
  workload.Setup();
  workload.Start();
  while (workload.rows_inserted() < 1000) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  // Figure out the tablet id of the created tablet.
  vector<ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  TServerDetails* ts = ts_map_[cluster_->tablet_server(kTsIndex)->uuid()];
  ASSERT_OK(WaitForNumTabletsOnTS(ts, 1, timeout, &tablets));
  string tablet_id = tablets[0].tablet_status().tablet_id();

  // Ensure all the servers agree before we proceed.
  workload.StopAndJoin();
  ASSERT_OK(WaitForServersToAgree(timeout, ts_map_, tablet_id, workload.batches_completed()));

  // Set up an FsManager to use with the RemoteBootstrapClient.
  FsManagerOpts opts;
  string testbase = GetTestPath("fake-ts");
  ASSERT_OK(env_->CreateDir(testbase));
  opts.wal_path = JoinPathSegments(testbase, "wals");
  opts.data_paths.push_back(JoinPathSegments(testbase, "data-0"));
  gscoped_ptr<FsManager> fs_manager(new FsManager(env_.get(), opts));
  ASSERT_OK(fs_manager->CreateInitialFileSystemLayout());
  ASSERT_OK(fs_manager->Open());

  // Start up a RemoteBootstrapClient and open a remote bootstrap session.
  gscoped_ptr<RemoteBootstrapClient> rb_client(
      new RemoteBootstrapClient(tablet_id, fs_manager.get(),
                                cluster_->messenger(), fs_manager->uuid()));
  scoped_refptr<tablet::TabletMetadata> meta;
  ASSERT_OK(rb_client->Start(cluster_->tablet_server(kTsIndex)->uuid(),
                             cluster_->tablet_server(kTsIndex)->bound_rpc_hostport(),
                             &meta));

  // Tombstone the tablet on the remote!
  ASSERT_OK(itest::DeleteTablet(ts, tablet_id, TABLET_DATA_TOMBSTONED, boost::none, timeout));

  // Now finish bootstrapping!
  tablet::TabletStatusListener listener(meta);
  ASSERT_OK(rb_client->FetchAll(&listener));
  ASSERT_OK(rb_client->Finish());

  // Run destructor, which closes the remote session.
  rb_client.reset();
  SleepFor(MonoDelta::FromMilliseconds(50));  // Give a little time for a crash (KUDU-1009).
  ASSERT_TRUE(cluster_->tablet_server(kTsIndex)->IsProcessAlive());
}

// This test ensures that a leader can remote-bootstrap a tombstoned replica
// that has a higher term recorded in the replica's consensus metadata if the
// replica's last-logged opid has the same term (or less) as the leader serving
// as the remote bootstrap source. When a tablet is tombstoned, its last-logged
// opid is stored in a field its on-disk superblock.
TEST_F(RemoteBootstrapITest, TestRemoteBootstrapFollowerWithHigherTerm) {
  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--enable_leader_failure_detection=false");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  const int kNumTabletServers = 2;
  NO_FATALS(StartCluster(ts_flags, master_flags, kNumTabletServers));

  const MonoDelta timeout = MonoDelta::FromSeconds(30);
  const int kFollowerIndex = 0;
  TServerDetails* follower_ts = ts_map_[cluster_->tablet_server(kFollowerIndex)->uuid()];

  TestWorkload workload(cluster_.get());
  workload.set_num_replicas(2);
  workload.Setup();

  // Figure out the tablet id of the created tablet.
  vector<ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  ASSERT_OK(WaitForNumTabletsOnTS(follower_ts, 1, timeout, &tablets));
  string tablet_id = tablets[0].tablet_status().tablet_id();

  // Wait until all replicas are up and running.
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(i)->uuid()],
                                            tablet_id, timeout));
  }

  // Elect a leader for term 1, then run some data through the cluster.
  const int kLeaderIndex = 1;
  TServerDetails* leader_ts = ts_map_[cluster_->tablet_server(kLeaderIndex)->uuid()];
  ASSERT_OK(itest::StartElection(leader_ts, tablet_id, timeout));
  workload.Start();
  while (workload.rows_inserted() < 100) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  workload.StopAndJoin();

  ASSERT_OK(WaitForServersToAgree(timeout, ts_map_, tablet_id, workload.batches_completed()));

  // Pause the leader and increment the term on the follower by starting an
  // election on the follower. The election will fail asynchronously but we
  // just wait until we see that its term has incremented.
  ASSERT_OK(cluster_->tablet_server(kLeaderIndex)->Pause());
  ASSERT_OK(itest::StartElection(follower_ts, tablet_id, timeout));
  int64_t term = 0;
  for (int i = 0; i < 1000; i++) {
    consensus::ConsensusStatePB cstate;
    ASSERT_OK(itest::GetConsensusState(follower_ts, tablet_id, CONSENSUS_CONFIG_COMMITTED,
                                       timeout, &cstate));
    term = cstate.current_term();
    if (term == 2) break;
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  ASSERT_EQ(2, term);

  // Now tombstone the follower.
  ASSERT_OK(itest::DeleteTablet(follower_ts, tablet_id, TABLET_DATA_TOMBSTONED, boost::none,
                                timeout));

  // Restart the follower's TS so that the leader's TS won't get its queued
  // vote request messages. This is a hack but seems to work.
  cluster_->tablet_server(kFollowerIndex)->Shutdown();
  ASSERT_OK(cluster_->tablet_server(kFollowerIndex)->Restart());

  // Now wake the leader. It should detect that the follower needs to be
  // remotely bootstrapped and proceed to bring it back up to date.
  ASSERT_OK(cluster_->tablet_server(kLeaderIndex)->Resume());

  // Wait for the follower to come back up.
  ASSERT_OK(WaitForServersToAgree(timeout, ts_map_, tablet_id, workload.batches_completed()));
}

// Test that multiple concurrent remote bootstraps do not cause problems.
// This is a regression test for KUDU-951, in which concurrent sessions on
// multiple tablets between the same remote bootstrap client host and remote
// bootstrap source host could corrupt each other.
TEST_F(RemoteBootstrapITest, TestConcurrentRemoteBootstraps) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipping test in fast-test mode.";
    return;
  }

  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--enable_leader_failure_detection=false");
  ts_flags.push_back("--log_cache_size_limit_mb=1");
  ts_flags.push_back("--log_segment_size_mb=1");
  ts_flags.push_back("--log_async_preallocate_segments=false");
  ts_flags.push_back("--log_min_segments_to_retain=100");
  ts_flags.push_back("--flush_threshold_mb=0"); // Constantly flush.
  ts_flags.push_back("--maintenance_manager_polling_interval_ms=10");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  NO_FATALS(StartCluster(ts_flags, master_flags));

  const MonoDelta timeout = MonoDelta::FromSeconds(60);

  // Create a table with several tablets. These will all be simultaneously
  // remotely bootstrapped to a single target node from the same leader host.
  const int kNumTablets = 10;
  KuduSchema client_schema(KuduSchemaFromSchema(GetSimpleTestSchema()));
  vector<const KuduPartialRow*> splits;
  for (int i = 0; i < kNumTablets - 1; i++) {
    KuduPartialRow* row = client_schema.NewRow();
    ASSERT_OK(row->SetInt32(0, numeric_limits<int32_t>::max() / kNumTablets * (i + 1)));
    splits.push_back(row);
  }
  gscoped_ptr<KuduTableCreator> table_creator(client_->NewTableCreator());
  ASSERT_OK(table_creator->table_name(TestWorkload::kDefaultTableName)
                          .split_rows(splits)
                          .schema(&client_schema)
                          .num_replicas(3)
                          .Create());

  const int kTsIndex = 0; // We'll test with the first TS.
  TServerDetails* target_ts = ts_map_[cluster_->tablet_server(kTsIndex)->uuid()];

  // Figure out the tablet ids of the created tablets.
  vector<ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  ASSERT_OK(WaitForNumTabletsOnTS(target_ts, kNumTablets, timeout, &tablets));

  vector<string> tablet_ids;
  for (const ListTabletsResponsePB::StatusAndSchemaPB& t : tablets) {
    tablet_ids.push_back(t.tablet_status().tablet_id());
  }

  // Wait until all replicas are up and running.
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    for (const string& tablet_id : tablet_ids) {
      ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(i)->uuid()],
                                              tablet_id, timeout));
    }
  }

  // Elect leaders on each tablet for term 1. All leaders will be on TS 1.
  const int kLeaderIndex = 1;
  const string kLeaderUuid = cluster_->tablet_server(kLeaderIndex)->uuid();
  for (const string& tablet_id : tablet_ids) {
    ASSERT_OK(itest::StartElection(ts_map_[kLeaderUuid], tablet_id, timeout));
  }

  TestWorkload workload(cluster_.get());
  workload.set_write_timeout_millis(10000);
  workload.set_timeout_allowed(true);
  workload.set_write_batch_size(10);
  workload.set_num_write_threads(10);
  workload.Setup();
  workload.Start();
  while (workload.rows_inserted() < 20000) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  workload.StopAndJoin();

  for (const string& tablet_id : tablet_ids) {
    ASSERT_OK(WaitForServersToAgree(timeout, ts_map_, tablet_id, 1));
  }

  // Now pause the leader so we can tombstone the tablets.
  ASSERT_OK(cluster_->tablet_server(kLeaderIndex)->Pause());

  for (const string& tablet_id : tablet_ids) {
    LOG(INFO) << "Tombstoning tablet " << tablet_id << " on TS " << target_ts->uuid();
    ASSERT_OK(itest::DeleteTablet(target_ts, tablet_id, TABLET_DATA_TOMBSTONED, boost::none,
                                  MonoDelta::FromSeconds(10)));
  }

  // Unpause the leader TS and wait for it to remotely bootstrap the tombstoned
  // tablets, in parallel.
  ASSERT_OK(cluster_->tablet_server(kLeaderIndex)->Resume());
  for (const string& tablet_id : tablet_ids) {
    ASSERT_OK(itest::WaitUntilTabletRunning(target_ts, tablet_id, timeout));
  }

  ClusterVerifier v(cluster_.get());
  NO_FATALS(v.CheckCluster());
  NO_FATALS(v.CheckRowCount(workload.table_name(), ClusterVerifier::AT_LEAST,
                            workload.rows_inserted()));
}

// Test that repeatedly runs a load, tombstones a follower, then tombstones the
// leader while the follower is remotely bootstrapping. Regression test for
// KUDU-1047.
TEST_F(RemoteBootstrapITest, TestDeleteLeaderDuringRemoteBootstrapStressTest) {
  // This test takes a while due to failure detection.
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipping test in fast-test mode.";
    return;
  }

  const MonoDelta timeout = MonoDelta::FromSeconds(60);
  NO_FATALS(StartCluster(vector<string>(), vector<string>(), 5));

  TestWorkload workload(cluster_.get());
  workload.set_num_replicas(5);
  workload.set_payload_bytes(FLAGS_test_delete_leader_payload_bytes);
  workload.set_num_write_threads(FLAGS_test_delete_leader_num_writer_threads);
  workload.set_write_batch_size(1);
  workload.set_write_timeout_millis(10000);
  workload.set_timeout_allowed(true);
  workload.set_not_found_allowed(true);
  workload.Setup();

  // Figure out the tablet id.
  const int kTsIndex = 0;
  TServerDetails* ts = ts_map_[cluster_->tablet_server(kTsIndex)->uuid()];
  vector<ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  ASSERT_OK(WaitForNumTabletsOnTS(ts, 1, timeout, &tablets));
  string tablet_id = tablets[0].tablet_status().tablet_id();

  // Wait until all replicas are up and running.
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(i)->uuid()],
                                            tablet_id, timeout));
  }

  int leader_index = -1;
  int follower_index = -1;
  TServerDetails* leader_ts = nullptr;
  TServerDetails* follower_ts = nullptr;

  for (int i = 0; i < FLAGS_test_delete_leader_num_iters; i++) {
    LOG(INFO) << "Iteration " << (i + 1);
    int rows_previously_inserted = workload.rows_inserted();

    // Find out who's leader.
    ASSERT_OK(FindTabletLeader(ts_map_, tablet_id, timeout, &leader_ts));
    leader_index = cluster_->tablet_server_index_by_uuid(leader_ts->uuid());

    // Select an arbitrary follower.
    follower_index = (leader_index + 1) % cluster_->num_tablet_servers();
    follower_ts = ts_map_[cluster_->tablet_server(follower_index)->uuid()];

    // Spin up the workload.
    workload.Start();
    while (workload.rows_inserted() < rows_previously_inserted +
                                      FLAGS_test_delete_leader_min_rows_per_iter) {
      SleepFor(MonoDelta::FromMilliseconds(10));
    }

    // Tombstone the follower.
    LOG(INFO) << "Tombstoning follower tablet " << tablet_id << " on TS " << follower_ts->uuid();
    ASSERT_OK(itest::DeleteTablet(follower_ts, tablet_id, TABLET_DATA_TOMBSTONED, boost::none,
                                  timeout));

    // Wait for remote bootstrap to start.
    ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(follower_index, tablet_id,
                                                   tablet::TABLET_DATA_COPYING, timeout));

    // Tombstone the leader.
    LOG(INFO) << "Tombstoning leader tablet " << tablet_id << " on TS " << leader_ts->uuid();
    ASSERT_OK(itest::DeleteTablet(leader_ts, tablet_id, TABLET_DATA_TOMBSTONED, boost::none,
                                  timeout));

    // Quiesce and rebuild to full strength. This involves electing a new
    // leader from the remaining three, which requires a unanimous vote, and
    // that leader then remotely bootstrapping the old leader.
    workload.StopAndJoin();
    ASSERT_OK(WaitForServersToAgree(timeout, ts_map_, tablet_id, 1));
  }

  ClusterVerifier v(cluster_.get());
  NO_FATALS(v.CheckCluster());
  NO_FATALS(v.CheckRowCount(workload.table_name(), ClusterVerifier::AT_LEAST,
                            workload.rows_inserted()));
}

namespace {
int64_t CountUpdateConsensusCalls(ExternalTabletServer* ets, const string& tablet_id) {
  int64_t ret;
  CHECK_OK(ets->GetInt64Metric(
               &METRIC_ENTITY_server,
               "kudu.tabletserver",
               &METRIC_handler_latency_kudu_consensus_ConsensusService_UpdateConsensus,
               "total_count",
               &ret));
  return ret;
}
int64_t CountLogMessages(ExternalTabletServer* ets) {
  int64_t total = 0;

  int64_t count;
  CHECK_OK(ets->GetInt64Metric(
               &METRIC_ENTITY_server,
               "kudu.tabletserver",
               &METRIC_glog_info_messages,
               "value",
               &count));
  total += count;

  CHECK_OK(ets->GetInt64Metric(
               &METRIC_ENTITY_server,
               "kudu.tabletserver",
               &METRIC_glog_warning_messages,
               "value",
               &count));
  total += count;

  CHECK_OK(ets->GetInt64Metric(
               &METRIC_ENTITY_server,
               "kudu.tabletserver",
               &METRIC_glog_error_messages,
               "value",
               &count));
  total += count;

  return total;
}
} // anonymous namespace

// Test that if remote bootstrap is disabled by a flag, we don't get into
// tight loops after a tablet is deleted. This is a regression test for situation
// similar to the bug described in KUDU-821: we were previously handling a missing
// tablet within consensus in such a way that we'd immediately send another RPC.
TEST_F(RemoteBootstrapITest, TestDisableRemoteBootstrap_NoTightLoopWhenTabletDeleted) {
  MonoDelta timeout = MonoDelta::FromSeconds(10);
  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--enable_leader_failure_detection=false");
  ts_flags.push_back("--enable_remote_bootstrap=false");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  NO_FATALS(StartCluster(ts_flags, master_flags));

  TestWorkload workload(cluster_.get());
  // TODO(KUDU-1054): the client should handle retrying on different replicas
  // if the tablet isn't found, rather than giving us this error.
  workload.set_not_found_allowed(true);
  workload.set_write_batch_size(1);
  workload.Setup();

  // Figure out the tablet id of the created tablet.
  vector<ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  ExternalTabletServer* replica_ets = cluster_->tablet_server(1);
  TServerDetails* replica_ts = ts_map_[replica_ets->uuid()];
  ASSERT_OK(WaitForNumTabletsOnTS(replica_ts, 1, timeout, &tablets));
  string tablet_id = tablets[0].tablet_status().tablet_id();

  // Wait until all replicas are up and running.
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(i)->uuid()],
                                            tablet_id, timeout));
  }

  // Elect a leader (TS 0)
  ExternalTabletServer* leader_ts = cluster_->tablet_server(0);
  ASSERT_OK(itest::StartElection(ts_map_[leader_ts->uuid()], tablet_id, timeout));

  // Start writing, wait for some rows to be inserted.
  workload.Start();
  while (workload.rows_inserted() < 100) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  // Tombstone the tablet on one of the servers (TS 1)
  ASSERT_OK(itest::DeleteTablet(replica_ts, tablet_id, TABLET_DATA_TOMBSTONED, boost::none,
                                timeout));

  // Ensure that, if we sleep for a second while still doing writes to the leader:
  // a) we don't spew logs on the leader side
  // b) we don't get hit with a lot of UpdateConsensus calls on the replica.
  int64_t num_update_rpcs_initial = CountUpdateConsensusCalls(replica_ets, tablet_id);
  int64_t num_logs_initial = CountLogMessages(leader_ts);

  SleepFor(MonoDelta::FromSeconds(1));
  int64_t num_update_rpcs_after_sleep = CountUpdateConsensusCalls(replica_ets, tablet_id);
  int64_t num_logs_after_sleep = CountLogMessages(leader_ts);

  // Calculate rate per second of RPCs and log messages
  int64_t update_rpcs_per_second = num_update_rpcs_after_sleep - num_update_rpcs_initial;
  EXPECT_LT(update_rpcs_per_second, 20);
  int64_t num_logs_per_second = num_logs_after_sleep - num_logs_initial;
  EXPECT_LT(num_logs_per_second, 20);
}

} // namespace kudu
