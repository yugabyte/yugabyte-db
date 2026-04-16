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

#include "yb/tserver/tserver_xcluster_context.h"
#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/tserver/tserver_xcluster_context_mock.h"
#include "yb/tserver/xcluster_output_client.h"
#include "yb/util/result.h"
#include "yb/util/test_util.h"

namespace yb::tserver {

namespace {
const int64_t kCompleteJsonb = 1;
const std::string kDDLCommandCreateTable = "CREATE TABLE";
const std::string kDDLCommandCreateIndex = "CREATE INDEX";

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
      const client::YBTableName& target_table_name, MockTserverXClusterContext& xcluster_context)
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
  }

  bool HasSafeTimeBatch() const { return !safe_time_batch_->commit_times.empty(); }

  xcluster::SafeTimeBatch GetSafeTimeBatch() { return *safe_time_batch_; }

  Status DoPersistAndUpdateSafeTimeBatch(xcluster::SafeTimeBatch new_safe_time_batch) override {
    safe_time_batch_ = new_safe_time_batch;
    return Status::OK();
  }

  HybridTime safe_time_ht_;
  std::vector<std::tuple<int64, int64, std::string>> rows_;
  int get_rows_to_process_calls_ = 0;
  HybridTime last_updated_safe_time_ = HybridTime::kInvalid;

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

  Status CheckForFailedQuery() override { return Status::OK(); };

  Status ProcessDDLQuery(const XClusterDDLQueryInfo& query_info) override { return Status::OK(); }

  Result<bool> IsAlreadyProcessed(const XClusterDDLQueryInfo& query_info) override {
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
}  // namespace yb::tserver
