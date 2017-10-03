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
#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <gtest/gtest.h>
#include <unordered_map>
#include <unordered_set>

#include "kudu/client/client-test-util.h"
#include "kudu/client/client.h"
#include "kudu/client/write_op.h"
#include "kudu/common/schema.h"
#include "kudu/common/wire_protocol-test-util.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/consensus.pb.h"
#include "kudu/consensus/consensus_peers.h"
#include "kudu/consensus/metadata.pb.h"
#include "kudu/consensus/quorum_util.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/strcat.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/integration-tests/cluster_verifier.h"
#include "kudu/integration-tests/test_workload.h"
#include "kudu/integration-tests/ts_itest-base.h"
#include "kudu/server/server_base.pb.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_util.h"

DEFINE_int32(num_client_threads, 8,
             "Number of client threads to launch");
DEFINE_int64(client_inserts_per_thread, 50,
             "Number of rows inserted by each client thread");
DEFINE_int64(client_num_batches_per_thread, 5,
             "In how many batches to group the rows, for each client");
DECLARE_int32(consensus_rpc_timeout_ms);

METRIC_DECLARE_entity(tablet);
METRIC_DECLARE_counter(transaction_memory_pressure_rejections);
METRIC_DECLARE_gauge_int64(raft_term);

namespace kudu {
namespace tserver {

using client::KuduInsert;
using client::KuduSession;
using client::KuduTable;
using client::sp::shared_ptr;
using consensus::ConsensusRequestPB;
using consensus::ConsensusResponsePB;
using consensus::ConsensusServiceProxy;
using consensus::MajoritySize;
using consensus::MakeOpId;
using consensus::RaftPeerPB;
using consensus::ReplicateMsg;
using itest::AddServer;
using itest::GetReplicaStatusAndCheckIfLeader;
using itest::LeaderStepDown;
using itest::RemoveServer;
using itest::StartElection;
using itest::WaitUntilLeader;
using itest::WriteSimpleTestRow;
using master::GetTabletLocationsRequestPB;
using master::GetTabletLocationsResponsePB;
using master::TabletLocationsPB;
using rpc::RpcController;
using server::SetFlagRequestPB;
using server::SetFlagResponsePB;
using std::unordered_map;
using std::unordered_set;
using std::vector;
using strings::Substitute;

static const int kConsensusRpcTimeoutForTests = 50;

static const int kTestRowKey = 1234;
static const int kTestRowIntVal = 5678;

// Integration test for the raft consensus implementation.
// Uses the whole tablet server stack with ExternalMiniCluster.
class RaftConsensusITest : public TabletServerIntegrationTestBase {
 public:
  RaftConsensusITest()
      : inserters_(FLAGS_num_client_threads) {
  }

  virtual void SetUp() OVERRIDE {
    TabletServerIntegrationTestBase::SetUp();
    FLAGS_consensus_rpc_timeout_ms = kConsensusRpcTimeoutForTests;
  }

  void ScanReplica(TabletServerServiceProxy* replica_proxy,
                   vector<string>* results) {

    ScanRequestPB req;
    ScanResponsePB resp;
    RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(10)); // Squelch warnings.

    NewScanRequestPB* scan = req.mutable_new_scan_request();
    scan->set_tablet_id(tablet_id_);
    ASSERT_OK(SchemaToColumnPBs(schema_, scan->mutable_projected_columns()));

    // Send the call
    {
      req.set_batch_size_bytes(0);
      SCOPED_TRACE(req.DebugString());
      ASSERT_OK(replica_proxy->Scan(req, &resp, &rpc));
      SCOPED_TRACE(resp.DebugString());
      if (resp.has_error()) {
        ASSERT_OK(StatusFromPB(resp.error().status()));
      }
    }

    if (!resp.has_more_results())
      return;

    // Drain all the rows from the scanner.
    NO_FATALS(DrainScannerToStrings(resp.scanner_id(),
                                    schema_,
                                    results,
                                    replica_proxy));

    std::sort(results->begin(), results->end());
  }

  // Scan the given replica in a loop until the number of rows
  // is 'expected_count'. If it takes more than 10 seconds, then
  // fails the test.
  void WaitForRowCount(TabletServerServiceProxy* replica_proxy,
                       int expected_count,
                       vector<string>* results) {
    LOG(INFO) << "Waiting for row count " << expected_count << "...";
    MonoTime start = MonoTime::Now(MonoTime::FINE);
    MonoTime deadline = MonoTime::Now(MonoTime::FINE);
    deadline.AddDelta(MonoDelta::FromSeconds(10));
    while (true) {
      results->clear();
      NO_FATALS(ScanReplica(replica_proxy, results));
      if (results->size() == expected_count) {
        return;
      }
      SleepFor(MonoDelta::FromMilliseconds(10));
      if (!MonoTime::Now(MonoTime::FINE).ComesBefore(deadline)) {
        break;
      }
    }
    MonoTime end = MonoTime::Now(MonoTime::FINE);
    LOG(WARNING) << "Didn't reach row count " << expected_count;
    FAIL() << "Did not reach expected row count " << expected_count
           << " after " << end.GetDeltaSince(start).ToString()
           << ": rows: " << *results;
  }


  // Add an Insert operation to the given consensus request.
  // The row to be inserted is generated based on the OpId.
  void AddOp(const OpId& id, ConsensusRequestPB* req);

  string DumpToString(TServerDetails* leader,
                      const vector<string>& leader_results,
                      TServerDetails* replica,
                      const vector<string>& replica_results) {
    string ret = strings::Substitute("Replica results did not match the leaders."
                                     "\nLeader: $0\nReplica: $1. Results size "
                                     "L: $2 R: $3",
                                     leader->ToString(),
                                     replica->ToString(),
                                     leader_results.size(),
                                     replica_results.size());

    StrAppend(&ret, "Leader Results: \n");
    for (const string& result : leader_results) {
      StrAppend(&ret, result, "\n");
    }

    StrAppend(&ret, "Replica Results: \n");
    for (const string& result : replica_results) {
      StrAppend(&ret, result, "\n");
    }

    return ret;
  }

  void InsertTestRowsRemoteThread(uint64_t first_row,
                                  uint64_t count,
                                  uint64_t num_batches,
                                  const vector<CountDownLatch*>& latches) {
    shared_ptr<KuduTable> table;
    CHECK_OK(client_->OpenTable(kTableId, &table));

    shared_ptr<KuduSession> session = client_->NewSession();
    session->SetTimeoutMillis(60000);
    CHECK_OK(session->SetFlushMode(KuduSession::MANUAL_FLUSH));

    for (int i = 0; i < num_batches; i++) {
      uint64_t first_row_in_batch = first_row + (i * count / num_batches);
      uint64_t last_row_in_batch = first_row_in_batch + count / num_batches;

      for (int j = first_row_in_batch; j < last_row_in_batch; j++) {
        gscoped_ptr<KuduInsert> insert(table->NewInsert());
        KuduPartialRow* row = insert->mutable_row();
        CHECK_OK(row->SetInt32(0, j));
        CHECK_OK(row->SetInt32(1, j * 2));
        CHECK_OK(row->SetStringCopy(2, Slice(StringPrintf("hello %d", j))));
        CHECK_OK(session->Apply(insert.release()));
      }

      // We don't handle write idempotency yet. (i.e making sure that when a leader fails
      // writes to it that were eventually committed by the new leader but un-ackd to the
      // client are not retried), so some errors are expected.
      // It's OK as long as the errors are Status::AlreadyPresent();

      int inserted = last_row_in_batch - first_row_in_batch;

      Status s = session->Flush();
      if (PREDICT_FALSE(!s.ok())) {
        std::vector<client::KuduError*> errors;
        ElementDeleter d(&errors);
        bool overflow;
        session->GetPendingErrors(&errors, &overflow);
        CHECK(!overflow);
        for (const client::KuduError* e : errors) {
          CHECK(e->status().IsAlreadyPresent()) << "Unexpected error: " << e->status().ToString();
        }
        inserted -= errors.size();
      }

      for (CountDownLatch* latch : latches) {
        latch->CountDown(inserted);
      }
    }

    inserters_.CountDown();
  }

  // Brings Chaos to a MiniTabletServer by introducing random delays. Does this by
  // pausing the daemon a random amount of time.
  void DelayInjectorThread(ExternalTabletServer* tablet_server, int timeout_msec) {
    while (inserters_.count() > 0) {

      // Adjust the value obtained from the normalized gauss. dist. so that we steal the lock
      // longer than the the timeout a small (~5%) percentage of the times.
      // (95% corresponds to 1.64485, in a normalized (0,1) gaussian distribution).
      double sleep_time_usec = 1000 *
          ((random_.Normal(0, 1) * timeout_msec) / 1.64485);

      if (sleep_time_usec < 0) sleep_time_usec = 0;

      // Additionally only cause timeouts at all 50% of the time, otherwise sleep.
      double val = (rand() * 1.0) / RAND_MAX;
      if (val < 0.5) {
        SleepFor(MonoDelta::FromMicroseconds(sleep_time_usec));
        continue;
      }

      ASSERT_OK(tablet_server->Pause());
      LOG_IF(INFO, sleep_time_usec > 0.0)
          << "Delay injector thread for TS " << tablet_server->instance_id().permanent_uuid()
          << " SIGSTOPped the ts, sleeping for " << sleep_time_usec << " usec...";
      SleepFor(MonoDelta::FromMicroseconds(sleep_time_usec));
      ASSERT_OK(tablet_server->Resume());
    }
  }

  // Thread which loops until '*finish' becomes true, trying to insert a row
  // on the given tablet server identified by 'replica_idx'.
  void StubbornlyWriteSameRowThread(int replica_idx, const AtomicBool* finish);

  // Stops the current leader of the configuration, runs leader election and then brings it back.
  // Before stopping the leader this pauses all follower nodes in regular intervals so that
  // we get an increased chance of stuff being pending.
  void StopOrKillLeaderAndElectNewOne() {
    bool kill = rand() % 2 == 0;

    TServerDetails* old_leader;
    CHECK_OK(GetLeaderReplicaWithRetries(tablet_id_, &old_leader));
    ExternalTabletServer* old_leader_ets = cluster_->tablet_server_by_uuid(old_leader->uuid());

    vector<TServerDetails*> followers;
    GetOnlyLiveFollowerReplicas(tablet_id_, &followers);

    for (TServerDetails* ts : followers) {
      ExternalTabletServer* ets = cluster_->tablet_server_by_uuid(ts->uuid());
      CHECK_OK(ets->Pause());
      SleepFor(MonoDelta::FromMilliseconds(100));
    }

    // When all are paused also pause or kill the current leader. Since we've waited a bit
    // the old leader is likely to have operations that must be aborted.
    if (kill) {
      old_leader_ets->Shutdown();
    } else {
      CHECK_OK(old_leader_ets->Pause());
    }

    // Resume the replicas.
    for (TServerDetails* ts : followers) {
      ExternalTabletServer* ets = cluster_->tablet_server_by_uuid(ts->uuid());
      CHECK_OK(ets->Resume());
    }

    // Get the new leader.
    TServerDetails* new_leader;
    CHECK_OK(GetLeaderReplicaWithRetries(tablet_id_, &new_leader));

    // Bring the old leader back.
    if (kill) {
      CHECK_OK(old_leader_ets->Restart());
      // Wait until we have the same number of followers.
      int initial_followers = followers.size();
      do {
        GetOnlyLiveFollowerReplicas(tablet_id_, &followers);
      } while (followers.size() < initial_followers);
    } else {
      CHECK_OK(old_leader_ets->Resume());
    }
  }

  // Writes 'num_writes' operations to the current leader. Each of the operations
  // has a payload of around 128KB. Causes a gtest failure on error.
  void Write128KOpsToLeader(int num_writes);

  // Check for and restart any TS that have crashed.
  // Returns the number of servers restarted.
  int RestartAnyCrashedTabletServers();

  // Assert that no tablet servers have crashed.
  // Tablet servers that have been manually Shutdown() are allowed.
  void AssertNoTabletServersCrashed();

  // Ensure that a majority of servers is required for elections and writes.
  // This is done by pausing a majority and asserting that writes and elections fail,
  // then unpausing the majority and asserting that elections and writes succeed.
  // If fails, throws a gtest assertion.
  // Note: This test assumes all tablet servers listed in tablet_servers are voters.
  void AssertMajorityRequiredForElectionsAndWrites(const TabletServerMap& tablet_servers,
                                                   const string& leader_uuid);

  // Return the replicas of the specified 'tablet_id', as seen by the Master.
  Status GetTabletLocations(const string& tablet_id, const MonoDelta& timeout,
                            master::TabletLocationsPB* tablet_locations);

  enum WaitForLeader {
    NO_WAIT_FOR_LEADER = 0,
    WAIT_FOR_LEADER = 1
  };

  // Wait for the specified number of replicas to be reported by the master for
  // the given tablet. Fails with an assertion if the timeout expires.
  void WaitForReplicasReportedToMaster(int num_replicas, const string& tablet_id,
                                       const MonoDelta& timeout,
                                       WaitForLeader wait_for_leader,
                                       bool* has_leader,
                                       master::TabletLocationsPB* tablet_locations);

  static const bool WITH_NOTIFICATION_LATENCY = true;
  static const bool WITHOUT_NOTIFICATION_LATENCY = false;
  void DoTestChurnyElections(bool with_latency);

