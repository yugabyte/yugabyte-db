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

#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/session.h"

#include "yb/client/yb_table_name.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/retryable_requests.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

using namespace std::literals;

DECLARE_int32(client_read_write_timeout_ms);
DECLARE_bool(enable_flush_retryable_requests);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(TEST_asyncrpc_finished_set_timedout);
DECLARE_bool(TEST_disable_flush_on_shutdown);
DECLARE_bool(TEST_pause_before_flushing_retryable_requests);
DECLARE_bool(TEST_pause_before_replicate_batch);
DECLARE_bool(TEST_pause_update_majority_replicated);
DECLARE_int32(ht_lease_duration_ms);
DECLARE_int32(leader_lease_duration_ms);
DECLARE_int32(retryable_request_timeout_secs);

namespace yb {
namespace integration_tests {

using tablet::TabletPeerPtr;

class RetryableRequestTest : public YBTableTestBase {
 protected:
  void BeforeStartCluster() override {
    FLAGS_enable_load_balancing = false;
    FLAGS_enable_flush_retryable_requests = true;
  }
  bool use_external_mini_cluster() override { return false; }

  size_t num_tablet_servers() override { return 3; }

  int num_tablets() override { return 1; }

  Result<std::string> GetOnlyTabletId(const client::YBTableName& table_name) {
    std::vector<std::string> ranges;
    std::vector<TabletId> tablet_ids;
    RETURN_NOT_OK(client_->GetTablets(table_name, /* max_tablets = */ 0, &tablet_ids, &ranges));
    CHECK_EQ(tablet_ids.size(), 1);
    return tablet_ids[0];
  }

  void ShutdownTabletPeer(const std::shared_ptr<tablet::TabletPeer> &tablet_peer) {
    ASSERT_OK(tablet_peer->Shutdown(tablet::ShouldAbortActiveTransactions::kTrue,
                                    tablet::DisableFlushOnShutdown::kFalse));
  }

  void CheckKeyValue(int key, int value) {
    auto result_kvs = GetScanResults(client::TableRange(table_));
    for (size_t i = 0; i < result_kvs.size(); i++) {
      std::string key_str = std::to_string(key);
      if (result_kvs[i].first == key_str) {
        ASSERT_EQ(std::to_string(value), result_kvs[i].second);
      }
    }
  }
};

class SingleServerRetryableRequestTest : public RetryableRequestTest {
 protected:
  void BeforeStartCluster() override {
    RetryableRequestTest::BeforeStartCluster();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = 10;
  }

  size_t num_tablet_servers() override { return 1; }
};

TEST_F_EX(RetryableRequestTest, YqlRequestTimeoutSecs, SingleServerRetryableRequestTest) {
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  DeleteTable();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = 660;

  // YQL table's retryable request timeout is
  // Min(FLAGS_retryable_request_timeout_secs, FLAGS_client_read_write_timeout_ms).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = 60000;
  CreateTable();
  OpenTable();
  auto tablet_id = ASSERT_RESULT(GetOnlyTabletId(table_.name()));
  auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));
  ASSERT_EQ(
      ASSERT_RESULT(tablet_peer->GetRaftConsensus())->TEST_RetryableRequestTimeoutSecs(), 60);
  DeleteTable();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = 661000;
  CreateTable();
  OpenTable();
  tablet_id = ASSERT_RESULT(GetOnlyTabletId(table_.name()));
  tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));
  ASSERT_EQ(
      ASSERT_RESULT(tablet_peer->GetRaftConsensus())->TEST_RetryableRequestTimeoutSecs(), 660);
  DeleteTable();
}

TEST_F_EX(RetryableRequestTest, TestRetryableRequestTooOld, SingleServerRetryableRequestTest) {
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  const auto tablet_id = ASSERT_RESULT(GetOnlyTabletId(table_.name()));
  auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));

  PutKeyValue("1", "1");
  ASSERT_OK(WaitFor([&] {
    return CHECK_RESULT(tablet_peer->GetRaftConsensus())->GetLastCommittedOpId().index == 2;
  }, 10s, "the second write is replicated"));

#ifndef NDEBUG
  SyncPoint::GetInstance()->LoadDependency({
      {"AsyncRpc::Finished:SetTimedOut:1",
       "RetryableRequestTest::TestRetryableRequestTooOld:WaitForSetTimedOut"},
      {"RetryableRequestTest::TestRetryableRequestTooOld:RowUpdated",
       "AsyncRpc::Finished:SetTimedOut:2"}
  });
  SyncPoint::GetInstance()->EnableProcessing();
