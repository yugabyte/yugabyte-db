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

#pragma once

#include <string>

#include "yb/client/transaction_manager.h"

#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/postgres-minicluster.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_int32(cdc_write_rpc_timeout_ms);
DECLARE_int32(master_ts_rpc_timeout_ms);
DECLARE_bool(TEST_check_broadcast_address);
DECLARE_bool(flush_rocksdb_on_shutdown);

DECLARE_int32(replication_factor);
DECLARE_int32(cdc_max_apply_batch_num_records);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(pgsql_proxy_webserver_port);
DECLARE_bool(enable_ysql);
DECLARE_bool(hide_pg_catalog_table_creation_logs);
DECLARE_bool(master_auto_run_initdb);
DECLARE_int32(pggate_rpc_timeout_secs);
DECLARE_bool(cdc_populate_safepoint_record);
DECLARE_uint32(max_replication_slots);
DECLARE_bool(ysql_yb_enable_replication_commands);
DECLARE_uint32(cdcsdk_retention_barrier_no_revision_interval_secs);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_string(allowed_preview_flags_csv);
DECLARE_bool(ysql_yb_enable_ddl_atomicity_infra);
DECLARE_bool(ysql_enable_pack_full_row_update);
DECLARE_bool(ysql_yb_enable_replica_identity);
DECLARE_bool(yb_enable_cdc_consistent_snapshot_streams);
DECLARE_bool(ysql_enable_packed_row_for_colocated_table);
DECLARE_bool(TEST_stop_committed_op_id_updation);
DECLARE_bool(ysql_yb_allow_replication_slot_lsn_types);
DECLARE_bool(cdc_send_null_before_image_if_not_exists);
DECLARE_bool(enable_tablet_split_of_replication_slot_streamed_tables);

