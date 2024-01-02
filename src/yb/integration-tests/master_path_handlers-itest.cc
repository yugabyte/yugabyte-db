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

#include <chrono>
#include <memory>
#include <regex>
#include <string>
#include <unordered_set>

#include "yb/client/client_fwd.h"
#include "yb/client/session.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/partition.h"
#include "yb/common/common_types.pb.h"
#include "yb/consensus/consensus_types.pb.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master-path-handlers.h"
#include "yb/master/master_fwd.h"
#include "yb/master/mini_master.h"

#include "yb/master/tasks_tracker.h"
#include "yb/server/webui_util.h"

#include "yb/tablet/tablet_types.pb.h"
#include "yb/tools/yb-admin_client.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/curl_util.h"
#include "yb/util/jsonreader.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

DECLARE_int32(tserver_unresponsive_timeout_ms);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_string(TEST_master_extra_list_host_port);
DECLARE_bool(TEST_tserver_disable_heartbeat);

DECLARE_int32(follower_unavailable_considered_failed_sec);

DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_uint32(leaderless_tablet_alert_delay_secs);

namespace yb {
namespace master {

using std::string;
using std::vector;
using std::unordered_set;

using namespace std::literals;

const std::string kKeyspaceName("my_keyspace");
const client::YBTableName table_name(YQL_DATABASE_CQL, kKeyspaceName, "test_table");
const uint kNumMasters(3);
const uint kNumTservers(3);
const uint kNumTablets(3);

template <class T>
class MasterPathHandlersBaseItest : public YBMiniClusterTestBase<T> {
 public:
  virtual void InitCluster() = 0;

  virtual void SetMasterHTTPURL() = 0;

  void SetUp() override {
    YBMiniClusterTestBase<T>::SetUp();
    InitCluster();
    SetMasterHTTPURL();
    yb_admin_client_ = std::make_unique<tools::ClusterAdminClient>(
        cluster_->GetMasterAddresses(), 30s /* timeout */);
    ASSERT_OK(yb_admin_client_->Init());
  }

  void DoTearDown() override {
    LOG(INFO) << "Calling DoTearDown in master path handlers";
    cluster_->Shutdown();
  }

 protected:
  void TestUrl(const string& query_path, faststring* result) {
    const string tables_url = master_http_url_ + query_path;
    EasyCurl curl;
    ASSERT_OK(curl.FetchURL(tables_url, result));
  }

  // Attempts to fetch url until a response with status OK, or until timeout.
  Status TestUrlWaitForOK(const string& query_path, faststring* result, MonoDelta timeout) {
    const string tables_url = master_http_url_ + query_path;
    EasyCurl curl;
    return WaitFor(
        [&]() -> bool {
          return curl.FetchURL(tables_url, result).ok();
        },
        timeout, "Wait for curl response to return with status OK");
  }

  virtual int num_tablet_servers() const {
    return kNumTservers;
  }

  virtual int num_masters() const {
    return kNumMasters;
  }

  std::shared_ptr<client::YBTable> CreateTestTable(const int num_tablets = 0) {
    auto client = CHECK_RESULT(cluster_->CreateClient());
    CHECK_OK(client->CreateNamespaceIfNotExists(kKeyspaceName));

    client::YBSchema schema;
    client::YBSchemaBuilder b;
    b.AddColumn("key")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    b.AddColumn("int_val")->Type(DataType::INT32)->NotNull();
    b.AddColumn("string_val")->Type(DataType::STRING);
    CHECK_OK(b.Build(&schema));
    std::unique_ptr<client::YBTableCreator> table_creator(client->NewTableCreator());
    if (num_tablets) {
      table_creator->num_tablets(num_tablets);
    }
    CHECK_OK(table_creator->table_name(table_name)
                 .schema(&schema)
                 .hash_schema(YBHashSchema::kMultiColumnHash)
                 .Create());

    std::shared_ptr<client::YBTable> table;
    CHECK_OK(client->OpenTable(table_name, &table));
    return table;
  }

  string GetLeaderlessTabletsString() {
    faststring result;
    auto url = "/tablet-replication";
    TestUrl(url, &result);
    const string& result_str = result.ToString();
    size_t pos_leaderless = result_str.find("Leaderless Tablets", 0);
    size_t pos_underreplicated = result_str.find("Underreplicated Tablets", 0);
    CHECK_NE(pos_leaderless, string::npos);
    CHECK_NE(pos_underreplicated, string::npos);
    CHECK_GT(pos_underreplicated, pos_leaderless);
    return result_str.substr(pos_leaderless, pos_underreplicated - pos_leaderless);
  }

  using YBMiniClusterTestBase<T>::cluster_;
  std::unique_ptr<tools::ClusterAdminClient> yb_admin_client_;
  string master_http_url_;
};

class MasterPathHandlersItest : public MasterPathHandlersBaseItest<MiniCluster> {
 public:
  void InitCluster() override {
    MiniClusterOptions opts;
    // Set low heartbeat timeout.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_unresponsive_timeout_ms) = 5000;
    opts.num_tablet_servers = num_tablet_servers();
    opts.num_masters = num_masters();
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());
  }

  void SetMasterHTTPURL() override {
    Endpoint master_http_endpoint =
        ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->bound_http_addr();
    master_http_url_ = "http://" + AsString(master_http_endpoint);
  }

  void SetUp() override {
    MasterPathHandlersBaseItest<MiniCluster>::SetUp();
    client_ = ASSERT_RESULT(cluster_->CreateClient());
  }

