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
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <future>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/client_fwd.h"
#include "yb/client/client-test-util.h"
#include "yb/client/schema.h"
#include "yb/client/table_handle.h"
#include "yb/client/meta_cache.h"
#include "yb/client/namespace_info.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_op.h"

#include "yb/common/common.pb.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_util.h"
#include "yb/master/master_admin.proxy.h"

#include "yb/util/async_util.h"
#include "yb/util/json_document.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/scope_exit.h"
#include "yb/rpc/sidecars.h"
#include "yb/util/sync_point.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"


using yb::client::YBTableName;
using yb::client::YBTableType;
// DECLARE_bool(TEST_always_return_consensus_info_for_succeeded_rpc);
DECLARE_bool(enable_metacache_partial_refresh);
DECLARE_int32(TEST_delay_connect_ms);
DECLARE_uint64(rpc_connecting_call_timeout_as_connect_failure_ms);

namespace yb {

class MetacacheRefreshITest : public MiniClusterTestWithClient<ExternalMiniCluster> {
 public:
  const std::string kPgsqlNamespaceName = "test_namespace";
  const std::string kPgsqlTableName = "test_table";
  const std::string kPgsqlTableId = "test_table_id";
  const std::string kPgsqlKeyspaceName = "test_keyspace";
  const std::string kPgsqlKeyspaceID = "test_keyspace_id";

  Result<pgwrapper::PGConn> ConnectToDB(
      const std::string& dbname, bool simple_query_protocol = false) {
    return pgwrapper::PGConnBuilder({.host = cluster_->ysql_hostport(0).host(),
                                     .port = cluster_->ysql_hostport(0).port(),
                                     .dbname = dbname})
        .Connect(simple_query_protocol);
  }

  void SetUp() {
    YBMiniClusterTestBase<ExternalMiniCluster>::SetUp();
    opts_.num_tablet_servers = 3;
    opts_.num_masters = 1;
    opts_.enable_ysql = true;
    cluster_.reset(new ExternalMiniCluster(opts_));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(MiniClusterTestWithClient<ExternalMiniCluster>::CreateClient());

    std::vector<std::string> hosts;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      hosts.push_back(cluster_->tablet_server(i)->bind_host());
    }
    CreatePgSqlTable();
  }

  void CreatePgSqlTable() {
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(client_->CreateNamespace(
        kPgsqlNamespaceName, YQL_DATABASE_PGSQL, "" /* creator */, "" /* ns_id */,
        "" /* src_ns_id */, std::nullopt /* next_pg_oid */, nullptr /* txn */, false));
    std::string kNamespaceId;
    {
      auto namespaces = ASSERT_RESULT(client_->ListNamespaces());
      for (const auto& ns : namespaces) {
        if (ns.id.name() == kPgsqlNamespaceName) {
          kNamespaceId = ns.id.id();
          break;
        }
      }
    }
    auto pgsql_table_name =
        YBTableName(YQL_DATABASE_PGSQL, kNamespaceId, kPgsqlNamespaceName, kPgsqlTableName);

    client::YBSchemaBuilder schema_builder;
    schema_builder.AddColumn("key")->PrimaryKey()->Type(DataType::STRING)->NotNull();
    schema_builder.AddColumn("value")->Type(DataType::INT64)->NotNull();
    EXPECT_OK(client_->CreateNamespaceIfNotExists(
        kPgsqlNamespaceName, YQLDatabase::YQL_DATABASE_PGSQL, "" /* creator_role_name */,
        kNamespaceId));
    client::YBSchema schema;
    EXPECT_OK(schema_builder.Build(&schema));
    EXPECT_OK(table_creator->table_name(pgsql_table_name)
                  .table_id(kPgsqlTableId)
                  .schema(&schema)
                  .table_type(YBTableType::PGSQL_TABLE_TYPE)
                  .set_range_partition_columns({"key"})
                  .num_tablets(1)
                  .Create());
  }

  Result<client::internal::RemoteTabletPtr> GetRemoteTablet(
      const TabletId& tablet_id, bool use_cache, client::YBClient* client) {
    std::promise<Result<client::internal::RemoteTabletPtr>> tablet_lookup_promise;
    auto future = tablet_lookup_promise.get_future();
    client->LookupTabletById(
        tablet_id, /* table =*/nullptr, master::IncludeHidden::kTrue,
        master::IncludeDeleted::kFalse, CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(1000),
        [&tablet_lookup_promise](const Result<client::internal::RemoteTabletPtr>& result) {
          tablet_lookup_promise.set_value(result);
        },
        client::UseCache(use_cache));
    return VERIFY_RESULT(future.get());
  }


