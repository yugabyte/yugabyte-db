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

#include <memory>
#include <optional>

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
            /* update_safetime_func */ [](HybridTime i) { return; }) {}

  Status ExecuteCommittedDDLs(
      const std::optional<HybridTime>& apply_safe_time,
      const std::set<uint64_t>& commit_times = {0}) {
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

  HybridTime safe_time_ht_;
  std::vector<std::tuple<int64, int64, std::string>> rows_;
  int get_rows_to_process_calls_ = 0;

 private:
  Status InitPGConnection() override { return Status::OK(); }

  Result<HybridTime> GetXClusterSafeTimeForNamespace() override { return safe_time_ht_; }

  Result<std::vector<std::tuple<int64, int64, std::string>>> GetRowsToProcess(
      const HybridTime& apply_safe_time) override {
    get_rows_to_process_calls_++;
    return rows_;
  }

  Status CheckForFailedQuery() override { return Status::OK(); };

  Status ProcessDDLQuery(const DDLQueryInfo& query_info) override { return Status::OK(); }

  Result<bool> CheckIfAlreadyProcessed(const DDLQueryInfo& query_info) override { return false; };

  Status ClearSafeTimeBatch() override {
    safe_time_batch_ = xcluster::SafeTimeBatch();
    return Status::OK();
  }
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
  const auto ht1 = HybridTime::FromMicrosecondsAndLogicalValue(1, 1);
  const auto ht2 = HybridTime::FromMicrosecondsAndLogicalValue(2, 1);
  const auto ht3 = HybridTime::FromMicrosecondsAndLogicalValue(3, 1);
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
  {
    // Check that we error if we have a DDL with a commit time greater than the target safe time.
    ddl_queue_handler.safe_time_ht_ = ht3;
    std::set<uint64_t> commit_times{ht2.ToUint64()};
    ddl_queue_handler.rows_.emplace_back(/* ddl_end_time */ 1, /*query_id*/ 1,
                                         ConstructJson(/* version */ 1, kDDLCommandCreateTable));
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, commit_times));
    // Should be ok if target safe time is equal or greater.
    ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht2, commit_times));
    ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht3, commit_times));
  }
}

TEST_F(XClusterDDLQueueHandlerMockedTest, VerifyBasicJsonParsing) {
  const auto ht1 = HybridTime::FromMicrosecondsAndLogicalValue(1, 1);
  int query_id = 1;

  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);
  ddl_queue_handler.safe_time_ht_ = ht1;

  // Verify that we receive the necessary base tags.
  std::string complete_json_str;
  complete_json_str = static_cast<char>(kCompleteJsonb);
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(1, query_id, Format("$0{}", kCompleteJsonb));
    // Should fail since its an empty json.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, query_id,
        Format("$0{\"command_tag\":\"CREATE TABLE\", \"query\": \"n/a\"}", kCompleteJsonb));
    // Should fail since missing version field.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, query_id, Format("$0{\"version\":1, \"query\": \"n/a\"}", kCompleteJsonb));
    // Should fail since missing command_tag field.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, query_id, Format("$0{\"version\":1, \"command_tag\":\"CREATE TABLE\"}", kCompleteJsonb));
    // Should fail since missing query field.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1));
  }

  // Verify correct values for basic fields.
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, query_id, ConstructJson(/* version */ 999, kDDLCommandCreateTable));
    // Should fail since version is unsupported.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, query_id, ConstructJson(/* version */ 1, "INVALID COMMAND TAG"));
    // Should fail since command tag is unknown.
    ASSERT_NOK(ddl_queue_handler.ExecuteCommittedDDLs(ht1));
  }

  // Verify that supported command tags are processed.
  {
    ddl_queue_handler.ClearState();
    for (const auto& command_tag : {kDDLCommandCreateTable, kDDLCommandCreateIndex}) {
      ddl_queue_handler.rows_.emplace_back(
          1, query_id, ConstructJson(/* version */ 1, command_tag));
    }
    ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1));
  }
}

TEST_F(XClusterDDLQueueHandlerMockedTest, SkipScanWhenNoNewRecords) {
  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);

  const auto ht1 = HybridTime::FromMicrosecondsAndLogicalValue(1, 1);
  ddl_queue_handler.safe_time_ht_ = ht1;

  // If we haven't had new records, then don't process.
  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {}));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 0);
  ASSERT_EQ(ddl_queue_handler.HasSafeTimeBatch(), false);  // Should be empty after succesful call.

  // If there are new records then process.
  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {1}));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 1);
  ASSERT_EQ(ddl_queue_handler.HasSafeTimeBatch(), false);  // Should be empty after succesful call.

  // Verify calls without an apply_safe_time correctly update applied_new_records_.
  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(
      /* apply_safe_time */ std::nullopt, {1, 2}));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 1);

  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(
      /* apply_safe_time */ std::nullopt, {}));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 1);

  // Next call with an apply_safe_time should process.
  ASSERT_OK(ddl_queue_handler.ExecuteCommittedDDLs(ht1, {}));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 3);
  ASSERT_EQ(ddl_queue_handler.HasSafeTimeBatch(), false);  // Should be empty after succesful call.
}

}  // namespace yb::tserver