 protected:
  std::unique_ptr<client::YBClient> client_;
};

bool verifyTServersAlive(int n, const string& result) {
  size_t pos = 0;
  for (int i = 0; i < n; i++) {
    pos = result.find(kTserverAlive, pos + 1);
    if (pos == string::npos) {
      return false;
    }
  }
  return result.find(kTserverAlive, pos + 1) == string::npos;
}

TEST_F(MasterPathHandlersItest, TestMasterPathHandlers) {
  faststring result;
  TestUrl("/table?id=1", &result);
  TestUrl("/tablet-servers", &result);
  TestUrl("/tables", &result);
  TestUrl("/dump-entities", &result);
  TestUrl("/cluster-config", &result);
  TestUrl("/tablet-replication", &result);
  TestUrl("/load-distribution", &result);
}

TEST_F(MasterPathHandlersItest, TestDeadTServers) {
  // Shutdown tserver and wait for heartbeat timeout.
  cluster_->mini_tablet_server(0)->Shutdown();
  std::this_thread::sleep_for(std::chrono::milliseconds(2 * FLAGS_tserver_unresponsive_timeout_ms));

  // Check UI page.
  faststring result;
  TestUrl("/tablet-servers", &result);
  const string &result_str = result.ToString();
  ASSERT_TRUE(verifyTServersAlive(2, result_str));

  // Now verify dead.
  size_t pos = result_str.find(kTserverDead, 0);
  ASSERT_TRUE(pos != string::npos);
  ASSERT_TRUE(result_str.find(kTserverDead, pos + 1) == string::npos);

  // Startup the tserver and wait for heartbeats.
  ASSERT_OK(cluster_->mini_tablet_server(0)->Start(tserver::WaitTabletsBootstrapped::kFalse));

  ASSERT_OK(WaitFor(
      [&]() -> bool {
        TestUrl("/tablet-servers", &result);
        return verifyTServersAlive(3, result.ToString());
      },
      10s /* timeout */, "Waiting for tserver heartbeat to master"));
}

TEST_F(MasterPathHandlersItest, TestTabletReplicationEndpoint) {
  // Alert for leaderless tablet is delayed for FLAGS_leaderless_tablet_alert_delay_secs secodns.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_leaderless_tablet_alert_delay_secs) =
      FLAGS_heartbeat_interval_ms / 1000 * 5;
  auto table = CreateTestTable(kNumTablets);

  // Choose a tablet to orphan and take note of the servers which are leaders/followers for this
  // tablet.
  google::protobuf::RepeatedPtrField<TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTabletsFromTableId(table->id(), kNumTablets, &tablets));
  std::vector<yb::tserver::MiniTabletServer *> followers;
  yb::tserver::MiniTabletServer* leader = nullptr;
  auto orphan_tablet = tablets.Get(0);
  for (const auto& replica : orphan_tablet.replicas()) {
    const auto uuid = replica.ts_info().permanent_uuid();
    auto* tserver = cluster_->find_tablet_server(uuid);
    ASSERT_ONLY_NOTNULL(tserver);
    if (replica.role() == PeerRole::LEADER) {
      leader = tserver;
    } else {
      followers.push_back(tserver);
    }
    // Shutdown all tservers.
    tserver->Shutdown();
  }
  ASSERT_ONLY_NOTNULL(leader);

  // Restart the server which was previously the leader of the now orphaned tablet.
  ASSERT_OK(leader->Start(tserver::WaitTabletsBootstrapped::kFalse));
  // Sleep here to give the master's catalog_manager time to receive heartbeat from "leader".
  std::this_thread::sleep_for(std::chrono::milliseconds(6 * FLAGS_heartbeat_interval_ms));

  // Call endpoint and validate format of response.
  faststring result;
  TestUrl("/api/v1/tablet-replication", &result);

  JsonReader r(result.ToString());
  ASSERT_OK(r.Init());
  const rapidjson::Value* json_obj = nullptr;
  EXPECT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
  EXPECT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_obj)->GetType());
  EXPECT_TRUE(json_obj->HasMember("leaderless_tablets"));
  EXPECT_EQ(rapidjson::kArrayType, (*json_obj)["leaderless_tablets"].GetType());
  const rapidjson::Value::ConstArray tablets_json = (*json_obj)["leaderless_tablets"].GetArray();
  std::vector<std::string> leaderless_tablet_uuids;
  for (const auto& tablet_json : tablets_json) {
    EXPECT_EQ(rapidjson::kObjectType, tablet_json.GetType());
    EXPECT_TRUE(tablet_json.HasMember("table_uuid"));
    EXPECT_EQ(rapidjson::kStringType, tablet_json["table_uuid"].GetType());
    EXPECT_TRUE(tablet_json.HasMember("tablet_uuid"));
    EXPECT_EQ(rapidjson::kStringType, tablet_json["tablet_uuid"].GetType());
  }

  auto has_orphan_tablet_result = std::any_of(
      tablets_json.begin(), tablets_json.end(),
      [&orphan_tablet](const auto& tablet_json) {
        return tablet_json["tablet_uuid"].GetString() == orphan_tablet.tablet_id();
      });
  EXPECT_TRUE(has_orphan_tablet_result) << "Expected to find orphan_tablet in leaderless tablets.";

  // YBMiniClusterTestBase test-end verification will fail if the cluster is up with stopped nodes.
  cluster_->Shutdown();
}

void verifyBasicTestTableAttributes(const rapidjson::Value* json_obj,
                                    const std::shared_ptr<client::YBTable>& table,
                                    uint64_t expected_version) {
  EXPECT_TRUE(json_obj->HasMember("table_id"));
  EXPECT_EQ(table->id(), (*json_obj)["table_id"].GetString());
  EXPECT_TRUE(json_obj->HasMember("table_name"));
  EXPECT_EQ(yb::server::TableLongName(kKeyspaceName, "test_table"),
            (*json_obj)["table_name"].GetString());
  EXPECT_TRUE(json_obj->HasMember("table_version"));
  EXPECT_EQ((*json_obj)["table_version"].GetUint64(), expected_version);
  EXPECT_TRUE(json_obj->HasMember("table_type"));
  EXPECT_EQ(TableType_Name(YQL_TABLE_TYPE), (*json_obj)["table_type"].GetString());
  EXPECT_TRUE(json_obj->HasMember("table_state"));
  EXPECT_EQ(strcmp("Running", (*json_obj)["table_state"].GetString()), 0);
}

void verifyTestTableSchema(const rapidjson::Value* json_obj) {
  EXPECT_TRUE(json_obj->HasMember("columns"));
  EXPECT_EQ(rapidjson::kArrayType, (*json_obj)["columns"].GetType());
  const rapidjson::Value::ConstArray columns_json =
      (*json_obj)["columns"].GetArray();

  for (const auto& column_json : columns_json) {
    EXPECT_EQ(rapidjson::kObjectType, column_json.GetType());
    EXPECT_TRUE(column_json.HasMember("id"));
    EXPECT_GT(strlen(column_json["id"].GetString()), 0);
    EXPECT_TRUE(column_json.HasMember("column"));
    if (!strcmp(column_json["column"].GetString(), "key")) {
      EXPECT_TRUE(column_json.HasMember("type"));
      EXPECT_EQ(strcmp("int32 NOT NULL PARTITION KEY", column_json["type"].GetString()), 0);
    } else if (!strcmp(column_json["column"].GetString(), "int_val")) {
      EXPECT_TRUE(column_json.HasMember("type"));
      EXPECT_EQ(strcmp("int32 NOT NULL NOT A PARTITION KEY", column_json["type"].GetString()), 0);
    } else if (!strcmp(column_json["column"].GetString(), "string_val")) {
      EXPECT_TRUE(column_json.HasMember("type"));
      EXPECT_EQ(strcmp("string NULLABLE NOT A PARTITION KEY", column_json["type"].GetString()), 0);
    } else {
      FAIL() << "Unknown column: " << column_json["column"].GetString();
    }
  }
}

