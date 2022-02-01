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

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <chrono>
#include <boost/assign.hpp>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/cdcsdk_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/cdc_consumer_registry_service.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master-test-util.h"
#include "yb/master/sys_catalog_initialization.h"

#include "yb/rpc/rpc_controller.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/tserver/cdc_consumer.h"
#include "yb/util/atomic.h"
#include "yb/util/faststring.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"
#include "yb/util/test_macros.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_int32(replication_factor);
DECLARE_int32(cdc_max_apply_batch_num_records);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(pgsql_proxy_webserver_port);
DECLARE_bool(enable_ysql);
DECLARE_bool(hide_pg_catalog_table_creation_logs);
DECLARE_bool(master_auto_run_initdb);
DECLARE_int32(pggate_rpc_timeout_secs);

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBColumnSchema;
using client::YBError;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTable;
using client::YBTableAlterer;
using client::YBTableCreator;
using client::YBTableName;
using client::YBTableType;
using master::GetNamespaceInfoResponsePB;
using master::MiniMaster;
using tserver::MiniTabletServer;
using tserver::enterprise::CDCConsumer;

using pgwrapper::GetInt32;
using pgwrapper::PGConn;
using pgwrapper::PGResultPtr;
using pgwrapper::PgSupervisor;
using pgwrapper::ToString;

using rpc::RpcController;

namespace cdc {
namespace enterprise {
constexpr static const char* const kTableName = "test_table";
constexpr static const char* const kKeyColumnName = "key";
constexpr static const char* const kValueColumnName = "value";

// todo (vaibhav): remove extra log comments
class CDCSDKYsqlTest : public CDCSDKTestBase {
 public:
  struct ExpectedRecord {
    std::string key;
    std::string value;
  };
  Status SetUpWithParams(
      uint32_t replication_factor, uint32_t num_masters = 1, bool colocated = false) {
    master::SetDefaultInitialSysCatalogSnapshotFlags();
    CDCSDKTestBase::SetUp();
    FLAGS_enable_ysql = true;
    FLAGS_master_auto_run_initdb = true;
    FLAGS_hide_pg_catalog_table_creation_logs = true;
    FLAGS_pggate_rpc_timeout_secs = 120;
    FLAGS_cdc_max_apply_batch_num_records = 1;
    FLAGS_cdc_enable_replicate_intents = true;

    MiniClusterOptions opts;
    opts.num_tablet_servers = replication_factor;
    opts.num_masters = num_masters;
    FLAGS_replication_factor = replication_factor;
    opts.cluster_id = "cdcsdk_cluster";
    test_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(opts);
    RETURN_NOT_OK(test_cluster()->StartSync());
    RETURN_NOT_OK(test_cluster()->WaitForTabletServerCount(replication_factor));
    RETURN_NOT_OK(WaitForInitDb(test_cluster()));
    test_cluster_.client_ = VERIFY_RESULT(test_cluster()->CreateClient());
    RETURN_NOT_OK(InitPostgres(&test_cluster_));
    RETURN_NOT_OK(CreateDatabase(&test_cluster_, kNamespaceName, colocated));

    LOG(INFO) << "Cluster created successfully for CDCSDK";
    return Status::OK();
  }

  Status InitPostgres(Cluster* cluster) {
    auto pg_ts = RandomElement(cluster->mini_cluster_->mini_tablet_servers());
    auto port = cluster->mini_cluster_->AllocateFreePort();
    yb::pgwrapper::PgProcessConf pg_process_conf =
        VERIFY_RESULT(yb::pgwrapper::PgProcessConf::CreateValidateAndRunInitDb(
            yb::ToString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
            pg_ts->options()->fs_opts.data_paths.front() + "/pg_data",
            pg_ts->server()->GetSharedMemoryFd()));
    pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
    pg_process_conf.force_disable_log_file = true;
    FLAGS_pgsql_proxy_webserver_port = cluster->mini_cluster_->AllocateFreePort();

    LOG(INFO) << "Starting PostgreSQL server listening on " << pg_process_conf.listen_addresses
              << ":" << pg_process_conf.pg_port << ", data: " << pg_process_conf.data_dir
              << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;
    cluster->pg_supervisor_ = std::make_unique<yb::pgwrapper::PgSupervisor>(pg_process_conf);
    RETURN_NOT_OK(cluster->pg_supervisor_->Start());

    cluster->pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);
    return Status::OK();
  }