 protected:
  // Flags needed for CauseFollowerToFallBehindLogGC() to work well.
  void AddFlagsForLogRolls(vector<string>* extra_tserver_flags);

  // Pause one of the followers and write enough data to the remaining replicas
  // to cause log GC, then resume the paused follower. On success,
  // 'leader_uuid' will be set to the UUID of the leader, 'orig_term' will be
  // set to the term of the leader before un-pausing the follower, and
  // 'fell_behind_uuid' will be set to the UUID of the follower that was paused
  // and caused to fall behind. These can be used for verification purposes.
  //
  // Certain flags should be set. You can add the required flags with
  // AddFlagsForLogRolls() before starting the cluster.
  void CauseFollowerToFallBehindLogGC(string* leader_uuid,
                                      int64_t* orig_term,
                                      string* fell_behind_uuid);

  shared_ptr<KuduTable> table_;
  std::vector<scoped_refptr<kudu::Thread> > threads_;
  CountDownLatch inserters_;
};

void RaftConsensusITest::AddFlagsForLogRolls(vector<string>* extra_tserver_flags) {
  // We configure a small log segment size so that we roll frequently,
  // configure a small cache size so that we evict data from the cache, and
  // retain as few segments as possible. We also turn off async segment
  // allocation -- this ensures that we roll many segments of logs (with async
  // allocation, it's possible that the preallocation is slow and we wouldn't
  // roll deterministically).
  extra_tserver_flags->push_back("--log_cache_size_limit_mb=1");
  extra_tserver_flags->push_back("--log_segment_size_mb=1");
  extra_tserver_flags->push_back("--log_async_preallocate_segments=false");
  extra_tserver_flags->push_back("--log_min_segments_to_retain=1");
  extra_tserver_flags->push_back("--log_min_seconds_to_retain=0");
  extra_tserver_flags->push_back("--maintenance_manager_polling_interval_ms=100");
}

// Test that we can retrieve the permanent uuid of a server running
// consensus service via RPC.
TEST_F(RaftConsensusITest, TestGetPermanentUuid) {
  BuildAndStart(vector<string>());

  RaftPeerPB peer;
  TServerDetails* leader = nullptr;
  ASSERT_OK(GetLeaderReplicaWithRetries(tablet_id_, &leader));
  peer.mutable_last_known_addr()->CopyFrom(leader->registration.rpc_addresses(0));
  const string expected_uuid = leader->instance_id.permanent_uuid();

  rpc::MessengerBuilder builder("test builder");
  builder.set_num_reactors(1);
  std::shared_ptr<rpc::Messenger> messenger;
  ASSERT_OK(builder.Build(&messenger));

  ASSERT_OK(consensus::SetPermanentUuidForRemotePeer(messenger, &peer));
  ASSERT_EQ(expected_uuid, peer.permanent_uuid());
}

// TODO allow the scan to define an operation id, fetch the last id
// from the leader and then use that id to make the replica wait
// until it is done. This will avoid the sleeps below.
TEST_F(RaftConsensusITest, TestInsertAndMutateThroughConsensus) {
  BuildAndStart(vector<string>());

  int num_iters = AllowSlowTests() ? 10 : 1;

  for (int i = 0; i < num_iters; i++) {
    InsertTestRowsRemoteThread(i * FLAGS_client_inserts_per_thread,
                               FLAGS_client_inserts_per_thread,
                               FLAGS_client_num_batches_per_thread,
                               vector<CountDownLatch*>());
  }
  ASSERT_ALL_REPLICAS_AGREE(FLAGS_client_inserts_per_thread * num_iters);
}

TEST_F(RaftConsensusITest, TestFailedTransaction) {
  BuildAndStart(vector<string>());

  // Wait until we have a stable leader.
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10), tablet_servers_,
                                  tablet_id_, 1));

  WriteRequestPB req;
  req.set_tablet_id(tablet_id_);
  ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));

  RowOperationsPB* data = req.mutable_row_operations();
  data->set_rows("some gibberish!");

  WriteResponsePB resp;
  RpcController controller;
  controller.set_timeout(MonoDelta::FromSeconds(FLAGS_rpc_timeout));

  TServerDetails* leader = nullptr;
  ASSERT_OK(GetLeaderReplicaWithRetries(tablet_id_, &leader));

  ASSERT_OK(DCHECK_NOTNULL(leader->tserver_proxy.get())->Write(req, &resp, &controller));
  ASSERT_TRUE(resp.has_error());

  // Add a proper row so that we can verify that all of the replicas continue
  // to process transactions after a failure. Additionally, this allows us to wait
  // for all of the replicas to finish processing transactions before shutting down,
  // avoiding a potential stall as we currently can't abort transactions (see KUDU-341).
  data->Clear();
  AddTestRowToPB(RowOperationsPB::INSERT, schema_, 0, 0, "original0", data);

  controller.Reset();
  controller.set_timeout(MonoDelta::FromSeconds(FLAGS_rpc_timeout));

  ASSERT_OK(DCHECK_NOTNULL(leader->tserver_proxy.get())->Write(req, &resp, &controller));
  SCOPED_TRACE(resp.ShortDebugString());
  ASSERT_FALSE(resp.has_error());

  ASSERT_ALL_REPLICAS_AGREE(1);
}

// Inserts rows through consensus and also starts one delay injecting thread
// that steals consensus peer locks for a while. This is meant to test that
// even with timeouts and repeated requests consensus still works.
TEST_F(RaftConsensusITest, MultiThreadedMutateAndInsertThroughConsensus) {
  BuildAndStart(vector<string>());

  if (500 == FLAGS_client_inserts_per_thread) {
    if (AllowSlowTests()) {
      FLAGS_client_inserts_per_thread = FLAGS_client_inserts_per_thread * 10;
      FLAGS_client_num_batches_per_thread = FLAGS_client_num_batches_per_thread * 10;
    }
  }

  int num_threads = FLAGS_num_client_threads;
  for (int i = 0; i < num_threads; i++) {
    scoped_refptr<kudu::Thread> new_thread;
    CHECK_OK(kudu::Thread::Create("test", strings::Substitute("ts-test$0", i),
                                  &RaftConsensusITest::InsertTestRowsRemoteThread,
                                  this, i * FLAGS_client_inserts_per_thread,
                                  FLAGS_client_inserts_per_thread,
                                  FLAGS_client_num_batches_per_thread,
                                  vector<CountDownLatch*>(),
                                  &new_thread));
    threads_.push_back(new_thread);
  }
  for (int i = 0; i < FLAGS_num_replicas; i++) {
    scoped_refptr<kudu::Thread> new_thread;
    CHECK_OK(kudu::Thread::Create("test", strings::Substitute("chaos-test$0", i),
                                  &RaftConsensusITest::DelayInjectorThread,
                                  this, cluster_->tablet_server(i),
                                  kConsensusRpcTimeoutForTests,
                                  &new_thread));
    threads_.push_back(new_thread);
  }
  for (scoped_refptr<kudu::Thread> thr : threads_) {
   CHECK_OK(ThreadJoiner(thr.get()).Join());
  }

  ASSERT_ALL_REPLICAS_AGREE(FLAGS_client_inserts_per_thread * FLAGS_num_client_threads);
}

TEST_F(RaftConsensusITest, TestInsertOnNonLeader) {
  BuildAndStart(vector<string>());

  // Wait for the initial leader election to complete.
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10), tablet_servers_,
                                  tablet_id_, 1));

  // Manually construct a write RPC to a replica and make sure it responds
  // with the correct error code.
  WriteRequestPB req;
  WriteResponsePB resp;
  RpcController rpc;
  req.set_tablet_id(tablet_id_);
  ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));
  AddTestRowToPB(RowOperationsPB::INSERT, schema_, kTestRowKey, kTestRowIntVal,
                 "hello world via RPC", req.mutable_row_operations());

  // Get the leader.
  vector<TServerDetails*> followers;
  GetOnlyLiveFollowerReplicas(tablet_id_, &followers);

  ASSERT_OK(followers[0]->tserver_proxy->Write(req, &resp, &rpc));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  Status s = StatusFromPB(resp.error().status());
  EXPECT_TRUE(s.IsIllegalState());
  ASSERT_STR_CONTAINS(s.ToString(), "is not leader of this config. Role: FOLLOWER");
  // TODO: need to change the error code to be something like REPLICA_NOT_LEADER
  // so that the client can properly handle this case! plumbing this is a little difficult
  // so not addressing at the moment.
  ASSERT_ALL_REPLICAS_AGREE(0);
}

TEST_F(RaftConsensusITest, TestRunLeaderElection) {
  // Reset consensus rpc timeout to the default value or the election might fail often.
  FLAGS_consensus_rpc_timeout_ms = 1000;

  BuildAndStart(vector<string>());

  int num_iters = AllowSlowTests() ? 10 : 1;

  InsertTestRowsRemoteThread(0,
                             FLAGS_client_inserts_per_thread * num_iters,
                             FLAGS_client_num_batches_per_thread,
                             vector<CountDownLatch*>());

  ASSERT_ALL_REPLICAS_AGREE(FLAGS_client_inserts_per_thread * num_iters);

  // Select the last follower to be new leader.
  vector<TServerDetails*> followers;
  GetOnlyLiveFollowerReplicas(tablet_id_, &followers);

  // Now shutdown the current leader.
  TServerDetails* leader = DCHECK_NOTNULL(GetLeaderReplicaOrNull(tablet_id_));
  ExternalTabletServer* leader_ets = cluster_->tablet_server_by_uuid(leader->uuid());
  leader_ets->Shutdown();

  TServerDetails* replica = followers.back();
  CHECK_NE(leader->instance_id.permanent_uuid(), replica->instance_id.permanent_uuid());

  // Make the new replica leader.
  ASSERT_OK(StartElection(replica, tablet_id_, MonoDelta::FromSeconds(10)));

  // Insert a bunch more rows.
  InsertTestRowsRemoteThread(FLAGS_client_inserts_per_thread * num_iters,
                             FLAGS_client_inserts_per_thread * num_iters,
                             FLAGS_client_num_batches_per_thread,
                             vector<CountDownLatch*>());

  // Restart the original replica and make sure they all agree.
  ASSERT_OK(leader_ets->Restart());

  ASSERT_ALL_REPLICAS_AGREE(FLAGS_client_inserts_per_thread * num_iters * 2);
}

void RaftConsensusITest::Write128KOpsToLeader(int num_writes) {
  TServerDetails* leader = nullptr;
  ASSERT_OK(GetLeaderReplicaWithRetries(tablet_id_, &leader));

  WriteRequestPB req;
  req.set_tablet_id(tablet_id_);
  ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));
  RowOperationsPB* data = req.mutable_row_operations();
  WriteResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(10000));
  int key = 0;

  // generate a 128Kb dummy payload
  string test_payload(128 * 1024, '0');
  for (int i = 0; i < num_writes; i++) {
    rpc.Reset();
    data->Clear();
    AddTestRowToPB(RowOperationsPB::INSERT, schema_, key, key,
                   test_payload, data);
    key++;
    ASSERT_OK(leader->tserver_proxy->Write(req, &resp, &rpc));

    ASSERT_FALSE(resp.has_error()) << resp.DebugString();
  }
}

// Test that when a follower is stopped for a long time, the log cache
// properly evicts operations, but still allows the follower to catch
// up when it comes back.
TEST_F(RaftConsensusITest, TestCatchupAfterOpsEvicted) {
  vector<string> extra_flags;
  extra_flags.push_back("--log_cache_size_limit_mb=1");
  extra_flags.push_back("--consensus_max_batch_size_bytes=500000");
  BuildAndStart(extra_flags);
  TServerDetails* replica = (*tablet_replicas_.begin()).second;
  ASSERT_TRUE(replica != nullptr);
  ExternalTabletServer* replica_ets = cluster_->tablet_server_by_uuid(replica->uuid());

  // Pause a replica
  ASSERT_OK(replica_ets->Pause());
  LOG(INFO)<< "Paused one of the replicas, starting to write.";

  // Insert 3MB worth of data.
  const int kNumWrites = 25;
  NO_FATALS(Write128KOpsToLeader(kNumWrites));

  // Now unpause the replica, the lagging replica should eventually catch back up.
  ASSERT_OK(replica_ets->Resume());

  ASSERT_ALL_REPLICAS_AGREE(kNumWrites);
}