void verifyTestTableReplicationInfo(const JsonReader& r,
                                    const rapidjson::Value* json_obj,
                                    const char* expected_zone) {
  EXPECT_TRUE(json_obj->HasMember("table_replication_info"));
  const rapidjson::Value* repl_info = nullptr;
  EXPECT_OK(r.ExtractObject(json_obj, "table_replication_info", &repl_info));
  EXPECT_TRUE(repl_info->HasMember("live_replicas"));
  const rapidjson::Value* live_replicas = nullptr;
  EXPECT_OK(r.ExtractObject(repl_info, "live_replicas", &live_replicas));
  EXPECT_TRUE(live_replicas->HasMember("num_replicas"));
  EXPECT_EQ((*live_replicas)["num_replicas"].GetUint64(), 3);
  EXPECT_TRUE(live_replicas->HasMember("placement_uuid"));
  EXPECT_EQ(strcmp((*live_replicas)["placement_uuid"].GetString(), "table_uuid"), 0);
  EXPECT_TRUE(live_replicas->HasMember("placement_blocks"));
  EXPECT_EQ(rapidjson::kArrayType, (*live_replicas)["placement_blocks"].GetType());
  const rapidjson::Value::ConstArray placement_blocks =
      (*live_replicas)["placement_blocks"].GetArray();
  const auto& placement_block = placement_blocks[0];
  EXPECT_EQ(rapidjson::kObjectType, placement_block.GetType());
  EXPECT_TRUE(placement_block.HasMember("cloud_info"));
  EXPECT_TRUE(placement_block["cloud_info"].HasMember("placement_cloud"));
  EXPECT_EQ(strcmp(placement_block["cloud_info"]["placement_cloud"].GetString(), "cloud"), 0);
  EXPECT_TRUE(placement_block["cloud_info"].HasMember("placement_region"));
  EXPECT_EQ(strcmp(placement_block["cloud_info"]["placement_region"].GetString(), "region"), 0);
  EXPECT_TRUE(placement_block["cloud_info"].HasMember("placement_zone"));
  EXPECT_EQ(strcmp(placement_block["cloud_info"]["placement_zone"].GetString(), expected_zone), 0);
  EXPECT_TRUE(placement_block.HasMember("min_num_replicas"));
  EXPECT_EQ(placement_block["min_num_replicas"].GetUint64(), 1);
}

void verifyTestTableTablets(const rapidjson::Value* json_obj) {
  EXPECT_TRUE(json_obj->HasMember("tablets"));
  EXPECT_EQ(rapidjson::kArrayType, (*json_obj)["tablets"].GetType());
  const rapidjson::Value::ConstArray tablets_json =
      (*json_obj)["tablets"].GetArray();

  for (const auto& tablet_json : tablets_json) {
    EXPECT_EQ(rapidjson::kObjectType, tablet_json.GetType());
    EXPECT_TRUE(tablet_json.HasMember("tablet_id"));
    EXPECT_GT(strlen(tablet_json["tablet_id"].GetString()), 0);
    EXPECT_TRUE(tablet_json.HasMember("partition"));
    EXPECT_EQ(strncmp("hash_split", tablet_json["partition"].GetString(), strlen("hash_split")), 0);
    EXPECT_TRUE(tablet_json.HasMember("split_depth"));
    EXPECT_EQ(tablet_json["split_depth"].GetUint64(), 0);
    EXPECT_TRUE(tablet_json.HasMember("state"));
    EXPECT_EQ(strcmp("Running", tablet_json["state"].GetString()), 0);
    EXPECT_TRUE(tablet_json.HasMember("hidden"));
    EXPECT_EQ(strcmp("false", tablet_json["hidden"].GetString()), 0);
    EXPECT_TRUE(tablet_json.HasMember("message"));
    EXPECT_EQ(strcmp("Tablet reported with an active leader",
                     tablet_json["message"].GetString()), 0);
    EXPECT_TRUE(tablet_json.HasMember("locations"));
    EXPECT_EQ(rapidjson::kArrayType, tablet_json["locations"].GetType());
    const rapidjson::Value::ConstArray locations_json = tablet_json["locations"].GetArray();

    int num_leaders = 0;
    int num_followers = 0;
    for (const auto& location_json : locations_json) {
      EXPECT_TRUE(location_json.HasMember("uuid"));
      EXPECT_EQ(strlen(location_json["uuid"].GetString()), 32);
      EXPECT_TRUE(location_json.HasMember("location"));
      EXPECT_GT(strlen(location_json["uuid"].GetString()), 0);
      EXPECT_TRUE(location_json.HasMember("role"));
      if (!strcmp(location_json["role"].GetString(), "LEADER")) {
        num_leaders++;
      } else if (!strcmp(location_json["role"].GetString(), "FOLLOWER")) {
        num_followers++;
      } else {
        FAIL() << "Unknown role: " << location_json["role"].GetString();
      }
    }
    EXPECT_EQ(1, num_leaders);
    EXPECT_EQ(2, num_followers);
  }
}

TEST_F(MasterPathHandlersItest, TestTableJsonEndpointValidTableId) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(client->CreateNamespaceIfNotExists(kKeyspaceName));

  auto table = CreateTestTable();

  // Add cluster level placement info
  auto yb_admin_client_ = std::make_unique<yb::tools::ClusterAdminClient>(
      cluster_->GetMasterAddresses(), MonoDelta::FromSeconds(30));
  ASSERT_OK(yb_admin_client_->Init());
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("cloud.region.zone", 3, "table_uuid"));

  // Call endpoint and validate format of response.
  faststring result;
  ASSERT_OK(
      TestUrlWaitForOK(Format("/api/v1/table?id=$0", table->id()), &result, 30s /* timeout */));

  JsonReader r(result.ToString());
  ASSERT_OK(r.Init());
  const rapidjson::Value* json_obj = nullptr;
  EXPECT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
  EXPECT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_obj)->GetType());
  verifyBasicTestTableAttributes(json_obj, table, 0);
  verifyTestTableReplicationInfo(r, json_obj, "zone");
  verifyTestTableSchema(json_obj);
  verifyTestTableTablets(json_obj);
}

TEST_F(MasterPathHandlersItest, TestTableJsonEndpointValidTableName) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(client->CreateNamespaceIfNotExists(kKeyspaceName));

  // Create table.
  auto table = CreateTestTable();

  // Add table level placement info
  auto yb_admin_client_ = std::make_unique<yb::tools::ClusterAdminClient>(
      cluster_->GetMasterAddresses(), MonoDelta::FromSeconds(30));
  ASSERT_OK(yb_admin_client_->Init());
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("cloud.region.zone", 3, "table_uuid"));
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(
    table->name(), "cloud.region.anotherzone", 3, "table_uuid"));

  // Call endpoint and validate format of response.
  faststring result;
  ASSERT_OK(
      TestUrlWaitForOK(
          Format("/api/v1/table?keyspace_name=$0&table_name=$1", kKeyspaceName, "test_table"),
          &result,
          30s /* timeout */));

  JsonReader r(result.ToString());
  ASSERT_OK(r.Init());
  const rapidjson::Value* json_obj = nullptr;
  EXPECT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
  EXPECT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_obj)->GetType());
  verifyBasicTestTableAttributes(json_obj, table, 1);
  verifyTestTableReplicationInfo(r, json_obj, "anotherzone");
  verifyTestTableSchema(json_obj);
  verifyTestTableTablets(json_obj);
}

