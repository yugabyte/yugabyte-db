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

#include "yb/integration-tests/cdcsdk_test_base.h"

#include <string>
#include <boost/assign.hpp>
#include <gtest/gtest.h>

#include "yb/cdc/cdc_service.proxy.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common.pb.h"

#include "yb/gutil/strings/util.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/xcluster_consumer_registry_service.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog_initialization.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/status_format.h"
#include "yb/util/test_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

using std::string;

DECLARE_bool(ysql_enable_pack_full_row_update);
DECLARE_bool(ysql_ddl_transaction_wait_for_ddl_verification);

namespace yb {
using client::YBClient;
using client::YBTableName;

namespace cdc {

std::string GenerateRandomReplicationSlotName() {
  // Generate a unique name for the replication slot as a UUID. Replication slot names cannot
  // contain dash. Hence, we remove them from here.
  auto uuid_without_dash = StringReplace(Uuid::Generate().ToString(), "-", "", true);
  return Format("test_replication_slot_$0", uuid_without_dash);
}

void CDCSDKTestBase::TearDown() {
  YBTest::TearDown();

  LOG(INFO) << "Destroying cluster for CDCSDK";

  if (test_cluster()) {
    if (test_cluster_.pg_supervisor_) {
      test_cluster_.pg_supervisor_->Stop();
    }
    test_cluster_.mini_cluster_->Shutdown();
    test_cluster_.mini_cluster_.reset();
  }
  test_cluster_.client_.reset();
}

std::unique_ptr<CDCServiceProxy> CDCSDKTestBase::GetCdcProxy() {
  YBClient* client_ = test_client();
  const auto mini_server = test_cluster()->mini_tablet_servers().front();
  std::unique_ptr<CDCServiceProxy> proxy = std::make_unique<CDCServiceProxy>(
      &client_->proxy_cache(), HostPort::FromBoundEndpoint(mini_server->bound_rpc_addr()));
  return proxy;
}

// Create a test database to work on.
Status CDCSDKTestBase::CreateDatabase(
    PostgresMiniCluster* cluster, const std::string& namespace_name, bool colocated) {
  auto conn = VERIFY_RESULT(cluster->Connect());
  RETURN_NOT_OK(conn.ExecuteFormat(
      "CREATE DATABASE $0$1", namespace_name, colocated ? " with colocation = true" : ""));
  return Status::OK();
}

Status CDCSDKTestBase::InitPostgres(PostgresMiniCluster* cluster) {
  auto pg_ts = RandomElement(cluster->mini_cluster_->mini_tablet_servers());
  auto port = cluster->mini_cluster_->AllocateFreePort();
  pgwrapper::PgProcessConf pg_process_conf =
      VERIFY_RESULT(pgwrapper::PgProcessConf::CreateValidateAndRunInitDb(
          AsString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
          pg_ts->options()->fs_opts.data_paths.front() + "/pg_data",
          pg_ts->server()->GetSharedMemoryFd()));
  pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
  pg_process_conf.force_disable_log_file = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pgsql_proxy_webserver_port) =
      cluster->mini_cluster_->AllocateFreePort();

  LOG(INFO) << "Starting PostgreSQL server listening on " << pg_process_conf.listen_addresses
            << ":" << pg_process_conf.pg_port << ", data: " << pg_process_conf.data_dir
            << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;
  cluster->pg_supervisor_ = std::make_unique<pgwrapper::PgSupervisor>(
      pg_process_conf, nullptr /* tserver */);
  RETURN_NOT_OK(cluster->pg_supervisor_->Start());

  cluster->pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);
  return Status::OK();
}

// Set up a cluster with the specified parameters.
Status CDCSDKTestBase::SetUpWithParams(
    uint32_t replication_factor, uint32_t num_masters, bool colocated,
    bool cdc_populate_safepoint_record) {
  master::SetDefaultInitialSysCatalogSnapshotFlags();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_auto_run_initdb) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_hide_pg_catalog_table_creation_logs) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pggate_rpc_timeout_secs) = 120;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = replication_factor;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_pack_full_row_update) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_populate_safepoint_record) = cdc_populate_safepoint_record;
  // Set max_replication_slots to a large value so that we don't run out of them during tests and
  // don't have to do cleanups after every test case.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_replication_slots) = 500;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_allowed_preview_flags_csv) = "ysql_yb_ddl_rollback_enabled";
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_rollback_enabled) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_ddl_transaction_wait_for_ddl_verification) = true;

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

Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>>
    CDCSDKTestBase::SetUpWithOneTablet(
        uint32_t replication_factor, uint32_t num_masters, bool colocated) {

  RETURN_NOT_OK(SetUpWithParams(replication_factor, num_masters, colocated));
  auto table = VERIFY_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  RETURN_NOT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  SCHECK_EQ(tablets.size(), 1, InternalError, "Only 1 tablet was expected");

  return tablets;
}