void RaftConsensusITest::CauseFollowerToFallBehindLogGC(string* leader_uuid,
                                                        int64_t* orig_term,
                                                        string* fell_behind_uuid) {
  MonoDelta kTimeout = MonoDelta::FromSeconds(10);
  // Wait for all of the replicas to have acknowledged the elected
  // leader and logged the first NO_OP.
  ASSERT_OK(WaitForServersToAgree(kTimeout, tablet_servers_, tablet_id_, 1));

  // Pause one server. This might be the leader, but pausing it will cause
  // a leader election to happen.
  TServerDetails* replica = (*tablet_replicas_.begin()).second;
  ExternalTabletServer* replica_ets = cluster_->tablet_server_by_uuid(replica->uuid());
  ASSERT_OK(replica_ets->Pause());

  // Find a leader. In case we paused the leader above, this will wait until
  // we have elected a new one.
  TServerDetails* leader = nullptr;
  while (true) {
    Status s = GetLeaderReplicaWithRetries(tablet_id_, &leader);
    if (s.ok() && leader != nullptr && leader != replica) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  *leader_uuid = leader->uuid();
  int leader_index = cluster_->tablet_server_index_by_uuid(*leader_uuid);

  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTableId);
  workload.set_timeout_allowed(true);
  workload.set_payload_bytes(128 * 1024); // Write ops of size 128KB.
  workload.set_write_batch_size(1);
  workload.set_num_write_threads(4);
  workload.Setup();
  workload.Start();

  LOG(INFO) << "Waiting until we've written at least 4MB...";
  while (workload.rows_inserted() < 8 * 4) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  workload.StopAndJoin();

  LOG(INFO) << "Waiting for log GC on " << leader->uuid();
  // Some WAL segments must exist, but wal segment 1 must not exist.
  ASSERT_OK(inspect_->WaitForFilePatternInTabletWalDirOnTs(
      leader_index, tablet_id_, { "wal-" }, { "wal-000000001" }));

  LOG(INFO) << "Log GC complete on " << leader->uuid();

  // Then wait another couple of seconds to be sure that it has bothered to try
  // to write to the paused peer.
  // TODO: would be nice to be able to poll the leader with an RPC like
  // GetLeaderStatus() which could tell us whether it has made any requests
  // since the log GC.
  SleepFor(MonoDelta::FromSeconds(2));

  // Make a note of whatever the current term of the cluster is,
  // before we resume the follower.
  {
    OpId op_id;
    ASSERT_OK(GetLastOpIdForReplica(tablet_id_, leader, consensus::RECEIVED_OPID, kTimeout,
                                    &op_id));
    *orig_term = op_id.term();
    LOG(INFO) << "Servers converged with original term " << *orig_term;
  }

  // Resume the follower.
  LOG(INFO) << "Resuming  " << replica->uuid();
  ASSERT_OK(replica_ets->Resume());

  // Ensure that none of the tablet servers crashed.
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    // Make sure it didn't crash.
    ASSERT_TRUE(cluster_->tablet_server(i)->IsProcessAlive())
      << "Tablet server " << i << " crashed";
  }
  *fell_behind_uuid = replica->uuid();
}

// Test that the leader doesn't crash if one of its followers has
// fallen behind so far that the logs necessary to catch it up
// have been GCed.
//
// In a real cluster, this will eventually cause the follower to be
// evicted/replaced. In any case, the leader should not crash.
//
// We also ensure that, when the leader stops writing to the follower,
// the follower won't disturb the other nodes when it attempts to elect
// itself.
//
// This is a regression test for KUDU-775 and KUDU-562.
TEST_F(RaftConsensusITest, TestFollowerFallsBehindLeaderGC) {
  // Disable follower eviction to maintain the original intent of this test.
  vector<string> extra_flags = { "--evict_failed_followers=false" };
  AddFlagsForLogRolls(&extra_flags); // For CauseFollowerToFallBehindLogGC().
  BuildAndStart(extra_flags);

  string leader_uuid;
  int64_t orig_term;
  string follower_uuid;
  NO_FATALS(CauseFollowerToFallBehindLogGC(&leader_uuid, &orig_term, &follower_uuid));

  // Wait for remaining majority to agree.
  TabletServerMap active_tablet_servers = tablet_servers_;
  ASSERT_EQ(3, active_tablet_servers.size());
  ASSERT_EQ(1, active_tablet_servers.erase(follower_uuid));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(30), active_tablet_servers, tablet_id_,
                                  1));

  if (AllowSlowTests()) {
    // Sleep long enough that the "abandoned" server's leader election interval
    // will trigger several times. Then, verify that the term has not increased.
    // This ensures that the other servers properly ignore the election requests
    // from the abandoned node.
    // TODO: would be nicer to use an RPC to check the current term of the
    // abandoned replica, and wait until it has incremented a couple of times.
    SleepFor(MonoDelta::FromSeconds(5));
    OpId op_id;
    TServerDetails* leader = tablet_servers_[leader_uuid];
    ASSERT_OK(GetLastOpIdForReplica(tablet_id_, leader, consensus::RECEIVED_OPID,
                                    MonoDelta::FromSeconds(10), &op_id));
    ASSERT_EQ(orig_term, op_id.term())
      << "expected the leader to have not advanced terms but has op " << op_id;
  }
}

int RaftConsensusITest::RestartAnyCrashedTabletServers() {
  int restarted = 0;
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    if (!cluster_->tablet_server(i)->IsProcessAlive()) {
      LOG(INFO) << "TS " << i << " appears to have crashed. Restarting.";
      cluster_->tablet_server(i)->Shutdown();
      CHECK_OK(cluster_->tablet_server(i)->Restart());
      restarted++;
    }
  }
  return restarted;
}

void RaftConsensusITest::AssertNoTabletServersCrashed() {
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    if (cluster_->tablet_server(i)->IsShutdown()) continue;

    ASSERT_TRUE(cluster_->tablet_server(i)->IsProcessAlive())
      << "Tablet server " << i << " crashed";
  }
}

// This test starts several tablet servers, and configures them with
// fault injection so that the leaders frequently crash just before
// sending RPCs to followers.
//
// This can result in various scenarios where leaders crash right after
// being elected and never succeed in replicating their first operation.
// For example, KUDU-783 reproduces from this test approximately 5% of the
// time on a slow-test debug build.
TEST_F(RaftConsensusITest, InsertWithCrashyNodes) {
  int kCrashesToCause = 3;
  if (AllowSlowTests()) {
    FLAGS_num_tablet_servers = 7;
    FLAGS_num_replicas = 7;
    kCrashesToCause = 15;
  }

  vector<string> ts_flags, master_flags;

  // Crash 5% of the time just before sending an RPC. With 7 servers,
  // this means we crash about 30% of the time before we've fully
  // replicated the NO_OP at the start of the term.
  ts_flags.push_back("--fault_crash_on_leader_request_fraction=0.05");

  // Inject latency to encourage the replicas to fall out of sync
  // with each other.
  ts_flags.push_back("--log_inject_latency");
  ts_flags.push_back("--log_inject_latency_ms_mean=30");
  ts_flags.push_back("--log_inject_latency_ms_stddev=60");

  // Make leader elections faster so we get through more cycles of
  // leaders.
  ts_flags.push_back("--raft_heartbeat_interval_ms=100");
  ts_flags.push_back("--leader_failure_monitor_check_mean_ms=50");
  ts_flags.push_back("--leader_failure_monitor_check_stddev_ms=25");

  // Avoid preallocating segments since bootstrap is a little bit
  // faster if it doesn't have to scan forward through the preallocated
  // log area.
  ts_flags.push_back("--log_preallocate_segments=false");

  CreateCluster("raft_consensus-itest-cluster", ts_flags, master_flags);

  TestWorkload workload(cluster_.get());
  workload.set_num_replicas(FLAGS_num_replicas);
  workload.set_timeout_allowed(true);
  workload.set_write_timeout_millis(1000);
  workload.set_num_write_threads(10);
  workload.set_write_batch_size(1);
  workload.Setup();
  workload.Start();

  int num_crashes = 0;
  while (num_crashes < kCrashesToCause &&
         workload.rows_inserted() < 100) {
    num_crashes += RestartAnyCrashedTabletServers();
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  workload.StopAndJoin();

  // After we stop the writes, we can still get crashes because heartbeats could
  // trigger the fault path. So, disable the faults and restart one more time.
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    ExternalTabletServer* ts = cluster_->tablet_server(i);
    vector<string>* flags = ts->mutable_flags();
    bool removed_flag = false;
    for (auto it = flags->begin(); it != flags->end(); ++it) {
      if (HasPrefixString(*it, "--fault_crash")) {
        flags->erase(it);
        removed_flag = true;
        break;
      }
    }
    ASSERT_TRUE(removed_flag) << "could not remove flag from TS " << i
                              << "\nFlags:\n" << *flags;
    ts->Shutdown();
    CHECK_OK(ts->Restart());
  }

  // Ensure that the replicas converge.
  // We don't know exactly how many rows got inserted, since the writer
  // probably saw many errors which left inserts in indeterminate state.
  // But, we should have at least as many as we got confirmation for.
  ClusterVerifier v(cluster_.get());
  NO_FATALS(v.CheckCluster());
  NO_FATALS(v.CheckRowCount(workload.table_name(), ClusterVerifier::AT_LEAST,
                            workload.rows_inserted()));
}

// This test sets all of the election timers to be very short, resulting
// in a lot of churn. We expect to make some progress and not diverge or
// crash, despite the frequent re-elections and races.
TEST_F(RaftConsensusITest, TestChurnyElections) {
  DoTestChurnyElections(WITHOUT_NOTIFICATION_LATENCY);
}

// The same test, except inject artificial latency when propagating notifications
// from the queue back to consensus. This can reproduce bugs like KUDU-1078 which
// normally only appear under high load. TODO: Re-enable once we get to the
// bottom of KUDU-1078.
TEST_F(RaftConsensusITest, DISABLED_TestChurnyElections_WithNotificationLatency) {
  DoTestChurnyElections(WITH_NOTIFICATION_LATENCY);
}

void RaftConsensusITest::DoTestChurnyElections(bool with_latency) {
  vector<string> ts_flags, master_flags;

#ifdef THREAD_SANITIZER
  // On TSAN builds, we need to be a little bit less churny in order to make
  // any progress at all.
  ts_flags.push_back("--raft_heartbeat_interval_ms=5");
#else
  ts_flags.push_back("--raft_heartbeat_interval_ms=1");
#endif
  ts_flags.push_back("--leader_failure_monitor_check_mean_ms=1");
  ts_flags.push_back("--leader_failure_monitor_check_stddev_ms=1");
  ts_flags.push_back("--never_fsync");
  if (with_latency) {
    ts_flags.push_back("--consensus_inject_latency_ms_in_notifications=50");
  }

  CreateCluster("raft_consensus-itest-cluster", ts_flags, master_flags);

  TestWorkload workload(cluster_.get());
  workload.set_num_replicas(FLAGS_num_replicas);
  workload.set_timeout_allowed(true);
  workload.set_write_timeout_millis(100);
  workload.set_num_write_threads(2);
  workload.set_write_batch_size(1);
  workload.Setup();
  workload.Start();

  // Run for either a prescribed number of writes, or 30 seconds,
  // whichever comes first. This prevents test timeouts on slower
  // build machines, TSAN builds, etc.
  Stopwatch sw;
  sw.start();
  const int kNumWrites = AllowSlowTests() ? 10000 : 1000;
  while (workload.rows_inserted() < kNumWrites &&
         sw.elapsed().wall_seconds() < 30) {
    SleepFor(MonoDelta::FromMilliseconds(10));
    NO_FATALS(AssertNoTabletServersCrashed());
  }
  workload.StopAndJoin();
  ASSERT_GT(workload.rows_inserted(), 0) << "No rows inserted";

  // Ensure that the replicas converge.
  // We don't know exactly how many rows got inserted, since the writer
  // probably saw many errors which left inserts in indeterminate state.
  // But, we should have at least as many as we got confirmation for.
  ClusterVerifier v(cluster_.get());
  NO_FATALS(v.CheckCluster());
  NO_FATALS(v.CheckRowCount(workload.table_name(), ClusterVerifier::AT_LEAST,
                            workload.rows_inserted()));
  NO_FATALS(AssertNoTabletServersCrashed());
}

TEST_F(RaftConsensusITest, MultiThreadedInsertWithFailovers) {
  int kNumElections = FLAGS_num_replicas;

  if (AllowSlowTests()) {
    FLAGS_num_tablet_servers = 7;
    FLAGS_num_replicas = 7;
    kNumElections = 3 * FLAGS_num_replicas;
  }

  // Reset consensus rpc timeout to the default value or the election might fail often.
  FLAGS_consensus_rpc_timeout_ms = 1000;

  // Start a 7 node configuration cluster (since we can't bring leaders back we start with a
  // higher replica count so that we kill more leaders).

  vector<string> flags;
  BuildAndStart(flags);

  OverrideFlagForSlowTests(
      "client_inserts_per_thread",
      strings::Substitute("$0", (FLAGS_client_inserts_per_thread * 100)));
  OverrideFlagForSlowTests(
      "client_num_batches_per_thread",
      strings::Substitute("$0", (FLAGS_client_num_batches_per_thread * 100)));

  int num_threads = FLAGS_num_client_threads;
  int64_t total_num_rows = num_threads * FLAGS_client_inserts_per_thread;

  // We create 2 * (kNumReplicas - 1) latches so that we kill the same node at least
  // twice.
  vector<CountDownLatch*> latches;
  for (int i = 1; i < kNumElections; i++) {
    latches.push_back(new CountDownLatch((i * total_num_rows)  / kNumElections));
  }

  for (int i = 0; i < num_threads; i++) {
    scoped_refptr<kudu::Thread> new_thread;
    CHECK_OK(kudu::Thread::Create("test", strings::Substitute("ts-test$0", i),
                                  &RaftConsensusITest::InsertTestRowsRemoteThread,
                                  this, i * FLAGS_client_inserts_per_thread,
                                  FLAGS_client_inserts_per_thread,
                                  FLAGS_client_num_batches_per_thread,
                                  latches,
                                  &new_thread));
    threads_.push_back(new_thread);
  }

  for (CountDownLatch* latch : latches) {
    latch->Wait();
    StopOrKillLeaderAndElectNewOne();
  }

  for (scoped_refptr<kudu::Thread> thr : threads_) {
   CHECK_OK(ThreadJoiner(thr.get()).Join());
  }

  ASSERT_ALL_REPLICAS_AGREE(FLAGS_client_inserts_per_thread * FLAGS_num_client_threads);
  STLDeleteElements(&latches);
}