TEST_F(MasterPathHandlersItest, TestTableJsonEndpointInvalidTableId) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Call endpoint and validate format of response.
  faststring result;
  ASSERT_OK(
      TestUrlWaitForOK("/api/v1/table?id=12345", &result, 30s /* timeout */));

  JsonReader r(result.ToString());
  ASSERT_OK(r.Init());
  const rapidjson::Value* json_obj = nullptr;
  EXPECT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
  EXPECT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_obj)->GetType());
  EXPECT_TRUE(json_obj->HasMember("error"));
  EXPECT_EQ(strcmp("Table not found!", (*json_obj)["error"].GetString()), 0);
}

TEST_F(MasterPathHandlersItest, TestTableJsonEndpointNoArgs) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Call endpoint and validate format of response.
  faststring result;
  ASSERT_OK(
      TestUrlWaitForOK("/api/v1/table", &result, 30s /* timeout */));

  JsonReader r(result.ToString());
  ASSERT_OK(r.Init());
  const rapidjson::Value* json_obj = nullptr;
  EXPECT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
  EXPECT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_obj)->GetType());
  EXPECT_TRUE(json_obj->HasMember("error"));
  EXPECT_EQ(strncmp("Missing", (*json_obj)["error"].GetString(), strlen("Missing")), 0);
}

TEST_F(MasterPathHandlersItest, TestTablesJsonEndpoint) {

  auto table = CreateTestTable();

  faststring result;
  ASSERT_OK(TestUrlWaitForOK("/api/v1/tables", &result, 30s /* timeout */));

  JsonReader r(result.ToString());
  ASSERT_OK(r.Init());
  const rapidjson::Value* json_obj = nullptr;
  EXPECT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
  EXPECT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_obj)->GetType());

  // Should have one user table, index should be empty array, system should have many tables.
  EXPECT_EQ((*json_obj)["user"].Size(), 1);
  EXPECT_EQ((*json_obj)["index"].Size(), 0);
  EXPECT_GE((*json_obj)["system"].Size(), 1);

  // Check that the test table is there and fields are correct.
  const rapidjson::Value& table_obj = (*json_obj)["user"][0];
  EXPECT_EQ(kKeyspaceName, table_obj["keyspace"].GetString());
  EXPECT_EQ(table_name.table_name(), table_obj["table_name"].GetString());
  EXPECT_EQ(SysTablesEntryPB_State_Name(SysTablesEntryPB_State_RUNNING),
      table_obj["state"].GetString());
  EXPECT_EQ(table_obj["message"].GetString(), string());
  EXPECT_EQ(table->id(), table_obj["uuid"].GetString());
  EXPECT_EQ(table_obj["ysql_oid"].GetString(), string());
  EXPECT_FALSE(table_obj["hidden"].GetBool());
  // Check disk size info is there.
  EXPECT_TRUE(table_obj["on_disk_size"].IsObject());
  const rapidjson::Value& disk_size_obj = table_obj["on_disk_size"];
  EXPECT_TRUE(disk_size_obj.HasMember("wal_files_size"));
  EXPECT_TRUE(disk_size_obj.HasMember("wal_files_size_bytes"));
  EXPECT_TRUE(disk_size_obj.HasMember("sst_files_size"));
  EXPECT_TRUE(disk_size_obj.HasMember("sst_files_size_bytes"));
  EXPECT_TRUE(disk_size_obj.HasMember("uncompressed_sst_file_size"));
  EXPECT_TRUE(disk_size_obj.HasMember("uncompressed_sst_file_size_bytes"));
  EXPECT_TRUE(disk_size_obj.HasMember("has_missing_size"));
}

class MultiMasterPathHandlersItest : public MasterPathHandlersItest {
 public:
  int num_masters() const override {
    return 3;
  }
};

TEST_F_EX(MasterPathHandlersItest, Forward, MultiMasterPathHandlersItest) {
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_master_extra_list_host_port) = RandomHumanReadableString(16) + ".com";
  EasyCurl curl;
  faststring content;
  for (size_t i = 0; i != cluster_->num_masters(); ++i) {
    auto url = Format("http://$0/tablet-servers", cluster_->mini_master(i)->bound_http_addr());
    content.clear();
    ASSERT_OK(curl.FetchURL(url, &content));
  }
}

class MasterPathHandlersExternalItest : public MasterPathHandlersBaseItest<ExternalMiniCluster> {
 public:
  void InitCluster() override {
    // Set low heartbeat timeout.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_unresponsive_timeout_ms) = 5000;
    cluster_.reset(new ExternalMiniCluster(opts_));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(cluster_->SetFlagOnMasters("tserver_unresponsive_timeout_ms", "10000"));
  }

  void SetMasterHTTPURL() override {
    HostPort master_http_endpoint = cluster_->master(0)->bound_http_hostport();
    master_http_url_ = "http://" + ToString(master_http_endpoint);
  }

 protected:
  ExternalMiniClusterOptions opts_;

  void SetUp() override {
    opts_.num_tablet_servers = num_tablet_servers();
    opts_.num_masters = num_masters();
    opts_.extra_tserver_flags.push_back("--placement_cloud=c");
    opts_.extra_tserver_flags.push_back("--placement_region=r");
    opts_.extra_tserver_flags.push_back("--placement_zone=z${index}");
    opts_.extra_tserver_flags.push_back("--placement_uuid=" + kLivePlacementUuid);
    opts_.extra_tserver_flags.push_back("--follower_unavailable_considered_failed_sec=10");

    MasterPathHandlersBaseItest<ExternalMiniCluster>::SetUp();

    yb_admin_client_ = std::make_unique<tools::ClusterAdminClient>(
        cluster_->GetMasterAddresses(), MonoDelta::FromSeconds(30));
    ASSERT_OK(yb_admin_client_->Init());

    // 3 live replicas, one in each zone.
    ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0:1,c.r.z1:1,c.r.z2:1", 3,
        kLivePlacementUuid));
  }

  Status AddTabletServer(const string& zone, const string& placement_uuid,
      const vector<string>& extra_flags = {}) {
    vector<string> flags;
    flags.push_back("--placement_cloud=c");
    flags.push_back("--placement_region=r");
    flags.push_back("--placement_zone=" + zone);
    flags.push_back("--placement_uuid=" + placement_uuid);
    flags.push_back("--follower_unavailable_considered_failed_sec=10");
    flags.insert(flags.end(), extra_flags.begin(), extra_flags.end());
    return cluster_->AddTabletServer(
        ExternalMiniClusterOptions::kDefaultStartCqlProxy, flags);
  }

  const string kReadReplicaPlacementUuid = "read_replica";
  const string kLivePlacementUuid = "live";
  std::unique_ptr<tools::ClusterAdminClient> yb_admin_client_;
};

