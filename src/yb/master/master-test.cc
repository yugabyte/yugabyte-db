// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <algorithm>
#include <memory>
#include <sstream>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "yb/common/partial_row.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master-test-util.h"
#include "yb/master/call_home.h"
#include "yb/master/master.h"
#include "yb/master/master.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/rpc/messenger.h"
#include "yb/server/rpc_server.h"
#include "yb/server/server_base.proxy.h"
#include "yb/util/jsonreader.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;
using std::make_shared;
using std::shared_ptr;

DECLARE_string(callhome_collection_level);
DECLARE_string(callhome_tag);
DECLARE_string(callhome_url);
DECLARE_bool(catalog_manager_check_ts_count_for_create_table);
DECLARE_double(leader_failure_max_missed_heartbeat_periods);

#define NAMESPACE_ENTRY(namespace) \
    std::make_tuple(k##namespace##NamespaceName, k##namespace##NamespaceId)

#define EXPECTED_SYSTEM_NAMESPACES \
    NAMESPACE_ENTRY(System), \
    NAMESPACE_ENTRY(SystemSchema), \
    NAMESPACE_ENTRY(SystemAuth) \
    /**/

#define EXPECTED_DEFAULT_NAMESPACE \
    std::make_tuple(default_namespace_name, default_namespace_id)

#define EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES \
    EXPECTED_DEFAULT_NAMESPACE, \
    EXPECTED_SYSTEM_NAMESPACES \
    /**/

#define TABLE_ENTRY(namespace, table) \
    std::make_tuple(k##namespace##table##TableName, \
        k##namespace##NamespaceName, k##namespace##NamespaceId)

#define EXPECTED_SYSTEM_TABLES \
    TABLE_ENTRY(System, Peers), \
    TABLE_ENTRY(System, Local), \
    TABLE_ENTRY(System, Partitions), \
    TABLE_ENTRY(System, SizeEstimates), \
    std::make_tuple(kSysCatalogTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId), \
    TABLE_ENTRY(SystemSchema, Aggregates), \
    TABLE_ENTRY(SystemSchema, Columns), \
    TABLE_ENTRY(SystemSchema, Functions), \
    TABLE_ENTRY(SystemSchema, Indexes), \
    TABLE_ENTRY(SystemSchema, Triggers), \
    TABLE_ENTRY(SystemSchema, Types), \
    TABLE_ENTRY(SystemSchema, Views), \
    TABLE_ENTRY(SystemSchema, Keyspaces), \
    TABLE_ENTRY(SystemSchema, Tables), \
    TABLE_ENTRY(SystemAuth, Roles), \
    TABLE_ENTRY(SystemAuth, RolePermissions), \
    TABLE_ENTRY(SystemAuth, ResourceRolePermissionsIndex)
    /**/

namespace yb {
namespace master {

using strings::Substitute;

class MasterTest : public YBTest {
 protected:

  string default_namespace_name = "default_namespace";
  string default_namespace_id;

  void SetUp() override {
    YBTest::SetUp();

    // Set an RPC timeout for the controllers.
    controller_ = make_shared<RpcController>();
    controller_->set_timeout(MonoDelta::FromSeconds(10));

    // In this test, we create tables to test catalog manager behavior,
    // but we have no tablet servers. Typically this would be disallowed.
    FLAGS_catalog_manager_check_ts_count_for_create_table = false;

    // Start master with the create flag on.
    mini_master_.reset(
        new MiniMaster(Env::Default(), GetTestPath("Master"),
                       AllocateFreePort(), AllocateFreePort(), 0));
    ASSERT_OK(mini_master_->Start());
    ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

    // Create a client proxy to it.
    client_messenger_ = ASSERT_RESULT(MessengerBuilder("Client").Build());
    rpc::ProxyCache proxy_cache(client_messenger_);
    proxy_.reset(new MasterServiceProxy(&proxy_cache, mini_master_->bound_rpc_addr()));

    // Create the default test namespace.
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(default_namespace_name, &resp));
    default_namespace_id = resp.id();
  }

  void TearDown() override {
    mini_master_->Shutdown();
    YBTest::TearDown();
  }

  void DoListTables(const ListTablesRequestPB& req, ListTablesResponsePB* resp);
  void DoListAllTables(ListTablesResponsePB* resp, const NamespaceName& namespace_name = "");

  Status CreateTable(const NamespaceName& namespace_name,
                     const TableName& table_name,
                     const Schema& schema);
  Status CreateTable(const TableName& table_name,
                     const Schema& schema) {
    return CreateTable(default_namespace_name, table_name, schema);
  }

  Status DoCreateTable(const NamespaceName& namespace_name,
                       const TableName& table_name,
                       const Schema& schema,
                       CreateTableRequestPB* request);
  Status DoCreateTable(const TableName& table_name,
                       const Schema& schema,
                       CreateTableRequestPB* request) {
    return DoCreateTable(default_namespace_name, table_name, schema, request);
  }

  Status DeleteTable(const NamespaceName& namespace_name,
                     const TableName& table_name,
                     TableId* table_id = nullptr);
  Status DeleteTable(const TableName& table_name,
                     TableId* table_id = nullptr) {
    return DeleteTable(default_namespace_name, table_name, table_id);
  }

  void DoListAllNamespaces(ListNamespacesResponsePB* resp);
  void DoListAllNamespaces(const boost::optional<YQLDatabase>& database_type,
                           ListNamespacesResponsePB* resp);

  Status CreateNamespace(const NamespaceName& ns_name, CreateNamespaceResponsePB* resp);
  Status CreateNamespace(const NamespaceName& ns_name,
                         const boost::optional<YQLDatabase>& database_type,
                         CreateNamespaceResponsePB* resp);

  RpcController* ResetAndGetController() {
    controller_->Reset();
    return controller_.get();
  }

  void CheckNamespaces(const std::set<std::tuple<NamespaceName, NamespaceId>>& namespace_info,
                       const ListNamespacesResponsePB& namespaces) {
    for (int i = 0; i < namespaces.namespaces_size(); i++) {
      auto search_key = std::make_tuple(namespaces.namespaces(i).name(),
                                        namespaces.namespaces(i).id());
      ASSERT_TRUE(namespace_info.find(search_key) != namespace_info.end())
                    << strings::Substitute("Couldn't find namespace $0", namespaces.namespaces(i)
                        .name());
    }

    ASSERT_EQ(namespaces.namespaces_size(), namespace_info.size());
  }

  void CheckTables(const std::set<std::tuple<TableName, NamespaceName, NamespaceId>>& table_info,
                   const ListTablesResponsePB& tables) {
    for (int i = 0; i < tables.tables_size(); i++) {
      auto search_key = std::make_tuple(tables.tables(i).name(),
                                        tables.tables(i).namespace_().name(),
                                        tables.tables(i).namespace_().id());
      ASSERT_TRUE(table_info.find(search_key) != table_info.end())
          << strings::Substitute("Couldn't find table $0.$1",
              tables.tables(i).namespace_().name(), tables.tables(i).name());
    }

    ASSERT_EQ(tables.tables_size(), table_info.size());
  }

  void UpdateMasterClusterConfig(SysClusterConfigEntryPB* cluster_config) {
    ChangeMasterClusterConfigRequestPB change_req;
    change_req.mutable_cluster_config()->CopyFrom(*cluster_config);
    ChangeMasterClusterConfigResponsePB change_resp;
    ASSERT_OK(proxy_->ChangeMasterClusterConfig(change_req, &change_resp, ResetAndGetController()));
    // Bump version number by 1, so we do not have to re-query.
    cluster_config->set_version(cluster_config->version() + 1);
    LOG(INFO) << "Update cluster config to: " << cluster_config->ShortDebugString();
  }

  shared_ptr<Messenger> client_messenger_;
  gscoped_ptr<MiniMaster> mini_master_;
  gscoped_ptr<MasterServiceProxy> proxy_;
  shared_ptr<RpcController> controller_;
};

TEST_F(MasterTest, TestPingServer) {
  // Ping the server.
  server::PingRequestPB req;
  server::PingResponsePB resp;

  rpc::ProxyCache proxy_cache(client_messenger_);
  server::GenericServiceProxy generic_proxy(&proxy_cache, mini_master_->bound_rpc_addr());
  ASSERT_OK(generic_proxy.Ping(req, &resp, ResetAndGetController()));
}

static void MakeHostPortPB(const std::string& host, uint32_t port, HostPortPB* pb) {
  pb->set_host(host);
  pb->set_port(port);
}

// Test that shutting down a MiniMaster without starting it does not
// SEGV.
TEST_F(MasterTest, TestShutdownWithoutStart) {
  MiniMaster m(Env::Default(), "/xxxx", AllocateFreePort(), AllocateFreePort(), 0);
  m.Shutdown();
}

TEST_F(MasterTest, TestCallHome) {
  string json;
  CountDownLatch latch(1);
  const char* tag_value = "callhome-test";

  auto webserver_dir = GetTestPath("webserver-docroot");
  CHECK_OK(env_->CreateDir(webserver_dir));

  WebserverOptions opts;
  opts.port = 0;
  opts.doc_root = webserver_dir;
  Webserver webserver(opts, "WebserverTest");
  ASSERT_OK(webserver.Start());

  std::vector<Endpoint> addrs;
  ASSERT_OK(webserver.GetBoundAddresses(&addrs));
  ASSERT_EQ(addrs.size(), 1);
  auto addr = addrs[0];

  auto handler = [&json, &latch] (const Webserver::WebRequest& req, std::stringstream* output) {
    ASSERT_EQ(req.request_method, "POST");
    ASSERT_EQ(json, req.post_data);
    latch.CountDown();
  };

  webserver.RegisterPathHandler("/callhome", "callhome", handler);
  FLAGS_callhome_tag = tag_value;
  FLAGS_callhome_url = Substitute("http://$0/callhome", ToString(addr));

  std::unordered_map<string, vector<string>> collection_levels;
  collection_levels["low"] = {"cluster_uuid", "node_uuid", "server_type", "version_info",
                              "timestamp", "tables", "hostname", "current_user", "masters",
                              "tservers", "tablets", "gflags"};
  auto& medium = collection_levels["medium"];
  medium = collection_levels["low"];
  medium.push_back("metrics");
  medium.push_back("rpcs");
  collection_levels["high"] = medium;

  for (const auto& collection_level : collection_levels) {
    LOG(INFO) << "Collection level: " << collection_level.first;
    FLAGS_callhome_collection_level = collection_level.first;
    CallHome call_home(mini_master_->master(), ServerType::MASTER);
    json = call_home.BuildJson();
    ASSERT_TRUE(!json.empty());
    JsonReader reader(json);
    ASSERT_OK(reader.Init());
    for (const auto& field : collection_level.second) {
      LOG(INFO) << "Checking json has field: " << field;
      ASSERT_TRUE(reader.root()->HasMember(field.c_str()));
    }
    LOG(INFO) << "Checking json has field: tag";
    ASSERT_TRUE(reader.root()->HasMember("tag"));

    string received_tag;
    ASSERT_OK(reader.ExtractString(reader.root(), "tag", &received_tag));
    ASSERT_EQ(received_tag, tag_value);

    string received_hostname;
    ASSERT_OK(reader.ExtractString(reader.root(), "hostname", &received_hostname));
    ASSERT_EQ(received_hostname, mini_master_->master()->get_hostname());

    string received_user;
    ASSERT_OK(reader.ExtractString(reader.root(), "current_user", &received_user));
    ASSERT_EQ(received_user, mini_master_->master()->get_current_user());

    auto count = reader.root()->MemberEnd() - reader.root()->MemberBegin();
    // The number of fields should be equal to the number of collectors plus one for the tag field.
    ASSERT_EQ(count, collection_level.second.size() + 1);

    call_home.SendData(json);
    ASSERT_TRUE(latch.WaitFor(MonoDelta::FromSeconds(10)));
    latch.Reset(1);
  }
}

TEST_F(MasterTest, TestRegisterAndHeartbeat) {
  const char *kTsUUID = "my-ts-uuid";

  TSToMasterCommonPB common;
  common.mutable_ts_instance()->set_permanent_uuid(kTsUUID);
  common.mutable_ts_instance()->set_instance_seqno(1);

  // Try a heartbeat. The server hasn't heard of us, so should ask us to re-register.
  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    ASSERT_OK(proxy_->TSHeartbeat(req, &resp, ResetAndGetController()));

    ASSERT_TRUE(resp.needs_reregister());
    ASSERT_TRUE(resp.needs_full_tablet_report());
  }

  vector<shared_ptr<TSDescriptor> > descs;
  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(0, descs.size()) << "Should not have registered anything";

  shared_ptr<TSDescriptor> ts_desc;
  ASSERT_FALSE(mini_master_->master()->ts_manager()->LookupTSByUUID(kTsUUID, &ts_desc));

  // Register the fake TS, without sending any tablet report.
  TSRegistrationPB fake_reg;
  MakeHostPortPB("localhost", 1000, fake_reg.mutable_common()->add_private_rpc_addresses());
  MakeHostPortPB("localhost", 2000, fake_reg.mutable_common()->add_http_addresses());

  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    req.mutable_registration()->CopyFrom(fake_reg);
    ASSERT_OK(proxy_->TSHeartbeat(req, &resp, ResetAndGetController()));

    ASSERT_FALSE(resp.needs_reregister());
    ASSERT_TRUE(resp.needs_full_tablet_report());
  }

  descs.clear();
  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(1, descs.size()) << "Should have registered the TS";
  TSRegistrationPB reg = descs[0]->GetRegistration();
  ASSERT_EQ(fake_reg.DebugString(), reg.DebugString()) << "Master got different registration";

  ASSERT_TRUE(mini_master_->master()->ts_manager()->LookupTSByUUID(kTsUUID, &ts_desc));
  ASSERT_EQ(ts_desc, descs[0]);

  // If the tablet server somehow lost the response to its registration RPC, it would
  // attempt to register again. In that case, we shouldn't reject it -- we should
  // just respond the same.
  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    req.mutable_registration()->CopyFrom(fake_reg);
    ASSERT_OK(proxy_->TSHeartbeat(req, &resp, ResetAndGetController()));

    ASSERT_FALSE(resp.needs_reregister());
    ASSERT_TRUE(resp.needs_full_tablet_report());
  }

  // Now send a tablet report
  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    TabletReportPB* tr = req.mutable_tablet_report();
    tr->set_is_incremental(false);
    tr->set_sequence_number(0);
    ASSERT_OK(proxy_->TSHeartbeat(req, &resp, ResetAndGetController()));

    ASSERT_FALSE(resp.needs_reregister());
    ASSERT_FALSE(resp.needs_full_tablet_report());
  }

  descs.clear();
  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(1, descs.size()) << "Should still only have one TS registered";

  ASSERT_TRUE(mini_master_->master()->ts_manager()->LookupTSByUUID(kTsUUID, &ts_desc));
  ASSERT_EQ(ts_desc, descs[0]);

  // Ensure that the ListTabletServers shows the faked server.
  {
    ListTabletServersRequestPB req;
    ListTabletServersResponsePB resp;
    ASSERT_OK(proxy_->ListTabletServers(req, &resp, ResetAndGetController()));
    LOG(INFO) << resp.DebugString();
    ASSERT_EQ(1, resp.servers_size());
    ASSERT_EQ("my-ts-uuid", resp.servers(0).instance_id().permanent_uuid());
    ASSERT_EQ(1, resp.servers(0).instance_id().instance_seqno());
  }
}

