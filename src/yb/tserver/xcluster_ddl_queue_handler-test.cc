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

#include <gmock/gmock.h>

#include "yb/cdc/xcluster_types.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/common_types.pb.h"

#include "yb/tserver/xcluster_ddl_queue_handler.h"

#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/tserver/tserver_xcluster_context_mock.h"
#include "yb/tserver/xcluster_output_client.h"
#include "yb/util/result.h"
#include "yb/util/test_util.h"

DECLARE_bool(xcluster_ddl_queue_enable_transactional_ddl);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);

namespace yb::tserver {

namespace {
const int64_t kCompleteJsonb = 1;
const std::string kDDLCommandCreateTable = "CREATE TABLE";
const std::string kDDLCommandCreateIndex = "CREATE INDEX";
const std::string kDDLCommandAlterTable = "ALTER TABLE";

const NamespaceId kSourceNamespaceId = "0000abcd000030008000000000000000";

std::string ConstructJson(
    int version, const std::string& command_tag, const std::string& other = "") {
  std::string complete_json_str;
  complete_json_str = static_cast<char>(kCompleteJsonb);
  return Format(
      "$0{\"version\": $1, \"command_tag\": \"$2\", \"user\": \"postgres\", \"query\": \"n/a\"$3}",
      complete_json_str, version, command_tag, (other.empty() ? "" : ", " + other));
}
}  // namespace

class XClusterDDLQueueHandlerMocked : public XClusterDDLQueueHandler {
 public:
  explicit XClusterDDLQueueHandlerMocked(
      const client::YBTableName& target_table_name, TserverXClusterContextIf& xcluster_context)
      : XClusterDDLQueueHandler(
            /* local_client */ nullptr, target_table_name.namespace_name(), kSourceNamespaceId,
            target_table_name.namespace_id(), /* log_prefix */ "", xcluster_context,
            /* connect_to_pg_func */ nullptr,
            /* update_safetime_func */
            [&](const HybridTime& ht) { last_updated_safe_time_ = ht; }) {}

  Status ExecuteCommittedDDLs(
      const std::optional<HybridTime>& apply_safe_time,
      const std::set<HybridTime>& commit_times = {}) {
    if (!safe_time_batch_) {
      safe_time_batch_ = xcluster::SafeTimeBatch();
    }
    // Construct safe_time_batch_.
    if (apply_safe_time) {
      safe_time_batch_->apply_safe_time = *apply_safe_time;
    }
    safe_time_batch_->commit_times.insert(commit_times.begin(), commit_times.end());

    return XClusterDDLQueueHandler::ExecuteCommittedDDLs();
  }

  void ClearState() {
    rows_.clear();
    get_rows_to_process_calls_ = 0;
    executed_queries_.clear();
    txn_control_queries_.clear();
    ddl_query_fail_at_index_ = -1;
    is_already_processed_calls_ = 0;
  }

  bool HasSafeTimeBatch() const { return !safe_time_batch_->commit_times.empty(); }

  xcluster::SafeTimeBatch GetSafeTimeBatch() { return *safe_time_batch_; }

  int GetNumFailsForThisDdl() const { return num_fails_for_this_ddl_; }

  Status DoPersistAndUpdateSafeTimeBatch(xcluster::SafeTimeBatch new_safe_time_batch) override {
    safe_time_batch_ = new_safe_time_batch;
    return Status::OK();
  }

  HybridTime safe_time_ht_;
  std::vector<std::tuple<int64, int64, std::string>> rows_;
  int get_rows_to_process_calls_ = 0;
  HybridTime last_updated_safe_time_ = HybridTime::kInvalid;
  std::vector<std::string> executed_queries_;
  std::vector<std::string> txn_control_queries_;
  int ddl_query_fail_at_index_ = -1;
  int is_already_processed_calls_ = 0;

 private:
  Status InitPGConnection() override { return Status::OK(); }

  Result<HybridTime> GetXClusterSafeTimeForNamespace() override { return safe_time_ht_; }

