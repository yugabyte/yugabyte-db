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

#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_int32(cdc_write_rpc_timeout_ms);
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

namespace yb {
using client::YBClient;
using client::YBTableName;

namespace cdc {
constexpr int kRpcTimeout = NonTsanVsTsan(60, 120);
static const std::string kUniverseId = "test_universe";
static const std::string kNamespaceName = "test_namespace";
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
  };

  void SetUp() override {
    YBTest::SetUp();
    // Allow for one-off network instability by ensuring a single CDC RPC timeout << test timeout.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_read_rpc_timeout_ms) = (kRpcTimeout / 4) * 1000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_write_rpc_timeout_ms) = (kRpcTimeout / 4) * 1000;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_check_broadcast_address) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_flush_rocksdb_on_shutdown) = false;
    google::SetVLOGLevel("cdc*", 4);
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
      Cluster* cluster,
      const std::string& namespace_name = kNamespaceName,
      bool colocated = false);

  Status InitPostgres(Cluster* cluster);

  Status SetUpWithParams(
      uint32_t replication_factor, uint32_t num_masters = 1, bool colocated = false,
      bool cdc_populate_safepoint_record = false);

  Result<YBTableName> GetTable(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& table_name,
      bool verify_table_name = true,
      bool exclude_system_tables = true);

  Result<YBTableName> CreateTable(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& table_name,
      const uint32_t num_tablets = 1,
      const bool add_primary_key = true,
      bool colocated = false,
      const int table_oid = 0,
      bool enum_value = false,
      const std::string& enum_suffix = "",
      const std::string& schema_name = "public",
      uint32_t num_cols = 2,
      const std::vector<std::string>& optional_cols_name = {});

  Status AddColumn(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& table_name,
      const std::string& add_column_name,
      const std::string& enum_suffix = "",
      const std::string& schema_name = "public");

  Status DropColumn(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& table_name,
      const std::string& column_name,
      const std::string& enum_suffix = "",
      const std::string& schema_name = "public");

  Status RenameColumn(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& table_name,
      const std::string& old_column_name,
      const std::string& new_column_name,
      const std::string& enum_suffix = "",
      const std::string& schema_name = "public");

  Result<std::string> GetNamespaceId(const std::string& namespace_name);

  Result<std::string> GetTableId(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& table_name,
      bool verify_table_name = true,
      bool exclude_system_tables = true);

  void InitCreateStreamRequest(
      CreateCDCStreamRequestPB* create_req,
      const CDCCheckpointType& checkpoint_type = CDCCheckpointType::EXPLICIT,
      const CDCRecordType& record_type = CDCRecordType::CHANGE,
      const std::string& namespace_name = kNamespaceName);

  Result<xrepl::StreamId> CreateDBStream(
      CDCCheckpointType checkpoint_type = CDCCheckpointType::EXPLICIT,
      CDCRecordType record_type = CDCRecordType::CHANGE);
  // Creates a DB stream on the database kNamespaceName using the Replication Slot syntax.
  // Only supports the CDCCheckpointType::EXPLICIT and CDCRecordType::CHANGE.
  // TODO(#19260): Support customizing the CDCRecordType.
  Result<xrepl::StreamId> CreateDBStreamWithReplicationSlot();
  Result<xrepl::StreamId> CreateDBStreamWithReplicationSlot(
      const std::string& replication_slot_name);

 protected:
  // Every test needs to initialize this cdc_proxy_.
  std::unique_ptr<CDCServiceProxy> cdc_proxy_;

  Cluster test_cluster_;
};
} // namespace cdc
} // namespace yb