Status MasterTest::CreateTable(const NamespaceName& namespace_name,
                               const TableName& table_name,
                               const Schema& schema) {
  CreateTableRequestPB req;
  return DoCreateTable(namespace_name, table_name, schema, &req);
}

Status MasterTest::DoCreateTable(const NamespaceName& namespace_name,
                                 const TableName& table_name,
                                 const Schema& schema,
                                 CreateTableRequestPB* request) {
  CreateTableResponsePB resp;

  request->set_name(table_name);
  SchemaToPB(schema, request->mutable_schema());

  if (!namespace_name.empty()) {
    request->mutable_namespace_()->set_name(namespace_name);
  }
  request->mutable_partition_schema()->set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
  request->set_num_tablets(8);

  // Dereferencing as the RPCs require const ref for request. Keeping request param as pointer
  // though, as that helps with readability and standardization.
  RETURN_NOT_OK(proxy_->CreateTable(*request, &resp, ResetAndGetController()));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

void MasterTest::DoListTables(const ListTablesRequestPB& req, ListTablesResponsePB* resp) {
  ASSERT_OK(proxy_->ListTables(req, resp, ResetAndGetController()));
  SCOPED_TRACE(resp->DebugString());
  ASSERT_FALSE(resp->has_error());
}

void MasterTest::DoListAllTables(ListTablesResponsePB* resp,
                                 const NamespaceName& namespace_name /*= ""*/) {
  ListTablesRequestPB req;

  if (!namespace_name.empty()) {
    req.mutable_namespace_()->set_name(namespace_name);
  }

  DoListTables(req, resp);
}

Status MasterTest::DeleteTable(const NamespaceName& namespace_name,
                               const TableName& table_name,
                               TableId* table_id /* = nullptr */) {
  DeleteTableRequestPB req;
  DeleteTableResponsePB resp;
  req.mutable_table()->set_table_name(table_name);

  if (!namespace_name.empty()) {
    req.mutable_table()->mutable_namespace_()->set_name(namespace_name);
  }

  RETURN_NOT_OK(proxy_->DeleteTable(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  if (table_id) {
    *table_id = resp.table_id();
  }

  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

TEST_F(MasterTest, TestCatalog) {
  const char *kTableName = "testtb";
  const char *kOtherTableName = "tbtest";
  const Schema kTableSchema({ ColumnSchema("key", INT32),
                              ColumnSchema("v1", UINT64),
                              ColumnSchema("v2", STRING) },
                            1);

  ASSERT_OK(CreateTable(kTableName, kTableSchema));

  ListTablesResponsePB tables;
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the table
  TableId id;
  ASSERT_OK(DeleteTable(default_namespace_name, kTableName, &id));

  IsDeleteTableDoneRequestPB done_req;
  done_req.set_table_id(id);
  IsDeleteTableDoneResponsePB done_resp;
  bool delete_done = false;

  for (int num_retries = 0; num_retries < 10; ++num_retries) {
    const Status s = proxy_->IsDeleteTableDone(done_req, &done_resp, ResetAndGetController());
    LOG(INFO) << "IsDeleteTableDone: " << s.ToString() << " done=" << done_resp.done();
    ASSERT_TRUE(s.ok());
    ASSERT_TRUE(done_resp.has_done());
    if (done_resp.done()) {
      LOG(INFO) << "Done on retry " << num_retries;
      delete_done = true;
      break;
    }

    SleepFor(MonoDelta::FromMilliseconds(10 * num_retries)); // sleep a bit more with each attempt.
  }

  ASSERT_TRUE(delete_done);

  // List tables, should show only system table
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Re-create the table
  ASSERT_OK(CreateTable(kTableName, kTableSchema));

  // Restart the master, verify the table still shows up.
  ASSERT_OK(mini_master_->Restart());
  ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Test listing tables with a filter.
  ASSERT_OK(CreateTable(kOtherTableName, kTableSchema));

  {
    ListTablesRequestPB req;
    req.set_name_filter("test");
    DoListTables(req, &tables);
    ASSERT_EQ(2, tables.tables_size());
  }

  {
    ListTablesRequestPB req;
    req.set_name_filter("tb");
    DoListTables(req, &tables);
    ASSERT_EQ(2, tables.tables_size());
  }

  {
    ListTablesRequestPB req;
    req.set_name_filter(kTableName);
    DoListTables(req, &tables);
    ASSERT_EQ(1, tables.tables_size());
    ASSERT_EQ(kTableName, tables.tables(0).name());
  }

  {
    ListTablesRequestPB req;
    req.set_name_filter("btes");
    DoListTables(req, &tables);
    ASSERT_EQ(1, tables.tables_size());
    ASSERT_EQ(kOtherTableName, tables.tables(0).name());
  }

  {
    ListTablesRequestPB req;
    req.set_name_filter("randomname");
    DoListTables(req, &tables);
    ASSERT_EQ(0, tables.tables_size());
  }

  {
    ListTablesRequestPB req;
    req.set_name_filter("peer");
    DoListTables(req, &tables);
    ASSERT_EQ(1, tables.tables_size());
    ASSERT_EQ(kSystemPeersTableName, tables.tables(0).name());
  }
}

// Regression test for KUDU-253/KUDU-592: crash if the schema passed to CreateTable
// is invalid.
TEST_F(MasterTest, TestCreateTableInvalidSchema) {
  CreateTableRequestPB req;
  CreateTableResponsePB resp;

  req.set_name("table");
  req.mutable_namespace_()->set_name(default_namespace_name);
  for (int i = 0; i < 2; i++) {
    ColumnSchemaPB* col = req.mutable_schema()->add_columns();
    col->set_name("col");
    QLType::Create(INT32)->ToQLTypePB(col->mutable_type());
    col->set_is_key(true);
  }

  ASSERT_OK(proxy_->CreateTable(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(AppStatusPB::INVALID_ARGUMENT, resp.error().status().code());
  ASSERT_EQ("Duplicate column name: col", resp.error().status().message());
}

// Regression test for KUDU-253/KUDU-592: crash if the GetTableLocations RPC call is
// invalid.
TEST_F(MasterTest, TestInvalidGetTableLocations) {
  const TableName kTableName = "test";
  Schema schema({ ColumnSchema("key", INT32) }, 1);
  ASSERT_OK(CreateTable(kTableName, schema));
  {
    GetTableLocationsRequestPB req;
    GetTableLocationsResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    // Set the "start" key greater than the "end" key.
    req.set_partition_key_start("zzzz");
    req.set_partition_key_end("aaaa");
    ASSERT_OK(proxy_->GetTableLocations(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(AppStatusPB::INVALID_ARGUMENT, resp.error().status().code());
    ASSERT_EQ("start partition key is greater than the end partition key",
              resp.error().status().message());
  }
}

TEST_F(MasterTest, TestInvalidPlacementInfo) {
  const TableName kTableName = "test";
  Schema schema({ColumnSchema("key", INT32)}, 1);
  GetMasterClusterConfigRequestPB config_req;
  GetMasterClusterConfigResponsePB config_resp;
  proxy_->GetMasterClusterConfig(config_req, &config_resp, ResetAndGetController());
  ASSERT_FALSE(config_resp.has_error());
  ASSERT_TRUE(config_resp.has_cluster_config());
  auto cluster_config = config_resp.cluster_config();

  CreateTableRequestPB req;

  // Fail due to not cloud_info.
  auto* live_replicas = cluster_config.mutable_replication_info()->mutable_live_replicas();
  live_replicas->set_num_replicas(5);
  auto* pb = live_replicas->add_placement_blocks();
  UpdateMasterClusterConfig(&cluster_config);
  Status s = DoCreateTable(kTableName, schema, &req);
  ASSERT_TRUE(s.IsInvalidArgument());

  // Fail due to min_num_replicas being more than num_replicas.
  auto* cloud_info = pb->mutable_cloud_info();
  pb->set_min_num_replicas(live_replicas->num_replicas() + 1);
  UpdateMasterClusterConfig(&cluster_config);
  s = DoCreateTable(kTableName, schema, &req);
  ASSERT_TRUE(s.IsInvalidArgument());

  // Succeed the CreateTable call, but expect to have errors on call.
  pb->set_min_num_replicas(live_replicas->num_replicas());
  cloud_info->set_placement_cloud("fail");
  UpdateMasterClusterConfig(&cluster_config);
  ASSERT_OK(DoCreateTable(kTableName, schema, &req));

  IsCreateTableDoneRequestPB is_create_req;
  IsCreateTableDoneResponsePB is_create_resp;

  is_create_req.mutable_table()->set_table_name(kTableName);
  is_create_req.mutable_table()->mutable_namespace_()->set_name(default_namespace_name);

  // TODO(bogdan): once there are mechanics to cancel a create table, or for it to be cancelled
  // automatically by the master, refactor this retry loop to an explicit wait and check the error.
  int num_retries = 10;
  while (num_retries > 0) {
    s = proxy_->IsCreateTableDone(is_create_req, &is_create_resp, ResetAndGetController());
    LOG(INFO) << s.ToString();
    // The RPC layer will respond OK, but the internal fields will be set to error.
    ASSERT_TRUE(s.ok());
    ASSERT_TRUE(is_create_resp.has_done());
    ASSERT_FALSE(is_create_resp.done());
    if (is_create_resp.has_error()) {
      ASSERT_EQ(is_create_resp.error().status().code(), AppStatusPB::INVALID_ARGUMENT);
    }

    --num_retries;
  }
}

void MasterTest::DoListAllNamespaces(ListNamespacesResponsePB* resp) {
  DoListAllNamespaces(boost::none, resp);
}

void MasterTest::DoListAllNamespaces(const boost::optional<YQLDatabase>& database_type,
                                     ListNamespacesResponsePB* resp) {
  ListNamespacesRequestPB req;
  if (database_type) {
    req.set_database_type(*database_type);
  }

  ASSERT_OK(proxy_->ListNamespaces(req, resp, ResetAndGetController()));
  SCOPED_TRACE(resp->DebugString());
  ASSERT_FALSE(resp->has_error());
}

Status MasterTest::CreateNamespace(const NamespaceName& ns_name, CreateNamespaceResponsePB* resp) {
  return CreateNamespace(ns_name, boost::none, resp);
}

Status MasterTest::CreateNamespace(const NamespaceName& ns_name,
                                   const boost::optional<YQLDatabase>& database_type,
                                   CreateNamespaceResponsePB* resp) {
  CreateNamespaceRequestPB req;
  req.set_name(ns_name);
  if (database_type) {
    req.set_database_type(*database_type);
  }

  RETURN_NOT_OK(proxy_->CreateNamespace(req, resp, ResetAndGetController()));
  if (resp->has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp->error().status()));
  }
  return Status::OK();
}

TEST_F(MasterTest, TestNamespaces) {
  ListNamespacesResponsePB namespaces;

  // Check default namespace.
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }

  // Create a new namespace.
  const NamespaceName other_ns_name = "testns";
  NamespaceId other_ns_id;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Try to create the existing namespace twice.
  {
    CreateNamespaceResponsePB resp;
    const Status s = CreateNamespace(other_ns_name, &resp);
    ASSERT_TRUE(s.IsAlreadyPresent()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(),
        Substitute("Keyspace '$0' already exists", other_ns_name));
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Delete the namespace (by ID).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_id(other_ns_id);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }

  // Re-create the namespace once again.
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Delete the namespace (by NAME).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }

  // Try to create the 'default' namespace.
  {
    CreateNamespaceResponsePB resp;
    const Status s = CreateNamespace(default_namespace_name, &resp);
    ASSERT_TRUE(s.IsAlreadyPresent()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(),
        Substitute("Keyspace '$0' already exists", default_namespace_name));
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }

  // Try to delete a non-existing namespace - by NAME.
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;

    req.mutable_namespace_()->set_name("nonexistingns");
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::NAMESPACE_NOT_FOUND);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::NOT_FOUND);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(), "Keyspace name not found");
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }
}