  Result<std::vector<std::tuple<int64, int64, std::string>>> GetRowsToProcess(
      const HybridTime& commit_time) override {
    get_rows_to_process_calls_++;
    auto rows = rows_;
    int64_t commit_time_int64 = commit_time.ToUint64();
    std::erase_if(rows, [commit_time_int64](const auto& row) {
      auto ddl_end_time = std::get<0>(row);
      LOG(INFO) << "Checking row with ddl_end_time: " << ddl_end_time
                << ", against commit_time: " << commit_time_int64;

      return ddl_end_time > commit_time_int64;
    });
    return rows;
  }

  Status RunAndLogQuery(const std::string& query) override {
    txn_control_queries_.push_back(query);
    return Status::OK();
  }

  Status CheckForFailedQuery() override { return Status::OK(); };

  Status ProcessDDLQuery(const XClusterDDLQueryInfo& query_info) override {
    int idx = static_cast<int>(executed_queries_.size());
    executed_queries_.push_back(query_info.command_tag);
    if (idx == ddl_query_fail_at_index_) {
      return STATUS(RuntimeError, "Simulated DDL failure");
    }
    return Status::OK();
  }

  Result<bool> IsAlreadyProcessed(const XClusterDDLQueryInfo& query_info) override {
    is_already_processed_calls_++;
    return false;
  };
};

class XClusterDDLQueueHandlerMockedTest : public YBTest {
 public:
  XClusterDDLQueueHandlerMockedTest() { google::SetVLOGLevel("xcluster*", 4); }

  const NamespaceName kNamespaceName = "testdb";
  const NamespaceId kNamespaceId = "00004000000030008000000000000000";
  const client::YBTableName ddl_queue_table = client::YBTableName(
      YQL_DATABASE_PGSQL, kNamespaceId, kNamespaceName, xcluster::kDDLQueueTableName);
};

TEST_F(XClusterDDLQueueHandlerMockedTest, VerifySafeTimes) {
  const auto ht1 = HybridTime(1, 1);
  const auto ht2 = HybridTime(2, 1);
  const auto ht3 = HybridTime(3, 1);
  const auto ht_invalid = HybridTime::kInvalid;

  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);
  {
    // Pass in invalid ht, should skip processing.
    auto s = ddl_queue_handler.ExecuteCommittedDDLs(ht_invalid);
    ASSERT_OK(s);
    ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 0);
  }
  {
    // Get invalid safe time for the namespace, results in InternalError.
    ddl_queue_handler.safe_time_ht_ = ht_invalid;
    auto s = ddl_queue_handler.ExecuteCommittedDDLs(ht1);
    ASSERT_NOK(s);
    ASSERT_EQ(s.code(), Status::Code::kInternalError);
  }
  {
    // Get lower safe time for the namespace, results in TryAgain.
    ddl_queue_handler.safe_time_ht_ = ht1;
    auto s = ddl_queue_handler.ExecuteCommittedDDLs(ht2);
    ASSERT_NOK(s);
    ASSERT_EQ(s.code(), Status::Code::kTryAgain);
  }
  {
    // Get equal safe time for the namespace, results in OK.
    ddl_queue_handler.safe_time_ht_ = ht2;
    ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht2));
  }
  {
    // Get greater safe time for the namespace, results in OK.
    ddl_queue_handler.safe_time_ht_ = ht3;
    ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht2));
  }
  // Test for commits times larger than the safe time is in HandleCommitTimesLargerThanSafeTime.
}

TEST_F(XClusterDDLQueueHandlerMockedTest, VerifyBasicJsonParsing) {
  const auto ht1 = HybridTime(1, 1);
  const auto ddl_end_time = HybridTime(1).ToUint64();
  int query_id = 1;

  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);
  ddl_queue_handler.safe_time_ht_ = ht1;

  // Verify that we receive the necessary base tags.
  std::string complete_json_str;
  complete_json_str = static_cast<char>(kCompleteJsonb);
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(ddl_end_time, query_id, Format("$0{}", kCompleteJsonb));
    // Should fail since its an empty json.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        ddl_end_time, query_id,
        Format("$0{\"command_tag\":\"CREATE TABLE\", \"query\": \"n/a\"}", kCompleteJsonb));
    // Should fail since missing version field.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        ddl_end_time, query_id, Format("$0{\"version\":1, \"query\": \"n/a\"}", kCompleteJsonb));
    // Should fail since missing command_tag field.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        ddl_end_time, query_id,
        Format("$0{\"version\":1, \"command_tag\":\"CREATE TABLE\"}", kCompleteJsonb));
    // Should fail since missing query field.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));
  }

  // Verify correct values for basic fields.
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        ddl_end_time, query_id, ConstructJson(/* version */ 999, kDDLCommandCreateTable));
    // Should fail since version is unsupported.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        ddl_end_time, query_id, ConstructJson(/* version */ 1, "INVALID COMMAND TAG"));
    // Should fail since command tag is unknown.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));
  }

  // Verify that supported command tags are processed.
  {
    ddl_queue_handler.ClearState();
    for (const auto& command_tag : {kDDLCommandCreateTable, kDDLCommandCreateIndex}) {
      ddl_queue_handler.rows_.emplace_back(
          ddl_end_time, query_id, ConstructJson(/* version */ 1, command_tag));
    }
    ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));
  }
}

