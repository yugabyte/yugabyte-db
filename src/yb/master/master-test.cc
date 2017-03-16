// Licensed to the Apache Software Foundation (ASF) under one
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

#include <algorithm>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "yb/common/partial_row.h"
#include "yb/common/row_operations.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master-test-util.h"
#include "yb/master/master.h"
#include "yb/master/master.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/rpc/messenger.h"
#include "yb/server/rpc_server.h"
#include "yb/server/server_base.proxy.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;
using std::make_shared;
using std::shared_ptr;

DECLARE_bool(catalog_manager_check_ts_count_for_create_table);

namespace yb {
namespace master {

using strings::Substitute;

class MasterTest : public YBTest {
 protected:
  virtual void SetUp() OVERRIDE {
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
                       AllocateFreePort(), AllocateFreePort(), true /* is_creating */));
    ASSERT_OK(mini_master_->Start());
    master_ = mini_master_->master();
    ASSERT_OK(master_->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

    // Create a client proxy to it.
    MessengerBuilder bld("Client");
    ASSERT_OK(bld.Build(&client_messenger_));
    proxy_.reset(new MasterServiceProxy(client_messenger_, mini_master_->bound_rpc_addr()));
  }

  virtual void TearDown() OVERRIDE {
    mini_master_->Shutdown();
    YBTest::TearDown();
  }

  void DoListTables(const ListTablesRequestPB& req, ListTablesResponsePB* resp);
  void DoListAllTables(ListTablesResponsePB* resp, const NamespaceName& namespace_name = "");

  Status CreateTable(const TableName& table_name,
                     const Schema& schema,
                     const NamespaceName& namespace_name = "");

  Status CreateTableWithSplits(const TableName& table_name,
                               const Schema& schema,
                               const vector<YBPartialRow>& split_rows,
                               const NamespaceName& namespace_name = "");

  Status DoCreateTable(const TableName& table_name,
                       const Schema& schema,
                       CreateTableRequestPB* request,
                       const NamespaceName& namespace_name = "");

  Status DeleteTable(const TableName& table_name,
                     const NamespaceName& namespace_name = "");

  void DoListAllNamespaces(ListNamespacesResponsePB* resp);
  Status CreateNamespace(const NamespaceName& ns_name, CreateNamespaceResponsePB* resp);

  RpcController* ResetAndGetController() {
    controller_->Reset();
    return controller_.get();
  }

  shared_ptr<Messenger> client_messenger_;
  gscoped_ptr<MiniMaster> mini_master_;
  Master* master_;
  gscoped_ptr<MasterServiceProxy> proxy_;
  shared_ptr<RpcController> controller_;
};

TEST_F(MasterTest, TestPingServer) {
  // Ping the server.
  server::PingRequestPB req;
  server::PingResponsePB resp;
  gscoped_ptr<server::GenericServiceProxy> generic_proxy;
  generic_proxy.reset(
      new server::GenericServiceProxy(client_messenger_, mini_master_->bound_rpc_addr()));
  ASSERT_OK(generic_proxy->Ping(req, &resp, ResetAndGetController()));
}

static void MakeHostPortPB(const std::string& host, uint32_t port, HostPortPB* pb) {
  pb->set_host(host);
  pb->set_port(port);
}

// Test that shutting down a MiniMaster without starting it does not
// SEGV.
TEST_F(MasterTest, TestShutdownWithoutStart) {
  MiniMaster m(Env::Default(), "/xxxx", AllocateFreePort(), AllocateFreePort(),
               true /* is_creating */);
  m.Shutdown();
}

TEST_F(MasterTest, TestRegisterAndHeartbeat) {
  const char *kTsUUID = "my-ts-uuid";

  TSToMasterCommonPB common;
  common.mutable_ts_instance()->set_permanent_uuid(kTsUUID);
  common.mutable_ts_instance()->set_instance_seqno(1);

  // Try a heartbeat. The server hasn't heard of us, so should ask us
  // to re-register.
  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    ASSERT_OK(proxy_->TSHeartbeat(req, &resp, ResetAndGetController()));

    ASSERT_TRUE(resp.needs_reregister());
    ASSERT_TRUE(resp.needs_full_tablet_report());
  }

