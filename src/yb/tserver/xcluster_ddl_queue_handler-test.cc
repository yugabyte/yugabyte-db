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
#include "yb/cdc/xcluster_types.h"
#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/wire_protocol.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/xcluster/xcluster_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/tserver/xcluster_ddl_queue_handler.h"

#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/test_util.h"

namespace yb::tserver {

namespace {
const int64_t kCompleteJsonb = 1;
const std::string kDDLCommandCreateTable = "CREATE TABLE";
const std::string kDDLCommandDropTable = "DROP TABLE";
}

class XClusterDDLQueueHandlerMocked : public XClusterDDLQueueHandler {
 public:
  explicit XClusterDDLQueueHandlerMocked(const client::YBTableName& table_name)
      : XClusterDDLQueueHandler(
            /* local_client */ nullptr, table_name.namespace_name(), table_name.namespace_id(),
            /* connect_to_pg_func */ nullptr) {}

  Status ProcessDDLQueueTable(const HybridTime& apply_safe_time) {
    XClusterOutputClientResponse resp;
    resp.get_changes_response = std::make_shared<cdc::GetChangesResponsePB>();
    resp.get_changes_response->set_safe_hybrid_time(apply_safe_time.ToUint64());
    return XClusterDDLQueueHandler::ProcessDDLQueueTable(resp);
  }

  void ClearState() {
    rows_.clear();
    command_tag_to_count_map_.clear();
    processed_keys_.clear();
  }

  HybridTime safe_time_ht_;
  std::vector<std::tuple<int64, yb::Uuid, int, std::string>> rows_;
  // Count of how many times a command tag has been processed.
  std::unordered_map<std::string, int> command_tag_to_count_map_;

 private:
  Status InitPGConnection(const HybridTime& apply_safe_time) override { return Status::OK(); }

  Result<HybridTime> GetXClusterSafeTimeForNamespace() override { return safe_time_ht_; }

  Result<std::vector<std::tuple<int64, yb::Uuid, int, std::string>>> GetRowsToProcess() override {
    return rows_;
  }

  Result<bool> CheckIfAlreadyProcessed(
      int64 start_time, const Uuid& node_id, int pg_backend_pid) override {
    return processed_keys_.contains({start_time, node_id, pg_backend_pid});
  }

  Status StoreSessionVariables(
      int64 start_time, const yb::Uuid& node_id, int pg_backend_pid) override {
    return Status::OK();
  }

  Status RemoveDdlQueueEntry(
      int64 start_time, const yb::Uuid& node_id, int pg_backend_pid) override {
    // Mark this key as having been processed.
    processed_keys_.insert(DdlQueuePrimaryKey{start_time, node_id, pg_backend_pid});
    return Status::OK();
  }

  Status HandleCreateTable(
      bool already_processed, const std::string& query, const rapidjson::Document& doc) override {
    if (already_processed) {
      // Verify that we don't execute the query (would fail as pg_conn_ is not set).
      return XClusterDDLQueueHandler::HandleCreateTable(already_processed, query, doc);
    }
    command_tag_to_count_map_[kDDLCommandCreateTable]++;
    return Status::OK();
  }

  Status HandleDropTable(
      bool already_processed, const std::string& query, const rapidjson::Document& doc) override {
    if (already_processed) {
      // Verify that we don't execute the query (would fail as pg_conn_ is not set).
      return XClusterDDLQueueHandler::HandleDropTable(already_processed, query, doc);
    }
    command_tag_to_count_map_[kDDLCommandDropTable]++;
    return Status::OK();
  }

  struct DdlQueuePrimaryKey {
    int64 start_time;
    Uuid node_id;
    int pg_backend_pid;

    struct Hash {
      std::size_t operator()(const DdlQueuePrimaryKey& pk) const noexcept {
        std::size_t hash = 0;
        boost::hash_combine(hash, pk.start_time);
        boost::hash_combine(hash, pk.node_id);
        boost::hash_combine(hash, pk.pg_backend_pid);

        return hash;
      }
    };

