// Copyright (c) YugabyteDB, Inc.
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

#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/atomic.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

class ConcurrentIsolationLevelsTest : public ExternalMiniClusterITestBase {
 public:
  void SetUp() override {
    ExternalMiniClusterITestBase::SetUp();

    std::vector<std::string> extra_tserver_flags = {
      "--ysql_enable_packed_row=true",
      "--TEST_skip_prefix_locks_invariance_check=true"
    };

    ExternalMiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    opts.num_masters = 1;
    opts.extra_tserver_flags = extra_tserver_flags;
    opts.enable_ysql = true;
    ASSERT_OK(StartCluster(opts));

    CreateTable();
  }

  void CreateTable() {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_OK(conn.Execute(
        "CREATE TABLE test_table ("
        "key INT PRIMARY KEY, "
        "value INT, "
        "thread_id INT, "
        "isolation_level TEXT)"
        " SPLIT INTO 1 TABLETS"));
  }

  void InsertWithIsolationLevel(IsolationLevel isolation_level, int thread_id, int num_inserts) {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());

    for (int i = 0; i < num_inserts; ++i) {
      // Use a combination of thread_id and counter to ensure unique keys
      int32_t key = thread_id * num_inserts + i;
      std::string isolation_str = IsolationLevel_Name(isolation_level);
      ASSERT_OK(conn.StartTransaction(isolation_level));

      // Insert the row
      ASSERT_OK(conn.ExecuteFormat(
          "INSERT INTO test_table (key, value, thread_id, isolation_level) VALUES "
            "($0, $1, $2, '$3')",
          key, i, thread_id, isolation_str));
      ASSERT_OK(conn.CommitTransaction());
    }

    LOG(INFO) << "Thread " << thread_id << " completed " << num_inserts
              << " inserts with isolation level " << IsolationLevel_Name(isolation_level);
  }

  void VerifyInserts(int expected_snapshot_inserts, int expected_serializable_inserts) {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());

    // Count inserts by isolation level
    auto snapshot_result = ASSERT_RESULT(conn.FetchRow<int64_t>(
        "SELECT COUNT(*) FROM test_table WHERE isolation_level = 'SNAPSHOT_ISOLATION'"));
    auto serializable_result = ASSERT_RESULT(conn.FetchRow<int64_t>(
        "SELECT COUNT(*) FROM test_table WHERE isolation_level = 'SERIALIZABLE_ISOLATION'"));

    LOG(INFO) << "Found " << snapshot_result << " snapshot inserts and "
              << serializable_result << " serializable inserts";

    ASSERT_EQ(snapshot_result, expected_snapshot_inserts);
    ASSERT_EQ(serializable_result, expected_serializable_inserts);
  }

  // Thread to toggle skip_prefix_locks gflag
  void ToggleSkipPrefixLocksThread(std::atomic<bool>& stop_flag) {
    bool current_value = false;
    int toggle_count = 0;
    while (!stop_flag.load(std::memory_order_acquire)) {
      current_value = !current_value;
      toggle_count++;
      for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
        auto* tserver = cluster_->tablet_server(i);
        if (tserver) {
          ASSERT_OK(cluster_->SetFlag(tserver, "skip_prefix_locks",
                                      toggle_count%2 ? "true" : "false"));
        }
      }
      std::this_thread::sleep_for(3s);
    }
    LOG(INFO) << "Gflag toggle thread stopped after " << toggle_count << " toggles";
  }

};

TEST_F(ConcurrentIsolationLevelsTest, ConcurrentSnapshotAndSerializableInserts) {
  constexpr int kNumInsertsPerThread = 1000;

  TestThreadHolder thread_holder_toggle;
  TestThreadHolder thread_holder;

  // Start the gflag toggle thread
  thread_holder_toggle.AddThreadFunctor([this, &thread_holder_toggle]() {
    ToggleSkipPrefixLocksThread(thread_holder_toggle.stop_flag());
  });

  // Start the insert threads
  constexpr int kNumThreads = 1;
  for (int i = 0; i < 2 * kNumThreads; ++i) {
  thread_holder.AddThreadFunctor([this, i]() {
    auto isolation = i < kNumThreads ? IsolationLevel::SNAPSHOT_ISOLATION
                                     : IsolationLevel::SERIALIZABLE_ISOLATION;
    InsertWithIsolationLevel(isolation, i, kNumInsertsPerThread);
  });
  }

  // Wait for both threads to complete
  thread_holder.JoinAll();
  thread_holder_toggle.Stop();

  // Verify that all inserts were successful
  VerifyInserts(kNumInsertsPerThread, kNumInsertsPerThread);

  // Verify that we have the expected total number of rows
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  auto total_rows = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM test_table"));

  LOG(INFO) << "Total rows in table: " << total_rows;
  ASSERT_EQ(total_rows, 2 * kNumInsertsPerThread * kNumThreads);
}
} // namespace yb