class MasterPathHandlersUnderReplicationItest : public MasterPathHandlersExternalItest {
 protected:
  Status CheckUnderReplicatedInPlacements(
      const unordered_set<TabletId> test_tablet_ids,
      const unordered_set<string>& placements) {
    faststring result;
    TestUrl("/api/v1/tablet-under-replication", &result);
    JsonReader r(result.ToString());
    RETURN_NOT_OK(r.Init());
    const rapidjson::Value* json_obj = nullptr;
    RETURN_NOT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
    const rapidjson::Value::ConstArray tablets_json =
        (*json_obj)["underreplicated_tablets"].GetArray();
    if (placements.empty()) {
      SCHECK_EQ(tablets_json.Size(), 0, IllegalState, "Expected no underreplicated tablets");
      return Status::OK();
    }

    SCHECK_EQ(tablets_json.Size(), test_tablet_ids.size(), IllegalState,
        "Unexpected amount of underreplicated tablets");
    for (auto& tablet_json : tablets_json) {
      auto tablet_id = tablet_json["tablet_uuid"].GetString();
      auto table_id = tablet_json["table_uuid"].GetString();
      if (!test_tablet_ids.contains(tablet_id)) {
        return STATUS_FORMAT(IllegalState, "Tablet $0 from table $1 unexpectedly underreplicated",
            tablet_id, table_id);
      }

      auto underreplicated_placements = tablet_json["underreplicated_placements"].GetArray();
      SCHECK_EQ(underreplicated_placements.Size(), placements.size(), IllegalState,
          "Actual number of underreplicated placements did not match expected");
      for (auto& placement : underreplicated_placements) {
        auto placement_id = placement.GetString();
        if (!placements.contains(placement_id)) {
          return STATUS_FORMAT(IllegalState, "Placement $0 unexpectedly underreplicated",
              placement_id);
        }
      }
    }
    return Status::OK();
  }

  Status CheckNotUnderReplicated(const unordered_set<TabletId> test_tablet_ids) {
    return CheckUnderReplicatedInPlacements(test_tablet_ids, {} /* placements */);
  }

  Result<unordered_set<TabletId>> CreateTestTableAndGetTabletIds() {
    table_ = CreateTestTable(kNumTablets);

    // Store tablet ids to check later.
    master::GetTableLocationsResponsePB table_locs;
    RETURN_NOT_OK(itest::GetTableLocations(cluster_.get(), table_->name(), 10s /* timeout */,
        RequireTabletsRunning::kFalse, &table_locs));
    unordered_set<TabletId> test_tablet_ids;
    for (auto& tablet : table_locs.tablet_locations()) {
      test_tablet_ids.insert(tablet.tablet_id());
    }
    SCHECK_EQ(test_tablet_ids.size(), kNumTablets, IllegalState, "Incorrect number of tablets");
    return test_tablet_ids;
  }

  std::shared_ptr<client::YBTable> table_;
};

TEST_F_EX(MasterPathHandlersItest, TestTabletUnderReplicationEndpoint,
    MasterPathHandlersUnderReplicationItest) {
  auto tablet_ids = ASSERT_RESULT(CreateTestTableAndGetTabletIds());
  ASSERT_OK(WaitFor([&]() {
    return CheckNotUnderReplicated(tablet_ids).ok();
  }, 10s, "Wait for not underreplicated"));

  cluster_->tablet_server(0)->Shutdown();
  ASSERT_OK(WaitFor([&]() {
    return CheckUnderReplicatedInPlacements(tablet_ids, {kLivePlacementUuid}).ok();
  }, 3s * FLAGS_follower_unavailable_considered_failed_sec, "Wait for underreplicated"));

  // Even after moving to the new tservers, the table should be underreplicated since there is no
  // z0 replica.
  ASSERT_OK(AddTabletServer("z1", kLivePlacementUuid));
  ASSERT_OK(WaitFor([&]() {
    return CheckUnderReplicatedInPlacements(tablet_ids, {kLivePlacementUuid}).ok();
  }, 10s, "Wait for underreplicated"));

  // YBMiniClusterTestBase test-end verification will fail if the cluster is up with stopped nodes.
  cluster_->Shutdown();
}

TEST_F_EX(MasterPathHandlersItest, TestTabletUnderReplicationEndpointDeadTserver,
    MasterPathHandlersUnderReplicationItest) {
  ASSERT_OK(cluster_->SetFlagOnMasters("tserver_unresponsive_timeout_ms", "3000"));

  auto tablet_ids = ASSERT_RESULT(CreateTestTableAndGetTabletIds());
  ASSERT_OK(WaitFor([&]() {
    return CheckNotUnderReplicated(tablet_ids).ok();
  }, 10s, "Wait for not underreplicated"));

  // The tablet endpoint should count replicas on the dead tserver as valid replicas (so the tablet
  // is not under-replicated).
  cluster_->tablet_server(0)->Shutdown();
  ASSERT_OK(cluster_->WaitForMasterToMarkTSDead(0));
  ASSERT_OK(CheckNotUnderReplicated(tablet_ids));

  // The tablet IS under-replicated once the replica on the dead tserver is kicked out of quorum.
  ASSERT_OK(WaitFor([&]() {
    return CheckUnderReplicatedInPlacements(tablet_ids, {kLivePlacementUuid}).ok();
  }, 3s * FLAGS_follower_unavailable_considered_failed_sec, "Wait for underreplicated"));

  // YBMiniClusterTestBase test-end verification will fail if the cluster is up with stopped nodes.
  cluster_->Shutdown();
}

TEST_F_EX(MasterPathHandlersItest, TestTabletUnderReplicationEndpointTableReplicationInfo,
    MasterPathHandlersUnderReplicationItest) {
  auto tablet_ids = ASSERT_RESULT(CreateTestTableAndGetTabletIds());
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(table_->name(), "c.r.z0:0,c.r.z1:0,c.r.z2:0",
      3, kLivePlacementUuid));
  ASSERT_OK(WaitFor([&]() {
    return CheckNotUnderReplicated(tablet_ids).ok();
  }, 10s, "Wait for not underreplicated"));

  cluster_->tablet_server(0)->Shutdown();
  ASSERT_OK(WaitFor([&]() {
    return CheckUnderReplicatedInPlacements(tablet_ids, {kLivePlacementUuid}).ok();
  }, 3s * FLAGS_follower_unavailable_considered_failed_sec, "Wait for underreplicated"));

  // After moving to the new tserver, the table is no longer underreplicated according to the
  // table placement info (but would be according to the cluster's).
  ASSERT_OK(AddTabletServer("z1", kLivePlacementUuid));
  ASSERT_OK(WaitFor([&]() {
    return CheckNotUnderReplicated(tablet_ids).ok();
  }, 3s * FLAGS_follower_unavailable_considered_failed_sec, "Wait for not underreplicated"));

  // YBMiniClusterTestBase test-end verification will fail if the cluster is up with stopped nodes.
  cluster_->Shutdown();
}

class MasterPathHandlersUnderReplicationTwoTsItest :
    public MasterPathHandlersUnderReplicationItest {
 protected:
  int num_tablet_servers() const override {
    return 2;
  }
};