Result<YBTableName> CDCSDKTestBase::GetTable(
    PostgresMiniCluster* cluster, const std::string& namespace_name, const std::string& table_name,
    bool verify_table_name, bool exclude_system_tables) {
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
      yb_table.set_table_name(table.name());
      return yb_table;
    }
  }
  return STATUS_FORMAT(
      IllegalState, "Unable to find table $0 in namespace $1", table_name, namespace_name);
}

Result<YBTableName> CDCSDKTestBase::CreateTable(
    PostgresMiniCluster* cluster, const std::string& namespace_name, const std::string& table_name,
    const uint32_t num_tablets, const bool add_primary_key, bool colocated, const int table_oid,
    const bool enum_value, const std::string& enum_suffix, const std::string& schema_name,
    uint32_t num_cols, const std::vector<string>& optional_cols_name) {
  auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));

  if (enum_value) {
    if (schema_name != "public") {
      RETURN_NOT_OK(conn.ExecuteFormat("create schema $0;", schema_name));
    }
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TYPE $0.coupon_discount_type$1 AS ENUM ('FIXED$2','PERCENTAGE$3');", schema_name,
        enum_suffix, enum_suffix, enum_suffix));
  }

  std::string table_oid_string = "";
  if (table_oid > 0) {
    // Need to turn on session flag to allow for CREATE WITH table_oid.
    RETURN_NOT_OK(conn.Execute("set yb_enable_create_with_table_oid=true"));
    table_oid_string = Format("table_oid = $0,", table_oid);
  }

  if (!optional_cols_name.empty()) {
    std::stringstream columns_name;
    std::stringstream columns_value;
    string primary_key = add_primary_key ? "PRIMARY KEY" : "";
    string second_column_type =
        enum_value ? (schema_name + "." + "coupon_discount_type" + enum_suffix) : " int";
    columns_name << "( " << kKeyColumnName << " int " << primary_key << "," << kValueColumnName
                 << second_column_type;
    for (const auto& optional_col_name : optional_cols_name) {
      columns_name << " , " << optional_col_name << " int ";
    }
    columns_name << " )";
    columns_value << " )";
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0.$1 $2 WITH ($3colocated = $4) "
        "SPLIT INTO $5 TABLETS",
        schema_name, table_name + enum_suffix, columns_name.str(), table_oid_string, colocated,
        num_tablets));
  } else if (num_cols > 2) {
    std::stringstream statement_buff;
    statement_buff << "CREATE TABLE $0.$1(col1 int PRIMARY KEY, col2 int";
    std::string rem_statement(" ) WITH ($2colocated = $3) SPLIT INTO $4 TABLETS");
    for (uint32_t col_num = 3; col_num <= num_cols; ++col_num) {
      statement_buff << ", col" << col_num << " int";
    }
    std::string statement(statement_buff.str() + rem_statement);

    RETURN_NOT_OK(conn.ExecuteFormat(
        statement, schema_name, table_name, table_oid_string, colocated, num_tablets));
  } else {
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0.$1($2 int $3, $4 $5) WITH ($6colocated = $7) "
        "SPLIT INTO $8 TABLETS",
        schema_name, table_name + enum_suffix, kKeyColumnName,
        (add_primary_key) ? "PRIMARY KEY" : "", kValueColumnName,
        enum_value ? (schema_name + "." + "coupon_discount_type" + enum_suffix) : "int",
        table_oid_string, colocated, num_tablets));
  }
  return GetTable(cluster, namespace_name, table_name + enum_suffix);
}

Status CDCSDKTestBase::AddColumn(
    PostgresMiniCluster* cluster, const std::string& namespace_name, const std::string& table_name,
    const std::string& add_column_name, const std::string& enum_suffix,
    const std::string& schema_name) {
  auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
  RETURN_NOT_OK(conn.ExecuteFormat(
      "ALTER TABLE $0.$1 ADD COLUMN $2 int", schema_name, table_name + enum_suffix,
      add_column_name));
  return Status::OK();
}

