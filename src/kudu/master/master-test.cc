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

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "kudu/common/partial_row.h"
#include "kudu/common/row_operations.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/master/master.h"
#include "kudu/master/master.proxy.h"
#include "kudu/master/master-test-util.h"
#include "kudu/master/mini_master.h"
#include "kudu/master/sys_catalog.h"
#include "kudu/master/ts_descriptor.h"
#include "kudu/master/ts_manager.h"
#include "kudu/rpc/messenger.h"
#include "kudu/server/rpc_server.h"
#include "kudu/util/status.h"
#include "kudu/util/test_util.h"

using kudu::rpc::Messenger;
using kudu::rpc::MessengerBuilder;
using kudu::rpc::RpcController;
using std::shared_ptr;
using std::string;

DECLARE_bool(catalog_manager_check_ts_count_for_create_table);

namespace kudu {
namespace master {

class MasterTest : public KuduTest {
 protected:
  virtual void SetUp() OVERRIDE {
    KuduTest::SetUp();

    // In this test, we create tables to test catalog manager behavior,
    // but we have no tablet servers. Typically this would be disallowed.
    FLAGS_catalog_manager_check_ts_count_for_create_table = false;

    // Start master
    mini_master_.reset(new MiniMaster(Env::Default(), GetTestPath("Master"), 0));
    ASSERT_OK(mini_master_->Start());
    master_ = mini_master_->master();
    ASSERT_OK(master_->WaitUntilCatalogManagerIsLeaderAndReadyForTests(MonoDelta::FromSeconds(5)));

    // Create a client proxy to it.
    MessengerBuilder bld("Client");
    ASSERT_OK(bld.Build(&client_messenger_));
    proxy_.reset(new MasterServiceProxy(client_messenger_, mini_master_->bound_rpc_addr()));
  }

  virtual void TearDown() OVERRIDE {
    mini_master_->Shutdown();
    KuduTest::TearDown();
  }

  void DoListTables(const ListTablesRequestPB& req, ListTablesResponsePB* resp);
  void DoListAllTables(ListTablesResponsePB* resp);
  Status CreateTable(const string& table_name,
                     const Schema& schema);
  Status CreateTable(const string& table_name,
                     const Schema& schema,
                     const vector<KuduPartialRow>& split_rows);

  shared_ptr<Messenger> client_messenger_;
  gscoped_ptr<MiniMaster> mini_master_;
  Master* master_;
  gscoped_ptr<MasterServiceProxy> proxy_;
};

TEST_F(MasterTest, TestPingServer) {
  // Ping the server.
  PingRequestPB req;
  PingResponsePB resp;
  RpcController controller;
  ASSERT_OK(proxy_->Ping(req, &resp, &controller));
}

static void MakeHostPortPB(const string& host, uint32_t port, HostPortPB* pb) {
  pb->set_host(host);
  pb->set_port(port);
}

// Test that shutting down a MiniMaster without starting it does not
// SEGV.
TEST_F(MasterTest, TestShutdownWithoutStart) {
  MiniMaster m(Env::Default(), "/xxxx", 0);
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
    RpcController rpc;
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    ASSERT_OK(proxy_->TSHeartbeat(req, &resp, &rpc));

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
  MakeHostPortPB("localhost", 1000, fake_reg.add_rpc_addresses());
  MakeHostPortPB("localhost", 2000, fake_reg.add_http_addresses());

  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    RpcController rpc;
    req.mutable_common()->CopyFrom(common);
    req.mutable_registration()->CopyFrom(fake_reg);
    ASSERT_OK(proxy_->TSHeartbeat(req, &resp, &rpc));

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
    RpcController rpc;
    req.mutable_common()->CopyFrom(common);
    req.mutable_registration()->CopyFrom(fake_reg);
    ASSERT_OK(proxy_->TSHeartbeat(req, &resp, &rpc));

    ASSERT_FALSE(resp.needs_reregister());
    ASSERT_TRUE(resp.needs_full_tablet_report());
  }

  // Now send a tablet report
  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    RpcController rpc;
    req.mutable_common()->CopyFrom(common);
    TabletReportPB* tr = req.mutable_tablet_report();
    tr->set_is_incremental(false);
    tr->set_sequence_number(0);
    ASSERT_OK(proxy_->TSHeartbeat(req, &resp, &rpc));

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
    RpcController rpc;
    ASSERT_OK(proxy_->ListTabletServers(req, &resp, &rpc));
    LOG(INFO) << resp.DebugString();
    ASSERT_EQ(1, resp.servers_size());
    ASSERT_EQ("my-ts-uuid", resp.servers(0).instance_id().permanent_uuid());
    ASSERT_EQ(1, resp.servers(0).instance_id().instance_seqno());
  }
}

Status MasterTest::CreateTable(const string& table_name,
                               const Schema& schema) {
  KuduPartialRow split1(&schema);
  RETURN_NOT_OK(split1.SetInt32("key", 10));

  KuduPartialRow split2(&schema);
  RETURN_NOT_OK(split2.SetInt32("key", 20));

  return CreateTable(table_name, schema, { split1, split2 });
}