// Test automatic leader election by killing leaders.
TEST_F(RaftConsensusITest, TestAutomaticLeaderElection) {
  if (AllowSlowTests()) {
    FLAGS_num_tablet_servers = 5;
    FLAGS_num_replicas = 5;
  }
  BuildAndStart(vector<string>());

  TServerDetails* leader;
  ASSERT_OK(GetLeaderReplicaWithRetries(tablet_id_, &leader));

  unordered_set<TServerDetails*> killed_leaders;

  const int kNumLeadersToKill = FLAGS_num_replicas / 2;
  const int kFinalNumReplicas = FLAGS_num_replicas / 2 + 1;

  for (int leaders_killed = 0; leaders_killed < kFinalNumReplicas; leaders_killed++) {
    LOG(INFO) << Substitute("Writing data to leader of $0-node config ($1 alive)...",
                            FLAGS_num_replicas, FLAGS_num_replicas - leaders_killed);

    InsertTestRowsRemoteThread(leaders_killed * FLAGS_client_inserts_per_thread,
                               FLAGS_client_inserts_per_thread,
                               FLAGS_client_num_batches_per_thread,
                               vector<CountDownLatch*>());

    // At this point, the writes are flushed but the commit index may not be
    // propagated to all replicas. We kill the leader anyway.
    if (leaders_killed < kNumLeadersToKill) {
      LOG(INFO) << "Killing current leader " << leader->instance_id.permanent_uuid() << "...";
      cluster_->tablet_server_by_uuid(leader->uuid())->Shutdown();
      InsertOrDie(&killed_leaders, leader);

      LOG(INFO) << "Waiting for new guy to be elected leader.";
      ASSERT_OK(GetLeaderReplicaWithRetries(tablet_id_, &leader));
    }
  }

  // Restart every node that was killed, and wait for the nodes to converge
  for (TServerDetails* killed_node : killed_leaders) {
    CHECK_OK(cluster_->tablet_server_by_uuid(killed_node->uuid())->Restart());
  }
  // Verify the data on the remaining replicas.
  ASSERT_ALL_REPLICAS_AGREE(FLAGS_client_inserts_per_thread * kFinalNumReplicas);
}

// Single-replica leader election test.
TEST_F(RaftConsensusITest, TestAutomaticLeaderElectionOneReplica) {
  FLAGS_num_tablet_servers = 1;
  FLAGS_num_replicas = 1;
  vector<string> ts_flags;
  vector<string> master_flags = { "--catalog_manager_allow_local_consensus=false" };
  BuildAndStart(ts_flags, master_flags);

  TServerDetails* leader;
  ASSERT_OK(GetLeaderReplicaWithRetries(tablet_id_, &leader));
}

void RaftConsensusITest::StubbornlyWriteSameRowThread(int replica_idx, const AtomicBool* finish) {
  vector<TServerDetails*> servers;
  AppendValuesFromMap(tablet_servers_, &servers);
  CHECK_LT(replica_idx, servers.size());
  TServerDetails* ts = servers[replica_idx];

  // Manually construct an RPC to our target replica. We expect most of the calls
  // to fail either with an "already present" or an error because we are writing
  // to a follower. That's OK, though - what we care about for this test is
  // just that the operations Apply() in the same order everywhere (even though
  // in this case the result will just be an error).
  WriteRequestPB req;
  WriteResponsePB resp;
  RpcController rpc;
  req.set_tablet_id(tablet_id_);
  ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));
  AddTestRowToPB(RowOperationsPB::INSERT, schema_, kTestRowKey, kTestRowIntVal,
                 "hello world", req.mutable_row_operations());

  while (!finish->Load()) {
    resp.Clear();
    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(10));
    ignore_result(ts->tserver_proxy->Write(req, &resp, &rpc));
    VLOG(1) << "Response from server " << replica_idx << ": "
            << resp.ShortDebugString();
  }
}

// Regression test for KUDU-597, an issue where we could mis-order operations on
// a machine if the following sequence occurred:
//  1) Replica is a FOLLOWER
//  2) A client request hits the machine
//  3) It receives some operations from the current leader
//  4) It gets elected LEADER
// In this scenario, it would incorrectly sequence the client request's PREPARE phase
// before the operations received in step (3), even though the correct behavior would be
// to either reject them or sequence them after those operations, because the operation
// index is higher.
//
// The test works by setting up three replicas and manually hammering them with write
// requests targeting a single row. If the bug exists, then TransactionOrderVerifier
// will trigger an assertion because the prepare order and the op indexes will become
// misaligned.
TEST_F(RaftConsensusITest, TestKUDU_597) {
  FLAGS_num_replicas = 3;
  FLAGS_num_tablet_servers = 3;
  BuildAndStart(vector<string>());

  AtomicBool finish(false);
  for (int i = 0; i < FLAGS_num_tablet_servers; i++) {
    scoped_refptr<kudu::Thread> new_thread;
    CHECK_OK(kudu::Thread::Create("test", strings::Substitute("ts-test$0", i),
                                  &RaftConsensusITest::StubbornlyWriteSameRowThread,
                                  this, i, &finish, &new_thread));
    threads_.push_back(new_thread);
  }

  const int num_loops = AllowSlowTests() ? 10 : 1;
  for (int i = 0; i < num_loops; i++) {
    StopOrKillLeaderAndElectNewOne();
    SleepFor(MonoDelta::FromSeconds(1));
    ASSERT_OK(CheckTabletServersAreAlive(FLAGS_num_tablet_servers));
  }

  finish.Store(true);
  for (scoped_refptr<kudu::Thread> thr : threads_) {
    CHECK_OK(ThreadJoiner(thr.get()).Join());
  }
}

void RaftConsensusITest::AddOp(const OpId& id, ConsensusRequestPB* req) {
  ReplicateMsg* msg = req->add_ops();
  msg->mutable_id()->CopyFrom(id);
  msg->set_timestamp(id.index());
  msg->set_op_type(consensus::WRITE_OP);
  WriteRequestPB* write_req = msg->mutable_write_request();
  CHECK_OK(SchemaToPB(schema_, write_req->mutable_schema()));
  write_req->set_tablet_id(tablet_id_);
  int key = id.index() * 10000 + id.term();
  AddTestRowToPB(RowOperationsPB::INSERT, schema_, key, id.term(),
                 id.ShortDebugString(), write_req->mutable_row_operations());
}