  void ChangeClusterConfig(size_t idx = 0) {
    // add TServer to blacklist
    ASSERT_OK(cluster_->AddTServerToBlacklist(cluster_->master(), cluster_->tablet_server(idx)));
    // Add a node to the cluster
    ASSERT_OK(cluster_->AddTabletServer());
    ASSERT_OK(cluster_->WaitForTabletServerCount(3, 10s * kTimeMultiplier));
  }

  client::YBPgsqlWriteOpPtr CreateNewWriteOp(
      rpc::Sidecars& sidecars, std::shared_ptr<client::YBTable> pgsql_table,
      const std::string& key) {
    auto pgsql_write_op = client::YBPgsqlWriteOp::NewInsert(
        pgsql_table, SharedThreadSafeArena(), &sidecars);
    auto* psql_write_request = pgsql_write_op->mutable_request();
    psql_write_request->add_range_column_values()->mutable_value()->dup_string_value(key);
    auto* pgsql_column = psql_write_request->add_column_values();
    pgsql_column->set_column_id(pgsql_table->schema().ColumnId(1));
    pgsql_column->mutable_expr()->mutable_value()->set_int64_value(3);
    return pgsql_write_op;
  }

  ExternalMiniClusterOptions opts_;
};

// Tests that the metacache refresh works when a follower read is issued.
// Can either go to a leader or a follower.
// TEST_F(MetacacheRefreshITest, TestMetacacheRefreshFromFollowerRead) {
//   // ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_always_return_consensus_info_for_succeeded_rpc) =
//   //     false;
//   ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_metacache_partial_refresh) =
//       true;
//   std::shared_ptr<client::YBTable> pgsql_table;
//   EXPECT_OK(client_->OpenTable(kPgsqlTableId, &pgsql_table));
//   std::shared_ptr<client::YBSession> session = client_->NewSession(10s * kTimeMultiplier);
//   rpc::Sidecars sidecars;
//   auto write_op = CreateNewWriteOp(sidecars, pgsql_table, "pgsql_key1");
//   session->Apply(write_op);
//   FlushSessionOrDie(session);

//   google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
//   ASSERT_OK(client_->GetTabletsFromTableId(kPgsqlTableId, 0, &tablets));
//   const auto& tablet = tablets.Get(0);
//   auto tablet_id = tablet.tablet_id();
//   ASSERT_FALSE(tablet_id.empty());
//   ChangeClusterConfig();

//   auto remote_tablet = ASSERT_RESULT(GetRemoteTablet(tablet.tablet_id(), true, client_.get()));
//   auto pgsql_read_op = client::YBPgsqlReadOp::NewSelect(pgsql_table, &sidecars);
//   pgsql_read_op->set_yb_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
//   session->Apply(pgsql_read_op);
//   auto* sync_point_instance = yb::SyncPoint::GetInstance();
//   Synchronizer sync;
//   bool refresh_succeeded = false;
//   sync_point_instance->SetCallBack(
//       "TabletInvoker::RefreshFinishedWithOkRPCResponse",
//       [sync_point_instance, callback = sync.AsStdStatusCallback(), &refresh_succeeded](void* arg)
//       {
//         refresh_succeeded = *reinterpret_cast<bool*>(arg);
//         sync_point_instance->DisableProcessing();
//         callback(Status::OK());
//       });
//   sync_point_instance->EnableProcessing();
//   FlushSessionOrDie(session);
//   ASSERT_OK(sync.Wait());
//   ASSERT_TRUE(refresh_succeeded);
// }

TEST_F(MetacacheRefreshITest, TestMetacacheNoRefreshFromWrite) {
  // ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_always_return_consensus_info_for_succeeded_rpc) =
  //     false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_metacache_partial_refresh) =
      true;
  std::shared_ptr<client::YBTable> pgsql_table;
  EXPECT_OK(client_->OpenTable(kPgsqlTableId, &pgsql_table));
  std::shared_ptr<client::YBSession> session = client_->NewSession(10s * kTimeMultiplier);
  rpc::Sidecars sidecars;
  auto write_op = CreateNewWriteOp(sidecars, pgsql_table, "pgsql_key1");
  session->Apply(write_op);
  FlushSessionOrDie(session);
  Synchronizer sync;
  bool refresh_succeeded = false;
  auto* sync_point_instance = yb::SyncPoint::GetInstance();
  sync_point_instance->SetCallBack(
      "TabletInvoker::RefreshFinishedWithOkRPCResponse",
      [sync_point_instance, callback = sync.AsStdStatusCallback(), &refresh_succeeded](void* arg) {
        refresh_succeeded = *reinterpret_cast<bool*>(arg);
        sync_point_instance->DisableProcessing();
        callback(Status::OK());
      });
  sync_point_instance->EnableProcessing();
  write_op = CreateNewWriteOp(sidecars, pgsql_table, "pgsql_key2");
  session->Apply(write_op);
  FlushSessionOrDie(session);
  ASSERT_OK(sync.Wait());
  ASSERT_FALSE(refresh_succeeded);
}

