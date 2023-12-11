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
#include "yb/util/test_util.h"

using namespace std::literals;

DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(retryable_request_timeout_secs);
DECLARE_bool(TEST_asyncrpc_finished_set_timedout);
DECLARE_bool(TEST_pause_before_replicate_batch);

namespace yb {
namespace integration_tests {

class RetryableRequestTest : public YBTableTestBase {
 protected:
  void BeforeStartCluster() override {
    FLAGS_retryable_request_timeout_secs = 10;
  }

  bool use_external_mini_cluster() override { return false; }

  size_t num_tablet_servers() override { return 1; }

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
  tablet::TabletPeerPtr tablet_peer;
  ASSERT_OK(tablet_server->server()->tablet_manager()->GetTabletPeer(tablet_id, &tablet_peer));
  ASSERT_EQ(tablet_peer->shared_raft_consensus()->TEST_RetryableRequestTimeoutSecs(), 60);
  DeleteTable();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = 661000;
  CreateTable();
  OpenTable();
  tablet_id = ASSERT_RESULT(GetOnlyTabletId(table_.name()));
  ASSERT_OK(tablet_server->server()->tablet_manager()->GetTabletPeer(tablet_id, &tablet_peer));
  ASSERT_EQ(tablet_peer->shared_raft_consensus()->TEST_RetryableRequestTimeoutSecs(), 660);
  DeleteTable();
}

TEST_F_EX(RetryableRequestTest, TestRetryableRequestTooOld, SingleServerRetryableRequestTest) {
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  const auto tablet_id = ASSERT_RESULT(GetOnlyTabletId(table_.name()));
  tablet::TabletPeerPtr tablet_peer;
  ASSERT_OK(tablet_server->server()->tablet_manager()->GetTabletPeer(tablet_id, &tablet_peer));

  PutKeyValue("1", "1");
  ASSERT_OK(WaitFor([&] {
    return tablet_peer->shared_raft_consensus()->GetLastCommittedOpId().index == 2;
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
    return tablet_peer->shared_raft_consensus()->GetLastCommittedOpId().index == 3;
  }, 10s, "the write is replicated"));

  TEST_SYNC_POINT("RetryableRequestTest::TestRetryableRequestTooOld:WaitForSetTimedOut");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = false;

  // Wait until the request is too old and cleanup expired requests.
  SleepFor(FLAGS_retryable_request_timeout_secs * 1s);
  ASSERT_OK(WaitFor([&] {
    auto op_id = tablet_peer->shared_raft_consensus()->MinRetryableRequestOpId();
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

TEST_F(RetryableRequestTest, TestRejectOldOriginalRequest) {
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  const auto tablet_id = ASSERT_RESULT(GetOnlyTabletId(table_.name()));
  tablet::TabletPeerPtr tablet_peer;
  ASSERT_OK(tablet_server->server()->tablet_manager()->GetTabletPeer(tablet_id, &tablet_peer));

  PutKeyValue("1", "1");
  ASSERT_OK(WaitFor([&] {
    return tablet_peer->shared_raft_consensus()->GetLastCommittedOpId().index == 2;
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
    auto op_id = tablet_peer->shared_raft_consensus()->MinRetryableRequestOpId();
    return op_id == OpId::Max();
  }, 10s, "the replicated request is GCed"));

  th.join();

  // Unblock the request.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_replicate_batch) = false;

  // The original write should be rejected.
  CheckKeyValue(/* key = */ 1, /* value = */ 1);
}

} // namespace integration_tests
} // namespace yb
