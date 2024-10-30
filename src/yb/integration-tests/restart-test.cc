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

#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;
using namespace std::literals;

DECLARE_bool(TEST_simulate_abrupt_server_restart);

DECLARE_bool(log_enable_background_sync);

DECLARE_int64(reuse_unclosed_segment_threshold_bytes);

DECLARE_bool(enable_flush_retryable_requests);

DECLARE_bool(TEST_asyncrpc_finished_set_timedout);

DECLARE_int32(retryable_request_timeout_secs);

namespace yb {
namespace integration_tests {

class RestartTest : public YBTableTestBase {
 protected:

  bool use_external_mini_cluster() override { return false; }

  size_t num_tablet_servers() override { return 3; }

  int num_tablets() override { return 1; }

  void GetTablet(const client::YBTableName& table_name, string* tablet_id) {
    std::vector<std::string> ranges;
    std::vector<TabletId> tablet_ids;
    ASSERT_OK(client_->GetTablets(table_name, 0 /* max_tablets */, &tablet_ids, &ranges));
    ASSERT_EQ(tablet_ids.size(), 1);
    *tablet_id = tablet_ids[0];
  }

  void ShutdownTabletPeer(const std::shared_ptr<tablet::TabletPeer> &tablet_peer) {
    ASSERT_OK(tablet_peer->Shutdown(tablet::ShouldAbortActiveTransactions::kTrue,
                                    tablet::DisableFlushOnShutdown::kFalse));
  }

  void CheckSampleKeysValues(int start, int end) {
    auto result_kvs = GetScanResults(client::TableRange(table_));
    ASSERT_EQ(end - start + 1, result_kvs.size());

    for(int i = start ; i <= end ; i++) {
      std::string num_str = std::to_string(i);
      ASSERT_EQ("key_" + num_str, result_kvs[i - start].first);
      ASSERT_EQ("value_" + num_str, result_kvs[i - start].second);
    }
  }

  void CheckKeyValue(int key, int value) {
    auto result_kvs = GetScanResults(client::TableRange(table_));
    for (size_t i = 0; i < result_kvs.size(); i++) {
      std::string key_str = "key_" + std::to_string(key);
      if (result_kvs[i].first == key_str) {
        ASSERT_EQ("value_" + std::to_string(value), result_kvs[i].second);
      }
    }
  }
};

class LogSyncTest : public RestartTest {
 protected:
  void BeforeStartCluster() override {
    // setting the flag immaterial of the default value
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_enable_background_sync) = true;
  }
};

TEST_F(RestartTest, WalFooterProperlyInitialized) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = true;
  // Disable reuse unclosed segment feature to prevent log from reusing
  // the last segment and skipping building footer.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reuse_unclosed_segment_threshold_bytes) = -1;
  auto timestamp_before_write = GetCurrentTimeMicros();
  PutKeyValue("key", "value");
  auto timestamp_after_write = GetCurrentTimeMicros();

  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  ASSERT_OK(tablet_server->Restart());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = false;

  string tablet_id;
  ASSERT_NO_FATALS(GetTablet(table_.name(), &tablet_id));
  auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));
  ASSERT_OK(tablet_server->WaitStarted());
  log::SegmentSequence segments;
  ASSERT_OK(tablet_peer->log()->GetLogReader()->GetSegmentsSnapshot(&segments));

  ASSERT_EQ(2, segments.size());
  log::ReadableLogSegmentPtr segment = ASSERT_RESULT(segments.front());
  ASSERT_TRUE(segment->HasFooter());
  ASSERT_TRUE(segment->footer().has_close_timestamp_micros());
  ASSERT_TRUE(segment->footer().close_timestamp_micros() > timestamp_before_write &&
              segment->footer().close_timestamp_micros() < timestamp_after_write);

}

TEST_F(LogSyncTest, BackgroundSync) {

  // triggers log background sync threadpool
  PutKeyValue("key_0", "value_0");
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  string tablet_id;
  ASSERT_NO_FATALS(GetTablet(table_.name(), &tablet_id));
  auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));
  CheckSampleKeysValues(0, 0);

  ASSERT_OK(tablet_server->Restart());
  ASSERT_NO_FATALS(GetTablet(table_.name(), &tablet_id));
  tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));
  ASSERT_OK(tablet_server->WaitStarted());

  // shutting down tablet_peer resets the BG sync threapool token maintained in the Log.
  PutKeyValue("key_1", "value_1");
  CheckSampleKeysValues(0, 1);
  ShutdownTabletPeer(tablet_peer);
}

