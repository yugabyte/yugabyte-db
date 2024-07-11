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
}

class XClusterDDLQueueHandlerMocked : public XClusterDDLQueueHandler {
 public:
  explicit XClusterDDLQueueHandlerMocked(
      const client::YBTableName& table_name, MockTserverXClusterContext& xcluster_context)
      : XClusterDDLQueueHandler(
            /* local_client */ nullptr, table_name.namespace_name(), table_name.namespace_id(),
            xcluster_context, /* connect_to_pg_func */ nullptr) {}

  Status ProcessDDLQueueTable(
      const std::optional<HybridTime>& apply_safe_time, int num_records = 1) {
    XClusterOutputClientResponse resp;
    resp.get_changes_response = std::make_shared<cdc::GetChangesResponsePB>();
    if (apply_safe_time) {
      resp.get_changes_response->set_safe_hybrid_time(apply_safe_time->ToUint64());
    }
    for (int i = 0; i < num_records; ++i) {
      resp.get_changes_response->add_records();
    }
    return XClusterDDLQueueHandler::ProcessDDLQueueTable(resp);
  }

  void ClearState() {
    rows_.clear();
    get_rows_to_process_calls_ = 0;
  }

  bool GetAppliedNewRecords() { return applied_new_records_; }

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

  Status ProcessDDLQuery(const DDLQueryInfo& query_info) override { return Status::OK(); }

  Result<bool> CheckIfAlreadyProcessed(const DDLQueryInfo& query_info) override { return false; };
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
  const auto ht1 = HybridTime(HybridTime::kInitial.ToUint64() + 1);
  const auto ht2 = HybridTime(ht1.ToUint64() + 1);
  const auto ht3 = HybridTime(ht2.ToUint64() + 1);
  const auto ht_invalid = HybridTime::kInvalid;

  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);
  {
    // Pass in invalid ht, should skip processing.
    auto s = ddl_queue_handler.ProcessDDLQueueTable(ht_invalid);
    ASSERT_OK(s);
    ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 0);
  }
  {
    // Get invalid safe time for the namespace, results in InternalError.
    ddl_queue_handler.safe_time_ht_ = ht_invalid;
    auto s = ddl_queue_handler.ProcessDDLQueueTable(ht1);
    ASSERT_NOK(s);
    ASSERT_EQ(s.code(), Status::Code::kInternalError);
  }
  {
    // Get lower safe time for the namespace, results in TryAgain.
    ddl_queue_handler.safe_time_ht_ = ht1;
    auto s = ddl_queue_handler.ProcessDDLQueueTable(ht2);
    ASSERT_NOK(s);
    ASSERT_EQ(s.code(), Status::Code::kTryAgain);
  }
  {
    // Get equal safe time for the namespace, results in OK.
    ddl_queue_handler.safe_time_ht_ = ht2;
    ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(ht2));
  }
  {
    // Get greater safe time for the namespace, results in OK.
    ddl_queue_handler.safe_time_ht_ = ht3;
    ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(ht2));
  }
}

std::string ConstructJson(
    int version, const std::string& command_tag, const std::string& other = "") {
  std::string complete_json_str;
  complete_json_str = static_cast<char>(kCompleteJsonb);
  return Format(
      "$0{\"version\": $1, \"command_tag\": \"$2\", \"user\": \"postgres\", \"query\": \"n/a\"$3}",
      complete_json_str, version, command_tag, (other.empty() ? "" : ", " + other));
}

TEST_F(XClusterDDLQueueHandlerMockedTest, VerifyBasicJsonParsing) {
  const auto ht1 = HybridTime(HybridTime::kInitial.ToUint64() + 1);
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
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, query_id,
        Format("$0{\"command_tag\":\"CREATE TABLE\", \"query\": \"n/a\"}", kCompleteJsonb));
    // Should fail since missing version field.
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, query_id, Format("$0{\"version\":1, \"query\": \"n/a\"}", kCompleteJsonb));
    // Should fail since missing command_tag field.
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, query_id, Format("$0{\"version\":1, \"command_tag\":\"CREATE TABLE\"}", kCompleteJsonb));
    // Should fail since missing query field.
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }

  // Verify correct values for basic fields.
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, query_id, ConstructJson(/* version */ 999, kDDLCommandCreateTable));
    // Should fail since version is unsupported.
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, query_id, ConstructJson(/* version */ 1, "INVALID COMMAND TAG"));
    // Should fail since command tag is unknown.
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }

  // Verify that supported command tags are processed.
  {
    ddl_queue_handler.ClearState();
    for (const auto& command_tag : {kDDLCommandCreateTable, kDDLCommandCreateIndex}) {
      ddl_queue_handler.rows_.emplace_back(
          1, query_id, ConstructJson(/* version */ 1, command_tag));
    }
    ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }
}

TEST_F(XClusterDDLQueueHandlerMockedTest, SkipScanWhenNoNewRecords) {
  MockTserverXClusterContext xcluster_context;
  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table, xcluster_context);

  const auto ht1 = HybridTime(HybridTime::kInitial.ToUint64() + 1);
  ddl_queue_handler.safe_time_ht_ = ht1;

  // Initial call should always process new records.
  ASSERT_EQ(ddl_queue_handler.GetAppliedNewRecords(), true);
  ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(ht1, /* num_records */ 0));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 1);
  ASSERT_EQ(ddl_queue_handler.GetAppliedNewRecords(), false);

  // If we haven't had new records, then don't process.
  ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(ht1, /* num_records */ 0));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 1);
  ASSERT_EQ(ddl_queue_handler.GetAppliedNewRecords(), false);

  // If there are new records then process.
  ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(ht1, /* num_records */ 1));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 2);
  ASSERT_EQ(ddl_queue_handler.GetAppliedNewRecords(), false);

  // Verify calls without an apply_safe_time correctly update applied_new_records_.
  ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(
      /* apply_safe_time */ std::nullopt, /* num_records */ 0));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 2);
  ASSERT_EQ(ddl_queue_handler.GetAppliedNewRecords(), false);

  ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(
      /* apply_safe_time */ std::nullopt, /* num_records */ 2));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 2);
  ASSERT_EQ(ddl_queue_handler.GetAppliedNewRecords(), true);

  ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(
      /* apply_safe_time */ std::nullopt, /* num_records */ 0));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 2);
  ASSERT_EQ(ddl_queue_handler.GetAppliedNewRecords(), true);

  // Next call with an apply_safe_time should process.
  ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(ht1, /* num_records */ 0));
  ASSERT_EQ(ddl_queue_handler.get_rows_to_process_calls_, 3);
  ASSERT_EQ(ddl_queue_handler.GetAppliedNewRecords(), false);
}

}  // namespace yb::tserver