  vector<shared_ptr<TSDescriptor> > descs;
  master_->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(0, descs.size()) << "Should not have registered anything";

  shared_ptr<TSDescriptor> ts_desc;
  ASSERT_FALSE(master_->ts_manager()->LookupTSByUUID(kTsUUID, &ts_desc));

  // Register the fake TS, without sending any tablet report.
  TSRegistrationPB fake_reg;
  MakeHostPortPB("localhost", 1000, fake_reg.mutable_common()->add_rpc_addresses());
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
  master_->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(1, descs.size()) << "Should have registered the TS";
  TSRegistrationPB reg;
  descs[0]->GetRegistration(&reg);
  ASSERT_EQ(fake_reg.DebugString(), reg.DebugString()) << "Master got different registration";

  ASSERT_TRUE(master_->ts_manager()->LookupTSByUUID(kTsUUID, &ts_desc));
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
  master_->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(1, descs.size()) << "Should still only have one TS registered";

  ASSERT_TRUE(master_->ts_manager()->LookupTSByUUID(kTsUUID, &ts_desc));
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

Status MasterTest::CreateTable(const TableName& table_name,
                               const Schema& schema,
                               const NamespaceName& namespace_name /* = "" */) {
  YBPartialRow split1(&schema);
  RETURN_NOT_OK(split1.SetInt32("key", 10));

  YBPartialRow split2(&schema);
  RETURN_NOT_OK(split2.SetInt32("key", 20));

  return CreateTableWithSplits(table_name, schema, { split1, split2 }, namespace_name);
}

Status MasterTest::CreateTableWithSplits(const TableName& table_name,
                                         const Schema& schema,
                                         const vector<YBPartialRow>& split_rows,
                                         const NamespaceName& namespace_name /* = "" */) {
  CreateTableRequestPB req;
  RowOperationsPBEncoder encoder(req.mutable_split_rows());
  for (const YBPartialRow& row : split_rows) {
    encoder.Add(RowOperationsPB::SPLIT_ROW, row);
  }
  return DoCreateTable(table_name, schema, &req, namespace_name);
}

Status MasterTest::DoCreateTable(const TableName& table_name,
                                 const Schema& schema,
                                 CreateTableRequestPB* request,
                                 const NamespaceName& namespace_name /* = "" */) {
  CreateTableResponsePB resp;

  request->set_name(table_name);
  RETURN_NOT_OK(SchemaToPB(schema, request->mutable_schema()));

  if (!namespace_name.empty()) {
    request->mutable_namespace_()->set_name(namespace_name);
  }

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

Status MasterTest::DeleteTable(const TableName& table_name,
                               const NamespaceName& namespace_name /* = "" */) {
  DeleteTableRequestPB req;
  DeleteTableResponsePB resp;
  req.mutable_table()->set_table_name(table_name);

  if (!namespace_name.empty()) {
    req.mutable_table()->mutable_namespace_()->set_name(namespace_name);
  }

  RETURN_NOT_OK(proxy_->DeleteTable(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());

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
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());

  // Delete the table
  ASSERT_OK(DeleteTable(kTableName));

  // List tables, should show no table
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(0, tables.tables_size());

  // Re-create the table
  ASSERT_OK(CreateTable(kTableName, kTableSchema));

  // Restart the master, verify the table still shows up.
  ASSERT_OK(mini_master_->Restart());
  ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());

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
}

TEST_F(MasterTest, TestCreateTableCheckSplitRows) {
  const char *kTableName = "testtb";
  const Schema kTableSchema({ ColumnSchema("key", INT32), ColumnSchema("val", INT32) }, 1);

  // No duplicate split rows.
  {
    YBPartialRow split1 = YBPartialRow(&kTableSchema);
    ASSERT_OK(split1.SetInt32("key", 1));
    YBPartialRow split2(&kTableSchema);
    ASSERT_OK(split2.SetInt32("key", 2));
    Status s = CreateTableWithSplits(kTableName, kTableSchema, { split1, split1, split2 });
    ASSERT_TRUE(s.IsInvalidArgument()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(), "Duplicate split row");
  }

  // No empty split rows.
  {
    YBPartialRow split1 = YBPartialRow(&kTableSchema);
    ASSERT_OK(split1.SetInt32("key", 1));
    YBPartialRow split2(&kTableSchema);
    Status s = CreateTableWithSplits(kTableName, kTableSchema, { split1, split2 });
    ASSERT_TRUE(s.IsInvalidArgument());
    ASSERT_STR_CONTAINS(s.ToString(/* no file/line */ false),
                        "Invalid argument: Split rows must contain a value for at "
                        "least one range partition column");
  }

  // No non-range columns
  {
    YBPartialRow split = YBPartialRow(&kTableSchema);
    ASSERT_OK(split.SetInt32("key", 1));
    ASSERT_OK(split.SetInt32("val", 1));
    Status s = CreateTableWithSplits(kTableName, kTableSchema, { split });
    ASSERT_TRUE(s.IsInvalidArgument());
    ASSERT_STR_CONTAINS(s.ToString(/* no file/line */ false),
                        "Invalid argument: Split rows may only contain values "
                        "for range partitioned columns: val");
  }
}

TEST_F(MasterTest, TestCreateTableInvalidKeyType) {
  const char *kTableName = "testtb";

  {
    const Schema kTableSchema({ ColumnSchema("key", BOOL) }, 1);
    Status s = CreateTableWithSplits(kTableName, kTableSchema, vector<YBPartialRow>());
    ASSERT_TRUE(s.IsInvalidArgument()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(),
        "Key column may not have type of BOOL, FLOAT, or DOUBLE");
  }

  {
    const Schema kTableSchema({ ColumnSchema("key", FLOAT) }, 1);
    Status s = CreateTableWithSplits(kTableName, kTableSchema, vector<YBPartialRow>());
    ASSERT_TRUE(s.IsInvalidArgument()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(),
        "Key column may not have type of BOOL, FLOAT, or DOUBLE");
  }

  {
    const Schema kTableSchema({ ColumnSchema("key", DOUBLE) }, 1);
    Status s = CreateTableWithSplits(kTableName, kTableSchema, vector<YBPartialRow>());
    ASSERT_TRUE(s.IsInvalidArgument()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(),
        "Key column may not have type of BOOL, FLOAT, or DOUBLE");
  }
}

// Regression test for KUDU-253/KUDU-592: crash if the schema passed to CreateTable
// is invalid.
TEST_F(MasterTest, TestCreateTableInvalidSchema) {
  CreateTableRequestPB req;
  CreateTableResponsePB resp;

  req.set_name("table");
  for (int i = 0; i < 2; i++) {
    ColumnSchemaPB* col = req.mutable_schema()->add_columns();
    col->set_name("col");
    col->set_type(INT32);
    col->set_is_key(true);
  }

  ASSERT_OK(proxy_->CreateTable(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ("code: INVALID_ARGUMENT message: \"Duplicate column name: col\"",
            resp.error().status().ShortDebugString());
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
    ASSERT_EQ("code: INVALID_ARGUMENT message: "
              "\"start partition key is greater than the end partition key\"",
              resp.error().status().ShortDebugString());
  }
}

TEST_F(MasterTest, TestInvalidPlacementInfo) {
  const TableName kTableName = "test";
  Schema schema({ColumnSchema("key", INT32)}, 1);
  CreateTableRequestPB req;
  req.mutable_replication_info()->mutable_live_replicas()->set_num_replicas(5);
  auto* pb = req.mutable_replication_info()->mutable_live_replicas()->add_placement_blocks();

  // Fail due to not cloud_info.
  Status s = DoCreateTable(kTableName, schema, &req);
  ASSERT_TRUE(s.IsInvalidArgument());

  auto* cloud_info = pb->mutable_cloud_info();
  pb->set_min_num_replicas(req.replication_info().live_replicas().num_replicas() + 1);

  // Fail due to min_num_replicas being more than num_replicas.
  s = DoCreateTable(kTableName, schema, &req);
  ASSERT_TRUE(s.IsInvalidArgument());

  // Succeed the CreateTable call, but expect to have errors on call.
  pb->set_min_num_replicas(req.replication_info().live_replicas().num_replicas());
  cloud_info->set_placement_cloud("fail");
  ASSERT_OK(DoCreateTable(kTableName, schema, &req));

  IsCreateTableDoneRequestPB is_create_req;
  IsCreateTableDoneResponsePB is_create_resp;

  is_create_req.mutable_table()->set_table_name(kTableName);

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
  ListNamespacesRequestPB req;

  ASSERT_OK(proxy_->ListNamespaces(req, resp, ResetAndGetController()));
  SCOPED_TRACE(resp->DebugString());
  ASSERT_FALSE(resp->has_error());
}

Status MasterTest::CreateNamespace(const NamespaceName& ns_name, CreateNamespaceResponsePB* resp) {
  CreateNamespaceRequestPB req;
  req.set_name(ns_name);

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
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId};
    for (int i = 0; i < 2; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
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
    // Order is not important. It's defined by the internal implementation.
    // So, the default namespace can be first (index == 0) or last (index=1).
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(3, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName, other_ns_name};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId, other_ns_id};
    for (int i = 0; i < 3; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }

  // Try to create the existing namespace twice.
  {
    CreateNamespaceResponsePB resp;
    const Status s = CreateNamespace(other_ns_name, &resp);
    ASSERT_TRUE(s.IsAlreadyPresent()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(),
        Substitute("Namespace $0 already exists", other_ns_name));
  }
  {
    // Order is not important. It's defined by the internal implementation.
    // So, the default namespace can be first (index == 0) or last (index=1).
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(3, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName, other_ns_name};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId, other_ns_id};
    for (int i = 0; i < 3; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
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
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId};
    for (int i = 0; i < 2; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }

  // Re-create the namespace once again.
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    // Order is not important. It's defined by the internal implementation.
    // So, the default namespace can be first (index == 0) or last (index=1).
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(3, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName, other_ns_name};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId, other_ns_id};
    for (int i = 0; i < 3; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
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
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId};
    for (int i = 0; i < 2; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }

