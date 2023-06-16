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
#include <regex>

#include "yb/client/session.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/dockv/partition.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master-path-handlers.h"
#include "yb/master/mini_master.h"

#include "yb/server/webui_util.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/curl_util.h"
#include "yb/util/jsonreader.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"

DECLARE_int32(tserver_unresponsive_timeout_ms);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_string(TEST_master_extra_list_host_port);

DECLARE_int32(follower_unavailable_considered_failed_sec);

DECLARE_uint64(master_maximum_heartbeats_without_lease);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_bool(enable_automatic_tablet_splitting);

namespace yb {
namespace master {

using std::string;

using namespace std::literals;

const std::string kKeyspaceName("my_keyspace");
const client::YBTableName table_name(YQL_DATABASE_CQL, kKeyspaceName, "test_table");
const uint kNumMasters(3);
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
                 .hash_schema(dockv::YBHashSchema::kMultiColumnHash)
                 .Create());

    std::shared_ptr<client::YBTable> table;
    CHECK_OK(client->OpenTable(table_name, &table));
    return table;
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
    FLAGS_tserver_unresponsive_timeout_ms = 5000;
    opts.num_tablet_servers = kNumTablets;
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
  auto table = CreateTestTable();

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
      EXPECT_EQ(strcmp("int32 NOT NULL HASH", column_json["type"].GetString()), 0);
    } else if (!strcmp(column_json["column"].GetString(), "int_val")) {
      EXPECT_TRUE(column_json.HasMember("type"));
      EXPECT_EQ(strcmp("int32 NOT NULL VALUE", column_json["type"].GetString()), 0);
    } else if (!strcmp(column_json["column"].GetString(), "string_val")) {
      EXPECT_TRUE(column_json.HasMember("type"));
      EXPECT_EQ(strcmp("string NULLABLE VALUE", column_json["type"].GetString()), 0);
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

TEST_F(MasterPathHandlersItest, TestTabletUnderReplicationEndpoint) {
  // Set test specific flag
  FLAGS_follower_unavailable_considered_failed_sec = 30;

  auto table = CreateTestTable();

  // Get all the tablets of this table and store them
  google::protobuf::RepeatedPtrField<TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTabletsFromTableId(table->id(), kNumTablets, &tablets));

  std::vector<std::string> tIds;
  bool isTestTrue = true;

  int numTablets = tablets.size();
  for(int i = 0; i < numTablets; i++) {
    auto tablet = tablets.Get(i);
    tIds.push_back(tablet.tablet_id());
  }

  // Now kill one of the servers
  // Since the replication factor is 3 and the number
  // of nodes is also 3, all the tablets
  // of this table should become under replicated
  cluster_->mini_tablet_server(0)->Shutdown();
  // Wait for 3*30 secs just to be safe
  std::this_thread::sleep_for(std::chrono::milliseconds(
    3 * FLAGS_follower_unavailable_considered_failed_sec * 1000));

  // Call endpoint and validate format of response.
  faststring result;
  TestUrl("/api/v1/tablet-under-replication", &result);

  JsonReader r(result.ToString());
  ASSERT_OK(r.Init());
  const rapidjson::Value* json_obj = nullptr;
  EXPECT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
  EXPECT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_obj)->GetType());
  EXPECT_TRUE(json_obj->HasMember("underreplicated_tablets"));
  EXPECT_EQ(rapidjson::kArrayType, (*json_obj)["underreplicated_tablets"].GetType());
  const rapidjson::Value::ConstArray tablets_json =
      (*json_obj)["underreplicated_tablets"].GetArray();

  for (const auto& tablet_json : tablets_json) {
    EXPECT_EQ(rapidjson::kObjectType, tablet_json.GetType());
    EXPECT_TRUE(tablet_json.HasMember("table_uuid"));
    EXPECT_EQ(rapidjson::kStringType, tablet_json["table_uuid"].GetType());
    EXPECT_TRUE(tablet_json.HasMember("tablet_uuid"));
    EXPECT_EQ(rapidjson::kStringType, tablet_json["tablet_uuid"].GetType());
  }

  // These tablets should be present in the json response
  for(const std::string &id : tIds) {
    isTestTrue = isTestTrue && std::any_of(tablets_json.begin(), tablets_json.end(),
                      [&id](const auto &tablet_json) {
                          return tablet_json["tablet_uuid"].GetString() == id;
                      });
  }

  EXPECT_TRUE(isTestTrue) << "Expected to find under-replicated tablets.";

  // YBMiniClusterTestBase test-end verification will fail if the cluster is up with stopped nodes.
  cluster_->Shutdown();
}

class MultiMasterPathHandlersItest : public MasterPathHandlersItest {
 public:
  int num_masters() const override {
    return 3;
  }
};

TEST_F_EX(MasterPathHandlersItest, Forward, MultiMasterPathHandlersItest) {
  FLAGS_TEST_master_extra_list_host_port = RandomHumanReadableString(16) + ".com";
  EasyCurl curl;
  faststring content;
  for (size_t i = 0; i != cluster_->num_masters(); ++i) {
    auto url = Format("http://$0/tablet-servers", cluster_->mini_master(i)->bound_http_addr());
    content.clear();
    ASSERT_OK(curl.FetchURL(url, &content));
  }
}

class TabletSplitMasterPathHandlersItest : public MasterPathHandlersItest {
 public:
  void SetUp() override {
    FLAGS_cleanup_split_tablets_interval_sec = 1;
    FLAGS_enable_automatic_tablet_splitting = false;
    MasterPathHandlersItest::SetUp();
  }
};