TEST_F(MasterTest, TestNamespaceSeparation) {
  ListNamespacesResponsePB namespaces;

  // Check default namespace.
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }

  // Create a new namespace for each of YCQL, YSQL and YEDIS database types.
  CreateNamespaceResponsePB resp;
  ASSERT_OK(CreateNamespace("test_cql", YQLDatabase::YQL_DATABASE_CQL, &resp));
  const NamespaceId cql_ns_id = resp.id();
  ASSERT_OK(CreateNamespace("test_pgsql", YQLDatabase::YQL_DATABASE_PGSQL, &resp));
  const NamespaceId pgsql_ns_id = resp.id();
  ASSERT_OK(CreateNamespace("test_redis", YQLDatabase::YQL_DATABASE_REDIS, &resp));
  const NamespaceId redis_ns_id = resp.id();

  // List all namespaces and by each database type.
  ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
  ASSERT_EQ(4 + kNumSystemNamespaces, namespaces.namespaces_size());
  CheckNamespaces(
      {
        EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
        std::make_tuple("test_cql", cql_ns_id),
        std::make_tuple("test_pgsql", pgsql_ns_id),
        std::make_tuple("test_redis", redis_ns_id),
      }, namespaces);

  ASSERT_NO_FATALS(DoListAllNamespaces(YQLDatabase::YQL_DATABASE_CQL, &namespaces));
  ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
  CheckNamespaces(
      {
        // Defalt and system namespaces are created in YCQL.
        EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
        std::make_tuple("test_cql", cql_ns_id),
      }, namespaces);

  ASSERT_NO_FATALS(DoListAllNamespaces(YQLDatabase::YQL_DATABASE_PGSQL, &namespaces));
  ASSERT_EQ(1, namespaces.namespaces_size());
  CheckNamespaces(
      {
        std::make_tuple("test_pgsql", pgsql_ns_id),
      }, namespaces);

  ASSERT_NO_FATALS(DoListAllNamespaces(YQLDatabase::YQL_DATABASE_REDIS, &namespaces));
  ASSERT_EQ(1, namespaces.namespaces_size());
  CheckNamespaces(
      {
        std::make_tuple("test_redis", redis_ns_id),
      }, namespaces);
}