// Regression test for KUDU-644:
// Triggers some complicated scenarios on the replica involving aborting and
// replacing transactions.
TEST_F(RaftConsensusITest, TestReplicaBehaviorViaRPC) {
  FLAGS_num_replicas = 3;
  FLAGS_num_tablet_servers = 3;
  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--enable_leader_failure_detection=false");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  BuildAndStart(ts_flags, master_flags);

  // Kill all the servers but one.
  TServerDetails *replica_ts;
  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers_, &tservers);
  ASSERT_EQ(3, tservers.size());

  // Elect server 2 as leader and wait for log index 1 to propagate to all servers.
  ASSERT_OK(StartElection(tservers[2], tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10), tablet_servers_, tablet_id_, 1));

  replica_ts = tservers[0];
  cluster_->tablet_server_by_uuid(tservers[1]->uuid())->Shutdown();
  cluster_->tablet_server_by_uuid(tservers[2]->uuid())->Shutdown();

  LOG(INFO) << "================================== Cluster setup complete.";

  // Check that the 'term' metric is correctly exposed.
  {
    int64_t term_from_metric = -1;
    ASSERT_OK(cluster_->tablet_server_by_uuid(replica_ts->uuid())->GetInt64Metric(
                  &METRIC_ENTITY_tablet,
                  nullptr,
                  &METRIC_raft_term,
                  "value",
                  &term_from_metric));
    ASSERT_EQ(term_from_metric, 1);
  }

  ConsensusServiceProxy* c_proxy = CHECK_NOTNULL(replica_ts->consensus_proxy.get());

  ConsensusRequestPB req;
  ConsensusResponsePB resp;
  RpcController rpc;

  // Send a simple request with no ops.
  req.set_tablet_id(tablet_id_);
  req.set_dest_uuid(replica_ts->uuid());
  req.set_caller_uuid("fake_caller");
  req.set_caller_term(2);
  req.mutable_committed_index()->CopyFrom(MakeOpId(1, 1));
  req.mutable_preceding_id()->CopyFrom(MakeOpId(1, 1));

  ASSERT_OK(c_proxy->UpdateConsensus(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error()) << resp.DebugString();

  // Send some operations, but don't advance the commit index.
  // They should not commit.
  AddOp(MakeOpId(2, 2), &req);
  AddOp(MakeOpId(2, 3), &req);
  AddOp(MakeOpId(2, 4), &req);
  rpc.Reset();
  ASSERT_OK(c_proxy->UpdateConsensus(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error()) << resp.DebugString();

  // We shouldn't read anything yet, because the ops should be pending.
  {
    vector<string> results;
    NO_FATALS(ScanReplica(replica_ts->tserver_proxy.get(), &results));
    ASSERT_EQ(0, results.size()) << results;
  }

  // Send op 2.6, but set preceding OpId to 2.4. This is an invalid
  // request, and the replica should reject it.
  req.mutable_preceding_id()->CopyFrom(MakeOpId(2, 4));
  req.clear_ops();
  AddOp(MakeOpId(2, 6), &req);
  rpc.Reset();
  ASSERT_OK(c_proxy->UpdateConsensus(req, &resp, &rpc));
  ASSERT_TRUE(resp.has_error()) << resp.DebugString();
  ASSERT_EQ(resp.error().status().message(),
            "New operation's index does not follow the previous op's index. "
            "Current: 2.6. Previous: 2.4");

  resp.Clear();
  req.clear_ops();
  // Send ops 3.5 and 2.6, then commit up to index 6, the replica
  // should fail because of the out-of-order terms.
  req.mutable_preceding_id()->CopyFrom(MakeOpId(2, 4));
  AddOp(MakeOpId(3, 5), &req);
  AddOp(MakeOpId(2, 6), &req);
  rpc.Reset();
  ASSERT_OK(c_proxy->UpdateConsensus(req, &resp, &rpc));
  ASSERT_TRUE(resp.has_error()) << resp.DebugString();
  ASSERT_EQ(resp.error().status().message(),
            "New operation's term is not >= than the previous op's term."
            " Current: 2.6. Previous: 3.5");

  // Regression test for KUDU-639: if we send a valid request, but the
  // current commit index is higher than the data we're sending, we shouldn't
  // commit anything higher than the last op sent by the leader.
  //
  // To test, we re-send operation 2.3, with the correct preceding ID 2.2,
  // but we set the committed index to 2.4. This should only commit
  // 2.2 and 2.3.
  resp.Clear();
  req.clear_ops();
  req.mutable_preceding_id()->CopyFrom(MakeOpId(2, 2));
  AddOp(MakeOpId(2, 3), &req);
  req.mutable_committed_index()->CopyFrom(MakeOpId(2, 4));
  rpc.Reset();
  ASSERT_OK(c_proxy->UpdateConsensus(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error()) << resp.DebugString();
  // Verify only 2.2 and 2.3 are committed.
  {
    vector<string> results;
    NO_FATALS(WaitForRowCount(replica_ts->tserver_proxy.get(), 2, &results));
    ASSERT_STR_CONTAINS(results[0], "term: 2 index: 2");
    ASSERT_STR_CONTAINS(results[1], "term: 2 index: 3");
  }

  resp.Clear();
  req.clear_ops();
  // Now send some more ops, and commit the earlier ones.
  req.mutable_committed_index()->CopyFrom(MakeOpId(2, 4));
  req.mutable_preceding_id()->CopyFrom(MakeOpId(2, 4));
  AddOp(MakeOpId(2, 5), &req);
  AddOp(MakeOpId(2, 6), &req);
  rpc.Reset();
  ASSERT_OK(c_proxy->UpdateConsensus(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error()) << resp.DebugString();

  // Verify they are committed.
  {
    vector<string> results;
    NO_FATALS(WaitForRowCount(replica_ts->tserver_proxy.get(), 3, &results));
    ASSERT_STR_CONTAINS(results[0], "term: 2 index: 2");
    ASSERT_STR_CONTAINS(results[1], "term: 2 index: 3");
    ASSERT_STR_CONTAINS(results[2], "term: 2 index: 4");
  }

  // At this point, we still have two operations which aren't committed. If we
  // try to perform a snapshot-consistent scan, we should time out rather than
  // hanging the RPC service thread.
  {
    ScanRequestPB req;
    ScanResponsePB resp;
    RpcController rpc;
    rpc.set_timeout(MonoDelta::FromMilliseconds(100));
    NewScanRequestPB* scan = req.mutable_new_scan_request();
    scan->set_tablet_id(tablet_id_);
    scan->set_read_mode(READ_AT_SNAPSHOT);
    ASSERT_OK(SchemaToColumnPBs(schema_, scan->mutable_projected_columns()));

    // Send the call. We expect to get a timeout passed back from the server side
    // (i.e. not an RPC timeout)
    req.set_batch_size_bytes(0);
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(replica_ts->tserver_proxy->Scan(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    string err_str = StatusFromPB(resp.error().status()).ToString();
    ASSERT_STR_CONTAINS(err_str, "Timed out waiting for all transactions");
    ASSERT_STR_CONTAINS(err_str, "to commit");
  }

  resp.Clear();
  req.clear_ops();
  int leader_term = 2;
  const int kNumTerms = AllowSlowTests() ? 10000 : 100;
  while (leader_term < kNumTerms) {
    leader_term++;
    // Now pretend to be a new leader (term 3) and replace the earlier ops
    // without committing the new replacements.
    req.set_caller_term(leader_term);
    req.set_caller_uuid("new_leader");
    req.mutable_preceding_id()->CopyFrom(MakeOpId(2, 4));
    req.clear_ops();
    AddOp(MakeOpId(leader_term, 5), &req);
    AddOp(MakeOpId(leader_term, 6), &req);
    rpc.Reset();
    ASSERT_OK(c_proxy->UpdateConsensus(req, &resp, &rpc));
    ASSERT_FALSE(resp.has_error()) << "Req: " << req.ShortDebugString()
        << " Resp: " << resp.DebugString();
  }

  // Send an empty request from the newest term which should commit
  // the earlier ops.
  {
    req.mutable_preceding_id()->CopyFrom(MakeOpId(leader_term, 6));
    req.mutable_committed_index()->CopyFrom(MakeOpId(leader_term, 6));
    req.clear_ops();
    rpc.Reset();
    ASSERT_OK(c_proxy->UpdateConsensus(req, &resp, &rpc));
    ASSERT_FALSE(resp.has_error()) << resp.DebugString();
  }

  // Verify the new rows are committed.
  {
    vector<string> results;
    NO_FATALS(WaitForRowCount(replica_ts->tserver_proxy.get(), 5, &results));
    SCOPED_TRACE(results);
    ASSERT_STR_CONTAINS(results[3], Substitute("term: $0 index: 5", leader_term));
    ASSERT_STR_CONTAINS(results[4], Substitute("term: $0 index: 6", leader_term));
  }
}

TEST_F(RaftConsensusITest, TestLeaderStepDown) {
  FLAGS_num_replicas = 3;
  FLAGS_num_tablet_servers = 3;

  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--enable_leader_failure_detection=false");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  BuildAndStart(ts_flags, master_flags);

  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers_, &tservers);

  // Start with no leader.
  Status s = GetReplicaStatusAndCheckIfLeader(tservers[0], tablet_id_, MonoDelta::FromSeconds(10));
  ASSERT_TRUE(s.IsIllegalState()) << "TS #0 should not be leader yet: " << s.ToString();

  // Become leader.
  ASSERT_OK(StartElection(tservers[0], tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitUntilLeader(tservers[0], tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WriteSimpleTestRow(tservers[0], tablet_id_, RowOperationsPB::INSERT,
                               kTestRowKey, kTestRowIntVal, "foo", MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10), tablet_servers_, tablet_id_, 2));

  // Step down and test that a 2nd stepdown returns the expected result.
  ASSERT_OK(LeaderStepDown(tservers[0], tablet_id_, MonoDelta::FromSeconds(10)));
  TabletServerErrorPB error;
  s = LeaderStepDown(tservers[0], tablet_id_, MonoDelta::FromSeconds(10), &error);
  ASSERT_TRUE(s.IsIllegalState()) << "TS #0 should not be leader anymore: " << s.ToString();
  ASSERT_EQ(TabletServerErrorPB::NOT_THE_LEADER, error.code()) << error.ShortDebugString();

  s = WriteSimpleTestRow(tservers[0], tablet_id_, RowOperationsPB::INSERT,
                         kTestRowKey, kTestRowIntVal, "foo", MonoDelta::FromSeconds(10));
  ASSERT_TRUE(s.IsIllegalState()) << "TS #0 should not accept writes as follower: "
                                  << s.ToString();
}

void RaftConsensusITest::AssertMajorityRequiredForElectionsAndWrites(
    const TabletServerMap& tablet_servers, const string& leader_uuid) {

  TServerDetails* initial_leader = FindOrDie(tablet_servers, leader_uuid);

  // Calculate number of servers to leave unpaused (minority).
  // This math is a little unintuitive but works for cluster sizes including 2 and 1.
  // Note: We assume all of these TSes are voters.
  int config_size = tablet_servers.size();
  int minority_to_retain = MajoritySize(config_size) - 1;

  // Only perform this part of the test if we have some servers to pause, else
  // the failure assertions will throw.
  if (config_size > 1) {
    // Pause enough replicas to prevent a majority.
    int num_to_pause = config_size - minority_to_retain;
    LOG(INFO) << "Pausing " << num_to_pause << " tablet servers in config of size " << config_size;
    vector<string> paused_uuids;
    for (const TabletServerMap::value_type& entry : tablet_servers) {
      if (paused_uuids.size() == num_to_pause) {
        continue;
      }
      const string& replica_uuid = entry.first;
      if (replica_uuid == leader_uuid) {
        // Always leave this one alone.
        continue;
      }
      ExternalTabletServer* replica_ts = cluster_->tablet_server_by_uuid(replica_uuid);
      ASSERT_OK(replica_ts->Pause());
      paused_uuids.push_back(replica_uuid);
    }

    // Ensure writes timeout while only a minority is alive.
    Status s = WriteSimpleTestRow(initial_leader, tablet_id_, RowOperationsPB::UPDATE,
                                  kTestRowKey, kTestRowIntVal, "foo",
                                  MonoDelta::FromMilliseconds(100));
    ASSERT_TRUE(s.IsTimedOut()) << s.ToString();

    // Step down.
    ASSERT_OK(LeaderStepDown(initial_leader, tablet_id_, MonoDelta::FromSeconds(10)));

    // Assert that elections time out without a live majority.
    // We specify a very short timeout here to keep the tests fast.
    ASSERT_OK(StartElection(initial_leader, tablet_id_, MonoDelta::FromSeconds(10)));
    s = WaitUntilLeader(initial_leader, tablet_id_, MonoDelta::FromMilliseconds(100));
    ASSERT_TRUE(s.IsTimedOut()) << s.ToString();
    LOG(INFO) << "Expected timeout encountered on election with weakened config: " << s.ToString();

    // Resume the paused servers.
    LOG(INFO) << "Resuming " << num_to_pause << " tablet servers in config of size " << config_size;
    for (const string& replica_uuid : paused_uuids) {
      ExternalTabletServer* replica_ts = cluster_->tablet_server_by_uuid(replica_uuid);
      ASSERT_OK(replica_ts->Resume());
    }
  }

  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(20), tablet_servers, tablet_id_, 1));

  // Now an election should succeed.
  ASSERT_OK(StartElection(initial_leader, tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitUntilLeader(initial_leader, tablet_id_, MonoDelta::FromSeconds(10)));
  LOG(INFO) << "Successful election with full config of size " << config_size;

  // And a write should also succeed.
  ASSERT_OK(WriteSimpleTestRow(initial_leader, tablet_id_, RowOperationsPB::UPDATE,
                               kTestRowKey, kTestRowIntVal, Substitute("qsz=$0", config_size),
                               MonoDelta::FromSeconds(10)));
}

// Return the replicas of the specified 'tablet_id', as seen by the Master.
Status RaftConsensusITest::GetTabletLocations(const string& tablet_id, const MonoDelta& timeout,
                                              master::TabletLocationsPB* tablet_locations) {
  RpcController rpc;
  rpc.set_timeout(timeout);
  GetTabletLocationsRequestPB req;
  *req.add_tablet_ids() = tablet_id;
  GetTabletLocationsResponsePB resp;
  RETURN_NOT_OK(cluster_->master_proxy()->GetTabletLocations(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  if (resp.errors_size() > 0) {
    CHECK_EQ(1, resp.errors_size()) << resp.ShortDebugString();
    CHECK_EQ(tablet_id, resp.errors(0).tablet_id()) << resp.ShortDebugString();
    return StatusFromPB(resp.errors(0).status());
  }
  CHECK_EQ(1, resp.tablet_locations_size()) << resp.ShortDebugString();
  *tablet_locations = resp.tablet_locations(0);
  return Status::OK();
}

void RaftConsensusITest::WaitForReplicasReportedToMaster(
    int num_replicas, const string& tablet_id,
    const MonoDelta& timeout,
    WaitForLeader wait_for_leader,
    bool* has_leader,
    master::TabletLocationsPB* tablet_locations) {
  MonoTime deadline(MonoTime::Now(MonoTime::FINE));
  deadline.AddDelta(timeout);
  while (true) {
    ASSERT_OK(GetTabletLocations(tablet_id, timeout, tablet_locations));
    *has_leader = false;
    if (tablet_locations->replicas_size() == num_replicas) {
      for (const master::TabletLocationsPB_ReplicaPB& replica :
                    tablet_locations->replicas()) {
        if (replica.role() == RaftPeerPB::LEADER) {
          *has_leader = true;
        }
      }
      if (wait_for_leader == NO_WAIT_FOR_LEADER ||
          (wait_for_leader == WAIT_FOR_LEADER && *has_leader)) {
        break;
      }
    }
    if (deadline.ComesBefore(MonoTime::Now(MonoTime::FINE))) break;
    SleepFor(MonoDelta::FromMilliseconds(20));
  }
  ASSERT_EQ(num_replicas, tablet_locations->replicas_size()) << tablet_locations->DebugString();
  if (wait_for_leader == WAIT_FOR_LEADER) {
    ASSERT_TRUE(*has_leader) << tablet_locations->DebugString();
  }
}

// Basic test of adding and removing servers from a configuration.
TEST_F(RaftConsensusITest, TestAddRemoveServer) {
  MonoDelta kTimeout = MonoDelta::FromSeconds(10);
  FLAGS_num_tablet_servers = 3;
  FLAGS_num_replicas = 3;
  vector<string> ts_flags = { "--enable_leader_failure_detection=false" };
  vector<string> master_flags = { "--master_add_server_when_underreplicated=false" };
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  NO_FATALS(BuildAndStart(ts_flags, master_flags));

  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers_, &tservers);
  ASSERT_EQ(FLAGS_num_tablet_servers, tservers.size());

  // Elect server 0 as leader and wait for log index 1 to propagate to all servers.
  TServerDetails* leader_tserver = tservers[0];
  const string& leader_uuid = tservers[0]->uuid();
  ASSERT_OK(StartElection(leader_tserver, tablet_id_, kTimeout));
  ASSERT_OK(WaitForServersToAgree(kTimeout, tablet_servers_, tablet_id_, 1));

  // Make sure the server rejects removal of itself from the configuration.
  Status s = RemoveServer(leader_tserver, tablet_id_, leader_tserver, boost::none, kTimeout);
  ASSERT_TRUE(s.IsInvalidArgument()) << "Should not be able to remove self from config: "
                                     << s.ToString();

  // Insert the row that we will update throughout the test.
  ASSERT_OK(WriteSimpleTestRow(leader_tserver, tablet_id_, RowOperationsPB::INSERT,
                               kTestRowKey, kTestRowIntVal, "initial insert", kTimeout));

  // Kill the master, so we can change the config without interference.
  cluster_->master()->Shutdown();

  TabletServerMap active_tablet_servers = tablet_servers_;

  // Do majority correctness check for 3 servers.
  NO_FATALS(AssertMajorityRequiredForElectionsAndWrites(active_tablet_servers, leader_uuid));
  OpId opid;
  ASSERT_OK(GetLastOpIdForReplica(tablet_id_, leader_tserver, consensus::RECEIVED_OPID, kTimeout,
                                  &opid));
  int64_t cur_log_index = opid.index();

  // Go from 3 tablet servers down to 1 in the configuration.
  vector<int> remove_list = { 2, 1 };
  for (int to_remove_idx : remove_list) {
    int num_servers = active_tablet_servers.size();
    LOG(INFO) << "Remove: Going from " << num_servers << " to " << num_servers - 1 << " replicas";

    TServerDetails* tserver_to_remove = tservers[to_remove_idx];
    LOG(INFO) << "Removing tserver with uuid " << tserver_to_remove->uuid();
    ASSERT_OK(RemoveServer(leader_tserver, tablet_id_, tserver_to_remove, boost::none, kTimeout));
    ASSERT_EQ(1, active_tablet_servers.erase(tserver_to_remove->uuid()));
    ASSERT_OK(WaitForServersToAgree(kTimeout, active_tablet_servers, tablet_id_, ++cur_log_index));

    // Do majority correctness check for each incremental decrease.
    NO_FATALS(AssertMajorityRequiredForElectionsAndWrites(active_tablet_servers, leader_uuid));
    ASSERT_OK(GetLastOpIdForReplica(tablet_id_, leader_tserver, consensus::RECEIVED_OPID, kTimeout,
                                    &opid));
    cur_log_index = opid.index();
  }

  // Add the tablet servers back, in reverse order, going from 1 to 3 servers in the configuration.
  vector<int> add_list = { 1, 2 };
  for (int to_add_idx : add_list) {
    int num_servers = active_tablet_servers.size();
    LOG(INFO) << "Add: Going from " << num_servers << " to " << num_servers + 1 << " replicas";

    TServerDetails* tserver_to_add = tservers[to_add_idx];
    LOG(INFO) << "Adding tserver with uuid " << tserver_to_add->uuid();
    ASSERT_OK(AddServer(leader_tserver, tablet_id_, tserver_to_add, RaftPeerPB::VOTER, boost::none,
                        kTimeout));
    InsertOrDie(&active_tablet_servers, tserver_to_add->uuid(), tserver_to_add);
    ASSERT_OK(WaitForServersToAgree(kTimeout, active_tablet_servers, tablet_id_, ++cur_log_index));

    // Do majority correctness check for each incremental increase.
    NO_FATALS(AssertMajorityRequiredForElectionsAndWrites(active_tablet_servers, leader_uuid));
    ASSERT_OK(GetLastOpIdForReplica(tablet_id_, leader_tserver, consensus::RECEIVED_OPID, kTimeout,
                                    &opid));
    cur_log_index = opid.index();
  }
}

// Regression test for KUDU-1169: a crash when a Config Change operation is replaced
// by a later leader.
TEST_F(RaftConsensusITest, TestReplaceChangeConfigOperation) {
  FLAGS_num_tablet_servers = 3;
  FLAGS_num_replicas = 3;
  vector<string> ts_flags = { "--enable_leader_failure_detection=false" };
  vector<string> master_flags = { "--master_add_server_when_underreplicated=false" };
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  NO_FATALS(BuildAndStart(ts_flags, master_flags));

  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers_, &tservers);
  ASSERT_EQ(FLAGS_num_tablet_servers, tservers.size());


  // Elect server 0 as leader and wait for log index 1 to propagate to all servers.
  TServerDetails* leader_tserver = tservers[0];

  TabletServerMap original_followers = tablet_servers_;
  ASSERT_EQ(1, original_followers.erase(leader_tserver->uuid()));


  ASSERT_OK(StartElection(leader_tserver, tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10), tablet_servers_, tablet_id_, 1));

  // Shut down servers 1 and 2, so that server 1 can't replicate anything.
  cluster_->tablet_server_by_uuid(tservers[1]->uuid())->Shutdown();
  cluster_->tablet_server_by_uuid(tservers[2]->uuid())->Shutdown();

  // Now try to replicate a ChangeConfig operation. This should get stuck and time out
  // because the server can't replicate any operations.
  TabletServerErrorPB::Code error_code;
  Status s = RemoveServer(leader_tserver, tablet_id_, tservers[1],
                          -1, MonoDelta::FromSeconds(1),
                          &error_code);
  ASSERT_TRUE(s.IsTimedOut());

  // Pause the leader, and restart the other servers.
  cluster_->tablet_server_by_uuid(tservers[0]->uuid())->Pause();
  ASSERT_OK(cluster_->tablet_server_by_uuid(tservers[1]->uuid())->Restart());
  ASSERT_OK(cluster_->tablet_server_by_uuid(tservers[2]->uuid())->Restart());

  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10), original_followers, tablet_id_, 1));

  // Elect one of the other servers.
  ASSERT_OK(StartElection(tservers[1], tablet_id_, MonoDelta::FromSeconds(10)));

  // Resume the original leader. Its change-config operation will now be aborted
  // since it was never replicated to the majority, and the new leader will have
  // replaced the operation.
  cluster_->tablet_server_by_uuid(tservers[0]->uuid())->Resume();

  // Insert some data and verify that it propagates to all servers.
  NO_FATALS(InsertTestRowsRemoteThread(0, 10, 1, vector<CountDownLatch*>()));
  ASSERT_ALL_REPLICAS_AGREE(10);
}