// The test process is the client and the cluster runs in separate processes, so
// TEST_delay_connect_ms only holds the client's own new connections in the connecting state. That
// is the situation of a client whose first RPC to a tserver finds the address black-holed.
class ConnectTimeoutMetacacheITest : public MiniClusterTestWithClient<ExternalMiniCluster> {
 public:
  static constexpr int kNumTablets = 4;

  void SetUp() override {
    YBMiniClusterTestBase<ExternalMiniCluster>::SetUp();
    opts_.num_tablet_servers = 1;
    opts_.num_masters = 1;
    opts_.extra_master_flags.push_back("--replication_factor=1");
    cluster_.reset(new ExternalMiniCluster(opts_));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(CreateClient());
    ASSERT_OK(client_->CreateNamespace(kTableName.namespace_name()));
    client::YBSchemaBuilder builder;
    builder.AddColumn("key")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    builder.AddColumn("value")->Type(DataType::INT32)->NotNull();
    ASSERT_OK(table_.Create(kTableName, kNumTablets, client_.get(), &builder));
  }

  // Caches every tablet of the table through the master alone, without contacting the tserver.
  Status WarmMetaCache() {
    std::promise<Result<std::vector<client::internal::RemoteTabletPtr>>> promise;
    client_->LookupAllTablets(
        table_.table(), CoarseMonoClock::Now() + 30s,
        [&promise](const auto& result) { promise.set_value(result); });
    auto tablets = VERIFY_RESULT(promise.get_future().get());
    SCHECK_EQ(tablets.size(), kNumTablets, IllegalState, "Unexpected number of tablets");
    return Status::OK();
  }

  // Number of cached tablets whose (only) replica is marked failed.
  Result<int> NumTabletsWithFailedReplica() {
    std::stringstream stream;
    JsonWriter writer(&stream, JsonWriter::COMPACT);
    client_->AddMetaCacheInfo(&writer);
    JsonDocument doc;
    auto root = VERIFY_RESULT(doc.Parse(stream.str()));
    int failed = 0;
    int total = 0;
    for (const auto& tablet : VERIFY_RESULT(root["tablets"].GetArray())) {
      ++total;
      for (const auto& replica : VERIFY_RESULT(tablet["replicas"].GetArray())) {
        if (VERIFY_RESULT(replica["failure_status"].GetString()) == "FAILED") {
          ++failed;
        }
      }
    }
    SCHECK_EQ(total, kNumTablets, IllegalState, "Unexpected number of cached tablets");
    return failed;
  }

  // A write with a 1s deadline while new connections take 10s to connect: it times out while the
  // connection is still connecting.
  Status WriteWithPendingConnect() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_connect_ms) = 10000;
    auto session = client_->NewSession(1s);
    auto op = table_.NewWriteOp(session->arena(), QLWriteRequestPB::QL_STMT_INSERT);
    QLAddInt32HashValue(op->mutable_request(), 1);
    table_.AddInt32ColumnValue(op->mutable_request(), "value", 1);
    session->Apply(op);
    auto status = session->TEST_Flush();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_connect_ms) = 0;
    SCHECK(!status.ok(), IllegalState, "Write unexpectedly succeeded");
    SCHECK(
        status.IsIOError() || status.IsTimedOut(), IllegalState, "Unexpected status: $0", status);
    return Status::OK();
  }

 protected:
  const client::YBTableName kTableName{YQL_DATABASE_CQL, "my_keyspace", "connect_timeout"};
  ExternalMiniClusterOptions opts_;
  client::TableHandle table_;
};

// A call that times out while its connection is still connecting marks the tserver failed on every
// cached tablet, like a connect timeout would, instead of only the tablet that was written to.
TEST_F(ConnectTimeoutMetacacheITest, TimeoutWhileConnectingMarksTServerFailed) {
  auto cleanup = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_connect_ms) = 0;
  });
  ASSERT_OK(WarmMetaCache());
  ASSERT_EQ(ASSERT_RESULT(NumTabletsWithFailedReplica()), 0);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_connecting_call_timeout_as_connect_failure_ms) = 200;
  ASSERT_OK(WriteWithPendingConnect());
  ASSERT_EQ(ASSERT_RESULT(NumTabletsWithFailedReplica()), kNumTablets);
}

// With the tagging disabled only the written tablet's replica is marked, as before.
TEST_F(ConnectTimeoutMetacacheITest, TimeoutWhileConnectingDisabled) {
  auto cleanup = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_connect_ms) = 0;
  });
  ASSERT_OK(WarmMetaCache());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_connecting_call_timeout_as_connect_failure_ms) = 0;
  ASSERT_OK(WriteWithPendingConnect());
  ASSERT_EQ(ASSERT_RESULT(NumTabletsWithFailedReplica()), 1);
}

}  // namespace yb