    bool operator==(const DdlQueuePrimaryKey&) const = default;
  };
  std::unordered_set<DdlQueuePrimaryKey, DdlQueuePrimaryKey::Hash> processed_keys_;
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

  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table);
  {
    // Pass in invalid ht, get InvalidArgument.
    auto s = ddl_queue_handler.ProcessDDLQueueTable(ht_invalid);
    ASSERT_NOK(s);
    ASSERT_EQ(s.code(), Status::Code::kInvalidArgument);
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
  Uuid node_id = Uuid::Generate();
  int pg_backend_pid = 1;

  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table);
  ddl_queue_handler.safe_time_ht_ = ht1;

  // Verify that we receive the necessary base tags.
  std::string complete_json_str;
  complete_json_str = static_cast<char>(kCompleteJsonb);
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, node_id, pg_backend_pid, Format("$0{}", kCompleteJsonb));
    // Should fail since its an empty json.
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, node_id, pg_backend_pid,
        Format("$0{\"command_tag\":\"CREATE TABLE\", \"query\": \"n/a\"}", kCompleteJsonb));
    // Should fail since missing version field.
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, node_id, pg_backend_pid,
        Format("$0{\"version\":1, \"query\": \"n/a\"}", kCompleteJsonb));
    // Should fail since missing command_tag field.
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, node_id, pg_backend_pid,
        Format("$0{\"version\":1, \"command_tag\":\"CREATE TABLE\"}", kCompleteJsonb));
    // Should fail since missing query field.
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }

  // Verify correct values for basic fields.
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, node_id, pg_backend_pid, ConstructJson(/* version */ 999, kDDLCommandCreateTable));
    // Should fail since version is unsupported.
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }
  {
    ddl_queue_handler.ClearState();
    ddl_queue_handler.rows_.emplace_back(
        1, node_id, pg_backend_pid, ConstructJson(/* version */ 1, "INVALID COMMAND TAG"));
    // Should fail since command tag is unknown.
    ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  }
}

TEST_F(XClusterDDLQueueHandlerMockedTest, VerifyAlreadyProcessed) {
  const auto ht1 = HybridTime(HybridTime::kInitial.ToUint64() + 1);
  const int kVersion = 1;
  Uuid node_id = Uuid::Generate();
  int pg_backend_pid = 1;

  auto ddl_queue_handler = XClusterDDLQueueHandlerMocked(ddl_queue_table);
  // Construct row list.
  ddl_queue_handler.rows_.emplace_back(
      1, node_id, pg_backend_pid, ConstructJson(kVersion, kDDLCommandCreateTable));
  ddl_queue_handler.rows_.emplace_back(
      2, node_id, pg_backend_pid,
      ConstructJson(kVersion, kDDLCommandCreateTable, "\"new_table_id\": \"n/a\""));
  ddl_queue_handler.rows_.emplace_back(
      3, node_id, pg_backend_pid, ConstructJson(kVersion, kDDLCommandDropTable));

  ddl_queue_handler.safe_time_ht_ = ht1;
  ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  // Ensure we called the proper tags the correct number of times.
  ASSERT_EQ(ddl_queue_handler.command_tag_to_count_map_[kDDLCommandCreateTable], 2);
  ASSERT_EQ(ddl_queue_handler.command_tag_to_count_map_[kDDLCommandDropTable], 1);

  // Try again with the same rows, since already_processed, will call main class functions to parse.
  // Will fail since first Create Table doesn't have new_table_id field.
  ASSERT_NOK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  std::get<3>(ddl_queue_handler.rows_[0]) =
      ConstructJson(kVersion, kDDLCommandCreateTable, "\"new_table_id\": \"n/a\"");

  // Try again with the same rows, ensure that we handle already_processed.
  ASSERT_OK(ddl_queue_handler.ProcessDDLQueueTable(ht1));
  // Should still have same numbers.
  ASSERT_EQ(ddl_queue_handler.command_tag_to_count_map_[kDDLCommandCreateTable], 2);
  ASSERT_EQ(ddl_queue_handler.command_tag_to_count_map_[kDDLCommandDropTable], 1);
}

}  // namespace yb::tserver