#endif

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = true;
  // Start a new thread for writing a new row.
  // This write will be replicated but since FLAGS_TEST_asyncrpc_finished_set_timedout is set,
  // will fake a timedout error to simulate duplicate write after ybclient gets the response.
  std::thread th([&] {
    PutKeyValueIgnoreError("0", "0");
  });

  ASSERT_OK(WaitFor([&] {
    return CHECK_RESULT(tablet_peer->GetRaftConsensus())->GetLastCommittedOpId().index == 3;
  }, 10s, "the write is replicated"));

  TEST_SYNC_POINT("RetryableRequestTest::TestRetryableRequestTooOld:WaitForSetTimedOut");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = false;

  // Wait until the request is too old and cleanup expired requests.
  SleepFor(FLAGS_retryable_request_timeout_secs * 1s);
  ASSERT_OK(WaitFor([&] {
    auto op_id = CHECK_RESULT(tablet_peer->GetRaftConsensus())->MinRetryableRequestOpId();
    return op_id == OpId::Max();
  }, 10s, "the replicated request is GCed"));

  PutKeyValue("0", "1");

  TEST_SYNC_POINT("RetryableRequestTest::TestRetryableRequestTooOld:RowUpdated");
  th.join();

  CheckKeyValue(/* key = */ 0, /* value = */ 1);

#ifndef NDEBUG
  SyncPoint::GetInstance()->DisableProcessing();
  SyncPoint::GetInstance()->ClearTrace();
#endif // NDEBUG
}

TEST_F_EX(RetryableRequestTest, TestRejectOldOriginalRequest, SingleServerRetryableRequestTest) {
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  const auto tablet_id = ASSERT_RESULT(GetOnlyTabletId(table_.name()));
  auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));

  PutKeyValue("1", "1");
  ASSERT_OK(WaitFor([&] {
    return CHECK_RESULT(tablet_peer->GetRaftConsensus())->GetLastCommittedOpId().index == 2;
  }, 10s, "the second write is replicated"));

  session_->SetTimeout(5s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_replicate_batch) = true;
  std::thread th([&] {
    PutKeyValueIgnoreError("1", "0");
  });

  auto se = ScopeExit([&] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_replicate_batch) = false;
  });

  // Wait until the request is too old and cleanup expired requests.
  SleepFor(FLAGS_retryable_request_timeout_secs * 1s);
  ASSERT_OK(WaitFor([&] {
    auto op_id = CHECK_RESULT(tablet_peer->GetRaftConsensus())->MinRetryableRequestOpId();
    return op_id == OpId::Max();
  }, 10s, "the replicated request is GCed"));

  th.join();

  // Unblock the request.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_replicate_batch) = false;

  // The original write should be rejected.
  CheckKeyValue(/* key = */ 1, /* value = */ 1);
}

TEST_F_EX(
    RetryableRequestTest, TestRetryableRequestFlusherShutdown, SingleServerRetryableRequestTest) {
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  const auto tablet_id = ASSERT_RESULT(GetOnlyTabletId(table_.name()));
  auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));

  PutKeyValue("1", "1");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_flushing_retryable_requests) = true;

  ASSERT_OK(tablet_peer->log()->AllocateSegmentAndRollOver());
  ASSERT_OK(WaitFor([&] {
    return tablet_peer->TEST_RetryableRequestsFlusherState() ==
        tablet::RetryableRequestsFlushState::kFlushing;
  }, 10s, "Start flushing retryable requests"));

  // If flusher is not shutdown correctly from Tablet::CompleteShutdown, will get error:
  // "Thread belonging to thread pool 'flush-retryable-requests' with name
  // 'flush-retryable-requests [worker]' called pool function that would result in deadlock"
  // See issue https://github.com/yugabyte/yugabyte-db/issues/18631
  const auto server = mini_cluster_->mini_tablet_server(0);
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&] {
    ASSERT_OK(server->Restart());
    ASSERT_OK(server->WaitStarted());
  });
  SleepFor(1s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_flushing_retryable_requests) = false;

  thread_holder.WaitAndStop(10s);
}

TEST_F_EX(RetryableRequestTest, TestMemTrackerMetric, SingleServerRetryableRequestTest) {
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  const auto tablet_id = ASSERT_RESULT(GetOnlyTabletId(table_.name()));
  auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));
  // Make sure mem_tracker metric is in the tablet metric entity.
  std::string mem_tracker_metric_name =
      "mem_tracker_server_1_Tablets_overhead_PerTablet_Retryable_Requests";
  auto tablet_metrics_entity = tablet_peer->tablet()->GetTabletMetricsEntity();
  ASSERT_TRUE(tablet_metrics_entity->TEST_ContainMetricName(mem_tracker_metric_name));
}

class MultiNodeRetryableRequestTest : public RetryableRequestTest {
 protected:
  bool enable_ysql() override { return false; }

