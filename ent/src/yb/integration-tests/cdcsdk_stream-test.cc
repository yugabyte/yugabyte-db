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
#include <string>
#include <utility>
#include <chrono>
#include <boost/assign.hpp>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_value.h"

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

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/cdcsdk_test_base.h"

#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/master.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/master-test-util.h"
#include "yb/master/sys_catalog_initialization.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/cdc_consumer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
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

class CDCSDKStreamTest : public CDCSDKTestBase {
 public:
  // Every test needs to initialize this cdc_proxy_.
  std::unique_ptr<CDCServiceProxy> cdc_proxy_;

  struct ExpectedRecord {
    std::string key;
    std::string value;
  };

  // Set up a cluster with the specified parameters.
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
    FLAGS_replication_factor = replication_factor;

    MiniClusterOptions opts;
    opts.num_masters = num_masters;
    opts.num_tablet_servers = replication_factor;
    opts.cluster_id = "cdcsdk_cluster";

    test_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(opts);

    RETURN_NOT_OK(test_cluster()->StartSync());
    RETURN_NOT_OK(test_cluster()->WaitForTabletServerCount(replication_factor));
    RETURN_NOT_OK(WaitForInitDb(test_cluster()));
    test_cluster_.client_ = VERIFY_RESULT(test_cluster()->CreateClient());
    RETURN_NOT_OK(InitPostgres(&test_cluster_));
    RETURN_NOT_OK(CreateDatabase(&test_cluster_, kNamespaceName, colocated));

    cdc_proxy_ = GetCdcProxy();