TEST_F_EX(
    MasterPathHandlersItest, TestTabletUnderReplicationEndpointBootstrapping,
    MasterPathHandlersUnderReplicationTwoTsItest) {
  // Set these to allow multiple tablets bootstrapping at the same time.
  ASSERT_OK(cluster_->SetFlagOnMasters("load_balancer_max_over_replicated_tablets", "10"));
  ASSERT_OK(cluster_->SetFlagOnMasters(
      "load_balancer_max_concurrent_tablet_remote_bootstraps", "10"));
  ASSERT_OK(cluster_->SetFlagOnMasters(
      "load_balancer_max_concurrent_tablet_remote_bootstraps_per_table", "10"));
  auto tablet_ids = ASSERT_RESULT(CreateTestTableAndGetTabletIds());

  // Start a third tserver. The load balancer will bootstrap new replicas onto this tserver to fix
  // the under-replication.
  vector<string> extra_flags;
  extra_flags.push_back("--TEST_pause_rbs_before_download_wal=true");
  extra_flags.push_back("--TEST_pause_after_set_bootstrapping=true");
  ASSERT_OK(AddTabletServer("z2", kLivePlacementUuid, extra_flags));
  auto new_ts = cluster_->tablet_server(2);

  // Waits for all tablets to be in the specified state according to the master leader.
  auto WaitForTabletsInState = [&](tablet::RaftGroupStatePB state) {
    return WaitFor([&]() -> Result<bool> {
      auto tablet_replicas = VERIFY_RESULT(itest::GetTabletsOnTsAccordingToMaster(
          cluster_.get(), new_ts->uuid(), table_->name(), 10s /* timeout */,
          RequireTabletsRunning::kFalse));
      if (tablet_replicas.size() != kNumTablets) {
        return false;
      }
      for (auto& replica : tablet_replicas) {
        if (replica.state() != state) {
          return false;
        }
      }
      return true;
    }, 10s * kTimeMultiplier, "Wait for tablets to be in state " + RaftGroupStatePB_Name(state));
  };

  // The tablet should be under-replicated while it is remote bootstrapping.
  ASSERT_OK(WaitForTabletsInState(tablet::RaftGroupStatePB::NOT_STARTED));
  ASSERT_OK(CheckUnderReplicatedInPlacements(tablet_ids, {kLivePlacementUuid}));

  // The tablet should be under-replicated while it is opening (local bootstrapping).
  ASSERT_OK(cluster_->SetFlag(new_ts, "TEST_pause_rbs_before_download_wal", "false"));
  ASSERT_OK(WaitForTabletsInState(tablet::RaftGroupStatePB::BOOTSTRAPPING));
  ASSERT_OK(CheckUnderReplicatedInPlacements(tablet_ids, {kLivePlacementUuid}));

  // The tablet should not be under-replicated once bootstrapping ends.
  ASSERT_OK(cluster_->SetFlag(new_ts, "TEST_pause_after_set_bootstrapping", "false"));
  ASSERT_OK(WaitForTabletsInState(tablet::RaftGroupStatePB::RUNNING));
  ASSERT_OK(CheckNotUnderReplicated(tablet_ids));
}

TEST_F_EX(MasterPathHandlersItest, TestTabletUnderReplicationEndpointReadReplicas,
    MasterPathHandlersUnderReplicationItest) {
  // 2 read replicas, one in z0 and one in z1.
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo("c.r.z0:1,c.r.z1:1", 2,
      kReadReplicaPlacementUuid));
  ASSERT_OK(AddTabletServer("z0", kReadReplicaPlacementUuid));
  ASSERT_OK(AddTabletServer("z1", kReadReplicaPlacementUuid));

  auto tablet_ids = ASSERT_RESULT(CreateTestTableAndGetTabletIds());
  ASSERT_OK(WaitFor([&]() {
    return CheckNotUnderReplicated(tablet_ids).ok();
  }, 10s, "Wait for not underreplicated"));

  // Should be under-replicated in only the live cluster, not the read replica.
  cluster_->tablet_server(0)->Shutdown();
  ASSERT_OK(WaitFor([&]() {
    return CheckUnderReplicatedInPlacements(tablet_ids, {kLivePlacementUuid}).ok();
  }, 3s * FLAGS_follower_unavailable_considered_failed_sec, "Wait for underreplicated in live"));

  ASSERT_OK(cluster_->tablet_server(0)->Start());
  ASSERT_OK(WaitFor([&]() {
    return CheckNotUnderReplicated(tablet_ids).ok();
  }, 3s * FLAGS_follower_unavailable_considered_failed_sec, "Wait for not underreplicated"));

  // Should be under-replicated in only the read replica, not the live cluster.
  cluster_->tablet_server(4)->Shutdown();
  ASSERT_OK(WaitFor([&]() {
    return CheckUnderReplicatedInPlacements(tablet_ids, {kReadReplicaPlacementUuid}).ok();
  }, 3s * FLAGS_follower_unavailable_considered_failed_sec, "Wait for underreplicated in RR"));

  // YBMiniClusterTestBase test-end verification will fail if the cluster is up with stopped nodes.
  cluster_->Shutdown();
}

TEST_F_EX(MasterPathHandlersItest, TestTablePlacementInfo, MasterPathHandlersExternalItest) {
  std::shared_ptr<client::YBTable> table = CreateTestTable(/* num_tablets */ 1);

  faststring result;
  auto url = Format("/table?id=$0", table->id());

  // Verify cluster level replication info.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("cloud.region.zone", 3, "table_uuid"));
  TestUrl(url, &result);
  const string& cluster_str = result.ToString();
  size_t pos = cluster_str.find("Replication Info", 0);
  ASSERT_NE(pos, string::npos);
  pos = cluster_str.find("placement_zone", pos + 1);
  ASSERT_NE(pos, string::npos);
  ASSERT_EQ(cluster_str.substr(pos + 22, 4), "zone");

  // Verify table level replication info.
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(
    table->name(), "cloud.region.anotherzone", 3, "table_uuid"));
  TestUrl(url, &result);
  const string& table_str = result.ToString();
  pos = table_str.find("Replication Info", 0);
  ASSERT_NE(pos, string::npos);
  pos = table_str.find("placement_zone", pos + 1);
  ASSERT_NE(pos, string::npos);
  ASSERT_EQ(table_str.substr(pos + 22, 11), "anotherzone");
}

class MasterPathHandlersLeaderlessITest : public MasterPathHandlersExternalItest {
 public:
  void SetUp() override {
    opts_.extra_tserver_flags.push_back(
        {Format("--tserver_heartbeat_metrics_interval_ms=$0", kTserverHeartbeatMetricsIntervalMs)});
    MasterPathHandlersExternalItest::SetUp();
  }

  void CreateSingleTabletTestTable() {
    table_ = CreateTestTable(1);
  }