TEST_F(XClusterDDLQueueHandlerMockedTest, SkipScanWhenNoNewRecords) {
  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);

  const auto ht1 = HybridTime(1, 1);
  ddl_queue_handler.safe_time_ht_ = ht1;

  // If we haven't had new records, then don't process.
  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {}));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 0);
  ASSERT_EQ(ddl_queue_handler.HasSafeTimeBatch(), false);  // Should be empty after succesful call.

  // If there are new records then process.
  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {HybridTime(1)}));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 1);
  ASSERT_EQ(ddl_queue_handler.HasSafeTimeBatch(), false);  // Should be empty after succesful call.

  // Verify calls without an apply_safe_time correctly update applied_new_records_.
  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(
      /* apply_safe_time */ std::nullopt, {HybridTime(1), HybridTime(2)}));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 1);

  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(
      /* apply_safe_time */ std::nullopt));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 1);

  // Next call with an apply_safe_time should process.
  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 3);
  ASSERT_EQ(ddl_queue_handler.HasSafeTimeBatch(), false);  // Should be empty after succesful call.
}

TEST_F(XClusterDDLQueueHandlerMockedTest, HandleCommitTimesLargerThanSafeTime) {
  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);

  // Create a batch with commit times larger than the safe time.
  // Ensure that we don't process these DDLs and don't bump up the safe time for them either.
  auto apply_safe_time = HybridTime(2, 1);
  auto commit_times = std::set<HybridTime>{
      HybridTime(1, 1),
      HybridTime(2, 1),
      // These are larger and should not be processed.
      HybridTime(3, 1),
      HybridTime(4, 1),
  };

  ddl_queue_handler.safe_time_ht_ = apply_safe_time;
  // ExecuteCommittedDDLs should return OK and not fail despite the larger commit times.
  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(apply_safe_time, commit_times));

  // Check that we only processed the DDLs with commit times less than or equal to the safe time (ie
  // the first 2 commit times).
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 2);
  // Apply safe time should still get updated to the apply_safe_time.
  ASSERT_EQ(ddl_queue_handler.last_updated_safe_time_, apply_safe_time);
  // Check that the last commit time processed is the second one.
  ASSERT_EQ(ddl_queue_handler.GetSafeTimeBatch().last_commit_time_processed, apply_safe_time);
  // Check the safe_time_batch_ afterwards still contains the larger times.
  ASSERT_EQ(ddl_queue_handler.GetSafeTimeBatch().commit_times.size(), 2);
  ASSERT_TRUE(ddl_queue_handler.GetSafeTimeBatch().commit_times.contains(HybridTime(3, 1)));
  ASSERT_TRUE(ddl_queue_handler.GetSafeTimeBatch().commit_times.contains(HybridTime(4, 1)));

  // Execute DDLs again with a higher apply_safe_time. This should process the remaining DDLs.
  apply_safe_time = HybridTime(5, 1);
  ddl_queue_handler.safe_time_ht_ = apply_safe_time;
  // Don't pass in any new commit times, want to check that we process the current batch.
  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(apply_safe_time, {}));

  // The last 2 DDLs should have been processed now.
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 4);
  // Apply safe time goes to the last commit time processed (XClusterPoller will update safe time to
  // the apply_safe_time).
  ASSERT_EQ(ddl_queue_handler.last_updated_safe_time_, HybridTime(*commit_times.rbegin()));
  // Check that the last commit time processed is the last one.
  ASSERT_EQ(
      ddl_queue_handler.GetSafeTimeBatch().last_commit_time_processed,
      HybridTime(*commit_times.rbegin()));
  // Safe time batch should be empty now.
  ASSERT_EQ(ddl_queue_handler.GetSafeTimeBatch().commit_times.size(), 0);

  ASSERT_EQ(ddl_queue_handler.last_updated_safe_time_, HybridTime(4, 1));
}