Status CDCSDKTestBase::DropColumn(
    PostgresMiniCluster* cluster, const std::string& namespace_name, const std::string& table_name,
    const std::string& column_name, const std::string& enum_suffix,
    const std::string& schema_name) {
  auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
  RETURN_NOT_OK(conn.ExecuteFormat(
      "ALTER TABLE $0.$1 DROP COLUMN $2", schema_name, table_name + enum_suffix, column_name));
  // Sleep to ensure that alter table is committed in docdb
  // TODO: (#21288) Remove the sleep once the best effort waiting mechanism for drop table lands.
  SleepFor(MonoDelta::FromSeconds(5));
  return Status::OK();
}

Status CDCSDKTestBase::RenameColumn(
    PostgresMiniCluster* cluster, const std::string& namespace_name, const std::string& table_name,
    const std::string& old_column_name, const std::string& new_column_name,
    const std::string& enum_suffix, const std::string& schema_name) {
  auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
  RETURN_NOT_OK(conn.ExecuteFormat(
      "ALTER TABLE $0.$1 RENAME COLUMN $2 TO $3", schema_name, table_name + enum_suffix,
      old_column_name, new_column_name));
  return Status::OK();
}

Result<std::string> CDCSDKTestBase::GetNamespaceId(const std::string& namespace_name) {
  master::GetNamespaceInfoResponsePB namespace_info_resp;

  RETURN_NOT_OK(test_client()->GetNamespaceInfo(
      std::string(), kNamespaceName, YQL_DATABASE_PGSQL, &namespace_info_resp));

  // Return namespace_id.
  return namespace_info_resp.namespace_().id();
}