  Status CreateDatabase(
      Cluster* cluster,
      const std::string& namespace_name = kNamespaceName,
      bool colocated = false) {
    auto conn = EXPECT_RESULT(cluster->Connect());
    EXPECT_OK(conn.ExecuteFormat(
        "CREATE DATABASE $0$1", namespace_name, colocated ? " colocated = true" : ""));
    return Status::OK();
  }

  Result<string> GetUniverseId(Cluster* cluster) {
    yb::master::GetMasterClusterConfigRequestPB req;
    yb::master::GetMasterClusterConfigResponsePB resp;

    master::MasterClusterProxy master_proxy(
        &cluster->client_->proxy_cache(),
        VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMasterBoundRpcAddr()));

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(master_proxy.GetMasterClusterConfig(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Error getting cluster config");
    }
    return resp.cluster_config().cluster_uuid();
  }

  Result<YBTableName> CreateTable(
      Cluster* cluster,
      const std::string& namespace_name,
      uint32_t num_tablets,
      bool colocated = false,
      const int table_oid = 0) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(namespace_name));
    std::string table_oid_string = "";
    if (table_oid > 0) {
      // Need to turn on session flag to allow for CREATE WITH table_oid.
      EXPECT_OK(conn.Execute("set yb_enable_create_with_table_oid=true"));
      table_oid_string = Format("table_oid = $0,", table_oid);
    }
    EXPECT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0($1 int PRIMARY KEY, $2 int) WITH ($3colocated = $4) "
        "SPLIT INTO $5 TABLETS",
        kTableName, kKeyColumnName, kValueColumnName, table_oid_string, colocated, num_tablets));
    return GetTable(cluster, namespace_name, kTableName);
  }

  Result<std::string> GetTableId(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& table_name,
      bool verify_table_name = true,
      bool exclude_system_tables = true) {
    master::ListTablesRequestPB req;
    master::ListTablesResponsePB resp;

    req.set_name_filter(table_name);
    req.mutable_namespace_()->set_name(namespace_name);
    req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
    if (!exclude_system_tables) {
      req.set_exclude_system_tables(true);
      req.add_relation_type_filter(master::USER_TABLE_RELATION);
    }

    master::MasterDdlProxy master_proxy(
        &cluster->client_->proxy_cache(),
        VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMasterBoundRpcAddr()));

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(master_proxy.ListTables(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Failed listing tables");
    }

    // Now need to find the table and return it.
    for (const auto& table : resp.tables()) {
      // If !verify_table_name, just return the first table.
      if (!verify_table_name ||
          (table.name() == table_name && table.namespace_().name() == namespace_name)) {
        return table.id();
      }
    }
    return STATUS(
        IllegalState,
        strings::Substitute("Unable to find table id for $0 in $1", table_name, namespace_name));
  }

  Result<YBTableName> GetTable(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& table_name,
      bool verify_table_name = true,
      bool exclude_system_tables = true) {
    master::ListTablesRequestPB req;
    master::ListTablesResponsePB resp;

    req.set_name_filter(table_name);
    req.mutable_namespace_()->set_name(namespace_name);
    req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
    if (!exclude_system_tables) {
      req.set_exclude_system_tables(true);
      req.add_relation_type_filter(master::USER_TABLE_RELATION);
    }

    master::MasterDdlProxy master_proxy(
        &cluster->client_->proxy_cache(),
        VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMasterBoundRpcAddr()));

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(master_proxy.ListTables(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Failed listing tables");
    }

    // Now need to find the table and return it.
    for (const auto& table : resp.tables()) {
      // If !verify_table_name, just return the first table.
      if (!verify_table_name ||
          (table.name() == table_name && table.namespace_().name() == namespace_name)) {
        YBTableName yb_table;
        yb_table.set_table_id(table.id());
        yb_table.set_namespace_id(table.namespace_().id());
        return yb_table;
      }
    }
    return STATUS(
        IllegalState,
        strings::Substitute("Unable to find table $0 in namespace $1", table_name, namespace_name));
  }

  // the range is exclusive of end i.e. [start, end)
  void WriteRows(uint32_t start, uint32_t end, Cluster* cluster) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s)";

    for (uint32_t i = start; i < end; ++i) {
      EXPECT_OK(conn.ExecuteFormat(
          "INSERT INTO $0($1, $2) VALUES ($3, $4)", kTableName, kKeyColumnName, kValueColumnName, i,
          i + 1));
    }
  }

  void WriteRowsInTransaction(uint32_t start, uint32_t end, Cluster* cluster) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    EXPECT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; ++i) {
      EXPECT_OK(conn.ExecuteFormat(
          "INSERT INTO $0($1, $2) VALUES ($3, $4)", kTableName, kKeyColumnName, kValueColumnName, i,
          i + 1));
    }
    EXPECT_OK(conn.Execute("COMMIT"));
  }

  void PrepareChangeRequest(
      GetChangesRequestPB* change_req, const CDCStreamId stream_id,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets) {
    change_req->set_stream_id(stream_id);
    change_req->set_tablet_id(tablets.Get(0).tablet_id());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_index(0);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_term(0);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_key("");
    change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(0);
  }

  void PrepareSetCheckpointRequest(
      SetCDCCheckpointRequestPB* set_checkpoint_req,
      const CDCStreamId stream_id,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB>
          tablets) {
    set_checkpoint_req->set_stream_id(stream_id);
    set_checkpoint_req->set_tablet_id(tablets.Get(0).tablet_id());
    set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_term(0);
    set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_index(0);
  }

  void AssertKeyValue(
      const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& key,
      std::string expected_key,
      const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& changes,
      std::string expected_value) {
    ASSERT_EQ(key.size(), 1);
    ASSERT_EQ(key[0].key(), "key");
    ASSERT_EQ(key[0].value().string_value(), expected_key);

    ASSERT_EQ(changes.size(), 1);
    ASSERT_EQ(changes[0].key(), "value");
    ASSERT_EQ(changes[0].value().string_value(), expected_value);
  }
};