  // Try to create the 'default' namespace.
  {
    CreateNamespaceResponsePB resp;
    const Status s = CreateNamespace(kDefaultNamespaceName, &resp);
    ASSERT_TRUE(s.IsAlreadyPresent()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(),
        Substitute("Namespace $0 already exists", kDefaultNamespaceName));
  }
  {
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId};
    for (int i = 0; i < 2; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }

  // Try to delete the 'default' namespace - by ID.
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;

    req.mutable_namespace_()->set_id(kDefaultNamespaceId);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::CANNOT_DELETE_DEFAULT_NAMESPACE);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::INVALID_ARGUMENT);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "Cannot delete default namespace");
  }
  {
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId};
    for (int i = 0; i < 2; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }

  // Try to delete the 'default' namespace - by NAME.
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;

    req.mutable_namespace_()->set_name(kDefaultNamespaceName);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::CANNOT_DELETE_DEFAULT_NAMESPACE);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::INVALID_ARGUMENT);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "Cannot delete default namespace");
  }
  {
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId};
    for (int i = 0; i < 2; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
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
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "The namespace does not exist");
  }
  {
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId};
    for (int i = 0; i < 2; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }
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
    // Order is not important. It's defined by the internal implementation.
    // So, the default namespace can be first (index == 0) or last (index=1).
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(3, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName, other_ns_name};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId, other_ns_id};
    for (int i = 0; i < 3; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }

  // Create a table.
  const TableName kTableName = "testtb";
  const Schema kTableSchema({ ColumnSchema("key", INT32) }, 1);

  ASSERT_OK(CreateTable(kTableName, kTableSchema, other_ns_name));

  ListTablesResponsePB tables;
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  ASSERT_EQ(other_ns_name, tables.tables(0).namespace_().name());
  ASSERT_EQ(other_ns_id, tables.tables(0).namespace_().id());

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
        "Cannot delete namespace which has a table: " + kTableName);
  }
  {
    // Order is not important. It's defined by the internal implementation.
    // So, the default namespace can be first (index == 0) or last (index=1).
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(3, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName, other_ns_name};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId, other_ns_id};
    for (int i = 0; i < 3; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
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
        "Cannot delete namespace which has a table: " + kTableName);
  }
  {
    // Order is not important. It's defined by the internal implementation.
    // So, the default namespace can be first (index == 0) or last (index=1).
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(3, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName, other_ns_name};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId, other_ns_id};
    for (int i = 0; i < 3; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }

  // Delete the table.
  ASSERT_OK(DeleteTable(kTableName, other_ns_name));

  // List tables, should show no table.
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(0, tables.tables_size());

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
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId};
    for (int i = 0; i < 2; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }
}