TEST_F(MasterTest, TestDeletingNonEmptyNamespace) {
  ListNamespacesResponsePB namespaces;

  // Create a new namespace.
  const NamespaceName other_ns_name = "testns";
  NamespaceId other_ns_id;

  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Create a table.
  const TableName kTableName = "testtb";
  const Schema kTableSchema({ ColumnSchema("key", INT32) }, 1);

  ASSERT_OK(CreateTable(other_ns_name, kTableName, kTableSchema));

  ListTablesResponsePB tables;
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Try to delete the non-empty namespace - by NAME.
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;

    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::NAMESPACE_IS_NOT_EMPTY);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::INVALID_ARGUMENT);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "Cannot delete namespace which has table: " + kTableName);
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Try to delete the non-empty namespace - by ID.
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;

    req.mutable_namespace_()->set_id(other_ns_id);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::NAMESPACE_IS_NOT_EMPTY);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::INVALID_ARGUMENT);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "Cannot delete namespace which has table: " + kTableName);
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Delete the table.
  ASSERT_OK(DeleteTable(other_ns_name, kTableName));

  // List tables, should show only system table.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the namespace (by NAME).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }
}

TEST_F(MasterTest, TestTablesWithNamespace) {
  const TableName kTableName = "testtb";
  const Schema kTableSchema({ ColumnSchema("key", INT32) }, 1);
  ListTablesResponsePB tables;

  // Create a table with default namespace.
  ASSERT_OK(CreateTable(kTableName, kTableSchema));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the table.
  ASSERT_OK(DeleteTable(kTableName));

  // List tables, should show 1 table.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Create a table with the default namespace.
  ASSERT_OK(CreateTable(default_namespace_name, kTableName, kTableSchema));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the table.
  ASSERT_OK(DeleteTable(default_namespace_name, kTableName));

  // List tables, should show 1 table.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Try to create a table with an unknown namespace.
  {
    Status s = CreateTable("nonexistingns", kTableName, kTableSchema);
    ASSERT_TRUE(s.IsNotFound()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(), "Keyspace name not found");
  }

  // List tables, should show 1 table.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  const NamespaceName other_ns_name = "testns";

  // Create a new namespace.
  NamespaceId other_ns_id;
  ListNamespacesResponsePB namespaces;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Create a table with the defined new namespace.
  ASSERT_OK(CreateTable(other_ns_name, kTableName, kTableSchema));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Alter table: try to change the table namespace name into an invalid one.
  {
    AlterTableRequestPB req;
    AlterTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    req.mutable_new_namespace()->set_name("nonexistingns");
    ASSERT_OK(proxy_->AlterTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::NAMESPACE_NOT_FOUND);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::NOT_FOUND);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(), "Keyspace name not found");
  }
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Alter table: try to change the table namespace id into an invalid one.
  {
    AlterTableRequestPB req;
    AlterTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    req.mutable_new_namespace()->set_id("deadbeafdeadbeafdeadbeafdeadbeaf");
    ASSERT_OK(proxy_->AlterTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::NAMESPACE_NOT_FOUND);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::NOT_FOUND);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(), "Keyspace identifier not found");
  }
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Alter table: change namespace name into the default one.
  {
    AlterTableRequestPB req;
    AlterTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    req.mutable_new_namespace()->set_name(default_namespace_name);
    ASSERT_OK(proxy_->AlterTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the table.
  ASSERT_OK(DeleteTable(default_namespace_name, kTableName));

  // List tables, should show 1 table.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the namespace (by NAME).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }
}

TEST_F(MasterTest, TestFullTableName) {
  const TableName kTableName = "testtb";
  const Schema kTableSchema({ ColumnSchema("key", INT32) }, 1);
  ListTablesResponsePB tables;

  // Create a table with the default namespace.
  ASSERT_OK(CreateTable(default_namespace_name, kTableName, kTableSchema));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  const NamespaceName other_ns_name = "testns";

  // Create a new namespace.
  NamespaceId other_ns_id;
  ListNamespacesResponsePB namespaces;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id)
        }, namespaces);
  }

  // Create a table with the defined new namespace.
  ASSERT_OK(CreateTable(other_ns_name, kTableName, kTableSchema));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(2 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id),
          std::make_tuple(kTableName, other_ns_name, other_ns_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Test ListTables() for one particular namespace.
  // There are 2 tables now: 'default_namespace::testtb' and 'testns::testtb'.
  ASSERT_NO_FATALS(DoListAllTables(&tables, default_namespace_name));
  ASSERT_EQ(1, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id),
      }, tables);

  ASSERT_NO_FATALS(DoListAllTables(&tables, other_ns_name));
  ASSERT_EQ(1, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id)
      }, tables);

  // Try to alter table: change namespace name into the default one.
  // Try to change 'testns::testtb' into 'default_namespace::testtb', but the target table exists,
  // so it must fail.
  {
    AlterTableRequestPB req;
    AlterTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    req.mutable_new_namespace()->set_name(default_namespace_name);
    ASSERT_OK(proxy_->AlterTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::OBJECT_ALREADY_PRESENT);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::ALREADY_PRESENT);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        " already exists");
  }
  // Check that nothing's changed (still have 3 tables).
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(2 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id),
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the table in the namespace 'testns'.
  ASSERT_OK(DeleteTable(other_ns_name, kTableName));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Try to delete the table from wrong namespace (table 'default_namespace::testtbl').
  {
    DeleteTableRequestPB req;
    DeleteTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_->DeleteTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::OBJECT_NOT_FOUND);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::NOT_FOUND);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "The object does not exist");
  }

  // Delete the table.
  ASSERT_OK(DeleteTable(default_namespace_name, kTableName));

  // List tables, should show only system tables.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the namespace (by NAME).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }
}