class PersistRetryableRequestsTest : public RestartTest {
 protected:
  void BeforeStartCluster() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = 10;
  }

  size_t num_tablet_servers() override { return 1; }

  void TestRetryableWrite(bool wait_file_to_expire);

  Status RollLog(tablet::TabletPeerPtr peer) {
    // Rollover the log to persist retryable requests.
    RETURN_NOT_OK(peer->log()->AllocateSegmentAndRollOver());
    if (!GetAtomicFlag(&FLAGS_enable_flush_retryable_requests)) {
      return Status::OK();
    }
    return WaitFor([&] {
      return peer->TEST_HasBootstrapStateOnDisk();
    }, 10s, "retryable requests flushed to disk");
  }
};

// Test for scenario:
// 1. Replicated an WriteOp on tablet 1 and tserver crashed before replying to client.
// 2. Restart the tserver.
// 3. The client gets TimedOut and retry the same write.
// 4. Tablet 1 should reject the duplicate write.
void PersistRetryableRequestsTest::TestRetryableWrite(bool wait_file_to_expire) {
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  string tablet_id;
  ASSERT_NO_FATALS(GetTablet(table_.name(), &tablet_id));
  auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));

  int64_t new_index = 1;
  PutKeyValue("key_1", "value_1");
  new_index++;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return VERIFY_RESULT(tablet_peer->GetRaftConsensus())->GetLastCommittedOpId().index ==
               new_index;
      },
      10s,
      Format("the write $0 is replicated", new_index)));

  if (wait_file_to_expire) {
    ASSERT_OK(RollLog(tablet_peer));

    // Don't flush newer versions to disk.
    SetAtomicFlag(false, &FLAGS_enable_flush_retryable_requests);
    PutKeyValue("key_1", "value_1");
    new_index++;
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          return VERIFY_RESULT(tablet_peer->GetRaftConsensus())->GetLastCommittedOpId().index ==
                 new_index;
        },
        10s,
        Format("the write $0 is replicated", new_index)));
    ASSERT_OK(RollLog(tablet_peer));

    // Sleep for enough time to make the persisted file old enough.
    // it shouldn't replay from the op id that is covered by the persisted file.
    SleepFor((FLAGS_retryable_request_timeout_secs + 1) * 1s);
    SetAtomicFlag(true, &FLAGS_enable_flush_retryable_requests);

    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          return VERIFY_RESULT(tablet_peer->GetRaftConsensus())->MinRetryableRequestOpId() ==
                 OpId::Max();
        },
        10s,
        "retryable requests get GCed"));
  }

#ifndef NDEBUG
  SyncPoint::GetInstance()->LoadDependency({
      {"AsyncRpc::Finished:SetTimedOut:1",
       "RestartTest::TestRetryableWriteAfterRestart:WaitForSetTimedOut"},
      {"RestartTest::TestRetryableWriteAfterRestart:RowUpdated",
       "AsyncRpc::Finished:SetTimedOut:2"}
  });

  SyncPoint::GetInstance()->EnableProcessing();
#endif

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = true;
  new_index++;
  // Start a new thread for writing a new row.
  std::thread th([&] {
    PutKeyValue("key_0", "value_0");
  });

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return VERIFY_RESULT(tablet_peer->GetRaftConsensus())->GetLastCommittedOpId().index ==
               new_index;
      },
      10s,
      Format("the write $0 is replicated", new_index)));

  if (!wait_file_to_expire) {
    ASSERT_OK(RollLog(tablet_peer));
  }

  TEST_SYNC_POINT("RestartTest::TestRetryableWriteAfterRestart:WaitForSetTimedOut");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = false;

  // Write a new op to the newly allocated segment.
  PutKeyValue("key_2", "value_2");
  ASSERT_OK(tablet_peer->shared_tablet()->Flush(tablet::FlushMode::kSync));

  // Restart tserver.
  ASSERT_OK(tablet_server->Restart());
  ASSERT_OK(tablet_server->WaitStarted());

  PutKeyValue("key_0", "value_1");

  TEST_SYNC_POINT("RestartTest::TestRetryableWriteAfterRestart:RowUpdated");

  th.join();

  CheckKeyValue(/* key = */ 0, /* value = */ 1);

#ifndef NDEBUG
  SyncPoint::GetInstance()->DisableProcessing();
  SyncPoint::GetInstance()->ClearTrace();
#endif // NDEBUG
}


TEST_F(PersistRetryableRequestsTest, TestRetryableWriteAfterRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = true;
  return TestRetryableWrite(/* wait_file_to_expire */ false);
}

TEST_F(PersistRetryableRequestsTest, TestRetryableWriteWithoutPersistence) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = false;
  return TestRetryableWrite(/* wait_file_to_expire */ false);
}

TEST_F(PersistRetryableRequestsTest, TestRetryableRequestsFileTooOld) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = true;
  return TestRetryableWrite(/* wait_file_to_expire */ true);
}

} // namespace integration_tests
} // namespace yb