TEST_F(MasterTest, TestTablesWithNamespace) {
  const TableName kTableName = "testtb";
  const Schema kTableSchema({ ColumnSchema("key", INT32) }, 1);
  ListTablesResponsePB tables;

  // Create a table with default namespace.
  ASSERT_OK(CreateTable(kTableName, kTableSchema));

  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  ASSERT_EQ(kDefaultNamespaceName, tables.tables(0).namespace_().name());
  ASSERT_EQ(kDefaultNamespaceId, tables.tables(0).namespace_().id());

  // Delete the table.
  ASSERT_OK(DeleteTable(kTableName));

  // List tables, should show no table.
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(0, tables.tables_size());

  // Create a table with the defined (default in fact) namespace.
  ASSERT_OK(CreateTable(kTableName, kTableSchema, kDefaultNamespaceName));

  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  ASSERT_EQ(kDefaultNamespaceName, tables.tables(0).namespace_().name());
  ASSERT_EQ(kDefaultNamespaceId, tables.tables(0).namespace_().id());

  // Delete the table.
  ASSERT_OK(DeleteTable(kTableName, kDefaultNamespaceName));

  // List tables, should show no table.
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(0, tables.tables_size());

  // Try to create a table with an unknown namespace.
  {
    Status s = CreateTable(kTableName, kTableSchema, "nonexistingns");
    ASSERT_TRUE(s.IsInvalidArgument()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(), "Invalid namespace id or namespace name");
  }

  // List tables, should show no table.
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(0, tables.tables_size());

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
    // Order is not important. It's defined by the internal implementation.
    // So, the default namespace can be first (index == 0) or last (index=1).
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(3, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName, other_ns_name};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId, other_ns_id};
    for (int i = 0; i < 3; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }

  // Create a table with the defined new namespace.
  ASSERT_OK(CreateTable(kTableName, kTableSchema, other_ns_name));

  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  ASSERT_EQ(other_ns_name, tables.tables(0).namespace_().name());
  ASSERT_EQ(other_ns_id, tables.tables(0).namespace_().id());

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
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::INVALID_ARGUMENT);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "Invalid namespace id or namespace name");
  }
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  // Check: namespace is NOT changed
  ASSERT_EQ(other_ns_name, tables.tables(0).namespace_().name());
  ASSERT_EQ(other_ns_id, tables.tables(0).namespace_().id());

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
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::INVALID_ARGUMENT);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "Invalid namespace id or namespace name");
  }
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  // Check: namespace is NOT changed
  ASSERT_EQ(other_ns_name, tables.tables(0).namespace_().name());
  ASSERT_EQ(other_ns_id, tables.tables(0).namespace_().id());

  // Alter table: change namespace name into the default one.
  {
    AlterTableRequestPB req;
    AlterTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    req.mutable_new_namespace()->set_name(kDefaultNamespaceName);
    ASSERT_OK(proxy_->AlterTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  // Check: namespace is CHANGED
  ASSERT_EQ(kDefaultNamespaceName, tables.tables(0).namespace_().name());
  ASSERT_EQ(kDefaultNamespaceId, tables.tables(0).namespace_().id());

  // Delete the table.
  ASSERT_OK(DeleteTable(kTableName, kDefaultNamespaceName));

  // List tables, should show no table.
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(0, tables.tables_size());

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
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId};
    for (int i = 0; i < 2; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }
}