TEST_F(XClusterDDLQueueHandlerMockedTest, GetSafeTimeBetweenDDLProcessing) {
  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);

  // Insert 2 DDLs and set the apply safe time.
  const int query_id = 1;
  ddl_queue_handler.rows_.emplace_back(
      /*ddl_end_time=*/HybridTime(1, 0).ToUint64(), query_id,
      ConstructJson(/*version=*/1, kDDLCommandCreateTable));
  ddl_queue_handler.rows_.emplace_back(
      /*ddl_end_time=*/HybridTime(2, 0).ToUint64(), query_id,
      ConstructJson(/*version=*/1, kDDLCommandCreateTable));
  ddl_queue_handler.safe_time_ht_ = HybridTime(4, 1);
  const auto apply_safe_time = HybridTime(1, 1);
  {
    xcluster::SafeTimeBatch new_safe_time_batch;
    new_safe_time_batch.apply_safe_time = apply_safe_time;
    new_safe_time_batch.commit_times.insert({HybridTime(1, 1), HybridTime(2, 1)});
    ASSERT_OK(ddl_queue_handler.DoPersistAndUpdateSafeTimeBatch(std::move(new_safe_time_batch)));
  }

  // Since none of the DDLs have been processed yet, the safe time should be invalid.
  ASSERT_OK(ddl_queue_handler.UpdateSafeTimeForPause());
  ASSERT_FALSE(ddl_queue_handler.last_updated_safe_time_.is_valid());

  // Simulate processing the first DDL by removing it from the queue.
  ddl_queue_handler.rows_.erase(ddl_queue_handler.rows_.begin());

  // Safe time should now be the first DDL commit time.
  ASSERT_OK(ddl_queue_handler.UpdateSafeTimeForPause());
  ASSERT_EQ(ddl_queue_handler.last_updated_safe_time_, HybridTime(1, 1));

  // Invoke ExecuteCommittedDDLs to update the safe time batch for the first DDL.
  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(apply_safe_time, {}));
  ASSERT_EQ(ddl_queue_handler.last_updated_safe_time_, apply_safe_time);
  ASSERT_EQ(ddl_queue_handler.GetSafeTimeBatch().last_commit_time_processed, apply_safe_time);
  ASSERT_EQ(ddl_queue_handler.GetSafeTimeBatch().commit_times.size(), 1);

  // Remove all DDLs from the queue.
  ddl_queue_handler.rows_.clear();

  // Safe time should now be the last DDL commit time.
  ASSERT_OK(ddl_queue_handler.UpdateSafeTimeForPause());
  ASSERT_EQ(ddl_queue_handler.last_updated_safe_time_, HybridTime(2, 1));
}

class XClusterTransactionalDDLQueueHandlerMockedTest
    : public XClusterDDLQueueHandlerMockedTest {
 public:
  void SetUp() override {
    XClusterDDLQueueHandlerMockedTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_ddl_queue_enable_transactional_ddl) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
  }
};

TEST_F(XClusterTransactionalDDLQueueHandlerMockedTest, TransactionalDDLBatch) {
  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);

  const auto ht1 = HybridTime(1, 1);
  const auto ddl_end_time1 = HybridTime(1, 0).ToUint64();
  const auto ddl_end_time2 = HybridTime(1, 0).ToUint64() + 1;
  int query_id = 1;

  ddl_queue_handler.rows_.emplace_back(
      ddl_end_time1, query_id, ConstructJson(1, kDDLCommandCreateTable));
  ddl_queue_handler.rows_.emplace_back(
      ddl_end_time2, query_id + 1, ConstructJson(1, kDDLCommandCreateIndex));
  ddl_queue_handler.safe_time_ht_ = ht1;

  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));

  ASSERT_EQ(ddl_queue_handler.executed_queries_.size(), 2);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[0], kDDLCommandCreateTable);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[1], kDDLCommandCreateIndex);

  ASSERT_EQ(ddl_queue_handler.txn_control_queries_.size(), 2);
  ASSERT_EQ(ddl_queue_handler.txn_control_queries_[0], "BEGIN");
  ASSERT_EQ(ddl_queue_handler.txn_control_queries_[1], "COMMIT");
}