  TabletId GetSingleTabletId() const {
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      const auto ts = cluster_->tablet_server(i);
      const auto tablets = CHECK_RESULT(cluster_->GetTablets(ts));
      for (auto& tablet : tablets) {
        if (tablet.table_name() == table_->name().table_name()) {
          return tablet.tablet_id();
        }
      }
    }
    LOG(FATAL) << "Didn't find a tablet id for table " << table_->name().table_name();
    return "";
  }

  Status WaitForLeaderPeer(const TabletId& tablet_id, uint64_t leader_idx) {
    return WaitFor([&] {
      const auto current_leader_idx_result = cluster_->GetTabletLeaderIndex(tablet_id);
      if (current_leader_idx_result.ok()) {
        return *current_leader_idx_result == leader_idx;
      }
      return false;
    },
    10s,
    Format("Peer $0 becomes leader of tablet $1",
           cluster_->tablet_server(leader_idx)->uuid(),
           tablet_id));
  }

  bool HasLeaderPeer(const TabletId& tablet_id) {
    const auto current_leader_idx_result = cluster_->GetTabletLeaderIndex(tablet_id);
    return current_leader_idx_result.ok();
  }

  std::shared_ptr<client::YBTable> table_;
  static constexpr int kTserverHeartbeatMetricsIntervalMs = 1000;
};

TEST_F(MasterPathHandlersLeaderlessITest, TestLeaderlessTabletEndpoint) {
  ASSERT_OK(cluster_->SetFlagOnMasters("leaderless_tablet_alert_delay_secs", "5"));
  CreateSingleTabletTestTable();
  auto tablet_id = GetSingleTabletId();

  // Verify leaderless tablets list is empty.
  string result = GetLeaderlessTabletsString();
  ASSERT_EQ(result.find(tablet_id), string::npos);

  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  const auto leader = cluster_->tablet_server(leader_idx);
  const auto follower_idx = (leader_idx + 1) % 3;
  const auto follower = cluster_->tablet_server(follower_idx);
  const auto other_follower_idx = (leader_idx + 2) % 3;
  const auto other_follower = cluster_->tablet_server(other_follower_idx);

  // Pause both followers.
  ASSERT_OK(follower->Pause());
  ASSERT_OK(other_follower->Pause());

  // Leaderless endpoint should catch the tablet.
  Status wait_status = WaitFor([&] {
    string result = GetLeaderlessTabletsString();
    return result.find(tablet_id) != string::npos;
  }, 20s * kTimeMultiplier, "leaderless tablet endpoint catch the tablet");

  const auto new_leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  if (new_leader_idx != leader_idx) {
    auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(
        cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>(), &cluster_->proxy_cache()));
    const auto new_leader = cluster_->tablet_server(new_leader_idx);
    ASSERT_OK(itest::LeaderStepDown(
        ts_map[new_leader->uuid()].get(), tablet_id, ts_map[leader->uuid()].get(), 10s));
  }

  ASSERT_OK(other_follower->Resume());
  ASSERT_OK(follower->Resume());

  ASSERT_OK(wait_status);

  ASSERT_OK(WaitFor([&] {
    string result = GetLeaderlessTabletsString();
    return result.find(tablet_id) == string::npos;
  }, 20s * kTimeMultiplier, "leaderless tablet endpoint becomes empty"));

  ASSERT_OK(other_follower->Pause());
  ASSERT_OK(leader->Pause());

  // Leaderless endpoint should catch the tablet.
  wait_status = WaitFor([&] {
    string result = GetLeaderlessTabletsString();
    return result.find(tablet_id) != string::npos;
  }, 20s * kTimeMultiplier, "leaderless tablet endpoint catch the tablet");

  ASSERT_OK(other_follower->Resume());
  ASSERT_OK(leader->Resume());

  ASSERT_OK(wait_status);

  ASSERT_OK(WaitFor([&] {
    string result = GetLeaderlessTabletsString();
    return result.find(tablet_id) == string::npos;
  }, 20s * kTimeMultiplier, "leaderless tablet endpoint becomes empty"));
}

TEST_F(MasterPathHandlersLeaderlessITest, TestLeaderChange) {
  const auto kMaxLeaderLeaseExpiredMs = 5000;
  const auto kHtLeaseDurationMs = 2000;
  const auto kMaxTabletWithoutValidLeaderMs = 5000;
  ASSERT_OK(cluster_->SetFlagOnMasters("maximum_tablet_leader_lease_expired_secs",
                                       std::to_string(kMaxLeaderLeaseExpiredMs / 1000)));
  ASSERT_OK(cluster_->SetFlagOnMasters("leaderless_tablet_alert_delay_secs",
                                       std::to_string(kMaxTabletWithoutValidLeaderMs / 1000)));
  ASSERT_OK(cluster_->SetFlagOnTServers("ht_lease_duration_ms",
                                        std::to_string(kHtLeaseDurationMs)));
  CreateSingleTabletTestTable();
  auto tablet_id = GetSingleTabletId();

  SleepFor(kTserverHeartbeatMetricsIntervalMs * 2ms);

  // Initially the leaderless tablets list should be empty.
  string result = GetLeaderlessTabletsString();
  ASSERT_EQ(result.find(tablet_id), string::npos);

  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  const auto leader = cluster_->tablet_server(leader_idx);
  const auto follower_idx = (leader_idx + 1) % 3;

  auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(
      cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>(), &cluster_->proxy_cache()));

  const auto new_leader_idx = follower_idx;
  const auto new_leader = cluster_->tablet_server(new_leader_idx);
  ASSERT_OK(itest::LeaderStepDown(
      ts_map[leader->uuid()].get(), tablet_id, ts_map[new_leader->uuid()].get(), 10s));
  ASSERT_OK(WaitForLeaderPeer(tablet_id, new_leader_idx));

  // Wait the old leader's tracked leader lease to be expired. The maximum wait time is
  // (ht_lease_duration + max_leader_lease_expired).
  SleepFor((kHtLeaseDurationMs + kMaxLeaderLeaseExpiredMs) * 1ms);

  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_skip_processing_tablet_metadata", "true"));
  ASSERT_OK(itest::LeaderStepDown(
      ts_map[new_leader->uuid()].get(), tablet_id, ts_map[leader->uuid()].get(), 10s));
  ASSERT_OK(WaitForLeaderPeer(tablet_id, leader_idx));

  // Wait the next ts heartbeat to report new leader.
  SleepFor(kTserverHeartbeatMetricsIntervalMs * 2ms);

  // We don't expect to be leaderless even though the lease is expired because not enough
  // time has passed for us to alert.
  result = GetLeaderlessTabletsString();
  ASSERT_EQ(result.find(tablet_id), string::npos);

  SleepFor(kMaxTabletWithoutValidLeaderMs * 1ms);
  result = GetLeaderlessTabletsString();
  ASSERT_NE(result.find(tablet_id), string::npos);

  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_skip_processing_tablet_metadata", "false"));
  SleepFor(kTserverHeartbeatMetricsIntervalMs * 2ms);
  result = GetLeaderlessTabletsString();
  ASSERT_EQ(result.find(tablet_id), string::npos);
}