TEST_F(CDCSDKYsqlTest, TestTableCreation) {
  YB_SKIP_TEST_IN_TSAN();

  // setting up a cluster with 3 RF
  ASSERT_OK(SetUpWithParams(3, 1, false /* colocated */));

  ASSERT_EQ(1, 1);
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, num_tablets));

  ASSERT_FALSE(table.is_cql_namespace());
}

TEST_F(CDCSDKYsqlTest, TestLoadInsertionOnly) {
  YB_SKIP_TEST_IN_TSAN();

  // setup an RF3 cluster
  ASSERT_OK(SetUpWithParams(3, 1, false));

  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, num_tablets));

  WriteRows(0, 10, &test_cluster_);
}

TEST_F(CDCSDKYsqlTest, CDCSDKOnSingleRF) {
  YB_SKIP_TEST_IN_TSAN();

  ASSERT_OK(SetUpWithParams(1, 1, false));

  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, num_tablets));

  std::unique_ptr<CDCServiceProxy> cdc_proxy_ = GetCdcProxy();

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets,
      /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  std::string table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  CDCStreamId stream_id;
  RpcController set_checkpoint_rpc;
  CreateCDCStream(cdc_proxy_, table_id, &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  PrepareSetCheckpointRequest(&set_checkpoint_req, stream_id, tablets);
  ASSERT_OK(
      cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &set_checkpoint_rpc));

  WriteRows(0 /* start */, 1 /* end */, &test_cluster_);
  std::string expected_record[] = {"0" /* key */, "1" /* value */};

  sleep(5);
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;
  PrepareChangeRequest(&change_req, stream_id, tablets);

  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    int record_size = change_resp.cdc_sdk_records_size();
    uint32_t ins_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      if (change_resp.cdc_sdk_records(i).operation() == CDCSDKRecordPB::INSERT) {
        const CDCSDKRecordPB record = change_resp.cdc_sdk_records(i);
        AssertKeyValue(record.key(), expected_record[0], record.changes(), expected_record[1]);
        ++ins_count;
      }
    }
    LOG(INFO) << "Got " << ins_count << " insert records";
    ASSERT_EQ(1, ins_count);
  }
}