Result<std::string> CDCSDKTestBase::GetTableId(
    PostgresMiniCluster* cluster, const std::string& namespace_name, const std::string& table_name,
    bool verify_table_name, bool exclude_system_tables) {
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

// Initialize a CreateCDCStreamRequest to be used while creating a DB stream ID.
void CDCSDKTestBase::InitCreateStreamRequest(
    CreateCDCStreamRequestPB* create_req,
    const CDCCheckpointType& checkpoint_type,
    const CDCRecordType& record_type,
    const std::string& namespace_name) {
  create_req->set_namespace_name(namespace_name);
  create_req->set_checkpoint_type(checkpoint_type);
  create_req->set_record_type(record_type);
  create_req->set_record_format(CDCRecordFormat::PROTO);
  create_req->set_source_type(CDCSDK);
}

// This creates a DB stream on the database kNamespaceName by default.
Result<xrepl::StreamId> CDCSDKTestBase::CreateDBStream(
    CDCCheckpointType checkpoint_type, CDCRecordType record_type) {
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

  InitCreateStreamRequest(&req, checkpoint_type, record_type);

  RETURN_NOT_OK(cdc_proxy_->CreateCDCStream(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return xrepl::StreamId::FromString(resp.db_stream_id());
}

Result<xrepl::StreamId> CDCSDKTestBase::CreateDBStreamWithReplicationSlot(
    CDCRecordType record_type) {
  auto slot_name = GenerateRandomReplicationSlotName();
  return CreateDBStreamWithReplicationSlot(slot_name, record_type);
}

Result<xrepl::StreamId> CDCSDKTestBase::CreateDBStreamWithReplicationSlot(
    const std::string& replication_slot_name,
    CDCRecordType record_type) {
  auto conn = VERIFY_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  RETURN_NOT_OK(conn.FetchFormat(
      "SELECT * FROM pg_create_logical_replication_slot('$0', 'pgoutput', false)",
      replication_slot_name));

  // Fetch the stream_id of the replication slot.
  auto stream_id = VERIFY_RESULT(conn.FetchRow<std::string>(Format(
      "select yb_stream_id from pg_replication_slots WHERE slot_name = '$0'",
      replication_slot_name)));
  return xrepl::StreamId::FromString(stream_id);
}

Result<xrepl::StreamId> CDCSDKTestBase::CreateConsistentSnapshotStreamWithReplicationSlot(
    const std::string& slot_name,
    CDCSDKSnapshotOption snapshot_option, bool verify_snapshot_name) {
  auto repl_conn = VERIFY_RESULT(test_cluster_.ConnectToDBWithReplication(kNamespaceName));

  std::string snapshot_action;
  switch (snapshot_option) {
    case NOEXPORT_SNAPSHOT:
      snapshot_action = "NOEXPORT_SNAPSHOT";
      break;
    case USE_SNAPSHOT:
      snapshot_action = "USE_SNAPSHOT";
      break;
  }

  auto result = VERIFY_RESULT(repl_conn.FetchFormat(
      "CREATE_REPLICATION_SLOT $0 LOGICAL pgoutput $1", slot_name, snapshot_action));
  auto snapshot_name =
      VERIFY_RESULT(pgwrapper::GetValue<std::optional<std::string>>(result.get(), 0, 2));
  LOG(INFO) << "Snapshot Name: " << (snapshot_name.has_value() ? *snapshot_name : "NULL");

  // TODO(#20816): Sleep for 1 second - temporary till sync implementation of CreateCDCStream.
  SleepFor(MonoDelta::FromSeconds(1));

  // Fetch the stream_id of the replication slot.
  auto stream_id = VERIFY_RESULT(repl_conn.FetchRow<std::string>(Format(
      "select yb_stream_id from pg_replication_slots WHERE slot_name = '$0'",
      slot_name)));
  auto xrepl_stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(stream_id));

  if (verify_snapshot_name)  {
    auto resp = VERIFY_RESULT(GetCDCStream(xrepl_stream_id));
    auto cstime = resp.stream().cdcsdk_consistent_snapshot_time();
    if (snapshot_option == NOEXPORT_SNAPSHOT) {
      SCHECK_EQ(snapshot_name.has_value(), false, InternalError, "Snapshot name is not NULL");
    } else {
      SCHECK_EQ(*snapshot_name, std::to_string(cstime), InternalError,
          "Snapshot Name is not matching the consistent snapshot time");
    }
  }

  return xrepl_stream_id;
}

Result<xrepl::StreamId> CDCSDKTestBase::CreateConsistentSnapshotStreamWithReplicationSlot(
    CDCSDKSnapshotOption snapshot_option, bool verify_snapshot_name) {
  auto slot_name = GenerateRandomReplicationSlotName();
  return CreateConsistentSnapshotStreamWithReplicationSlot(
      slot_name, snapshot_option, verify_snapshot_name);
}

// This creates a Consistent Snapshot stream on the database kNamespaceName by default.
Result<xrepl::StreamId> CDCSDKTestBase::CreateConsistentSnapshotStream(
    CDCSDKSnapshotOption snapshot_option,
    CDCCheckpointType checkpoint_type,
    CDCRecordType record_type) {
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

  InitCreateStreamRequest(&req, checkpoint_type, record_type);
  req.set_cdcsdk_consistent_snapshot_option(snapshot_option);

  RETURN_NOT_OK(cdc_proxy_->CreateCDCStream(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  // TODO(#20816): Sleep for 1 second - temporary till sync implementation of CreateCDCStream
  SleepFor(MonoDelta::FromSeconds(1));

  return xrepl::StreamId::FromString(resp.db_stream_id());
}

Result<xrepl::StreamId> CDCSDKTestBase::CreateDBStreamBasedOnCheckpointType(
    CDCCheckpointType checkpoint_type) {
  return checkpoint_type == CDCCheckpointType::EXPLICIT ? CreateDBStreamWithReplicationSlot()
                                                        : CreateDBStream(IMPLICIT);
}

Result<master::GetCDCStreamResponsePB> CDCSDKTestBase::GetCDCStream(
    const xrepl::StreamId& db_stream_id) {
  master::GetCDCStreamRequestPB get_req;
  master::GetCDCStreamResponsePB get_resp;
  get_req.set_stream_id(db_stream_id.ToString());

  rpc::RpcController get_rpc;
  get_rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

  master::MasterReplicationProxy master_proxy_(
      &test_client()->proxy_cache(),
      VERIFY_RESULT(test_cluster_.mini_cluster_->GetLeaderMasterBoundRpcAddr()));

  RETURN_NOT_OK(master_proxy_.GetCDCStream(get_req, &get_resp, &get_rpc));
  return get_resp;
}

Result<master::ListCDCStreamsResponsePB> CDCSDKTestBase::ListDBStreams() {
  auto ns_id = VERIFY_RESULT(GetNamespaceId(kNamespaceName));

  master::ListCDCStreamsRequestPB req;
  master::ListCDCStreamsResponsePB resp;

  req.set_namespace_id(ns_id);

  master::MasterReplicationProxy master_proxy(
      &test_cluster_.client_->proxy_cache(),
      VERIFY_RESULT(test_cluster_.mini_cluster_->GetLeaderMasterBoundRpcAddr()));

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(master_proxy.ListCDCStreams(req, &resp, &rpc));
  if (resp.has_error()) {
    return STATUS(IllegalState, "Failed listing CDC streams");
  }

  return resp;
}

}  // namespace cdc
}  // namespace yb