Status MasterTest::CreateTable(const string& table_name,
                               const Schema& schema,
                               const vector<KuduPartialRow>& split_rows) {

  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  RpcController controller;

  req.set_name(table_name);
  RETURN_NOT_OK(SchemaToPB(schema, req.mutable_schema()));
  RowOperationsPBEncoder encoder(req.mutable_split_rows());
  for (const KuduPartialRow& row : split_rows) {
    encoder.Add(RowOperationsPB::SPLIT_ROW, row);
  }

  RETURN_NOT_OK(proxy_->CreateTable(req, &resp, &controller));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

void MasterTest::DoListTables(const ListTablesRequestPB& req, ListTablesResponsePB* resp) {
  RpcController controller;
  ASSERT_OK(proxy_->ListTables(req, resp, &controller));
  SCOPED_TRACE(resp->DebugString());
  ASSERT_FALSE(resp->has_error());
}

void MasterTest::DoListAllTables(ListTablesResponsePB* resp) {
  ListTablesRequestPB req;
  DoListTables(req, resp);
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
  {
    DeleteTableRequestPB req;
    DeleteTableResponsePB resp;
    RpcController controller;
    req.mutable_table()->set_table_name(kTableName);
    ASSERT_OK(proxy_->DeleteTable(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  // List tables, should show no table
  ASSERT_NO_FATAL_FAILURE(DoListAllTables(&tables));
  ASSERT_EQ(0, tables.tables_size());

  // Re-create the table
  ASSERT_OK(CreateTable(kTableName, kTableSchema));

  // Restart the master, verify the table still shows up.
  ASSERT_OK(mini_master_->Restart());
  ASSERT_OK(mini_master_->master()->
      WaitUntilCatalogManagerIsLeaderAndReadyForTests(MonoDelta::FromSeconds(5)));

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
    KuduPartialRow split1 = KuduPartialRow(&kTableSchema);
    ASSERT_OK(split1.SetInt32("key", 1));
    KuduPartialRow split2(&kTableSchema);
    ASSERT_OK(split2.SetInt32("key", 2));
    Status s = CreateTable(kTableName, kTableSchema, { split1, split1, split2 });
    ASSERT_TRUE(s.IsInvalidArgument()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(), "Duplicate split row");
  }

  // No empty split rows.
  {
    KuduPartialRow split1 = KuduPartialRow(&kTableSchema);
    ASSERT_OK(split1.SetInt32("key", 1));
    KuduPartialRow split2(&kTableSchema);
    Status s = CreateTable(kTableName, kTableSchema, { split1, split2 });
    ASSERT_TRUE(s.IsInvalidArgument());
    ASSERT_STR_CONTAINS(s.ToString(),
                        "Invalid argument: Split rows must contain a value for at "
                        "least one range partition column");
  }

  // No non-range columns
  {
    KuduPartialRow split = KuduPartialRow(&kTableSchema);
    ASSERT_OK(split.SetInt32("key", 1));
    ASSERT_OK(split.SetInt32("val", 1));
    Status s = CreateTable(kTableName, kTableSchema, { split });
    ASSERT_TRUE(s.IsInvalidArgument());
    ASSERT_STR_CONTAINS(s.ToString(),
                        "Invalid argument: Split rows may only contain values "
                        "for range partitioned columns: val")
  }
}

TEST_F(MasterTest, TestCreateTableInvalidKeyType) {
  const char *kTableName = "testtb";

  {
    const Schema kTableSchema({ ColumnSchema("key", BOOL) }, 1);
    Status s = CreateTable(kTableName, kTableSchema, vector<KuduPartialRow>());
    ASSERT_TRUE(s.IsInvalidArgument()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(),
        "Key column may not have type of BOOL, FLOAT, or DOUBLE");
  }

  {
    const Schema kTableSchema({ ColumnSchema("key", FLOAT) }, 1);
    Status s = CreateTable(kTableName, kTableSchema, vector<KuduPartialRow>());
    ASSERT_TRUE(s.IsInvalidArgument()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(),
        "Key column may not have type of BOOL, FLOAT, or DOUBLE");
  }

  {
    const Schema kTableSchema({ ColumnSchema("key", DOUBLE) }, 1);
    Status s = CreateTable(kTableName, kTableSchema, vector<KuduPartialRow>());
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
  RpcController controller;

  req.set_name("table");
  for (int i = 0; i < 2; i++) {
    ColumnSchemaPB* col = req.mutable_schema()->add_columns();
    col->set_name("col");
    col->set_type(INT32);
    col->set_is_key(true);
  }

  ASSERT_OK(proxy_->CreateTable(req, &resp, &controller));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ("code: INVALID_ARGUMENT message: \"Duplicate column name: col\"",
            resp.error().status().ShortDebugString());
}

// Regression test for KUDU-253/KUDU-592: crash if the GetTableLocations RPC call is
// invalid.
TEST_F(MasterTest, TestInvalidGetTableLocations) {
  const string kTableName = "test";
  Schema schema({ ColumnSchema("key", INT32) }, 1);
  ASSERT_OK(CreateTable(kTableName, schema));
  {
    GetTableLocationsRequestPB req;
    GetTableLocationsResponsePB resp;
    RpcController controller;
    req.mutable_table()->set_table_name(kTableName);
    // Set the "start" key greater than the "end" key.
    req.set_partition_key_start("zzzz");
    req.set_partition_key_end("aaaa");
    ASSERT_OK(proxy_->GetTableLocations(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ("code: INVALID_ARGUMENT message: "
              "\"start partition key is greater than the end partition key\"",
              resp.error().status().ShortDebugString());
  }
}

} // namespace master
} // namespace kudu