// When a DDL fails mid-batch, later DDLs should be skipped and the next attempt should rerun
// the full batch.
TEST_F(XClusterTransactionalDDLQueueHandlerMockedTest, TransactionalDDLBatchFailureAndRetry) {
  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);

  // ht1 must be larger than all ddl_end_times so GetRowsToProcess returns all 3 rows.
  const auto ht1 = HybridTime(2, 1);
  const auto ddl_end_time1 = HybridTime(1, 0).ToUint64();
  int query_id = 1;

  ddl_queue_handler.rows_.emplace_back(
      ddl_end_time1, query_id, ConstructJson(1, kDDLCommandCreateTable));
  ddl_queue_handler.rows_.emplace_back(
      ddl_end_time1 + 1, query_id + 1, ConstructJson(1, kDDLCommandCreateIndex));
  ddl_queue_handler.rows_.emplace_back(
      ddl_end_time1 + 2, query_id + 2, ConstructJson(1, kDDLCommandAlterTable));
  ddl_queue_handler.safe_time_ht_ = ht1;
  ddl_queue_handler.ddl_query_fail_at_index_ = 1;

  auto s = ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1});
  ASSERT_NOK(s);
  // DDL 3 should never be attempted after DDL 2 fails.
  ASSERT_EQ(ddl_queue_handler.executed_queries_.size(), 2);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[0], kDDLCommandCreateTable);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[1], kDDLCommandCreateIndex);
  ASSERT_EQ(ddl_queue_handler.txn_control_queries_.size(), 2);
  ASSERT_EQ(ddl_queue_handler.txn_control_queries_[0], "BEGIN");
  ASSERT_EQ(ddl_queue_handler.txn_control_queries_[1], "ABORT");

  // Retry should rerun all 3 DDLs from scratch.
  ddl_queue_handler.ddl_query_fail_at_index_ = -1;
  ddl_queue_handler.executed_queries_.clear();
  ddl_queue_handler.txn_control_queries_.clear();

  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));
  ASSERT_EQ(ddl_queue_handler.executed_queries_.size(), 3);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[0], kDDLCommandCreateTable);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[1], kDDLCommandCreateIndex);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[2], kDDLCommandAlterTable);
  ASSERT_EQ(ddl_queue_handler.txn_control_queries_.size(), 2);
  ASSERT_EQ(ddl_queue_handler.txn_control_queries_[0], "BEGIN");
  ASSERT_EQ(ddl_queue_handler.txn_control_queries_[1], "COMMIT");
}

// Retry counter accumulates across failed attempts, and all DDLs are rechecked via
// IsAlreadyProcessed on the successful retry since the rolled-back txn undid prior inserts.
TEST_F(XClusterTransactionalDDLQueueHandlerMockedTest, TransactionalDDLRetryCounterAccumulates) {
  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);

  const auto ht1 = HybridTime(1, 1);
  const auto ddl_end_time1 = HybridTime(1, 0).ToUint64();
  const auto ddl_end_time2 = HybridTime(1, 0).ToUint64() + 1;
  int query_id = 1;

  ddl_queue_handler.rows_.emplace_back(
      ddl_end_time1, query_id, ConstructJson(1, kDDLCommandCreateTable));
  ddl_queue_handler.rows_.emplace_back(
      ddl_end_time2, query_id + 1, ConstructJson(1, kDDLCommandCreateIndex));
  ddl_queue_handler.safe_time_ht_ = ht1;
  ddl_queue_handler.ddl_query_fail_at_index_ = 1;

  // Counter should increment on each failed attempt.
  for (int attempt = 1; attempt <= 3; attempt++) {
    ddl_queue_handler.executed_queries_.clear();
    ddl_queue_handler.txn_control_queries_.clear();

    auto s = ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1});
    ASSERT_NOK(s);
    ASSERT_EQ(ddl_queue_handler.GetNumFailsForThisDdl(), attempt);
    ASSERT_EQ(ddl_queue_handler.txn_control_queries_.size(), 2);
    ASSERT_EQ(ddl_queue_handler.txn_control_queries_[0], "BEGIN");
    ASSERT_EQ(ddl_queue_handler.txn_control_queries_[1], "ABORT");
  }

  // Counter should reset to 0 after a successful batch commit, and both DDLs should be rechecked
  // since DDL 1's previous insert into replicated_ddls was rolled back with the failed txn.
  ddl_queue_handler.ddl_query_fail_at_index_ = -1;
  ddl_queue_handler.is_already_processed_calls_ = 0;
  ddl_queue_handler.executed_queries_.clear();
  ddl_queue_handler.txn_control_queries_.clear();

  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));
  ASSERT_EQ(ddl_queue_handler.GetNumFailsForThisDdl(), 0);
  ASSERT_EQ(ddl_queue_handler.is_already_processed_calls_, 2);
  ASSERT_EQ(ddl_queue_handler.executed_queries_.size(), 2);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[0], kDDLCommandCreateTable);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[1], kDDLCommandCreateIndex);
}