    LOG(INFO) << "Cluster created successfully for CDCSDK";
    return Status::OK();
  }

  Status InitPostgres(Cluster* cluster) {
    auto pg_ts = RandomElement(cluster->mini_cluster_->mini_tablet_servers());
    auto port = cluster->mini_cluster_->AllocateFreePort();
    pgwrapper::PgProcessConf pg_process_conf =
        VERIFY_RESULT(pgwrapper::PgProcessConf::CreateValidateAndRunInitDb(
            AsString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
            pg_ts->options()->fs_opts.data_paths.front() + "/pg_data",
            pg_ts->server()->GetSharedMemoryFd()));
    pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
    pg_process_conf.force_disable_log_file = true;
    FLAGS_pgsql_proxy_webserver_port = cluster->mini_cluster_->AllocateFreePort();

    LOG(INFO) << "Starting PostgreSQL server listening on " << pg_process_conf.listen_addresses
              << ":" << pg_process_conf.pg_port << ", data: " << pg_process_conf.data_dir
              << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;
    cluster->pg_supervisor_ = std::make_unique<pgwrapper::PgSupervisor>(pg_process_conf);
    RETURN_NOT_OK(cluster->pg_supervisor_->Start());

    cluster->pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);
    return Status::OK();
  }

  // Create a test database to work on.
  Status CreateDatabase(
      Cluster* cluster,
      const std::string& namespace_name = kNamespaceName,
      bool colocated = false) {
    auto conn = VERIFY_RESULT(cluster->Connect());
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE DATABASE $0$1", namespace_name, colocated ? " colocated = true" : ""));
    return Status::OK();
  }

  Result<std::string> GetNamespaceId(const std::string& namespace_name) {
    master::GetNamespaceInfoResponsePB namespace_info_resp;

    RETURN_NOT_OK(test_client()->GetNamespaceInfo(
        std::string(), kNamespaceName, YQL_DATABASE_PGSQL, &namespace_info_resp));

    // Return namespace_id.
    return namespace_info_resp.namespace_().id();
  }

  Result<YBTableName> CreateTable(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& table_name,
      const uint32_t num_tablets,
      const bool add_primary_key = true,
      bool colocated = false,
      const int table_oid = 0) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
    std::string table_oid_string = "";
    if (table_oid > 0) {
      // Need to turn on session flag to allow for CREATE WITH table_oid.
      RETURN_NOT_OK(conn.Execute("set yb_enable_create_with_table_oid=true"));
      table_oid_string = Format("table_oid = $0,", table_oid);
    }
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0($1 int $2, $3 int) WITH ($4colocated = $5) "
        "SPLIT INTO $6 TABLETS",
        table_name, kKeyColumnName, (add_primary_key) ? "PRIMARY KEY" : "", kValueColumnName,
        table_oid_string, colocated, num_tablets));
    return GetTable(cluster, namespace_name, table_name);
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
    return STATUS_FORMAT(
        IllegalState, "Unable to find table id for $0 in $1", table_name, namespace_name);
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
    return STATUS_FORMAT(
        IllegalState, "Unable to find table $0 in namespace $1", table_name, namespace_name);
  }

  // Initialize a CreateCDCStreamRequest to be used while creating a DB stream ID.
  void InitCreateStreamRequest(
      CreateCDCStreamRequestPB* create_req,
      const CDCCheckpointType& checkpoint_type = CDCCheckpointType::EXPLICIT,
      const std::string& namespace_name = kNamespaceName) {
    create_req->set_namespace_name(namespace_name);
    create_req->set_checkpoint_type(checkpoint_type);
    create_req->set_record_type(CDCRecordType::CHANGE);
    create_req->set_record_format(CDCRecordFormat::PROTO);
    create_req->set_source_type(CDCSDK);
  }

  Result<std::string> CreateDBStream(
      CDCCheckpointType checkpoint_type = CDCCheckpointType::EXPLICIT) {
    CreateCDCStreamRequestPB req;
    CreateCDCStreamResponsePB resp;

    RpcController rpc;
    rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    InitCreateStreamRequest(&req, checkpoint_type);

    RETURN_NOT_OK(cdc_proxy_->CreateCDCStream(req, &resp, &rpc));

    return resp.db_stream_id();
  }

  CHECKED_STATUS DeleteCDCStream(const std::string& db_stream_id) {
    RpcController delete_rpc;
    delete_rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    DeleteCDCStreamRequestPB delete_req;
    DeleteCDCStreamResponsePB delete_resp;
    delete_req.add_stream_id(db_stream_id);

    // The following line assumes that cdc_proxy_ has been initialized in the test already
    return cdc_proxy_->DeleteCDCStream(delete_req, &delete_resp, &delete_rpc);
  }

  Result<std::vector<std::string>> CreateDBStreams(const int num_streams) {
    std::vector<std::string> created_streams;
    // We will create some DB Streams to be listed out later.
    for (int i = 0; i < num_streams; i++) {
      std::string db_stream_id = VERIFY_RESULT(CreateDBStream());
      SCHECK(!db_stream_id.empty(), IllegalState, "The created db_stream_id is empty!");
      created_streams.push_back(db_stream_id);
    }

    // Sorting the stream IDs in order to simplify assertion.
    std::sort(created_streams.begin(), created_streams.end());
    return created_streams;
  }

  Result<google::protobuf::RepeatedPtrField<yb::master::CDCStreamInfoPB>> ListDBStreams() {
    // Listing the streams now.
    master::ListCDCStreamsRequestPB list_req;
    master::ListCDCStreamsResponsePB list_resp;

    list_req.set_id_type(master::IdTypePB::NAMESPACE_ID);
    list_req.set_namespace_id(VERIFY_RESULT(GetNamespaceId(kNamespaceName)));

    RpcController list_rpc;
    list_rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    master::MasterReplicationProxy master_proxy_(
        &test_client()->proxy_cache(),
        VERIFY_RESULT(test_cluster_.mini_cluster_->GetLeaderMasterBoundRpcAddr()));

    RETURN_NOT_OK(master_proxy_.ListCDCStreams(list_req, &list_resp, &list_rpc));

    if (list_resp.has_error()) {
      return StatusFromPB(list_resp.error().status());
    }

    return list_resp.streams();
  }

  Result<master::GetCDCDBStreamInfoResponsePB> GetDBStreamInfo(std::string db_stream_id) {
    master::GetCDCDBStreamInfoRequestPB get_req;
    master::GetCDCDBStreamInfoResponsePB get_resp;
    get_req.set_db_stream_id(db_stream_id);

    RpcController get_rpc;
    get_rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    master::MasterReplicationProxy master_proxy_(
        &test_client()->proxy_cache(),
        VERIFY_RESULT(test_cluster_.mini_cluster_->GetLeaderMasterBoundRpcAddr()));

    RETURN_NOT_OK(master_proxy_.GetCDCDBStreamInfo(get_req, &get_resp, &get_rpc));

    return get_resp;
  }

  void TestListDBStreams(bool with_table) {
    // Create one table.
    uint32_t num_tablets = 1;
    std::string table_id;

    if (with_table) {
      auto table =
          ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));

      // Get the table_id of the created table.
      table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    }
    // We will create some DB Streams to be listed out later.
    auto created_streams = ASSERT_RESULT(CreateDBStreams(3));

    const size_t total_created_streams = created_streams.size();

    google::protobuf::RepeatedPtrField<yb::master::CDCStreamInfoPB> list_streams =
        ASSERT_RESULT(ListDBStreams());

    const uint32_t num_streams = list_streams.size();
    ASSERT_EQ(total_created_streams, num_streams);

    std::vector<std::string> resp_stream_ids;
    for (uint32_t i = 0; i < num_streams; ++i) {
      if (with_table) {
        // Since there is one table, all the streams would contain one table_id in their response.
        ASSERT_EQ(1, list_streams.Get(i).table_id_size());
        // That particular table_id would be equal to the created table id.
        ASSERT_EQ(table_id, list_streams.Get(i).table_id(0));
      } else {
        // Since there are no tables in DB, there would be no table_ids in the response.
        ASSERT_EQ(0, list_streams.Get(i).table_id_size());
      }

      resp_stream_ids.push_back(list_streams.Get(i).stream_id());
    }
    // Sorting to simplify assertion.
    std::sort(resp_stream_ids.begin(), resp_stream_ids.end());

    // Verify if the stream ids returned with the response are the same as the ones created.
    for (uint32_t i = 0; i < resp_stream_ids.size(); ++i) {
      ASSERT_EQ(created_streams[i], resp_stream_ids[i]);
    }
  }

  void TestDBStreamInfo(
      const vector<std::string>& table_with_pk, const vector<std::string>& table_without_pk) {
    uint32_t num_tablets = 1;
    std::vector<std::string>::size_type num_of_tables_with_pk = table_with_pk.size();

    for (const auto& table_name : table_with_pk) {
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, table_name, num_tablets));
    }

    for (const auto& table_name : table_without_pk) {
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, table_name, num_tablets, false));
    }

    std::vector<std::string> created_table_ids_with_pk;

    for (const auto& table_name : table_with_pk) {
      created_table_ids_with_pk.push_back(
          ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, table_name)));
    }

    std::vector<std::string> created_table_ids_without_pk;

    // Sorting would make assertion easier later on.
    std::sort(created_table_ids_with_pk.begin(), created_table_ids_with_pk.end());
    std::string db_stream_id = ASSERT_RESULT(CreateDBStream());

    auto get_resp = ASSERT_RESULT(GetDBStreamInfo(db_stream_id));
    ASSERT_FALSE(get_resp.has_error());

    // Get the namespace ID.
    std::string namespace_id = ASSERT_RESULT(GetNamespaceId(kNamespaceName));

    // We have only 1 table, so the response will (should) have 1 table info only.
    uint32_t table_info_size = get_resp.table_info_size();
    ASSERT_EQ(num_of_tables_with_pk, table_info_size);

    // Check whether the namespace ID in the response is correct.
    ASSERT_EQ(namespace_id, get_resp.namespace_id());

    // Store the table IDs received in the response.
    std::vector<std::string> table_ids_in_resp;
    for (uint32_t i = 0; i < table_info_size; ++i) {
      // Also assert that all the table_info(s) contain the same db_stream_id.
      ASSERT_EQ(db_stream_id, get_resp.table_info(i).stream_id());

      table_ids_in_resp.push_back(get_resp.table_info(i).table_id());
    }
    std::sort(table_ids_in_resp.begin(), table_ids_in_resp.end());

    // Verifying that the table IDs received in the response are for the tables which were
    // created earlier.
    for (uint32_t i = 0; i < table_ids_in_resp.size(); ++i) {
      ASSERT_EQ(created_table_ids_with_pk[i], table_ids_in_resp[i]);
    }
  }
};

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(CreateCDCSDKStreamImplicit)) {
  // Create a cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  std::string db_stream_id = ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT));
  ASSERT_NE(0, db_stream_id.length());
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(CreateCDCSDKStreamExplicit)) {
  // Create a cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  // The function CreateDBStream() creates a stream with EXPLICIT checkpointing by default.
  std::string db_stream_id = ASSERT_RESULT(CreateDBStream());
  ASSERT_NE(0, db_stream_id.length());
}