// Test the atomic CAS arguments to ChangeConfig() add server and remove server.
TEST_F(RaftConsensusITest, TestAtomicAddRemoveServer) {
  FLAGS_num_tablet_servers = 3;
  FLAGS_num_replicas = 3;
  vector<string> ts_flags = { "--enable_leader_failure_detection=false" };
  vector<string> master_flags = { "--master_add_server_when_underreplicated=false" };
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  NO_FATALS(BuildAndStart(ts_flags, master_flags));

  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers_, &tservers);
  ASSERT_EQ(FLAGS_num_tablet_servers, tservers.size());

  // Elect server 0 as leader and wait for log index 1 to propagate to all servers.
  TServerDetails* leader_tserver = tservers[0];
  ASSERT_OK(StartElection(leader_tserver, tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10), tablet_servers_, tablet_id_, 1));
  int64_t cur_log_index = 1;

  TabletServerMap active_tablet_servers = tablet_servers_;

  TServerDetails* follower_ts = tservers[2];

  // Initial committed config should have opid_index == -1.
  // Server should reject request to change config from opid other than this.
  int64_t invalid_committed_opid_index = 7;
  TabletServerErrorPB::Code error_code;
  Status s = RemoveServer(leader_tserver, tablet_id_, follower_ts,
                          invalid_committed_opid_index, MonoDelta::FromSeconds(10),
                          &error_code);
  ASSERT_EQ(TabletServerErrorPB::CAS_FAILED, error_code);
  ASSERT_STR_CONTAINS(s.ToString(), "of 7 but the committed config has opid_index of -1");

  // Specifying the correct committed opid index should work.
  int64_t committed_opid_index = -1;
  ASSERT_OK(RemoveServer(leader_tserver, tablet_id_, follower_ts,
                         committed_opid_index, MonoDelta::FromSeconds(10)));

  ASSERT_EQ(1, active_tablet_servers.erase(follower_ts->uuid()));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10),
                                  active_tablet_servers, tablet_id_, ++cur_log_index));

  // Now, add the server back. Again, specifying something other than the
  // latest committed_opid_index should fail.
  invalid_committed_opid_index = -1; // The old one is no longer valid.
  s = AddServer(leader_tserver, tablet_id_, follower_ts, RaftPeerPB::VOTER,
                invalid_committed_opid_index, MonoDelta::FromSeconds(10),
                &error_code);
  ASSERT_EQ(TabletServerErrorPB::CAS_FAILED, error_code);
  ASSERT_STR_CONTAINS(s.ToString(), "of -1 but the committed config has opid_index of 2");

  // Specifying the correct committed opid index should work.
  // The previous config change op is the latest entry in the log.
  committed_opid_index = cur_log_index;
  ASSERT_OK(AddServer(leader_tserver, tablet_id_, follower_ts, RaftPeerPB::VOTER,
                      committed_opid_index, MonoDelta::FromSeconds(10)));

  InsertOrDie(&active_tablet_servers, follower_ts->uuid(), follower_ts);
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10),
                                  active_tablet_servers, tablet_id_, ++cur_log_index));
}

// Ensure that we can elect a server that is in the "pending" configuration.
// This is required by the Raft protocol. See Diego Ongaro's PhD thesis, section
// 4.1, where it states that "it is the caller’s configuration that is used in
// reaching consensus, both for voting and for log replication".
//
// This test also tests the case where a node comes back from the dead to a
// leader that was not in its configuration when it died. That should also work, i.e.
// the revived node should accept writes from the new leader.
TEST_F(RaftConsensusITest, TestElectPendingVoter) {
  // Test plan:
  //  1. Disable failure detection to avoid non-deterministic behavior.
  //  2. Start with a configuration size of 5, all servers synced.
  //  3. Remove one server from the configuration, wait until committed.
  //  4. Pause the 3 remaining non-leaders (SIGSTOP).
  //  5. Run a config change to add back the previously-removed server.
  //     Ensure that, while the op cannot be committed yet due to lack of a
  //     majority in the new config (only 2 out of 5 servers are alive), the op
  //     has been replicated to both the local leader and the new member.
  //  6. Force the existing leader to step down.
  //  7. Resume one of the paused nodes so that a majority (of the 5-node
  //     configuration, but not the original 4-node configuration) will be available.
  //  8. Start a leader election on the new (pending) node. It should win.
  //  9. Unpause the two remaining stopped nodes.
  // 10. Wait for all nodes to sync to the new leader's log.
  FLAGS_num_tablet_servers = 5;
  FLAGS_num_replicas = 5;
  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--enable_leader_failure_detection=false");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  BuildAndStart(ts_flags, master_flags);

  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers_, &tservers);
  ASSERT_EQ(FLAGS_num_tablet_servers, tservers.size());

  // Elect server 0 as leader and wait for log index 1 to propagate to all servers.
  TServerDetails* initial_leader = tservers[0];
  ASSERT_OK(StartElection(initial_leader, tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10), tablet_servers_, tablet_id_, 1));

  // The server we will remove and then bring back.
  TServerDetails* final_leader = tservers[4];

  // Kill the master, so we can change the config without interference.
  cluster_->master()->Shutdown();

  // Now remove server 4 from the configuration.
  TabletServerMap active_tablet_servers = tablet_servers_;
  LOG(INFO) << "Removing tserver with uuid " << final_leader->uuid();
  ASSERT_OK(RemoveServer(initial_leader, tablet_id_, final_leader, boost::none,
                         MonoDelta::FromSeconds(10)));
  ASSERT_EQ(1, active_tablet_servers.erase(final_leader->uuid()));
  int64_t cur_log_index = 2;
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10),
                                  active_tablet_servers, tablet_id_, cur_log_index));

  // Pause tablet servers 1 through 3, so they won't see the operation to add
  // server 4 back.
  LOG(INFO) << "Pausing 3 replicas...";
  for (int i = 1; i <= 3; i++) {
    ExternalTabletServer* replica_ts = cluster_->tablet_server_by_uuid(tservers[i]->uuid());
    ASSERT_OK(replica_ts->Pause());
  }

  // Now add server 4 back to the peers.
  // This operation will time out on the client side.
  LOG(INFO) << "Adding back Peer " << final_leader->uuid() << " and expecting timeout...";
  Status s = AddServer(initial_leader, tablet_id_, final_leader, RaftPeerPB::VOTER, boost::none,
                       MonoDelta::FromMilliseconds(100));
  ASSERT_TRUE(s.IsTimedOut()) << "Expected AddServer() to time out. Result: " << s.ToString();
  LOG(INFO) << "Timeout achieved.";
  active_tablet_servers = tablet_servers_; // Reset to the unpaused servers.
  for (int i = 1; i <= 3; i++) {
    ASSERT_EQ(1, active_tablet_servers.erase(tservers[i]->uuid()));
  }
  // Only wait for TS 0 and 4 to agree that the new change config op has been
  // replicated.
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10),
                                  active_tablet_servers, tablet_id_, ++cur_log_index));

  // Now that TS 4 is electable (and pending), have TS 0 step down.
  LOG(INFO) << "Forcing Peer " << initial_leader->uuid() << " to step down...";
  ASSERT_OK(LeaderStepDown(initial_leader, tablet_id_, MonoDelta::FromSeconds(10)));

  // Resume TS 1 so we have a majority of 3 to elect a new leader.
  LOG(INFO) << "Resuming Peer " << tservers[1]->uuid() << " ...";
  ASSERT_OK(cluster_->tablet_server_by_uuid(tservers[1]->uuid())->Resume());
  InsertOrDie(&active_tablet_servers, tservers[1]->uuid(), tservers[1]);

  // Now try to get TS 4 elected. It should succeed and push a NO_OP.
  LOG(INFO) << "Trying to elect Peer " << tservers[4]->uuid() << " ...";
  ASSERT_OK(StartElection(final_leader, tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10),
                                  active_tablet_servers, tablet_id_, ++cur_log_index));

  // Resume the remaining paused nodes.
  LOG(INFO) << "Resuming remaining nodes...";
  ASSERT_OK(cluster_->tablet_server_by_uuid(tservers[2]->uuid())->Resume());
  ASSERT_OK(cluster_->tablet_server_by_uuid(tservers[3]->uuid())->Resume());
  active_tablet_servers = tablet_servers_;

  // Do one last operation on the new leader: an insert.
  ASSERT_OK(WriteSimpleTestRow(final_leader, tablet_id_, RowOperationsPB::INSERT,
                               kTestRowKey, kTestRowIntVal, "Ob-La-Di, Ob-La-Da",
                               MonoDelta::FromSeconds(10)));

  // Wait for all servers to replicate everything up through the last write op.
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10),
                                  active_tablet_servers, tablet_id_, ++cur_log_index));
}

// Writes test rows in ascending order to a single tablet server.
// Essentially a poor-man's version of TestWorkload that only operates on a
// single tablet. Does not batch, does not tolerate timeouts, and does not
// interact with the Master. 'rows_inserted' is used to determine row id and is
// incremented prior to each successful insert. Since a write failure results in
// a crash, as long as there is no crash then 'rows_inserted' will have a
// correct count at the end of the run.
// Crashes on any failure, so 'write_timeout' should be high.
void DoWriteTestRows(const TServerDetails* leader_tserver,
                     const string& tablet_id,
                     const MonoDelta& write_timeout,
                     AtomicInt<int32_t>* rows_inserted,
                     const AtomicBool* finish) {

  while (!finish->Load()) {
    int row_key = rows_inserted->Increment();
    CHECK_OK(WriteSimpleTestRow(leader_tserver, tablet_id, RowOperationsPB::INSERT,
                                row_key, row_key, Substitute("key=$0", row_key),
                                write_timeout));
  }
}