namespace yb {
using client::YBClient;
using client::YBTableName;

namespace cdc {
// TODO(#19752): Remove the YB_DISABLE_TEST_IN_TSAN
#define CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(fixture, test_name)                       \
  TEST_F(fixture, YB_DISABLE_TEST_IN_TSAN(test_name##Explicit)) { test_name(EXPLICIT); }  \
                                                                                          \
  TEST_F(fixture, YB_DISABLE_TEST_IN_TSAN(test_name##Implicit)) { test_name(IMPLICIT); }

constexpr int kRpcTimeout = 60 * kTimeMultiplier;
constexpr int kFlushTimeoutSecs = 60 * kTimeMultiplier;
static const std::string kUniverseId = "test_universe";
static const std::string kNamespaceName = "test_namespace";
static const std::string kEnumTypeName = "coupon_discount_type";
static const std::string kReplicationSlotName = "test_replication_slot";
constexpr static const char* const kTableName = "test_table";
constexpr static const char* const kKeyColumnName = "key";
constexpr static const char* const kValueColumnName = "value_1";
constexpr static const char* const kValue2ColumnName = "value_2";
constexpr static const char* const kValue3ColumnName = "value_3";
constexpr static const char* const kValue4ColumnName = "value_4";

struct CDCSDKTestParams {
  CDCSDKTestParams(int batch_size_, bool enable_replicate_intents_) :
      batch_size(batch_size_), enable_replicate_intents(enable_replicate_intents_) {}

  int batch_size;
  bool enable_replicate_intents;
};

class CDCSDKTestBase : public YBTest {
 public:
  class Cluster {
   public:
    std::unique_ptr<MiniCluster> mini_cluster_;
    std::unique_ptr<YBClient> client_;
    std::unique_ptr<yb::pgwrapper::PgSupervisor> pg_supervisor_;
    HostPort pg_host_port_;
    boost::optional<client::TransactionManager> txn_mgr_;

    Result<pgwrapper::PGConn> Connect() {
      return ConnectToDB(std::string() /* dbname */);
    }

    Result<pgwrapper::PGConn> ConnectToDB(const std::string& dbname) {
      return pgwrapper::PGConnBuilder({
        .host = pg_host_port_.host(),
        .port = pg_host_port_.port(),
        .dbname = dbname,
      }).Connect();
    }

    Result<pgwrapper::PGConn> ConnectToDBWithReplication(const std::string& dbname) {
      return pgwrapper::PGConnBuilder({
        .host = pg_host_port_.host(),
        .port = pg_host_port_.port(),
        .dbname = dbname,
        .replication = "database",
      }).Connect(/*simple_query_protocol=*/true);
    }
  };

  void SetUp() override {
    YBTest::SetUp();
    // Allow for one-off network instability by ensuring a single CDC RPC timeout << test timeout.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_read_rpc_timeout_ms) = (kRpcTimeout / 4) * 1000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_write_rpc_timeout_ms) = (kRpcTimeout / 4) * 1000;

    // This timeout is used in the cdc_state_table client. So set to a custom value for sanitizer
    // builds to avoid timeouts while waiting for the table creation.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_ts_rpc_timeout_ms) = 30 * 1000 * kTimeMultiplier;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_check_broadcast_address) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_flush_rocksdb_on_shutdown) = false;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = true;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs) = 0;

    // TODO(#23000) Rationalize the tests to run with consistent / non-consistent snapshot streams.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = false;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row_for_colocated_table) = true;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_replication_slot_streamed_tables) =
        true;
  }

  void TearDown() override;

  std::unique_ptr<CDCServiceProxy> GetCdcProxy();

  MiniCluster* test_cluster() {
    return test_cluster_.mini_cluster_.get();
  }

  client::TransactionManager* test_cluster_txn_mgr() {
    return test_cluster_.txn_mgr_.get_ptr();
  }

  YBClient* test_client() {
    return test_cluster_.client_.get();
  }

  Status CreateDatabase(
      PostgresMiniCluster* cluster, const std::string& namespace_name = kNamespaceName,
      bool colocated = false);

  Status InitPostgres(PostgresMiniCluster* cluster);

  Status InitPostgres(PostgresMiniCluster* cluster, const size_t pg_ts_idx, uint16_t pg_port);

  Status SetUpWithParams(
      uint32_t replication_factor, uint32_t num_masters = 1, bool colocated = false,
      bool cdc_populate_safepoint_record = false, bool set_pgsql_proxy_bind_address = false);

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> SetUpWithOneTablet(
      uint32_t replication_factor, uint32_t num_masters = 1, bool colocated = false);

  Result<YBTableName> GetTable(
      PostgresMiniCluster* cluster, const std::string& namespace_name,
      const std::string& table_name, bool verify_table_name = true,
      bool exclude_system_tables = true);

  Result<YBTableName> CreateTable(
      PostgresMiniCluster* cluster, const std::string& namespace_name,
      const std::string& table_name, const uint32_t num_tablets = 1,
      const bool add_primary_key = true, bool colocated = false, const int table_oid = 0,
      bool enum_value = false, const std::string& enum_suffix = "",
      const std::string& schema_name = "public", uint32_t num_cols = 2,
      const std::vector<std::string>& optional_cols_name = {});

  Status AddColumn(
      PostgresMiniCluster* cluster, const std::string& namespace_name,
      const std::string& table_name, const std::string& add_column_name,
      pgwrapper::PGConn *conn, const std::string& enum_suffix = "",
      const std::string& schema_name = "public");

  Status DropColumn(
      PostgresMiniCluster* cluster, const std::string& namespace_name,
      const std::string& table_name, const std::string& column_name,
      pgwrapper::PGConn *conn, const std::string& enum_suffix = "",
      const std::string& schema_name = "public");

  Status RenameColumn(
      PostgresMiniCluster* cluster, const std::string& namespace_name,
      const std::string& table_name, const std::string& old_column_name,
      const std::string& new_column_name, pgwrapper::PGConn *conn,
      const std::string& enum_suffix = "", const std::string& schema_name = "public");

  Result<std::string> GetNamespaceId(const std::string& namespace_name);

  Result<std::string> GetTableId(
      PostgresMiniCluster* cluster, const std::string& namespace_name,
      const std::string& table_name, bool verify_table_name = true,
      bool exclude_system_tables = true);

  void InitCreateStreamRequest(
      CreateCDCStreamRequestPB* create_req,
      const CDCCheckpointType& checkpoint_type = CDCCheckpointType::EXPLICIT,
      const CDCRecordType& record_type = CDCRecordType::CHANGE,
      const std::string& namespace_name = kNamespaceName,
      CDCSDKDynamicTablesOption dynamic_tables_option =
          CDCSDKDynamicTablesOption::DYNAMIC_TABLES_ENABLED);

  Result<xrepl::StreamId> CreateDBStream(
      CDCCheckpointType checkpoint_type = CDCCheckpointType::EXPLICIT,
      CDCRecordType record_type = CDCRecordType::CHANGE,
      std::string namespace_name = kNamespaceName,
      CDCSDKDynamicTablesOption dynamic_tables_option =
          CDCSDKDynamicTablesOption::DYNAMIC_TABLES_ENABLED);

  // Creates a DB stream on the database kNamespaceName using the Replication Slot syntax.
  // Only supports the CDCCheckpointType::EXPLICIT and CDCRecordType::CHANGE.
  // TODO(#19260): Support customizing the CDCRecordType.
  Result<xrepl::StreamId> CreateDBStreamWithReplicationSlot(
      CDCRecordType record_type = CDCRecordType::CHANGE);
  Result<xrepl::StreamId> CreateDBStreamWithReplicationSlot(
      const std::string& replication_slot_name, CDCRecordType record_type = CDCRecordType::CHANGE);

  Result<xrepl::StreamId> CreateConsistentSnapshotStreamWithReplicationSlot(
      const std::string& replication_slot_name,
      CDCSDKSnapshotOption snapshot_option = CDCSDKSnapshotOption::USE_SNAPSHOT,
      bool verify_snapshot_name = false,
      std::string namespace_name = kNamespaceName);

  Result<xrepl::StreamId> CreateConsistentSnapshotStreamWithReplicationSlot(
      CDCSDKSnapshotOption snapshot_option = CDCSDKSnapshotOption::USE_SNAPSHOT,
      bool verify_snapshot_name = false);
  Result<xrepl::StreamId> CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption snapshot_option = CDCSDKSnapshotOption::USE_SNAPSHOT,
      CDCCheckpointType checkpoint_type = CDCCheckpointType::EXPLICIT,
      CDCRecordType record_type = CDCRecordType::CHANGE,
      std::string namespace_name = kNamespaceName);

  Result<xrepl::StreamId> CreateDBStreamBasedOnCheckpointType(CDCCheckpointType checkpoint_type);

  Result<master::GetCDCStreamResponsePB> GetCDCStream(const xrepl::StreamId& stream_id);

  Result<master::ListCDCStreamsResponsePB> ListDBStreams();

 protected:
  // Every test needs to initialize this cdc_proxy_.
  std::unique_ptr<CDCServiceProxy> cdc_proxy_;

  PostgresMiniCluster test_cluster_;
};
} // namespace cdc
} // namespace yb