TEST_F(MasterTest, TestGetTableSchema) {
  // Create a new namespace.
  const NamespaceName other_ns_name = "testns";
  NamespaceId other_ns_id;
  ListNamespacesResponsePB namespaces;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Create a table with the defined new namespace.
  const TableName kTableName = "testtb";
  const Schema kTableSchema({ ColumnSchema("key", INT32) }, 1);
  ASSERT_OK(CreateTable(other_ns_name, kTableName, kTableSchema));

  ListTablesResponsePB tables;
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  TableId table_id;
  for (int i = 0; i < tables.tables_size(); ++i) {
    if (tables.tables(i).name() == kTableName) {
        table_id = tables.tables(i).id();
        break;
    }
  }

  ASSERT_FALSE(table_id.empty()) << "Couldn't get table id for table " << kTableName;

  // Check GetTableSchema().
  {
    GetTableSchemaRequestPB req;
    GetTableSchemaResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);

    // Check the request.
    ASSERT_OK(proxy_->GetTableSchema(req, &resp, ResetAndGetController()));

    // Check the responsed data.
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    ASSERT_TRUE(resp.has_table_type());
    ASSERT_TRUE(resp.has_create_table_done());
    // SchemaPB schema.
    ASSERT_TRUE(resp.has_schema());
    ASSERT_EQ(1, resp.schema().columns_size());
    ASSERT_EQ(Schema::first_column_id(), resp.schema().columns(0).id());
    ASSERT_EQ("key", resp.schema().columns(0).name());
    ASSERT_EQ(INT32, resp.schema().columns(0).type().main());
    ASSERT_TRUE(resp.schema().columns(0).is_key());
    ASSERT_FALSE(resp.schema().columns(0).is_nullable());
    ASSERT_EQ(1, resp.schema().columns(0).sorting_type());
    // PartitionSchemaPB partition_schema.
    ASSERT_TRUE(resp.has_partition_schema());
    ASSERT_TRUE(resp.partition_schema().has_range_schema());
    ASSERT_EQ(resp.partition_schema().hash_schema(), PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
    // TableIdentifierPB identifier.
    ASSERT_TRUE(resp.has_identifier());
    ASSERT_TRUE(resp.identifier().has_table_name());
    ASSERT_EQ(kTableName, resp.identifier().table_name());
    ASSERT_TRUE(resp.identifier().has_table_id());
    ASSERT_EQ(table_id, resp.identifier().table_id());
    ASSERT_TRUE(resp.identifier().has_namespace_());
    ASSERT_TRUE(resp.identifier().namespace_().has_name());
    ASSERT_EQ(other_ns_name, resp.identifier().namespace_().name());
    ASSERT_TRUE(resp.identifier().namespace_().has_id());
    ASSERT_EQ(other_ns_id, resp.identifier().namespace_().id());
  }

  // Delete the table in the namespace 'testns'.
  ASSERT_OK(DeleteTable(other_ns_name, kTableName));

  // List tables, should show only system tables.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the namespace (by NAME).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }
}

} // namespace master
} // namespace yb