TEST_F(MasterTest, TestFullTableName) {
  const TableName kTableName = "testtb";
  const Schema kTableSchema({ ColumnSchema("key", INT32) }, 1);
  ListTablesResponsePB tables;

  // Create a table with the defined (default in fact) namespace.
  ASSERT_OK(CreateTable(kTableName, kTableSchema, kDefaultNamespaceName));

  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  ASSERT_EQ(kDefaultNamespaceName, tables.tables(0).namespace_().name());
  ASSERT_EQ(kDefaultNamespaceId, tables.tables(0).namespace_().id());

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
    // Order is not important. It's defined by the internal implementation.
    // So, the default namespace can be first (index == 0) or last (index=1).
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(3, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName, other_ns_name};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId, other_ns_id};
    for (int i = 0; i < 3; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }

  // Create a table with the defined new namespace.
  ASSERT_OK(CreateTable(kTableName, kTableSchema, other_ns_name));

  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(2, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  ASSERT_EQ(other_ns_name, tables.tables(0).namespace_().name());
  ASSERT_EQ(other_ns_id, tables.tables(0).namespace_().id());
  ASSERT_EQ(kTableName, tables.tables(1).name());
  ASSERT_EQ(kDefaultNamespaceName, tables.tables(1).namespace_().name());
  ASSERT_EQ(kDefaultNamespaceId, tables.tables(1).namespace_().id());

  // Test ListTables() for one particular namespace.
  // There are 2 tables now: 'default::testtb' and 'testns::testtb'.
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables, kDefaultNamespaceName));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  ASSERT_EQ(kDefaultNamespaceName, tables.tables(0).namespace_().name());
  ASSERT_EQ(kDefaultNamespaceId, tables.tables(0).namespace_().id());

  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables, other_ns_name));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  ASSERT_EQ(other_ns_name, tables.tables(0).namespace_().name());
  ASSERT_EQ(other_ns_id, tables.tables(0).namespace_().id());

  // Try to alter table: change namespace name into the default one.
  // Try to change 'testns::testtb' into 'default::testtb', but the target table exists,
  // so it must fail.
  {
    AlterTableRequestPB req;
    AlterTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    req.mutable_new_namespace()->set_name(kDefaultNamespaceName);
    ASSERT_OK(proxy_->AlterTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::TABLE_ALREADY_PRESENT);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::ALREADY_PRESENT);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "Table already exists");
  }
  // Check that nothing's changed (still have 2 tables).
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(2, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  ASSERT_EQ(other_ns_name, tables.tables(0).namespace_().name());
  ASSERT_EQ(other_ns_id, tables.tables(0).namespace_().id());
  ASSERT_EQ(kTableName, tables.tables(1).name());
  ASSERT_EQ(kDefaultNamespaceName, tables.tables(1).namespace_().name());
  ASSERT_EQ(kDefaultNamespaceId, tables.tables(1).namespace_().id());

  // Delete the table in the namespace 'testns'.
  ASSERT_OK(DeleteTable(kTableName, other_ns_name));

  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(1, tables.tables_size());
  ASSERT_EQ(kTableName, tables.tables(0).name());
  ASSERT_EQ(kDefaultNamespaceName, tables.tables(0).namespace_().name());
  ASSERT_EQ(kDefaultNamespaceId, tables.tables(0).namespace_().id());

  // Try to delete the table from wrong namespace (table 'default::testtbl').
  {
    DeleteTableRequestPB req;
    DeleteTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_->DeleteTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::TABLE_NOT_FOUND);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::NOT_FOUND);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "The table does not exist");
  }

  // Delete the table.
  ASSERT_OK(DeleteTable(kTableName, kDefaultNamespaceName));

  // List tables, should show no table.
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(0, tables.tables_size());

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
    ASSERT_NO_FATAL_FAILURE(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2, namespaces.namespaces_size());
    std::set<string> namespace_names {kDefaultNamespaceName, kSystemNamespaceName};
    std::set<string> namespace_ids {kDefaultNamespaceId, kSystemNamespaceId};
    for (int i = 0; i < 2; i++) {
      ASSERT_TRUE(namespace_names.find(namespaces.namespaces(i).name()) != namespace_names.end());
      ASSERT_TRUE(namespace_ids.find(namespaces.namespaces(i).id()) != namespace_ids.end());
    }
  }
}

} // namespace master
} // namespace yb