TEST_F(CDCSDKYsqlTest, CDCSDKOnThreeRF) {
  YB_SKIP_TEST_IN_TSAN();

  ASSERT_OK(SetUpWithParams(3, 1, false));

  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, num_tablets));

  std::unique_ptr<CDCServiceProxy> cdc_proxy_ = GetCdcProxy();

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets,
      /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  std::string table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  CDCStreamId stream_id;
  RpcController set_checkpoint_rpc;
  CreateCDCStream(cdc_proxy_, table_id, &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  PrepareSetCheckpointRequest(&set_checkpoint_req, stream_id, tablets);
  ASSERT_OK(
      cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &set_checkpoint_rpc));

  WriteRows(0 /* start */, 1 /* end */, &test_cluster_);
  ExpectedRecord expected_record = {"0" /* key */, "1" /* value */};

  sleep(5);
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;
  PrepareChangeRequest(&change_req, stream_id, tablets);

  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    int record_size = change_resp.cdc_sdk_records_size();
    uint32_t ins_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      if (change_resp.cdc_sdk_records(i).operation() == CDCSDKRecordPB::INSERT) {
        const CDCSDKRecordPB record = change_resp.cdc_sdk_records(i);
        AssertKeyValue(record.key(), expected_record.key, record.changes(), expected_record.value);
        ++ins_count;
      }
    }
    LOG(INFO) << "Got " << ins_count << " insert records";
    ASSERT_EQ(1, ins_count);
  }
}

TEST_F(CDCSDKYsqlTest, MultiRowInsertion) {
  YB_SKIP_TEST_IN_TSAN();

  ASSERT_OK(SetUpWithParams(3, 1, false));

  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, num_tablets));

  std::unique_ptr<CDCServiceProxy> cdc_proxy_ = GetCdcProxy();

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets,
      /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  std::string table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  CDCStreamId stream_id;
  RpcController set_checkpoint_rpc;
  CreateCDCStream(cdc_proxy_, table_id, &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  PrepareSetCheckpointRequest(&set_checkpoint_req, stream_id, tablets);
  ASSERT_OK(
      cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &set_checkpoint_rpc));

  WriteRows(0 /* start */, 10 /* end */, &test_cluster_);

  // Records will follow this structure: {key, value}
  ExpectedRecord expected_records[] = {{"0", "1"}, {"1", "2"}, {"2", "3"}, {"3", "4"}, {"4", "5"},
                                       {"5", "6"}, {"6", "7"}, {"7", "8"}, {"8", "9"}, {"9", "10"}};

  sleep(5);
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;
  PrepareChangeRequest(&change_req, stream_id, tablets);

  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    int record_size = change_resp.cdc_sdk_records_size();
    uint32_t ins_count = 0;
    uint32_t idx = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      if (change_resp.cdc_sdk_records(i).operation() == CDCSDKRecordPB::INSERT) {
        const CDCSDKRecordPB record = change_resp.cdc_sdk_records(i);
        AssertKeyValue(
            record.key(), expected_records[idx].key, record.changes(), expected_records[idx].value);
        ++idx;
        ++ins_count;
      }
    }
    LOG(INFO) << "Got " << ins_count << " insert records";
    ASSERT_EQ(10, ins_count);
  }
}

}  // namespace enterprise
}  // namespace cdc
}  // namespace yb