// Test that config change works while running a workload.
TEST_F(RaftConsensusITest, TestConfigChangeUnderLoad) {
  FLAGS_num_tablet_servers = 3;
  FLAGS_num_replicas = 3;
  vector<string> ts_flags = { "--enable_leader_failure_detection=false" };
  vector<string> master_flags = { "--master_add_server_when_underreplicated=false" };
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  BuildAndStart(ts_flags, master_flags);

  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers_, &tservers);
  ASSERT_EQ(FLAGS_num_tablet_servers, tservers.size());

  // Elect server 0 as leader and wait for log index 1 to propagate to all servers.
  TServerDetails* leader_tserver = tservers[0];
  ASSERT_OK(StartElection(leader_tserver, tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10), tablet_servers_, tablet_id_, 1));

  TabletServerMap active_tablet_servers = tablet_servers_;

  // Start a write workload.
  LOG(INFO) << "Starting write workload...";
  vector<scoped_refptr<Thread> > threads;
  AtomicInt<int32_t> rows_inserted(0);
  AtomicBool finish(false);
  int num_threads = FLAGS_num_client_threads;
  for (int i = 0; i < num_threads; i++) {
    scoped_refptr<Thread> thread;
    ASSERT_OK(Thread::Create(CURRENT_TEST_NAME(), Substitute("row-writer-$0", i),
                             &DoWriteTestRows,
                             leader_tserver, tablet_id_, MonoDelta::FromSeconds(10),
                             &rows_inserted, &finish,
                             &thread));
    threads.push_back(thread);
  }

  LOG(INFO) << "Removing servers...";
  // Go from 3 tablet servers down to 1 in the configuration.
  vector<int> remove_list = { 2, 1 };
  for (int to_remove_idx : remove_list) {
    int num_servers = active_tablet_servers.size();
    LOG(INFO) << "Remove: Going from " << num_servers << " to " << num_servers - 1 << " replicas";

    TServerDetails* tserver_to_remove = tservers[to_remove_idx];
    LOG(INFO) << "Removing tserver with uuid " << tserver_to_remove->uuid();
    ASSERT_OK(RemoveServer(leader_tserver, tablet_id_, tserver_to_remove, boost::none,
                           MonoDelta::FromSeconds(10)));
    ASSERT_EQ(1, active_tablet_servers.erase(tserver_to_remove->uuid()));
    ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(active_tablet_servers.size(),
                                                  leader_tserver, tablet_id_,
                                                  MonoDelta::FromSeconds(10)));
  }

  LOG(INFO) << "Adding servers...";
  // Add the tablet servers back, in reverse order, going from 1 to 3 servers in the configuration.
  vector<int> add_list = { 1, 2 };
  for (int to_add_idx : add_list) {
    int num_servers = active_tablet_servers.size();
    LOG(INFO) << "Add: Going from " << num_servers << " to " << num_servers + 1 << " replicas";

    TServerDetails* tserver_to_add = tservers[to_add_idx];
    LOG(INFO) << "Adding tserver with uuid " << tserver_to_add->uuid();
    ASSERT_OK(AddServer(leader_tserver, tablet_id_, tserver_to_add, RaftPeerPB::VOTER, boost::none,
                        MonoDelta::FromSeconds(10)));
    InsertOrDie(&active_tablet_servers, tserver_to_add->uuid(), tserver_to_add);
    ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(active_tablet_servers.size(),
                                                  leader_tserver, tablet_id_,
                                                  MonoDelta::FromSeconds(10)));
  }

  LOG(INFO) << "Joining writer threads...";
  finish.Store(true);
  for (const scoped_refptr<Thread>& thread : threads) {
    ASSERT_OK(ThreadJoiner(thread.get()).Join());
  }

  LOG(INFO) << "Waiting for replicas to agree...";
  // Wait for all servers to replicate everything up through the last write op.
  // Since we don't batch, there should be at least # rows inserted log entries,
  // plus the initial leader's no-op, plus 2 for the removed servers, plus 2 for
  // the added servers for a total of 5.
  int min_log_index = rows_inserted.Load() + 5;
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10),
                                  active_tablet_servers, tablet_id_,
                                  min_log_index));

  LOG(INFO) << "Number of rows inserted: " << rows_inserted.Load();
  ASSERT_ALL_REPLICAS_AGREE(rows_inserted.Load());
}

TEST_F(RaftConsensusITest, TestMasterNotifiedOnConfigChange) {
  MonoDelta timeout = MonoDelta::FromSeconds(30);
  FLAGS_num_tablet_servers = 3;
  FLAGS_num_replicas = 2;
  vector<string> ts_flags;
  vector<string> master_flags = { "--master_add_server_when_underreplicated=false" };
  NO_FATALS(BuildAndStart(ts_flags, master_flags));

  LOG(INFO) << "Finding tablet leader and waiting for things to start...";
  string tablet_id = tablet_replicas_.begin()->first;

  // Determine the list of tablet servers currently in the config.
  TabletServerMap active_tablet_servers;
  for (itest::TabletReplicaMap::const_iterator iter = tablet_replicas_.find(tablet_id);
       iter != tablet_replicas_.end(); ++iter) {
    InsertOrDie(&active_tablet_servers, iter->second->uuid(), iter->second);
  }

  // Determine the server to add to the config.
  string uuid_to_add;
  for (const TabletServerMap::value_type& entry : tablet_servers_) {
    if (!ContainsKey(active_tablet_servers, entry.second->uuid())) {
      uuid_to_add = entry.second->uuid();
    }
  }
  ASSERT_FALSE(uuid_to_add.empty());

  // Get a baseline config reported to the master.
  LOG(INFO) << "Waiting for Master to see the current replicas...";
  master::TabletLocationsPB tablet_locations;
  bool has_leader;
  NO_FATALS(WaitForReplicasReportedToMaster(2, tablet_id, timeout, WAIT_FOR_LEADER,
                                            &has_leader, &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << tablet_locations.DebugString();

  // Wait for initial NO_OP to be committed by the leader.
  TServerDetails* leader_ts;
  ASSERT_OK(FindTabletLeader(tablet_servers_, tablet_id, timeout, &leader_ts));
  ASSERT_OK(WaitForServersToAgree(timeout, active_tablet_servers, tablet_id, 1));

  // Change the config.
  TServerDetails* tserver_to_add = tablet_servers_[uuid_to_add];
  LOG(INFO) << "Adding tserver with uuid " << tserver_to_add->uuid();
  ASSERT_OK(AddServer(leader_ts, tablet_id_, tserver_to_add, RaftPeerPB::VOTER, boost::none,
                      timeout));
  ASSERT_OK(WaitForServersToAgree(timeout, tablet_servers_, tablet_id_, 2));

  // Wait for the master to be notified of the config change.
  // It should continue to have the same leader, even without waiting.
  LOG(INFO) << "Waiting for Master to see config change...";
  NO_FATALS(WaitForReplicasReportedToMaster(3, tablet_id, timeout, NO_WAIT_FOR_LEADER,
                                            &has_leader, &tablet_locations));
  ASSERT_TRUE(has_leader) << tablet_locations.DebugString();
  LOG(INFO) << "Tablet locations:\n" << tablet_locations.DebugString();

  // Change the config again.
  LOG(INFO) << "Removing tserver with uuid " << tserver_to_add->uuid();
  ASSERT_OK(RemoveServer(leader_ts, tablet_id_, tserver_to_add, boost::none, timeout));
  active_tablet_servers = tablet_servers_;
  ASSERT_EQ(1, active_tablet_servers.erase(tserver_to_add->uuid()));
  ASSERT_OK(WaitForServersToAgree(timeout, active_tablet_servers, tablet_id_, 3));

  // Wait for the master to be notified of the removal.
  LOG(INFO) << "Waiting for Master to see config change...";
  NO_FATALS(WaitForReplicasReportedToMaster(2, tablet_id, timeout, NO_WAIT_FOR_LEADER,
                                            &has_leader, &tablet_locations));
  ASSERT_TRUE(has_leader) << tablet_locations.DebugString();
  LOG(INFO) << "Tablet locations:\n" << tablet_locations.DebugString();
}

// Test that even with memory pressure, a replica will still commit pending
// operations that the leader has committed.
TEST_F(RaftConsensusITest, TestEarlyCommitDespiteMemoryPressure) {
  // Enough operations to put us over our memory limit (defined below).
  const int kNumOps = 10000;

  // Set up a 3-node configuration with only one live follower so that we can
  // manipulate it directly via RPC.
  vector<string> ts_flags, master_flags;

  // If failure detection were on, a follower could be elected as leader after
  // we kill the leader below.
  ts_flags.push_back("--enable_leader_failure_detection=false");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");

  // Very low memory limit to ease testing.
  ts_flags.push_back("--memory_limit_hard_bytes=4194304");

  // Don't let transaction memory tracking get in the way.
  ts_flags.push_back("--tablet_transaction_memory_limit_mb=-1");

  BuildAndStart(ts_flags, master_flags);

  // Elect server 2 as leader, then kill it and server 1, leaving behind
  // server 0 as the sole follower.
  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers_, &tservers);
  ASSERT_EQ(3, tservers.size());
  ASSERT_OK(StartElection(tservers[2], tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(10), tablet_servers_, tablet_id_, 1));
  TServerDetails *replica_ts = tservers[0];
  cluster_->tablet_server_by_uuid(tservers[1]->uuid())->Shutdown();
  cluster_->tablet_server_by_uuid(tservers[2]->uuid())->Shutdown();

  // Pretend to be the leader and send a request to replicate some operations.
  ConsensusRequestPB req;
  ConsensusResponsePB resp;
  RpcController rpc;
  req.set_dest_uuid(replica_ts->uuid());
  req.set_tablet_id(tablet_id_);
  req.set_caller_uuid(tservers[2]->instance_id.permanent_uuid());
  req.set_caller_term(1);
  req.mutable_committed_index()->CopyFrom(MakeOpId(1, 1));
  req.mutable_preceding_id()->CopyFrom(MakeOpId(1, 1));
  for (int i = 0; i < kNumOps; i++) {
    AddOp(MakeOpId(1, 2 + i), &req);
  }
  OpId last_opid = MakeOpId(1, 2 + kNumOps - 1);
  ASSERT_OK(replica_ts->consensus_proxy->UpdateConsensus(req, &resp, &rpc));

  // At the time that the follower received our request it was still under the
  // tiny memory limit defined above, so the request should have succeeded.
  ASSERT_FALSE(resp.has_error()) << resp.DebugString();
  ASSERT_TRUE(resp.has_status());
  ASSERT_TRUE(resp.status().has_last_committed_idx());
  ASSERT_EQ(last_opid.index(), resp.status().last_received().index());
  ASSERT_EQ(1, resp.status().last_committed_idx());

  // But no operations have been applied yet; there should be no data.
  vector<string> rows;
  WaitForRowCount(replica_ts->tserver_proxy.get(), 0, &rows);

  // Try again, but this time:
  // 1. Replicate just one new operation.
  // 2. Tell the follower that the previous set of operations were committed.
  req.mutable_preceding_id()->CopyFrom(last_opid);
  req.mutable_committed_index()->CopyFrom(last_opid);
  req.mutable_ops()->Clear();
  AddOp(MakeOpId(1, last_opid.index() + 1), &req);
  rpc.Reset();
  Status s = replica_ts->consensus_proxy->UpdateConsensus(req, &resp, &rpc);

  // Our memory limit was truly tiny, so we should be over it by now...
  ASSERT_TRUE(s.IsRemoteError());
  ASSERT_STR_CONTAINS(s.ToString(), "Soft memory limit exceeded");

  // ...but despite rejecting the request, we should have committed the
  // previous set of operations. That is, we should be able to see those rows.
  WaitForRowCount(replica_ts->tserver_proxy.get(), kNumOps, &rows);
}

// Test that we can create (vivify) a new tablet via remote bootstrap.
TEST_F(RaftConsensusITest, TestAutoCreateReplica) {
  FLAGS_num_tablet_servers = 3;
  FLAGS_num_replicas = 2;
  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--enable_leader_failure_detection=false");
  ts_flags.push_back("--log_cache_size_limit_mb=1");
  ts_flags.push_back("--log_segment_size_mb=1");
  ts_flags.push_back("--log_async_preallocate_segments=false");
  ts_flags.push_back("--flush_threshold_mb=1");
  ts_flags.push_back("--maintenance_manager_polling_interval_ms=300");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  BuildAndStart(ts_flags, master_flags);

  // 50K is enough to cause flushes & log rolls.
  int num_rows_to_write = 50000;
  if (AllowSlowTests()) {
    num_rows_to_write = 150000;
  }

  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers_, &tservers);
  ASSERT_EQ(FLAGS_num_tablet_servers, tservers.size());

  TabletServerMap active_tablet_servers;
  TabletServerMap::const_iterator iter = tablet_replicas_.find(tablet_id_);
  TServerDetails* leader = iter->second;
  TServerDetails* follower = (++iter)->second;
  InsertOrDie(&active_tablet_servers, leader->uuid(), leader);
  InsertOrDie(&active_tablet_servers, follower->uuid(), follower);

  TServerDetails* new_node = nullptr;
  for (TServerDetails* ts : tservers) {
    if (!ContainsKey(active_tablet_servers, ts->uuid())) {
      new_node = ts;
      break;
    }
  }
  ASSERT_TRUE(new_node != nullptr);

  // Elect the leader (still only a consensus config size of 2).
  ASSERT_OK(StartElection(leader, tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(30), active_tablet_servers,
                                  tablet_id_, 1));

  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTableId);
  workload.set_num_replicas(FLAGS_num_replicas);
  workload.set_num_write_threads(10);
  workload.set_write_batch_size(100);
  workload.Setup();

  LOG(INFO) << "Starting write workload...";
  workload.Start();

  while (true) {
    int rows_inserted = workload.rows_inserted();
    if (rows_inserted >= num_rows_to_write) {
      break;
    }
    LOG(INFO) << "Only inserted " << rows_inserted << " rows so far, sleeping for 100ms";
    SleepFor(MonoDelta::FromMilliseconds(100));
  }

  LOG(INFO) << "Adding tserver with uuid " << new_node->uuid() << " as VOTER...";
  ASSERT_OK(AddServer(leader, tablet_id_, new_node, RaftPeerPB::VOTER, boost::none,
                      MonoDelta::FromSeconds(10)));
  InsertOrDie(&active_tablet_servers, new_node->uuid(), new_node);
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(active_tablet_servers.size(),
                                                leader, tablet_id_,
                                                MonoDelta::FromSeconds(10)));

  workload.StopAndJoin();
  int num_batches = workload.batches_completed();

  LOG(INFO) << "Waiting for replicas to agree...";
  // Wait for all servers to replicate everything up through the last write op.
  // Since we don't batch, there should be at least # rows inserted log entries,
  // plus the initial leader's no-op, plus 1 for
  // the added replica for a total == #rows + 2.
  int min_log_index = num_batches + 2;
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(120),
                                  active_tablet_servers, tablet_id_,
                                  min_log_index));

  int rows_inserted = workload.rows_inserted();
  LOG(INFO) << "Number of rows inserted: " << rows_inserted;
  ASSERT_ALL_REPLICAS_AGREE(rows_inserted);
}