  void BeforeStartCluster() override {
    RetryableRequestTest::BeforeStartCluster();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ht_lease_duration_ms) = 20 * 1000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_leader_lease_duration_ms) = 20 * 1000;
  }

  Result<int> GetTabletLeaderIdx(const std::string& tablet_id) {
    int index = 0;
    for (const auto& server : mini_cluster()->mini_tablet_servers()) {
      auto peer = VERIFY_RESULT(
          server->server()->tablet_manager()->GetServingTablet(tablet_id));
      if (VERIFY_RESULT(peer->GetRaftConsensus())->GetLeaderStatus() ==
              consensus::LeaderStatus::LEADER_AND_READY) {
        return index;
      }
      ++index;
    }
    return STATUS(NotFound, "Cannot find a leader for tablet " + tablet_id);
  }
};

TEST_F_EX(RetryableRequestTest,
          PersistedRetryableRequestsWithUncommittedOpId,
          MultiNodeRetryableRequestTest) {
  // This test needs a longer lease because it needs to send write to leader when
  // both followers are down.
  const int kRows = 2;
  const auto id = CHECK_RESULT(GetOnlyTabletId(table_.name()));
  LOG(INFO) << "Tablet id is " << id;
  const auto leader_idx = CHECK_RESULT(GetTabletLeaderIdx(id));
  const auto follower_to_restart_idx = (leader_idx + 1) % 3;
  const auto other_follower_idx = (leader_idx + 2) % 3;

  // Kill one of the followers.
  const auto other_follower_server = mini_cluster_->mini_tablet_server(other_follower_idx);
  other_follower_server->Shutdown();

  // Write kvs, and by setting FLAGS_TEST_pause_update_majority_replicated,
  // ops should not be applied on the leader.
  TestThreadHolder thread_holder;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_update_majority_replicated) = true;
  for (int i = 0; i < kRows; i++) {
    thread_holder.AddThreadFunctor([this, i] {
      ASSERT_OK(PutKeyValue(client_->NewSession(60s).get(), std::to_string(i), std::to_string(i)));
    });
  }

  auto se = ScopeExit([&] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_update_majority_replicated) = false;
    thread_holder.WaitAndStop(30s);
  });

  // Wait all kvs to be applied and committed on follower.
  const auto follower_server = mini_cluster_->mini_tablet_server(follower_to_restart_idx);
  const auto follower_to_restart = ASSERT_RESULT(
          follower_server->server()->tablet_manager()->GetServingTablet(id));
  ASSERT_OK(WaitFor([&] {
    auto index = CHECK_RESULT(
        follower_to_restart->GetRaftConsensus())->GetLastCommittedOpId().index;
    LOG(INFO) << "follower last committed index " << index;
    return index > kRows;
  }, 10s, "Ops committed on follower"));

  // Shutdown the follower and try to write again, the kv shouldn't reach consensus.
  follower_server->Shutdown();
  thread_holder.AddThreadFunctor([&] {
    PutKeyValueIgnoreError(std::to_string(kRows + 1), std::to_string(kRows + 1));
  });

  // Wait one more kv has been received on leader.
  const auto leader_server = mini_cluster_->mini_tablet_server(leader_idx);
  const auto leader_peer = ASSERT_RESULT(
      leader_server->server()->tablet_manager()->GetServingTablet(id));
  ASSERT_OK(WaitFor([&] {
    auto index = CHECK_RESULT(leader_peer->GetRaftConsensus())->GetLastReceivedOpId().index;
    LOG(INFO) << "leader last received index: " << index;
    return index > kRows + 1;
  }, 10s, "Ops received on leader"));

  // Resume UpdateMajorityReplicated and the first few kvs should be able to commit on leader.
  // The last one will remain as a pending operation.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_update_majority_replicated) = false;
  ASSERT_OK(WaitFor([&] {
    auto index = CHECK_RESULT(leader_peer->GetRaftConsensus())->GetLastCommittedOpId().index;
    LOG(INFO) << "leader last committed index: " << index;
    return index > kRows;
  }, 10s, "Ops committed on leader"));

  // Flush the retryable requests that contain requests that wrote the first several kvs.
  ASSERT_OK(leader_peer->FlushRetryableRequests());

  // Restart the leader, with issue (https://github.com/yugabyte/yugabyte-db/issues/18412),
  // tablet local bootstrap will fail with the following error:
  // 'Cannot register retryable request on follower: Duplicate request...'.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_flush_on_shutdown) = true;
  ASSERT_OK(leader_server->Restart());
  ASSERT_OK(leader_server->WaitStarted());

  ASSERT_OK(follower_server->Start());
  ASSERT_OK(follower_server->WaitStarted());
  ASSERT_OK(other_follower_server->Start());
  ASSERT_OK(other_follower_server->WaitStarted());
}

} // namespace integration_tests
} // namespace yb