// A single auto query should not be wrapped in BEGIN/COMMIT even with both flags enabled.
TEST_F(XClusterTransactionalDDLQueueHandlerMockedTest, TransactionalDDLSingleDDLNoWrapping) {
  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);

  const auto ht1 = HybridTime(1, 1);
  const auto ddl_end_time1 = HybridTime(1, 0).ToUint64();
  int query_id = 1;

  ddl_queue_handler.rows_.emplace_back(
      ddl_end_time1, query_id, ConstructJson(1, kDDLCommandCreateTable));
  ddl_queue_handler.safe_time_ht_ = ht1;

  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));
  ASSERT_EQ(ddl_queue_handler.executed_queries_.size(), 1);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[0], kDDLCommandCreateTable);
  ASSERT_TRUE(ddl_queue_handler.txn_control_queries_.empty());
}

// Manual queries should not count toward the auto-query size check that gates wrapping. A batch
// with 1 manual + 2 auto queries should still wrap the auto queries in BEGIN/COMMIT.
TEST_F(XClusterTransactionalDDLQueueHandlerMockedTest, TransactionalDDLMixedManualAndAutoQueries) {
  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);

  // ht1 must be larger than all ddl_end_times so GetRowsToProcess returns all 3 rows.
  const auto ht1 = HybridTime(2, 1);
  const auto ddl_end_time1 = HybridTime(1, 0).ToUint64();
  const auto ddl_end_time2 = HybridTime(1, 0).ToUint64() + 1;
  const auto ddl_end_time3 = HybridTime(1, 0).ToUint64() + 2;
  int query_id = 1;

  ddl_queue_handler.rows_.emplace_back(
      ddl_end_time1, query_id,
      ConstructJson(1, kDDLCommandCreateTable, "\"manual_replication\": true"));
  ddl_queue_handler.rows_.emplace_back(
      ddl_end_time2, query_id + 1, ConstructJson(1, kDDLCommandCreateTable));
  ddl_queue_handler.rows_.emplace_back(
      ddl_end_time3, query_id + 2, ConstructJson(1, kDDLCommandCreateIndex));
  ddl_queue_handler.safe_time_ht_ = ht1;

  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {ht1}));

  // Only the auto queries should have been executed via ProcessDDLQuery.
  ASSERT_EQ(ddl_queue_handler.executed_queries_.size(), 2);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[0], kDDLCommandCreateTable);
  ASSERT_EQ(ddl_queue_handler.executed_queries_[1], kDDLCommandCreateIndex);

  // The manual EXECUTE should be issued first, then the auto queries wrapped in BEGIN/COMMIT.
  ASSERT_EQ(ddl_queue_handler.txn_control_queries_.size(), 3);
  ASSERT_STR_CONTAINS(
      ddl_queue_handler.txn_control_queries_[0], "EXECUTE manual_replication_insert");
  ASSERT_EQ(ddl_queue_handler.txn_control_queries_[1], "BEGIN");
  ASSERT_EQ(ddl_queue_handler.txn_control_queries_[2], "COMMIT");
}

}  // namespace yb::tserver