TEST_F_EX(MasterPathHandlersItest, ShowDeletedTablets, TabletSplitMasterPathHandlersItest) {
  const int num_rows_to_insert = 500;

  CreateTestTable(1 /* num_tablets */);

  client::TableHandle table;
  ASSERT_OK(table.Open(table_name, client_.get()));

  auto session = client_->NewSession();
  for (int i = 0; i < num_rows_to_insert; i++) {
    auto insert = table.NewInsertOp();
    auto req = insert->mutable_request();
    QLAddInt32HashValue(req, i);
    ASSERT_OK(session->ApplyAndFlushSync(insert));
  }

  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto tablet = catalog_manager.GetTableInfo(table->id())->GetTablets()[0];

  const auto webpage_shows_deleted_tablets =
      [this, &table](const bool should_show_deleted) -> Result<bool> {
        faststring result;
        RETURN_NOT_OK(
            TestUrlWaitForOK(
                "/table?id=" + table->id() + (should_show_deleted ? "&show_deleted" : ""),
                &result,
                30s /* timeout */));
    const auto webpage = result.ToString();
    std::smatch match;
    const std::regex regex(
        "<tr>.*<td>Delete*d</td><td>0</td><td>Not serving tablet deleted upon request "
        "at(.|\n)*</tr>");
    std::regex_search(webpage, match, regex);
    return !match.empty();
  };

  ASSERT_OK(yb_admin_client_->FlushTables(
      {table_name}, false /* add_indexes */, 30 /* timeout_secs */, false /* is_compaction */));
  ASSERT_OK(catalog_manager.TEST_SplitTablet(tablet, 1 /* split_hash_code */));

  ASSERT_OK(WaitFor(
      [&]() { return tablet->LockForRead()->is_deleted(); },
      30s /* timeout */,
      "Wait for tablet split to complete and parent to be deleted"));

  ASSERT_FALSE(ASSERT_RESULT(webpage_shows_deleted_tablets(false /* should_show_deleted */)));
  ASSERT_TRUE(ASSERT_RESULT(webpage_shows_deleted_tablets(true /* should_show_deleted */)));
}

class MasterPathHandlersExternalItest : public MasterPathHandlersBaseItest<ExternalMiniCluster> {
 public:
  void InitCluster() override {
    ExternalMiniClusterOptions opts;
    // Set low heartbeat timeout.
    FLAGS_tserver_unresponsive_timeout_ms = 5000;
    opts.num_tablet_servers = kNumTablets;
    opts.num_masters = num_masters();
    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());
  }

  void SetMasterHTTPURL() override {
    HostPort master_http_endpoint = cluster_->master(0)->bound_http_hostport();
    master_http_url_ = "http://" + ToString(master_http_endpoint);
  }
};

TEST_F_EX(MasterPathHandlersItest, TestTablePlacementInfo, MasterPathHandlersExternalItest) {
  std::shared_ptr<client::YBTable> table = CreateTestTable(/* num_tablets */ 1);

  // Verify replication info is empty.
  faststring result;
  auto url = Format("/table?id=$0", table->id());
  TestUrl(url, &result);
  const string& result_str = result.ToString();
  size_t pos = result_str.find("Replication Info", 0);
  ASSERT_NE(pos, string::npos);
  ASSERT_EQ(result_str.find("live_replicas", pos + 1), string::npos);

  // Verify cluster level replication info.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("cloud.region.zone", 3, "table_uuid"));
  TestUrl(url, &result);
  const string& cluster_str = result.ToString();
  pos = cluster_str.find("Replication Info", 0);
  ASSERT_NE(pos, string::npos);
  pos = cluster_str.find("placement_zone", pos + 1);
  ASSERT_NE(pos, string::npos);
  ASSERT_EQ(cluster_str.substr(pos + 17, 4), "zone");

  // Verify table level replication info.
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(
    table->name(), "cloud.region.anotherzone", 3, "table_uuid"));
  TestUrl(url, &result);
  const string& table_str = result.ToString();
  pos = table_str.find("Replication Info", 0);
  ASSERT_NE(pos, string::npos);
  pos = table_str.find("placement_zone", pos + 1);
  ASSERT_NE(pos, string::npos);
  ASSERT_EQ(table_str.substr(pos + 17, 11), "anotherzone");
}

class MasterPathHandlersLeaderlessITest : public MasterPathHandlersExternalItest {
 public:
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

  std::shared_ptr<client::YBTable> table_;
};

TEST_F(MasterPathHandlersLeaderlessITest, TestHeartbeatsWithoutLeaderLease) {
  ASSERT_OK(cluster_->SetFlagOnMasters("master_maximum_heartbeats_without_lease", "2"));
  ASSERT_OK(cluster_->SetFlagOnMasters("tserver_heartbeat_metrics_interval_ms", "1000"));
  CreateSingleTabletTestTable();
  auto tablet_id = GetSingleTabletId();

  // Verify leaderless tablets list is empty.
  string result = GetLeaderlessTabletsString();
  ASSERT_EQ(result.find(tablet_id), string::npos);

  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
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

  ASSERT_OK(other_follower->Resume());
  ASSERT_OK(follower->Resume());

  if (!wait_status.ok()) {
    ASSERT_OK(wait_status);
  }

  ASSERT_OK(WaitFor([&] {
    string result = GetLeaderlessTabletsString();
    return result.find(tablet_id) == string::npos;
  }, 20s * kTimeMultiplier, "leaderless tablet endpoint becomes empty"));
}

}  // namespace master
}  // namespace yb