TEST_F(RaftConsensusITest, TestMemoryRemainsConstantDespiteTwoDeadFollowers) {
  const int64_t kMinRejections = 100;
  const MonoDelta kMaxWaitTime = MonoDelta::FromSeconds(60);

  // Start the cluster with a low per-tablet transaction memory limit, so that
  // the test can complete faster.
  vector<string> flags;
  flags.push_back("--tablet_transaction_memory_limit_mb=2");
  BuildAndStart(flags);

  // Kill both followers.
  TServerDetails* details;
  ASSERT_OK(GetLeaderReplicaWithRetries(tablet_id_, &details));
  int num_shutdown = 0;
  int leader_ts_idx = -1;
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    ExternalTabletServer* ts = cluster_->tablet_server(i);
    if (ts->instance_id().permanent_uuid() != details->uuid()) {
      ts->Shutdown();
      num_shutdown++;
    } else {
      leader_ts_idx = i;
    }
  }
  ASSERT_EQ(2, num_shutdown);
  ASSERT_NE(-1, leader_ts_idx);

  // Because the majority of the cluster is dead and because of this workload's
  // timeout behavior, more and more wedged transactions will accumulate in the
  // leader. To prevent memory usage from skyrocketing, the leader will
  // eventually reject new transactions. That's what we're testing for here.
  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTableId);
  workload.set_timeout_allowed(true);
  workload.set_write_timeout_millis(50);
  workload.Setup();
  workload.Start();

  // Run until the leader has rejected several transactions.
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(kMaxWaitTime);
  while (true) {
    int64_t num_rejections = 0;
    ASSERT_OK(cluster_->tablet_server(leader_ts_idx)->GetInt64Metric(
        &METRIC_ENTITY_tablet,
        nullptr,
        &METRIC_transaction_memory_pressure_rejections,
        "value",
        &num_rejections));
    if (num_rejections >= kMinRejections) {
      break;
    } else if (deadline.ComesBefore(MonoTime::Now(MonoTime::FINE))) {
      FAIL() << "Ran for " << kMaxWaitTime.ToString() << ", deadline expired";
    }
    SleepFor(MonoDelta::FromMilliseconds(200));
  }
}

static void EnableLogLatency(server::GenericServiceProxy* proxy) {
  typedef unordered_map<string, string> FlagMap;
  FlagMap flags;
  InsertOrDie(&flags, "log_inject_latency", "true");
  InsertOrDie(&flags, "log_inject_latency_ms_mean", "1000");
  for (const FlagMap::value_type& e : flags) {
    SetFlagRequestPB req;
    SetFlagResponsePB resp;
    RpcController rpc;
    req.set_flag(e.first);
    req.set_value(e.second);
    ASSERT_OK(proxy->SetFlag(req, &resp, &rpc));
  }
}

// Run a regular workload with a leader that's writing to its WAL slowly.
TEST_F(RaftConsensusITest, TestSlowLeader) {
  if (!AllowSlowTests()) return;
  BuildAndStart(vector<string>());

  TServerDetails* leader;
  ASSERT_OK(GetLeaderReplicaWithRetries(tablet_id_, &leader));
  NO_FATALS(EnableLogLatency(leader->generic_proxy.get()));

  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTableId);
  workload.Setup();
  workload.Start();
  SleepFor(MonoDelta::FromSeconds(60));
}

// Run a regular workload with one follower that's writing to its WAL slowly.
TEST_F(RaftConsensusITest, TestSlowFollower) {
  if (!AllowSlowTests()) return;
  BuildAndStart(vector<string>());

  TServerDetails* leader;
  ASSERT_OK(GetLeaderReplicaWithRetries(tablet_id_, &leader));
  int num_reconfigured = 0;
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    ExternalTabletServer* ts = cluster_->tablet_server(i);
    if (ts->instance_id().permanent_uuid() != leader->uuid()) {
      TServerDetails* follower;
      follower = GetReplicaWithUuidOrNull(tablet_id_, ts->instance_id().permanent_uuid());
      ASSERT_TRUE(follower);
      NO_FATALS(EnableLogLatency(follower->generic_proxy.get()));
      num_reconfigured++;
      break;
    }
  }
  ASSERT_EQ(1, num_reconfigured);

  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTableId);
  workload.Setup();
  workload.Start();
  SleepFor(MonoDelta::FromSeconds(60));
}

// Run a special workload that constantly updates a single row on a cluster
// where every replica is writing to its WAL slowly.
TEST_F(RaftConsensusITest, TestHammerOneRow) {
  if (!AllowSlowTests()) return;
  BuildAndStart(vector<string>());

  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    ExternalTabletServer* ts = cluster_->tablet_server(i);
    TServerDetails* follower;
    follower = GetReplicaWithUuidOrNull(tablet_id_, ts->instance_id().permanent_uuid());
    ASSERT_TRUE(follower);
    NO_FATALS(EnableLogLatency(follower->generic_proxy.get()));
  }

  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTableId);
  workload.set_pathological_one_row_enabled(true);
  workload.set_num_write_threads(20);
  workload.Setup();
  workload.Start();
  SleepFor(MonoDelta::FromSeconds(60));
}

// Test that followers that fall behind the leader's log GC threshold are
// evicted from the config.
TEST_F(RaftConsensusITest, TestEvictAbandonedFollowers) {
  vector<string> ts_flags;
  AddFlagsForLogRolls(&ts_flags); // For CauseFollowerToFallBehindLogGC().
  vector<string> master_flags = { "--master_add_server_when_underreplicated=false" };
  NO_FATALS(BuildAndStart(ts_flags, master_flags));

  MonoDelta timeout = MonoDelta::FromSeconds(30);
  TabletServerMap active_tablet_servers = tablet_servers_;
  ASSERT_EQ(3, active_tablet_servers.size());

  string leader_uuid;
  int64_t orig_term;
  string follower_uuid;
  NO_FATALS(CauseFollowerToFallBehindLogGC(&leader_uuid, &orig_term, &follower_uuid));

  // Wait for the abandoned follower to be evicted.
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(2, tablet_servers_[leader_uuid],
                                                tablet_id_, timeout));
  ASSERT_EQ(1, active_tablet_servers.erase(follower_uuid));
  ASSERT_OK(WaitForServersToAgree(timeout, active_tablet_servers, tablet_id_, 2));
}

// Test that followers that fall behind the leader's log GC threshold are
// evicted from the config.
TEST_F(RaftConsensusITest, TestMasterReplacesEvictedFollowers) {
  vector<string> extra_flags;
  AddFlagsForLogRolls(&extra_flags); // For CauseFollowerToFallBehindLogGC().
  BuildAndStart(extra_flags);

  MonoDelta timeout = MonoDelta::FromSeconds(30);

  string leader_uuid;
  int64_t orig_term;
  string follower_uuid;
  NO_FATALS(CauseFollowerToFallBehindLogGC(&leader_uuid, &orig_term, &follower_uuid));

  // The follower will be evicted. Now wait for the master to cause it to be
  // remotely bootstrapped.
  ASSERT_OK(WaitForServersToAgree(timeout, tablet_servers_, tablet_id_, 2));

  ClusterVerifier v(cluster_.get());
  NO_FATALS(v.CheckCluster());
  NO_FATALS(v.CheckRowCount(kTableId, ClusterVerifier::AT_LEAST, 1));
}

// Test that a ChangeConfig() request is rejected unless the leader has
// replicated one of its own log entries during the current term.
// This is required for correctness of Raft config change. For details,
// see https://groups.google.com/forum/#!topic/raft-dev/t4xj6dJTP6E
TEST_F(RaftConsensusITest, TestChangeConfigRejectedUnlessNoopReplicated) {
  vector<string> ts_flags = { "--enable_leader_failure_detection=false" };
  vector<string> master_flags = { "--catalog_manager_wait_for_new_tablets_to_elect_leader=false" };
  BuildAndStart(ts_flags, master_flags);

  MonoDelta timeout = MonoDelta::FromSeconds(30);

  int kLeaderIndex = 0;
  TServerDetails* leader_ts = tablet_servers_[cluster_->tablet_server(kLeaderIndex)->uuid()];

  // Prevent followers from accepting UpdateConsensus requests from the leader,
  // even though they will vote. This will allow us to get the distributed
  // system into a state where there is a valid leader (based on winning an
  // election) but that leader will be unable to commit any entries from its
  // own term, making it illegal to accept ChangeConfig() requests.
  for (int i = 1; i <= 2; i++) {
    ASSERT_OK(cluster_->SetFlag(cluster_->tablet_server(i),
              "follower_reject_update_consensus_requests", "true"));
  }

  // Elect the leader.
  ASSERT_OK(StartElection(leader_ts, tablet_id_, timeout));
  ASSERT_OK(WaitUntilLeader(leader_ts, tablet_id_, timeout));

  // Now attempt to do a config change. It should be rejected because there
  // have not been any ops (notably the initial NO_OP) from the leader's term
  // that have been committed yet.
  Status s = itest::RemoveServer(leader_ts, tablet_id_,
                                 tablet_servers_[cluster_->tablet_server(1)->uuid()],
                                 boost::none, timeout);
  ASSERT_TRUE(!s.ok()) << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "Latest committed op is not from this term");
}

// Test that if for some reason none of the transactions can be prepared, that it will come
// back as an error in UpdateConsensus().
TEST_F(RaftConsensusITest, TestUpdateConsensusErrorNonePrepared) {
  const int kNumOps = 10;

  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--enable_leader_failure_detection=false");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  BuildAndStart(ts_flags, master_flags);

  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers_, &tservers);
  ASSERT_EQ(3, tservers.size());

  // Shutdown the other servers so they don't get chatty.
  cluster_->tablet_server_by_uuid(tservers[1]->uuid())->Shutdown();
  cluster_->tablet_server_by_uuid(tservers[2]->uuid())->Shutdown();

  // Configure the first server to fail all on prepare.
  TServerDetails *replica_ts = tservers[0];
  ASSERT_OK(cluster_->SetFlag(cluster_->tablet_server(0),
                "follower_fail_all_prepare", "true"));

  // Pretend to be the leader and send a request that should return an error.
  ConsensusRequestPB req;
  ConsensusResponsePB resp;
  RpcController rpc;
  req.set_dest_uuid(replica_ts->uuid());
  req.set_tablet_id(tablet_id_);
  req.set_caller_uuid(tservers[2]->instance_id.permanent_uuid());
  req.set_caller_term(0);
  req.mutable_committed_index()->CopyFrom(MakeOpId(0, 0));
  req.mutable_preceding_id()->CopyFrom(MakeOpId(0, 0));
  for (int i = 0; i < kNumOps; i++) {
    AddOp(MakeOpId(0, 1 + i), &req);
  }

  ASSERT_OK(replica_ts->consensus_proxy->UpdateConsensus(req, &resp, &rpc));
  LOG(INFO) << resp.ShortDebugString();
  ASSERT_TRUE(resp.status().has_error());
  ASSERT_EQ(consensus::ConsensusErrorPB::CANNOT_PREPARE, resp.status().error().code());
  ASSERT_STR_CONTAINS(resp.ShortDebugString(), "Could not prepare a single transaction");
}

}  // namespace tserver
}  // namespace kudu