// This test is to verify the fix for the following:
// [#10945] Error while creating a DB Stream if any table in the database is without a primary key.
TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(TestStreamCreation)) {
  // Create a cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  uint32_t num_tablets = 1;

  // Create a table with primary key.
  auto table1 =
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "table_with_pk", num_tablets));
  // Create another table without primary key.
  auto table2 = ASSERT_RESULT(
      CreateTable(&test_cluster_, kNamespaceName, "table_without_pk", num_tablets, false));

  // We have a table with primary key and one without primary key so while creating
  // the DB Stream ID, the latter one will be ignored and will not be a part of streaming with CDC.
  // Now we just need to ensure that everything is working fine.
  std::string db_stream_id = ASSERT_RESULT(CreateDBStream());
  ASSERT_NE(0, db_stream_id.length());
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(TestOnSingleRF)) {
  // Create a cluster.
  ASSERT_OK(SetUpWithParams(1, 1, false));

  std::string db_stream_id = ASSERT_RESULT(CreateDBStream());
  ASSERT_NE(0, db_stream_id.length());
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DeleteDBStream)) {
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  // Create a DB Stream ID to be deleted later on.
  std::string db_stream_id = ASSERT_RESULT(CreateDBStream());
  ASSERT_NE(0, db_stream_id.length());

  // Deleting the created DB Stream ID.
  ASSERT_OK(DeleteCDCStream(db_stream_id));
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(CreateMultipleStreams)) {
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  auto stream_ids = ASSERT_RESULT(CreateDBStreams(3));
  ASSERT_EQ(3, stream_ids.size());
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DeleteMultipleStreams)) {
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  auto stream_ids = ASSERT_RESULT(CreateDBStreams(3));
  ASSERT_EQ(3, stream_ids.size());

  for (const auto& stream_id : stream_ids) {
    // Since we have created 3 streams, we will be deleting 3 streams too.
    ASSERT_OK(DeleteCDCStream(stream_id));
  }
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(ListDBStreams)) {
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  TestListDBStreams(true);
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(ListDBStreams_NoTablesInDB)) {
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  TestListDBStreams(false);
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DBStreamInfoTest)) {
  // Set up a cluster with RF 3.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  TestDBStreamInfo(std::vector<std::string>{kTableName}, {});
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DBStreamInfoTest_MultipleTablesInDB)) {
  // Set up a cluster with RF 3.
  ASSERT_OK(SetUpWithParams(3, 1, false));
  std::vector<std::string> table_names_with_pk = {
      "pk_table1", "pk_table2", "pk_table3", "pk_table4"};
  std::vector<std::string> table_names_without_pk = {"table_without_pk"};

  TestDBStreamInfo(table_names_with_pk, table_names_without_pk);
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DBStreamInfoTest_NoTablesInDB)) {
  // Set up a cluster with RF 3.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  TestDBStreamInfo({}, {});
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DBStreamInfoTest_AllTablesWithoutPrimaryKey)) {
  // Set up a cluster with RF 3.
  ASSERT_OK(SetUpWithParams(3, 1, false));
  std::vector<std::string> table_names_without_pk = {"table_without_pk_1", "table_without_pk_2"};

  TestDBStreamInfo({}, table_names_without_pk);
}
}  // namespace enterprise
}  // namespace cdc
}  // namespace yb