TEST_F(MasterPathHandlersLeaderlessITest, TestAllFollowers) {
  const auto kMaxTabletWithoutValidLeaderMs = 5000;
  ASSERT_OK(cluster_->SetFlagOnMasters("leaderless_tablet_alert_delay_secs",
                                       std::to_string(kMaxTabletWithoutValidLeaderMs / 1000)));
  CreateSingleTabletTestTable();
  auto tablet_id = GetSingleTabletId();

  // Initially the leaderless tablets list should be empty.
  string result = GetLeaderlessTabletsString();
  ASSERT_EQ(result.find(tablet_id), string::npos);

  // Disable new leader election after leader stepdown.
  ASSERT_OK(cluster_->SetFlagOnTServers("stepdown_disable_graceful_transition", "true"));
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_election_when_fail_detected", "true"));

  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  const auto leader = cluster_->tablet_server(leader_idx);
  auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(
      cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>(), &cluster_->proxy_cache()));
  // Leader step down and don't assign new leader, all three peers should be FOLLOWER.
  ASSERT_OK(itest::LeaderStepDown(
      ts_map[leader->uuid()].get(), tablet_id, nullptr, 10s));

  // Wait for next TS heartbeat to report all three followers.
  ASSERT_FALSE(HasLeaderPeer(tablet_id));
  SleepFor(kTserverHeartbeatMetricsIntervalMs * 2ms);

  // Shouldn't report it as leaderless before kMaxTabletWithoutValidLeaderMs.
  result = GetLeaderlessTabletsString();
  ASSERT_EQ(result.find(tablet_id), string::npos);

  ASSERT_OK(WaitFor([&] {
    string result = GetLeaderlessTabletsString();
    return result.find(tablet_id) != string::npos &&
           result.find("No valid leader reported") != string::npos;
  }, 20s * kTimeMultiplier, "leaderless tablet endpoint catch the tablet"));

  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_election_when_fail_detected", "false"));

  ASSERT_OK(WaitFor([&] {
    string result = GetLeaderlessTabletsString();
    return result.find(tablet_id) == string::npos;
  }, 20s * kTimeMultiplier, "leaderless tablet endpoint becomes empty"));
}

TEST_F(MasterPathHandlersItest, TestVarzAutoFlag) {
  static const auto kExpectedAutoFlag = "use_parent_table_id_field";

  // In LTO builds yb-master links to all of yb-tserver so it includes all AutoFlags. So test for a
  // non-AutoFlag instead.
  static const auto kUnExpectedFlag = "TEST_assert_local_op";

  // Test the HTML endpoint.
  static const auto kAutoFlagsStart = "<h2>Auto Flags</h2>";
  static const auto kAutoFlagsEnd = "<h2>Default Flags</h2>";
  faststring result;
  TestUrl("/varz", &result);
  auto result_str = result.ToString();

  auto it_auto_flags_start = result_str.find(kAutoFlagsStart);
  ASSERT_NE(it_auto_flags_start, std::string::npos);
  auto it_auto_flags_end = result_str.find(kAutoFlagsEnd);
  ASSERT_NE(it_auto_flags_end, std::string::npos);

  auto it_expected_flag = result_str.find(kExpectedAutoFlag);
  ASSERT_GT(it_expected_flag, it_auto_flags_start);
  ASSERT_LT(it_expected_flag, it_auto_flags_end);

  auto it_unexpected_flag = result_str.find(kUnExpectedFlag);
  ASSERT_GT(it_unexpected_flag, it_auto_flags_end);

  // Test the JSON API endpoint.
  TestUrl("/api/v1/varz", &result);

  JsonReader r(result.ToString());
  ASSERT_OK(r.Init());
  const rapidjson::Value* json_obj = nullptr;
  ASSERT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
  ASSERT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_obj)->GetType());
  ASSERT_TRUE(json_obj->HasMember("flags"));
  ASSERT_EQ(rapidjson::kArrayType, (*json_obj)["flags"].GetType());
  const rapidjson::Value::ConstArray flags = (*json_obj)["flags"].GetArray();

  auto it_expected_json_flag = std::find_if(flags.Begin(), flags.End(), [](const auto& flag) {
    return flag["name"] == kExpectedAutoFlag;
  });
  ASSERT_NE(it_expected_json_flag, flags.End());
  ASSERT_EQ((*it_expected_json_flag)["type"], "Auto");

  auto it_unexpected_json_flag = std::find_if(
      flags.Begin(), flags.End(), [](const auto& flag) { return flag["name"] == kUnExpectedFlag; });

  ASSERT_NE(it_unexpected_json_flag, flags.End());
  ASSERT_EQ((*it_unexpected_json_flag)["type"], "Default");
}

TEST_F(MasterPathHandlersItest, TestLeaderlessDeletedTablet) {
  auto table = CreateTestTable(kNumTablets);
  const auto kLeaderlessTabletAlertDelaySecs = 5;

  // Prevent heartbeats from overwriting replica locations.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_leaderless_tablet_alert_delay_secs) =
      kLeaderlessTabletAlertDelaySecs;

  auto& catalog_mgr = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto table_info = catalog_mgr.GetTableInfo(table->id());
  auto tablets = table_info->GetTablets();
  ASSERT_EQ(tablets.size(), kNumTablets);

  // Make all tablets leaderless.
  MonoTime last_time_with_valid_leader_override = MonoTime::Now();
  last_time_with_valid_leader_override.SubtractDelta(kLeaderlessTabletAlertDelaySecs * 1s);
  for (auto& tablet : tablets) {
    auto replicas = std::make_shared<TabletReplicaMap>(*tablet->GetReplicaLocations());
    for (auto& replica : *replicas) {
      replica.second.role = PeerRole::FOLLOWER;
    }
    tablet->SetReplicaLocations(replicas);
    tablet->TEST_set_last_time_with_valid_leader(last_time_with_valid_leader_override);
  }
  auto running_tablet = tablets[0];
  auto deleted_tablet = tablets[1];
  auto replaced_tablet = tablets[2];

  auto deleted_lock = deleted_tablet->LockForWrite();
  deleted_lock.mutable_data()->set_state(SysTabletsEntryPB::DELETED, "");
  deleted_lock.Commit();

  auto replaced_lock = replaced_tablet->LockForWrite();
  replaced_lock.mutable_data()->set_state(SysTabletsEntryPB::REPLACED, "");
  replaced_lock.Commit();

  // Only the RUNNING tablet should be returned in the endpoint.
  string result = GetLeaderlessTabletsString();
  LOG(INFO) << result;
  ASSERT_NE(result.find(running_tablet->id()), string::npos);
  ASSERT_EQ(result.find(deleted_tablet->id()), string::npos);
  ASSERT_EQ(result.find(replaced_tablet->id()), string::npos);

  // Shutdown cluster to prevent cluster consistency check from failing because of the edited
  // tablet states.
  cluster_->Shutdown();
}

}  // namespace master
}  // namespace yb
