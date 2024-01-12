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
#include <algorithm>
#include <chrono>
#include <utility>

#include <boost/assign.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"

#include "yb/client/client-test-util.h"
#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/gutil/walltime.h"
#include "yb/rocksdb/db.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/client/yb_op.h"

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_value.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/cdcsdk_test_base.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_admin.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/enums.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/stol_utils.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/thread.h"
#include "yb/tablet/tablet_types.pb.h"
#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/statement_result.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

using std::map;
using std::min;
using std::pair;
using std::string;
using std::vector;

DECLARE_int64(cdc_intent_retention_ms);
DECLARE_bool(enable_update_local_peer_min_index);
DECLARE_int32(update_min_cdc_indices_interval_secs);
DECLARE_bool(stream_truncate_record);
DECLARE_int32(cdc_state_checkpoint_update_interval_ms);
DECLARE_int32(update_metrics_interval_ms);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_uint64(consensus_max_batch_size_bytes);
DECLARE_uint64(aborted_intent_cleanup_ms);
DECLARE_int32(cdc_min_replicated_index_considered_stale_secs);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_bool(tablet_enable_ttl_file_filter);
DECLARE_int32(timestamp_syscatalog_history_retention_interval_sec);
DECLARE_int32(cdc_max_stream_intent_records);
DECLARE_bool(enable_single_record_update);
DECLARE_bool(enable_delete_truncate_cdcsdk_table);
DECLARE_bool(enable_load_balancing);
DECLARE_int32(cdc_parent_tablet_deletion_task_retry_secs);
DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_int32(cdcsdk_table_processing_limit_per_run);
DECLARE_int32(cdc_snapshot_batch_size);
DECLARE_bool(TEST_cdc_snapshot_failure);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_uint64(ysql_packed_row_size_limit);
DECLARE_bool(cdc_populate_safepoint_record);
DECLARE_string(vmodule);
DECLARE_int32(ysql_num_shards_per_tserver);
DECLARE_int32(TEST_txn_participant_inject_latency_on_apply_update_txn_ms);
DECLARE_bool(cdc_enable_consistent_records);
DECLARE_bool(cdc_populate_end_markers_transactions);
DECLARE_uint64(cdc_stream_records_threshold_size_bytes);
DECLARE_int64(cdc_resolve_intent_lag_threshold_ms);
DECLARE_bool(enable_tablet_split_of_cdcsdk_streamed_tables);
DECLARE_bool(TEST_cdcsdk_skip_processing_dynamic_table_addition);

namespace yb {

using client::YBClient;
using client::YBTableName;

using pgwrapper::PGConn;
using pgwrapper::PGResultPtr;

using rpc::RpcController;

namespace cdc {

YB_DEFINE_ENUM(IntentCountCompareOption, (GreaterThanOrEqualTo)(GreaterThan)(EqualTo));
YB_DEFINE_ENUM(OpIdExpectedValue, (MaxOpId)(InvalidOpId)(ValidNonMaxOpId));

class CDCSDKYsqlTest : public CDCSDKTestBase {
 public:
  struct ExpectedRecord {
    int32_t key;
    int32_t value;
  };

  struct ExpectedRecordWithThreeColumns {
    int32_t key;
    int32_t value;
    int32_t value2;
  };

  struct VaryingExpectedRecord {
    uint32_t key;
    vector<std::pair<std::string, uint32_t>> val_vec;
  };

  struct CdcStateTableRow {
    OpId op_id = OpId::Max();
    int64_t cdc_sdk_latest_active_time = 0;
    HybridTime cdc_sdk_safe_time = HybridTime::kInvalid;
  };

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

  struct GetAllPendingChangesResponse {
    vector<CDCSDKProtoRecordPB> records;
    CDCSDKCheckpointPB checkpoint;
    int64 safe_hybrid_time = -1;
  };

  void VerifyCdcStateMatches(
      client::YBClient* client, const CDCStreamId& stream_id, const TabletId& tablet_id,
      const uint64_t term, const uint64_t index) {
    client::TableHandle table;
    client::YBTableName cdc_state_table(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
    ASSERT_OK(table.Open(cdc_state_table, client));
    auto session = client->NewSession();

    auto row = ASSERT_RESULT(FetchCdcStreamInfo(
        &table, session.get(), tablet_id, stream_id, {master::kCdcCheckpoint}));

    LOG(INFO) << strings::Substitute(
        "Verifying tablet: $0, stream: $1, op_id: $2", tablet_id, stream_id,
        OpId(term, index).ToString());

    string checkpoint = row.column(0).string_value();
    auto result = OpId::FromString(checkpoint);
    ASSERT_OK(result);
    OpId op_id = *result;

    ASSERT_EQ(op_id.term, term);
    ASSERT_EQ(op_id.index, index);
  }

  Status WriteRowsToTwoTables(
      uint32_t start, uint32_t end, Cluster* cluster, bool flag, const char* const first_table_name,
      const char* const second_table_name, uint32_t num_cols = 2) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    RETURN_NOT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; ++i) {
      uint32_t value = i;
      std::stringstream statement_buff;
      statement_buff << "INSERT INTO $0 VALUES (";
      for (uint32_t iter = 0; iter < num_cols; ++value, ++iter) {
        statement_buff << value << ",";
      }

      std::string statement(statement_buff.str());
      statement.at(statement.size() - 1) = ')';
      RETURN_NOT_OK(conn.ExecuteFormat(statement, first_table_name));
      RETURN_NOT_OK(conn.ExecuteFormat(statement, second_table_name));
    }

    if (flag) {
      RETURN_NOT_OK(conn.Execute("COMMIT"));
    } else {
      RETURN_NOT_OK(conn.Execute("ABORT"));
    }
    return Status::OK();
  }

  void VerifyStreamDeletedFromCdcState(
      client::YBClient* client, const CDCStreamId& stream_id, const TabletId& tablet_id,
      int timeout_secs = 120) {
    client::TableHandle table;
    const client::YBTableName cdc_state_table(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
    ASSERT_OK(table.Open(cdc_state_table, client));
    auto session = client->NewSession();

    // The deletion of cdc_state rows for the specified stream happen in an asynchronous thread,
    // so even if the request has returned, it doesn't mean that the rows have been deleted yet.
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          auto row = VERIFY_RESULT(FetchOptionalCdcStreamInfo(
              &table, session.get(), tablet_id, stream_id, {master::kCdcCheckpoint}));
          return !row;
        },
        MonoDelta::FromSeconds(timeout_secs),
        "Failed to delete stream rows from cdc_state table."));
  }

  Result<OpId> GetStreamCheckpointInCdcState(
      client::YBClient* client, const CDCStreamId& stream_id, const TabletId& tablet_id) {
    client::TableHandle table;
    const client::YBTableName cdc_state_table(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
    RETURN_NOT_OK(table.Open(cdc_state_table, client));
    auto session = client->NewSession();

    auto row = VERIFY_RESULT(FetchCdcStreamInfo(
        &table, session.get(), tablet_id, stream_id, {master::kCdcCheckpoint}));

    auto op_id_result = OpId::FromString(row.column(0).string_value());
    RETURN_NOT_OK(op_id_result);
    auto op_id = *op_id_result;

    return op_id;
  }

  void VerifyStreamCheckpointInCdcState(
      client::YBClient* client, const CDCStreamId& stream_id, const TabletId& tablet_id,
      OpIdExpectedValue op_id_expected_value = OpIdExpectedValue::ValidNonMaxOpId,
      int timeout_secs = 120) {
    client::TableHandle table;
    const client::YBTableName cdc_state_table(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
    ASSERT_OK(table.Open(cdc_state_table, client));
    auto session = client->NewSession();

    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          auto row = VERIFY_RESULT(FetchCdcStreamInfo(
              &table, session.get(), tablet_id, stream_id, {master::kCdcCheckpoint}));
          auto op_id_result = OpId::FromString(row.column(0).string_value());
          if (!op_id_result.ok()) {
            return false;
          }
          auto op_id = *op_id_result;

          switch (op_id_expected_value) {
            case OpIdExpectedValue::MaxOpId:
              if (op_id == OpId::Max()) return true;
              break;
            case (OpIdExpectedValue::InvalidOpId):
              if (op_id == OpId::Invalid()) return true;
              break;
            case (OpIdExpectedValue::ValidNonMaxOpId):
              if (op_id.valid() && op_id != OpId::Max()) return true;
              break;
            default:
              break;
          }

          return false;
        },
        MonoDelta::FromSeconds(timeout_secs),
        "Checkpoint not OpId::Max in cdc_state table."));
  }

  void VerifyTransactionParticipant(const TabletId& tablet_id, const OpId& opid) {
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
            for (const auto& peer : test_cluster()->GetTabletPeers(i)) {
              if (peer->tablet_id() == tablet_id) {
                LOG(INFO) << "Tablet peer cdc_sdk_min_checkpoint_op_id: "
                          << peer->cdc_sdk_min_checkpoint_op_id();
                if (peer->cdc_sdk_min_checkpoint_op_id() == opid) {
                  return true;
                }
              }
            }
          }
          return false;
        },
        MonoDelta::FromSeconds(60),
        "The cdc_sdk_min_checkpoint_op_id doesn't match with expected op_id."));
  }

  Status DropDB(Cluster* cluster) {
    const std::string db_name = "testdatabase";
    RETURN_NOT_OK(CreateDatabase(&test_cluster_, db_name, true));
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(db_name));
    RETURN_NOT_OK(conn.ExecuteFormat("DROP DATABASE $0", kNamespaceName));
    return Status::OK();
  }

  Status TruncateTable(Cluster* cluster, const std::vector<string>& table_ids) {
    RETURN_NOT_OK(cluster->client_->TruncateTables(table_ids));
    return Status::OK();
  }

  // The range is exclusive of end i.e. [start, end)
  Status WriteRows(
      uint32_t start, uint32_t end, Cluster* cluster,
      const vector<string>& optional_cols_name = {}) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s)";

    for (uint32_t i = start; i < end; ++i) {
      if (!optional_cols_name.empty()) {
        std::stringstream columns_name;
        std::stringstream columns_value;
        columns_name << "( " << kKeyColumnName << "," << kValueColumnName;
        columns_value << "( " << i << "," << i + 1;
        for (const auto& optional_col_name : optional_cols_name) {
          columns_name << ", " << optional_col_name;
          columns_value << "," << i + 1;
        }
        columns_name << " )";
        columns_value << " )";
        RETURN_NOT_OK(conn.ExecuteFormat(
            "INSERT INTO $0 $1 VALUES $2", kTableName, columns_name.str(), columns_value.str()));
      } else {
        RETURN_NOT_OK(conn.ExecuteFormat(
            "INSERT INTO $0($1, $2) VALUES ($3, $4)", kTableName, kKeyColumnName, kValueColumnName,
            i, i + 1));
      }
    }
    return Status::OK();
  }

  // The range is exclusive of end i.e. [start, end)
  Status WriteRows(uint32_t start, uint32_t end, Cluster* cluster, uint32_t num_cols) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s)";

    for (uint32_t i = start; i < end; ++i) {
      uint32_t value = i;
      std::stringstream statement_buff;
      statement_buff << "INSERT INTO $0 VALUES (";
      for (uint32_t iter = 0; iter < num_cols; ++value, ++iter) {
        statement_buff << value << ",";
      }

      std::string statement(statement_buff.str());
      statement.at(statement.size() - 1) = ')';
      RETURN_NOT_OK(conn.ExecuteFormat(statement, kTableName));
    }
    return Status::OK();
  }

  void DropTable(Cluster* cluster, const char* table_name = kTableName) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(kNamespaceName));
    ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table_name));
  }

  Status WriteRowsHelper(
      uint32_t start, uint32_t end, Cluster* cluster, bool flag, uint32_t num_cols = 2,
      const char* const table_name = kTableName, const vector<string>& optional_cols_name = {}) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    RETURN_NOT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; ++i) {
      if (!optional_cols_name.empty()) {
        std::stringstream columns_name;
        std::stringstream columns_value;
        columns_name << "( " << kKeyColumnName << "," << kValueColumnName;
        columns_value << "( " << i << "," << i + 1;
        for (const auto& optional_col_name : optional_cols_name) {
          columns_name << ", " << optional_col_name;
          columns_value << "," << i + 1;
        }
        columns_name << " )";
        columns_value << " )";
        RETURN_NOT_OK(conn.ExecuteFormat(
            "INSERT INTO $0 $1 VALUES $2", table_name, columns_name.str(), columns_value.str()));
      } else {
        uint32_t value = i;
        std::stringstream statement_buff;
        statement_buff << "INSERT INTO $0 VALUES (";
        for (uint32_t iter = 0; iter < num_cols; ++value, ++iter) {
          statement_buff << value << ",";
        }

        std::string statement(statement_buff.str());
        statement.at(statement.size() - 1) = ')';
        RETURN_NOT_OK(conn.ExecuteFormat(statement, table_name));
      }
    }
    if (flag) {
      RETURN_NOT_OK(conn.Execute("COMMIT"));
    } else {
      RETURN_NOT_OK(conn.Execute("ABORT"));
    }
    return Status::OK();
  }

  Status CreateTableWithoutPK(Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLE test1_no_pk(id1 int, id2 int)"));
    return Status::OK();
  }

  Status WriteAndUpdateRowsHelper(
      uint32_t start, uint32_t end, Cluster* cluster, bool flag,
      const std::multimap<uint32_t, uint32_t>& col_val_map, const std::string& table_id) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    for (uint32_t i = start; i < end; ++i) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "INSERT INTO $0($1, $2) VALUES ($3, $4)", kTableName, kKeyColumnName, kValueColumnName, i,
          i + 1));
    }
    RETURN_NOT_OK(test_client()->FlushTables(
        {table_id}, /* add_indexes = */ false,
        /* timeout_secs = */ 30, /* is_compaction = */ false));

    RETURN_NOT_OK(conn.Execute("BEGIN"));
    for (auto& col_value_pair : col_val_map) {
      LOG(INFO) << "Updating row for key " << col_value_pair.first << " with value "
                << col_value_pair.second;
      RETURN_NOT_OK(conn.ExecuteFormat(
          "UPDATE $0 SET $1 = $2 WHERE $3 = $4", kTableName, kValueColumnName,
          col_value_pair.second, kKeyColumnName, col_value_pair.first));
    }
    RETURN_NOT_OK(test_client()->FlushTables(
        {table_id}, /* add_indexes = */ false,
        /* timeout_secs = */ 30, /* is_compaction = */ false));

    if (flag) {
      RETURN_NOT_OK(conn.Execute("COMMIT"));
    } else {
      RETURN_NOT_OK(conn.Execute("ABORT"));
    }
    return Status::OK();
  }

  Status CreateColocatedObjects(Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLEGROUP tg1"));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLE test1(id1 int primary key) TABLEGROUP tg1;"));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLE test2(id2 text primary key) TABLEGROUP tg1;"));
    return Status::OK();
  }

  Status AddColocatedTable(
      Cluster* cluster, const TableName& table_name, const std::string& table_group_name = "tg1") {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0(id2 text primary key) TABLEGROUP $1;", table_name, table_group_name));
    return Status::OK();
  }

  Status PopulateColocatedData(Cluster* cluster, int insert_count, bool transaction = false) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    if (transaction) {
      RETURN_NOT_OK(conn.Execute("BEGIN"));
    }
    for (int i = 0; i < insert_count; ++i) {
      LOG(INFO) << "Inserting entry " << i;
      RETURN_NOT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0)", i));
      RETURN_NOT_OK(conn.ExecuteFormat("INSERT INTO test2 VALUES ('$0')", i));
    }
    if (transaction) {
      RETURN_NOT_OK(conn.Execute("COMMIT"));
    }
    return Status::OK();
  }

  Status WriteEnumsRows(
      uint32_t start, uint32_t end, Cluster* cluster, const string& enum_suffix = "",
      string database_name = kNamespaceName, string table_name = kTableName,
      string schema_name = "public") {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(database_name));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    RETURN_NOT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; ++i) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "INSERT INTO $0.$1($2, $3) VALUES ($4, '$5')", schema_name, table_name + enum_suffix,
          kKeyColumnName, kValueColumnName, i,
          std::string(i % 2 ? "FIXED" : "PERCENTAGE") + enum_suffix));
    }
    RETURN_NOT_OK(conn.Execute("COMMIT"));
    return Status::OK();
  }

  Result<YBTableName> CreateCompositeTable(
      Cluster* cluster, const uint32_t num_tablets, const std::string& type_suffix = "") {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TYPE composite_name$0 AS (first text, last text);", type_suffix));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE emp(id int primary key, name composite_name) "
        "SPLIT INTO $0 TABLETS",
        num_tablets));
    return GetTable(cluster, kNamespaceName, "emp");
  }

  Status WriteCompositeRows(uint32_t start, uint32_t end, Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    RETURN_NOT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; ++i) {
      RETURN_NOT_OK(
          conn.ExecuteFormat("INSERT INTO emp(id, name) VALUES ($0, ('John', 'Doe'))", i));
    }
    RETURN_NOT_OK(conn.Execute("COMMIT"));
    return Status::OK();
  }

  Result<YBTableName> CreateNestedCompositeTable(
      Cluster* cluster, const uint32_t num_tablets, const std::string& type_suffix = "") {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));

    RETURN_NOT_OK(
        conn.ExecuteFormat("CREATE TYPE part_name$0 AS (first text, middle text);", type_suffix));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TYPE full_name$0 AS (part part_name$0, last text);", type_suffix));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE emp_nested(id int primary key, name full_name$0) "
        "SPLIT INTO $1 TABLETS",
        type_suffix, num_tablets));
    return GetTable(cluster, kNamespaceName, "emp_nested");
  }

  Status WriteNestedCompositeRows(uint32_t start, uint32_t end, Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    RETURN_NOT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; ++i) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "INSERT INTO emp_nested(id, name) VALUES ($0, (('John', 'Middle'), 'Doe'))", i));
    }
    RETURN_NOT_OK(conn.Execute("COMMIT"));
    return Status::OK();
  }

  Result<YBTableName> CreateArrayCompositeTable(
      Cluster* cluster, const uint32_t num_tablets, const std::string& type_suffix = "") {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));

    RETURN_NOT_OK(
        conn.ExecuteFormat("CREATE TYPE emp_data$0 AS (name text[], phone int[]);", type_suffix));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE emp_array(id int primary key, data emp_data$0) "
        "SPLIT INTO $1 TABLETS",
        type_suffix, num_tablets));
    return GetTable(cluster, kNamespaceName, "emp_array");
  }

  Status WriteArrayCompositeRows(uint32_t start, uint32_t end, Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    RETURN_NOT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; ++i) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "INSERT INTO emp_array(id, data) VALUES ($0, ('{\"John\", \"Middle\", \"Doe\"}', '{123, "
          "456}'))",
          i));
    }
    RETURN_NOT_OK(conn.Execute("COMMIT"));
    return Status::OK();
  }

  Result<YBTableName> CreateRangeCompositeTable(
      Cluster* cluster, const uint32_t num_tablets, const std::string& type_suffix = "") {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE type range_composite$0 AS (r1 numrange, r2 int4range);", type_suffix));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE range_composite_table(id int primary key, data range_composite$0) "
        "SPLIT INTO $1 TABLETS",
        type_suffix, num_tablets));
    return GetTable(cluster, kNamespaceName, "range_composite_table");
  }

  Status WriteRangeCompositeRows(uint32_t start, uint32_t end, Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    RETURN_NOT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; ++i) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "INSERT INTO range_composite_table(id, data) VALUES ($0, ('[$1, $2]', '[$3, $4]'))", i, i,
          i + 10, i + 11, i + 20));
    }
    RETURN_NOT_OK(conn.Execute("COMMIT"));
    return Status::OK();
  }

  Result<YBTableName> CreateRangeArrayCompositeTable(
      Cluster* cluster, const uint32_t num_tablets, const std::string& type_suffix = "") {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE type range_array_composite$0 AS (r1 numrange[], r2 int4range[]);", type_suffix));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE range_array_composite_table(id int primary key, data "
        "range_array_composite$0) "
        "SPLIT INTO $1 TABLETS",
        type_suffix, num_tablets));
    return GetTable(cluster, kNamespaceName, "range_array_composite_table");
  }

  Status WriteRangeArrayCompositeRows(uint32_t start, uint32_t end, Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    RETURN_NOT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; ++i) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "INSERT INTO range_array_composite_table(id, data) VALUES ($0, ('{\"[$1, $2]\", \"[$3, "
          "$4]\"}', '{\"[$5, $6]\"}'))",
          i, i, i + 10, i + 11, i + 20, i + 21, i + 30));
    }
    RETURN_NOT_OK(conn.Execute("COMMIT"));
    return Status::OK();
  }

  Status UpdateRows(uint32_t key, uint32_t value, Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Updating row for key " << key << " with value " << value;
    RETURN_NOT_OK(conn.ExecuteFormat(
        "UPDATE $0 SET $1 = $2 WHERE $3 = $4", kTableName, kValueColumnName, value, kKeyColumnName,
        key));
    return Status::OK();
  }

  Status UpdatePrimaryKey(uint32_t key, uint32_t value, Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Updating primary key " << key << " with value " << value;
    RETURN_NOT_OK(conn.ExecuteFormat(
        "UPDATE $0 SET $1 = $2 WHERE $3 = $4", kTableName, kKeyColumnName, value, kKeyColumnName,
        key));
    return Status::OK();
  }

  Status UpdateRows(
      uint32_t key, const std::map<std::string, uint32_t>& col_val_map, Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    std::stringstream log_buff;
    log_buff << "Updating row for key " << key << " with";
    for (auto& col_value_pair : col_val_map) {
      log_buff << " (" << col_value_pair.first << ":" << col_value_pair.second << ")";
    }
    LOG(INFO) << log_buff.str();

    std::stringstream statement_buff;
    statement_buff << "UPDATE $0 SET ";
    for (auto col_value_pair : col_val_map) {
      statement_buff << col_value_pair.first << "=" << col_value_pair.second << ",";
    }

    std::string statement(statement_buff.str());
    statement.at(statement.size() - 1) = ' ';
    std::string where_clause("WHERE $1 = $2");
    statement += where_clause;
    RETURN_NOT_OK(conn.ExecuteFormat(statement, kTableName, "col1", key));
    return Status::OK();
  }

  Status UpdateRowsHelper(
      uint32_t start, uint32_t end, Cluster* cluster, bool flag, uint32_t key,
      const std::map<std::string, uint32_t>& col_val_map1,
      const std::map<std::string, uint32_t>& col_val_map2, uint32_t num_cols) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    std::stringstream log_buff1, log_buff2;
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    RETURN_NOT_OK(conn.Execute("BEGIN"));

    for (uint32_t i = start; i < end; ++i) {
      uint32_t value = i;
      std::stringstream statement_buff;
      statement_buff << "INSERT INTO $0 VALUES (";
      for (uint32_t iter = 0; iter < num_cols; ++value, ++iter) {
        statement_buff << value << ",";
      }

      std::string statement(statement_buff.str());
      statement.at(statement.size() - 1) = ')';
      RETURN_NOT_OK(conn.ExecuteFormat(statement, kTableName));
    }

    log_buff1 << "Updating row for key " << key << " with";
    for (auto& col_value_pair : col_val_map1) {
      log_buff1 << " (" << col_value_pair.first << ":" << col_value_pair.second << ")";
    }
    LOG(INFO) << log_buff1.str();

    std::stringstream statement_buff1, statement_buff2;
    statement_buff1 << "UPDATE $0 SET ";
    for (auto& col_value_pair : col_val_map1) {
      statement_buff1 << col_value_pair.first << "=" << col_value_pair.second << ",";
    }

    std::string statement1(statement_buff1.str());
    statement1.at(statement1.size() - 1) = ' ';
    std::string where_clause("WHERE $1 = $2");
    statement1 += where_clause;
    RETURN_NOT_OK(conn.ExecuteFormat(statement1, kTableName, "col1", key));

    log_buff2 << "Updating row for key " << key << " with";
    for (auto& col_value_pair : col_val_map2) {
      log_buff2 << " (" << col_value_pair.first << ":" << col_value_pair.second << ")";
    }
    LOG(INFO) << log_buff2.str();

    statement_buff2 << "UPDATE $0 SET ";
    for (auto& col_value_pair : col_val_map2) {
      statement_buff2 << col_value_pair.first << "=" << col_value_pair.second << ",";
    }

    std::string statement2(statement_buff2.str());
    statement2.at(statement2.size() - 1) = ' ';
    statement2 += where_clause;
    RETURN_NOT_OK(conn.ExecuteFormat(statement2, kTableName, "col1", key));

    if (flag) {
      RETURN_NOT_OK(conn.Execute("COMMIT"));
    } else {
      RETURN_NOT_OK(conn.Execute("ABORT"));
    }
    return Status::OK();
  }

  Status UpdateDeleteRowsHelper(
      uint32_t start, uint32_t end, Cluster* cluster, bool flag, uint32_t key,
      const std::map<std::string, uint32_t>& col_val_map, uint32_t num_cols) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    std::stringstream log_buff1, log_buff2;
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    RETURN_NOT_OK(conn.Execute("BEGIN"));

    for (uint32_t i = start; i < end; ++i) {
      uint32_t value = i;
      std::stringstream statement_buff;
      statement_buff << "INSERT INTO $0 VALUES (";
      for (uint32_t iter = 0; iter < num_cols; ++value, ++iter) {
        statement_buff << value << ",";
      }

      std::string statement(statement_buff.str());
      statement.at(statement.size() - 1) = ')';
      RETURN_NOT_OK(conn.ExecuteFormat(statement, kTableName));
    }

    log_buff1 << "Updating row for key " << key << " with";
    for (auto col_value_pair : col_val_map) {
      log_buff1 << " (" << col_value_pair.first << ":" << col_value_pair.second << ")";
    }
    LOG(INFO) << log_buff1.str();

    std::stringstream statement_buff1, statement_buff2;
    statement_buff1 << "UPDATE $0 SET ";
    for (auto col_value_pair : col_val_map) {
      statement_buff1 << col_value_pair.first << "=" << col_value_pair.second << ",";
    }

    std::string statement1(statement_buff1.str());
    statement1.at(statement1.size() - 1) = ' ';
    std::string where_clause("WHERE $1 = $2");
    statement1 += where_clause;
    RETURN_NOT_OK(conn.ExecuteFormat(statement1, kTableName, "col1", key));

    log_buff2 << "Updating row for key " << key << " with";
    for (auto& col_value_pair : col_val_map) {
      log_buff2 << " (" << col_value_pair.first << ":" << col_value_pair.second << ")";
    }
    LOG(INFO) << log_buff2.str();

    statement_buff2 << "DELETE FROM $0 ";

    std::string statement2(statement_buff2.str());
    statement2.at(statement2.size() - 1) = ' ';
    statement2 += where_clause;
    RETURN_NOT_OK(conn.ExecuteFormat(statement2, kTableName, "col1", key));

    if (flag) {
      RETURN_NOT_OK(conn.Execute("COMMIT"));
    } else {
      RETURN_NOT_OK(conn.Execute("ABORT"));
    }
    return Status::OK();
  }

  Status DeleteRows(uint32_t key, Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Deleting row for key " << key;
    RETURN_NOT_OK(
        conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = $2", kTableName, kKeyColumnName, key));
    return Status::OK();
  }

  Status SplitTablet(const TabletId& tablet_id, Cluster* cluster) {
    yb::master::SplitTabletRequestPB req;
    req.set_tablet_id(tablet_id);
    yb::master::SplitTabletResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(30.0) * kTimeMultiplier);

    RETURN_NOT_OK(cluster->mini_cluster_->mini_master()->catalog_manager().SplitTablet(
        tablet_id, master::ManualSplit::kTrue));

    if (resp.has_error()) {
      RETURN_NOT_OK(StatusFromPB(resp.error().status()));
    }
    return Status::OK();
  }

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> SetUpCluster() {
    FLAGS_enable_single_record_update = false;
    RETURN_NOT_OK(SetUpWithParams(3, 1, false));
    auto table = EXPECT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
    return tablets;
  }

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>>
  SetUpClusterMultiColumnUsecase(uint32_t num_cols) {
    FLAGS_enable_single_record_update = true;
    RETURN_NOT_OK(SetUpWithParams(3, 1, false));
    auto table = EXPECT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, 1, true, false, 0, false, "", "public",
        num_cols));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
    return tablets;
  }

  Result<GetChangesResponsePB> UpdateSnapshotDone(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const TableId table_id = "") {
    GetChangesRequestPB change_req2;
    GetChangesResponsePB change_resp2;
    PrepareChangeRequest(
        &change_req2, stream_id, tablets, 0, -1, -1, kCDCSDKSnapshotDoneKey, 0, 0, table_id);
    RpcController get_changes_rpc;
    RETURN_NOT_OK(cdc_proxy_->GetChanges(change_req2, &change_resp2, &get_changes_rpc));
    if (change_resp2.has_error()) {
      return StatusFromPB(change_resp2.error().status());
    }

    return change_resp2;
  }

  Result<GetChangesResponsePB> UpdateCheckpoint(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const GetChangesResponsePB* change_resp,
      const TableId table_id = "") {
    GetChangesRequestPB change_req2;
    GetChangesResponsePB change_resp2;
    PrepareChangeRequest(
        &change_req2, stream_id, tablets, 0, change_resp->cdc_sdk_checkpoint().index(),
        change_resp->cdc_sdk_checkpoint().term(), change_resp->cdc_sdk_checkpoint().key(),
        change_resp->cdc_sdk_checkpoint().write_id(),
        change_resp->cdc_sdk_checkpoint().snapshot_time(), table_id);
    RpcController get_changes_rpc;
    RETURN_NOT_OK(cdc_proxy_->GetChanges(change_req2, &change_resp2, &get_changes_rpc));
    if (change_resp2.has_error()) {
      return StatusFromPB(change_resp2.error().status());
    }

    return change_resp2;
  }

  std::unique_ptr<tserver::TabletServerAdminServiceProxy> GetTServerAdminProxy(
      const uint32_t tserver_index) {
    auto tserver = test_cluster()->mini_tablet_server(tserver_index);
    return std::make_unique<tserver::TabletServerAdminServiceProxy>(
        &tserver->server()->proxy_cache(), HostPort::FromBoundEndpoint(tserver->bound_rpc_addr()));
  }

  Status GetIntentCounts(const uint32_t tserver_index, int64* num_intents) {
    tserver::CountIntentsRequestPB req;
    tserver::CountIntentsResponsePB resp;
    RpcController rpc;

    auto ts_admin_service_proxy = GetTServerAdminProxy(tserver_index);
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(ts_admin_service_proxy->CountIntents(req, &resp, &rpc));
    *num_intents = resp.num_intents();
    return Status::OK();
  }

  void PrepareChangeRequest(
      GetChangesRequestPB* change_req, const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const int tablet_idx = 0, int64 index = 0, int64 term = 0, std::string key = "",
      int32_t write_id = 0, int64 snapshot_time = 0, const TableId table_id = "",
      int64 safe_hybrid_time = -1, int32_t wal_segment_index = 0,
      const bool populate_checkpoint = true) {
    change_req->set_stream_id(stream_id);
    change_req->set_tablet_id(tablets.Get(tablet_idx).tablet_id());
    if (populate_checkpoint) {
      change_req->mutable_from_cdc_sdk_checkpoint()->set_index(index);
      change_req->mutable_from_cdc_sdk_checkpoint()->set_term(term);
      change_req->mutable_from_cdc_sdk_checkpoint()->set_key(key);
      change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(write_id);
      change_req->mutable_from_cdc_sdk_checkpoint()->set_snapshot_time(snapshot_time);
    }
    change_req->set_wal_segment_index(wal_segment_index);
    if (!table_id.empty()) {
      change_req->set_table_id(table_id);
    }
    change_req->set_safe_hybrid_time(safe_hybrid_time);
  }

  void PrepareChangeRequest(
      GetChangesRequestPB* change_req, const CDCStreamId& stream_id, const TabletId& tablet_id,
      const int tablet_idx = 0, int64 index = 0, int64 term = 0, std::string key = "",
      int32_t write_id = 0, int64 snapshot_time = 0) {
    change_req->set_stream_id(stream_id);
    change_req->set_tablet_id(tablet_id);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_index(index);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_term(term);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_key(key);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(write_id);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_snapshot_time(snapshot_time);
  }

  void PrepareChangeRequest(
      GetChangesRequestPB* change_req, const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB& cp, const int tablet_idx = 0, const TableId table_id = "",
      int64 safe_hybrid_time = -1, int32_t wal_segment_index = 0) {
    change_req->set_stream_id(stream_id);
    change_req->set_tablet_id(tablets.Get(tablet_idx).tablet_id());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_term(cp.term());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_index(cp.index());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_key(cp.key());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(cp.write_id());
    if (!table_id.empty()) {
      change_req->set_table_id(table_id);
    }
    change_req->set_safe_hybrid_time(safe_hybrid_time);
    change_req->set_wal_segment_index(wal_segment_index);
  }

  void PrepareChangeRequest(
      GetChangesRequestPB* change_req, const CDCStreamId& stream_id, const TabletId& tablet_id,
      const CDCSDKCheckpointPB& cp, const int tablet_idx = 0) {
    change_req->set_stream_id(stream_id);
    change_req->set_tablet_id(tablet_id);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_term(cp.term());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_index(cp.index());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_key(cp.key());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(cp.write_id());
  }

  void PrepareChangeRequestWithExplicitCheckpoint(
      GetChangesRequestPB* change_req, const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB& cp, const int tablet_idx = 0) {
    change_req->set_stream_id(stream_id);
    change_req->set_tablet_id(tablets.Get(tablet_idx).tablet_id());

    change_req->mutable_from_cdc_sdk_checkpoint()->set_term(cp.term());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_index(cp.index());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_key(cp.key());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(cp.write_id());

    change_req->mutable_explicit_cdc_sdk_checkpoint()->set_term(cp.term());
    change_req->mutable_explicit_cdc_sdk_checkpoint()->set_index(cp.index());
    change_req->mutable_explicit_cdc_sdk_checkpoint()->set_key(cp.key());
    change_req->mutable_explicit_cdc_sdk_checkpoint()->set_write_id(cp.write_id());
  }

  void PrepareSetCheckpointRequest(
      SetCDCCheckpointRequestPB* set_checkpoint_req,
      const CDCStreamId stream_id,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB>
          tablets,
      const int tablet_idx,
      const OpId& op_id,
      bool initial_checkpoint,
      const uint64_t cdc_sdk_safe_time,
      bool bootstrap) {
    set_checkpoint_req->set_stream_id(stream_id);
    set_checkpoint_req->set_initial_checkpoint(initial_checkpoint);
    set_checkpoint_req->set_cdc_sdk_safe_time(cdc_sdk_safe_time);
    set_checkpoint_req->set_bootstrap(bootstrap);
    set_checkpoint_req->set_tablet_id(tablets.Get(tablet_idx).tablet_id());
    set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_term(op_id.term);
    set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_index(op_id.index);
  }

  Result<SetCDCCheckpointResponsePB> SetCDCCheckpoint(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const OpId& op_id = OpId::Min(), const uint64_t cdc_sdk_safe_time = 0,
      bool initial_checkpoint = true, const int tablet_idx = 0, bool bootstrap = false) {
    Status st;
    SetCDCCheckpointResponsePB set_checkpoint_resp;

    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          RpcController set_checkpoint_rpc;
          SetCDCCheckpointRequestPB set_checkpoint_req;

          auto deadline = CoarseMonoClock::now() + test_client()->default_rpc_timeout();
          set_checkpoint_rpc.set_deadline(deadline);

          PrepareSetCheckpointRequest(
              &set_checkpoint_req, stream_id, tablets, tablet_idx, op_id, initial_checkpoint,
              cdc_sdk_safe_time, bootstrap);
          st = cdc_proxy_->SetCDCCheckpoint(
              set_checkpoint_req, &set_checkpoint_resp, &set_checkpoint_rpc);

          if (set_checkpoint_resp.has_error() &&
              set_checkpoint_resp.error().code() != CDCErrorPB::TABLET_NOT_FOUND &&
              set_checkpoint_resp.error().code() != CDCErrorPB::LEADER_NOT_READY) {
            return STATUS_FORMAT(
                InternalError, "Response had error: $0", set_checkpoint_resp.DebugString());
          }

          if (st.ok() && !set_checkpoint_resp.has_error()) {
            return true;
          }

          return false;
        },
        MonoDelta::FromSeconds(kRpcTimeout),
        "Set CDC Checkpoint timed out " + set_checkpoint_resp.DebugString()));

    return set_checkpoint_resp;
  }

  Result<std::vector<OpId>> GetCDCCheckpoint(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
    GetCheckpointRequestPB get_checkpoint_req;
    std::vector<OpId> op_ids;

    op_ids.reserve(tablets.size());
    for (const auto& tablet : tablets) {
      get_checkpoint_req.set_stream_id(stream_id);
      get_checkpoint_req.set_tablet_id(tablet.tablet_id());

      RETURN_NOT_OK(WaitFor(
          [&]() -> Result<bool> {
            GetCheckpointResponsePB get_checkpoint_resp;
            RpcController get_checkpoint_rpc;
            auto deadline = CoarseMonoClock::now() + test_client()->default_rpc_timeout();
            get_checkpoint_rpc.set_deadline(deadline);

            RETURN_NOT_OK(cdc_proxy_->GetCheckpoint(
                get_checkpoint_req, &get_checkpoint_resp, &get_checkpoint_rpc));

            if (get_checkpoint_resp.has_error() &&
                (get_checkpoint_resp.error().code() != CDCErrorPB::TABLET_NOT_FOUND &&
                 get_checkpoint_resp.error().code() != CDCErrorPB::LEADER_NOT_READY)) {
              return STATUS_FORMAT(
                  InternalError, "Response had error: $0", get_checkpoint_resp.DebugString());
            }

            if (!get_checkpoint_resp.has_error()) {
              op_ids.push_back(OpId::FromPB(get_checkpoint_resp.checkpoint().op_id()));
              return true;
            }

            return false;
          },
          MonoDelta::FromSeconds(kRpcTimeout), "Get CDC Checkpoint timed out "));
    }

    return op_ids;
  }

  Result<GetCheckpointResponsePB> GetCDCSnapshotCheckpoint(
      const CDCStreamId& stream_id, const TabletId& tablet_id, const TableId& table_id = "") {
    RpcController get_checkpoint_rpc;
    GetCheckpointRequestPB get_checkpoint_req;
    GetCheckpointResponsePB get_checkpoint_resp;
    auto deadline = CoarseMonoClock::now() + test_client()->default_rpc_timeout();
    get_checkpoint_rpc.set_deadline(deadline);
    get_checkpoint_req.set_stream_id(stream_id);

    if (!table_id.empty()) {
      get_checkpoint_req.set_table_id(table_id);
    }

    get_checkpoint_req.set_tablet_id(tablet_id);
    RETURN_NOT_OK(
        cdc_proxy_->GetCheckpoint(get_checkpoint_req, &get_checkpoint_resp, &get_checkpoint_rpc));

    return get_checkpoint_resp;
  }

  Result<GetTabletListToPollForCDCResponsePB> GetTabletListToPollForCDC(
      const CDCStreamId& stream_id, const TableId& table_id, const TabletId& tablet_id = "") {
    RpcController rpc;
    GetTabletListToPollForCDCRequestPB get_tablet_list_req;
    GetTabletListToPollForCDCResponsePB get_tablet_list_resp;
    auto deadline = CoarseMonoClock::now() + test_client()->default_rpc_timeout();
    rpc.set_deadline(deadline);

    TableInfo table_info;
    table_info.set_table_id(table_id);
    table_info.set_stream_id(stream_id);

    get_tablet_list_req.mutable_table_info()->set_table_id(table_id);
    get_tablet_list_req.mutable_table_info()->set_stream_id(stream_id);
    get_tablet_list_req.set_tablet_id(tablet_id);

    RETURN_NOT_OK(
        cdc_proxy_->GetTabletListToPollForCDC(get_tablet_list_req, &get_tablet_list_resp, &rpc));

    return get_tablet_list_resp;
  }

  void AssertKeyValue(
      const CDCSDKProtoRecordPB& record, const int32_t& key, const int32_t& value,
      const bool& validate_third_column = false, const int32_t& value2 = 0) {
    ASSERT_EQ(key, record.row_message().new_tuple(0).datum_int32());
    if (value != INT_MAX) {
      for (int index = 0; index < record.row_message().new_tuple_size(); ++index) {
        if (record.row_message().new_tuple(index).column_name() == kValueColumnName) {
          ASSERT_EQ(value, record.row_message().new_tuple(index).datum_int32());
        }
      }
    }
    if (validate_third_column && value2 != INT_MAX) {
      for (int index = 0; index < record.row_message().new_tuple_size(); ++index) {
        if (record.row_message().new_tuple(index).column_name() == kValueColumnName) {
          ASSERT_EQ(value, record.row_message().new_tuple(index).datum_int32());
        }
        if (record.row_message().new_tuple(index).column_name() == kValue2ColumnName) {
          ASSERT_EQ(value2, record.row_message().new_tuple(index).datum_int32());
        }
      }
    }
  }

  void AssertBeforeImageKeyValue(
      const CDCSDKProtoRecordPB& record, const int32_t& key, const int32_t& value,
      const bool& validate_third_column = false, const int32_t& value2 = 0) {
    if (record.row_message().old_tuple_size() > 0) {
      ASSERT_EQ(key, record.row_message().old_tuple(0).datum_int32());
    }
    if (value != INT_MAX) {
      ASSERT_EQ(value, record.row_message().old_tuple(1).datum_int32());
    }
    if (validate_third_column && value2 != INT_MAX) {
      if (value == INT_MAX) {
        ASSERT_EQ(value2, record.row_message().old_tuple(1).datum_int32());
      } else {
        ASSERT_EQ(value2, record.row_message().old_tuple(2).datum_int32());
      }
    }
  }

  void AssertKeyValues(
      const CDCSDKProtoRecordPB& record, const int32_t& key,
      const vector<std::pair<std::string, uint32_t>>& col_val_vec) {
    uint32_t iter = 1;
    ASSERT_EQ(key, record.row_message().new_tuple(0).datum_int32());
    for (auto vec_iter = col_val_vec.begin(); vec_iter != col_val_vec.end(); ++iter, ++vec_iter) {
      ASSERT_EQ(vec_iter->second, record.row_message().new_tuple(iter).datum_int32());
    }
  }

  void EnableCDCServiceInAllTserver(uint32_t num_tservers) {
    for (uint32_t i = 0; i < num_tservers; ++i) {
      const auto& tserver = test_cluster()->mini_tablet_server(i)->server();
      auto cdc_service = dynamic_cast<CDCServiceImpl*>(
          tserver->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());
      cdc_service->SetCDCServiceEnabled();
    }
  }

  int FindTserversWithCacheHit(
      const CDCStreamId stream_id, const TabletId tablet_id, uint32_t num_tservers) {
    int count = 0;
    // check the CDC Service Cache of all the tservers.
    for (uint32_t i = 0; i < num_tservers; ++i) {
      const auto& tserver = test_cluster()->mini_tablet_server(i)->server();
      auto cdc_service = dynamic_cast<CDCServiceImpl*>(
          tserver->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());
      auto status = cdc_service->TEST_GetTabletInfoFromCache({"" /* UUID */, stream_id, tablet_id});
      if (status.ok()) {
        count += 1;
      }
    }
    return count;
  }

  void CheckRecord(
      const CDCSDKProtoRecordPB& record, CDCSDKYsqlTest::ExpectedRecord expected_records,
      uint32_t* count, const bool& validate_old_tuple = false,
      CDCSDKYsqlTest::ExpectedRecord expected_before_image_records = {}) {
    // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
    switch (record.row_message().op()) {
      case RowMessage::DDL: {
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[0]++;
      } break;
      case RowMessage::INSERT: {
        AssertKeyValue(record, expected_records.key, expected_records.value);
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[1]++;
      } break;
      case RowMessage::UPDATE: {
        AssertKeyValue(record, expected_records.key, expected_records.value);
        if (validate_old_tuple) {
          AssertBeforeImageKeyValue(
              record, expected_before_image_records.key, expected_before_image_records.value);
        }
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[2]++;
      } break;
      case RowMessage::DELETE: {
        ASSERT_EQ(record.row_message().old_tuple(0).datum_int32(), expected_records.key);
        if (validate_old_tuple) {
          AssertBeforeImageKeyValue(
              record, expected_before_image_records.key, expected_before_image_records.value);
        }
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[3]++;
      } break;
      case RowMessage::READ: {
        AssertKeyValue(record, expected_records.key, expected_records.value);
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[4]++;
      } break;
      case RowMessage::TRUNCATE: {
        count[5]++;
      } break;
      case RowMessage::BEGIN:
        break;
      case RowMessage::COMMIT:
        break;
      default:
        ASSERT_FALSE(true);
        break;
    }
  }

  void CheckRecordWithThreeColumns(
      const CDCSDKProtoRecordPB& record,
      CDCSDKYsqlTest::ExpectedRecordWithThreeColumns expected_records, uint32_t* count,
      const bool& validate_old_tuple = false,
      CDCSDKYsqlTest::ExpectedRecordWithThreeColumns expected_before_image_records = {},
      const bool& validate_third_column = false) {
    // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
    switch (record.row_message().op()) {
      case RowMessage::DDL: {
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[0]++;
      } break;
      case RowMessage::INSERT: {
        if (validate_third_column) {
          ASSERT_EQ(record.row_message().new_tuple_size(), 3);
          ASSERT_EQ(record.row_message().old_tuple_size(), 3);
          AssertKeyValue(
              record, expected_records.key, expected_records.value, true, expected_records.value2);
        } else {
          AssertKeyValue(record, expected_records.key, expected_records.value);
        }
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[1]++;
      } break;
      case RowMessage::UPDATE: {
        if (validate_third_column) {
          ASSERT_EQ(record.row_message().new_tuple_size(), 3);
          AssertKeyValue(
              record, expected_records.key, expected_records.value, true, expected_records.value2);
          if (validate_old_tuple) {
            ASSERT_EQ(record.row_message().old_tuple_size(), 3);
            AssertBeforeImageKeyValue(
                record, expected_before_image_records.key, expected_before_image_records.value,
                true, expected_before_image_records.value2);
          }
        } else {
          AssertKeyValue(record, expected_records.key, expected_records.value);
          if (validate_old_tuple) {
            AssertBeforeImageKeyValue(
                record, expected_before_image_records.key, expected_before_image_records.value);
          }
        }
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[2]++;
      } break;
      case RowMessage::DELETE: {
        ASSERT_EQ(record.row_message().old_tuple(0).datum_int32(), expected_records.key);
        if (validate_old_tuple) {
          if (validate_third_column) {
            ASSERT_EQ(record.row_message().old_tuple_size(), 3);
            ASSERT_EQ(record.row_message().new_tuple_size(), 3);
            AssertBeforeImageKeyValue(
                record, expected_before_image_records.key, expected_before_image_records.value,
                true, expected_before_image_records.value2);
          } else {
            AssertBeforeImageKeyValue(
                record, expected_before_image_records.key, expected_before_image_records.value);
          }
        }
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[3]++;
      } break;
      case RowMessage::READ: {
        AssertKeyValue(record, expected_records.key, expected_records.value);
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[4]++;
      } break;
      case RowMessage::TRUNCATE: {
        count[5]++;
      } break;
      case RowMessage::BEGIN:
        break;
      case RowMessage::COMMIT:
        break;
      default:
        ASSERT_FALSE(true);
        break;
    }
  }

  void CheckCount(const uint32_t* expected_count, uint32_t* count) {
    for (int i = 0; i < 6; i++) {
      ASSERT_EQ(expected_count[i], count[i]);
    }
  }

  void CheckRecord(
      const CDCSDKProtoRecordPB& record, CDCSDKYsqlTest::VaryingExpectedRecord expected_records,
      uint32_t* count, uint32_t num_cols) {
    // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
    switch (record.row_message().op()) {
      case RowMessage::DDL: {
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[0]++;
      } break;
      case RowMessage::INSERT: {
        AssertKeyValues(record, expected_records.key, expected_records.val_vec);
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[1]++;
      } break;
      case RowMessage::UPDATE: {
        AssertKeyValues(record, expected_records.key, expected_records.val_vec);
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[2]++;
      } break;
      case RowMessage::DELETE: {
        ASSERT_EQ(record.row_message().old_tuple(0).datum_int32(), expected_records.key);
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[3]++;
      } break;
      case RowMessage::READ: {
        AssertKeyValues(record, expected_records.key, expected_records.val_vec);
        ASSERT_EQ(record.row_message().table(), kTableName);
        count[4]++;
      } break;
      case RowMessage::TRUNCATE: {
        count[5]++;
      } break;
      case RowMessage::BEGIN: {
        count[6]++;
      } break;
      case RowMessage::COMMIT: {
        count[7]++;
      } break;
      default:
        ASSERT_FALSE(true);
        break;
    }
  }

  Result<GetChangesResponsePB> GetChangesFromCDC(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp = nullptr,
      int tablet_idx = 0,
      int64 safe_hybrid_time = -1,
      int wal_segment_index = 0,
      const bool populate_checkpoint = true) {
    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;

    if (cp == nullptr) {
      PrepareChangeRequest(
          &change_req, stream_id, tablets, tablet_idx, 0, 0, "", 0, 0, "", safe_hybrid_time,
          wal_segment_index, populate_checkpoint);
    } else {
      PrepareChangeRequest(
          &change_req, stream_id, tablets, *cp, tablet_idx, "", safe_hybrid_time,
          wal_segment_index);
    }

    // Retry only on LeaderNotReadyToServe errors
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          RpcController get_changes_rpc;
          auto status = cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc);

          if (status.ok() && change_resp.has_error()) {
            status = StatusFromPB(change_resp.error().status());
          }

          if (status.IsLeaderNotReadyToServe()) {
            return false;
          }

          RETURN_NOT_OK(status);
          return true;
        },
        MonoDelta::FromSeconds(kRpcTimeout),
        "GetChanges timed out waiting for Leader to get ready"));

    return change_resp;
  }

  Result<GetChangesResponsePB> GetChangesFromCDC(
      const CDCStreamId& stream_id,
      const TabletId& tablet_id,
      const CDCSDKCheckpointPB* cp = nullptr,
      int tablet_idx = 0) {
    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;

    if (cp == nullptr) {
      PrepareChangeRequest(&change_req, stream_id, tablet_id, tablet_idx);
    } else {
      PrepareChangeRequest(&change_req, stream_id, tablet_id, *cp, tablet_idx);
    }

    // Retry only on LeaderNotReadyToServe errors
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          RpcController get_changes_rpc;
          auto status = cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc);

          if (status.ok() && change_resp.has_error()) {
            status = StatusFromPB(change_resp.error().status());
          }

          if (status.IsLeaderNotReadyToServe()) {
            return false;
          }

          RETURN_NOT_OK(status);
          return true;
        },
        MonoDelta::FromSeconds(kRpcTimeout),
        "GetChanges timed out waiting for Leader to get ready"));

    return change_resp;
  }

  GetAllPendingChangesResponse GetAllPendingChangesFromCdc(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp = nullptr,
      int tablet_idx = 0,
      int64 safe_hybrid_time = -1,
      int wal_segment_index = 0) {
    GetAllPendingChangesResponse resp;

    int prev_records = 0;
    CDCSDKCheckpointPB prev_checkpoint;
    int64 prev_safetime = safe_hybrid_time;
    int prev_index = wal_segment_index;
    const CDCSDKCheckpointPB* prev_checkpoint_ptr = cp;

    do {
      GetChangesResponsePB change_resp;
      auto get_changes_result = GetChangesFromCDC(
          stream_id, tablets, prev_checkpoint_ptr, tablet_idx, prev_safetime, prev_index);

      if (get_changes_result.ok()) {
        change_resp = *get_changes_result;
      } else {
        LOG(ERROR) << "Encountered error while calling GetChanges on tablet: "
                   << tablets[tablet_idx].tablet_id()
                   << ", status: " << get_changes_result.status();
        break;
      }

      for (int i = 0; i < change_resp.cdc_sdk_proto_records_size(); i++) {
        resp.records.push_back(change_resp.cdc_sdk_proto_records(i));
      }

      prev_checkpoint = change_resp.cdc_sdk_checkpoint();
      prev_checkpoint_ptr = &prev_checkpoint;
      prev_safetime = change_resp.has_safe_hybrid_time() ? change_resp.safe_hybrid_time() : -1;
      prev_index = change_resp.wal_segment_index();
      prev_records = change_resp.cdc_sdk_proto_records_size();
    } while (prev_records != 0);

    resp.checkpoint = prev_checkpoint;
    resp.safe_hybrid_time = prev_safetime;
    return resp;
  }

  GetAllPendingChangesResponse GetAllPendingChangesWithRandomReqSafeTimeChanges(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp = nullptr,
      int tablet_idx = 0,
      int64 safe_hybrid_time = -1,
      int wal_segment_index = 0) {
    unsigned int seed = SeedRandom();
    GetAllPendingChangesResponse resp;

    size_t prev_records_size = 0;
    CDCSDKCheckpointPB prev_checkpoint;
    int64 prev_safetime = safe_hybrid_time;
    int prev_index = wal_segment_index;
    const CDCSDKCheckpointPB* prev_checkpoint_ptr = cp;

    bool reset_req_checkpoint = false;
    do {
      GetChangesResponsePB change_resp;

      auto get_changes_result = GetChangesFromCDC(
          stream_id, tablets, prev_checkpoint_ptr, tablet_idx, prev_safetime, prev_index);

      if (get_changes_result.ok()) {
        change_resp = *get_changes_result;
      } else {
        LOG(ERROR) << "Encountered error while calling GetChanges on tablet: "
                   << tablets[tablet_idx].tablet_id()
                   << ", status: " << get_changes_result.status();
        break;
      }

      prev_records_size = change_resp.cdc_sdk_proto_records_size();

      if (reset_req_checkpoint && change_resp.cdc_sdk_proto_records_size() != 0) {
        // Don't change the prev_checkpoint, resue the same from the last GetChanges call.
        int random_index = rand_r(&seed) % change_resp.cdc_sdk_proto_records_size();

        prev_safetime =
            change_resp.cdc_sdk_proto_records().Get(random_index).row_message().commit_time() - 1;
        prev_index = 0;

        // We will only copy the records upto and including the 'random_index', since the rest of
        // the records should come up in the next GetChanges response.
        for (int i = 0; i <= random_index; ++i) {
          resp.records.push_back(change_resp.cdc_sdk_proto_records(i));
        }
      } else {
        prev_checkpoint = change_resp.cdc_sdk_checkpoint();
        prev_safetime = change_resp.has_safe_hybrid_time() ? change_resp.safe_hybrid_time() : -1;
        prev_index = change_resp.wal_segment_index();

        for (int i = 0; i < change_resp.cdc_sdk_proto_records_size(); ++i) {
          resp.records.push_back(change_resp.cdc_sdk_proto_records(i));
        }
      }

      prev_checkpoint_ptr = &prev_checkpoint;

      // flip the flag every iteration.
      reset_req_checkpoint = !reset_req_checkpoint;
    } while (prev_records_size != 0);

    return resp;
  }

  Result<GetChangesResponsePB> GetChangesFromCDCWithExplictCheckpoint(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp = nullptr,
      int tablet_idx = 0) {
    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;

    if (cp == nullptr) {
      PrepareChangeRequest(&change_req, stream_id, tablets, tablet_idx);
    } else {
      PrepareChangeRequestWithExplicitCheckpoint(&change_req, stream_id, tablets, *cp, tablet_idx);
    }

    // Retry only on LeaderNotReadyToServe errors
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          RpcController get_changes_rpc;
          auto status = cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc);

          if (status.ok() && change_resp.has_error()) {
            status = StatusFromPB(change_resp.error().status());
          }

          if (status.IsLeaderNotReadyToServe()) {
            return false;
          }

          RETURN_NOT_OK(status);
          return true;
        },
        MonoDelta::FromSeconds(kRpcTimeout),
        "GetChanges timed out waiting for Leader to get ready"));

    return change_resp;
  }

  bool DeleteCDCStream(const std::string& db_stream_id) {
    RpcController delete_rpc;
    delete_rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    DeleteCDCStreamRequestPB delete_req;
    DeleteCDCStreamResponsePB delete_resp;
    delete_req.add_stream_id(db_stream_id);

    // The following line assumes that cdc_proxy_ has been initialized in the test already
    auto result = cdc_proxy_->DeleteCDCStream(delete_req, &delete_resp, &delete_rpc);
    return result.ok() && !delete_resp.has_error();
  }

  Result<GetChangesResponsePB> GetChangesFromCDCSnapshot(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const TableId& colocated_table_id = "") {
    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;
    PrepareChangeRequest(&change_req, stream_id, tablets, 0, 0, 0, "", -1, 0, colocated_table_id);
    RpcController get_changes_rpc;
    RETURN_NOT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));

    if (change_resp.has_error()) {
      return StatusFromPB(change_resp.error().status());
    }
    return change_resp;
  }

  void TestGetChanges(
      const uint32_t replication_factor, bool add_tables_without_primary_key = false) {
    ASSERT_OK(SetUpWithParams(replication_factor, 1, false));

    if (add_tables_without_primary_key) {
      // Adding tables without primary keys, they should not disturb any CDC related processes.
      std::string tables_wo_pk[] = {"table_wo_pk_1", "table_wo_pk_2", "table_wo_pk_3"};
      for (const auto& table_name : tables_wo_pk) {
        auto temp = ASSERT_RESULT(
            CreateTable(&test_cluster_, kNamespaceName, table_name, 1 /* num_tablets */, false));
      }
    }

    auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(
        table, 0, &tablets,
        /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), 1);

    std::string table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream());

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());
    ASSERT_OK(WriteRows(0 /* start */, 1 /* end */, &test_cluster_));

    const uint32_t expected_records_size = 1;
    int expected_record[] = {0 /* key */, 1 /* value */};

    SleepFor(MonoDelta::FromSeconds(5));
    GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    uint32_t ins_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      if (change_resp.cdc_sdk_proto_records(i).row_message().op() == RowMessage::INSERT) {
        const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
        AssertKeyValue(record, expected_record[0], expected_record[1]);
        ++ins_count;
      }
    }
    LOG(INFO) << "Got " << ins_count << " insert records";
    ASSERT_EQ(expected_records_size, ins_count);
  }

  void TestIntentGarbageCollectionFlag(
      const uint32_t num_tservers,
      const bool set_flag_to_a_smaller_value,
      const uint32_t cdc_intent_retention_ms,
      const bool extend_expiration = false) {
    if (set_flag_to_a_smaller_value) {
      FLAGS_cdc_intent_retention_ms = cdc_intent_retention_ms;
    }
    FLAGS_enable_update_local_peer_min_index = false;
    FLAGS_update_min_cdc_indices_interval_secs = 1;

    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));

    auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(
        test_client()->GetTablets(table, 0, &tablets, /* partition_list_version = */ nullptr));

    TabletId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT));
    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    EnableCDCServiceInAllTserver(num_tservers);

    // Call GetChanges once to set the initial value in the cdc_state table.
    GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

    // This will write one row with PK = 0.
    ASSERT_OK(WriteRows(0 /* start */, 1 /* end */, &test_cluster_));

    // Count intents here, they should be 0 here.
    for (uint32_t i = 0; i < num_tservers; ++i) {
      int64 intents_count = 0;
      ASSERT_OK(GetIntentCounts(i, &intents_count));
      ASSERT_EQ(0, intents_count);
    }

    ASSERT_OK(WriteRowsHelper(1, 2, &test_cluster_, true));
    // Sleep for 60s for the background thread to update the consumer op_id so that garbage
    // collection can happen.
    vector<int64> intent_counts(num_tservers, -1);
    ASSERT_OK(WaitFor(
        [this, &num_tservers, &set_flag_to_a_smaller_value, &extend_expiration, &intent_counts,
         &stream_id, &tablets]() -> Result<bool> {
          uint32_t i = 0;
          while (i < num_tservers) {
            if (extend_expiration) {
              // Call GetChanges once to set the initial value in the cdc_state table.
              auto result = GetChangesFromCDC(stream_id, tablets);
              if (!result.ok()) {
                return false;
              }
              yb::cdc::GetChangesResponsePB change_resp = *result;
              if (change_resp.has_error()) {
                return false;
              }
            }

            auto status = GetIntentCounts(i, &intent_counts[i]);
            if (!status.ok()) {
              continue;
            }

            if (set_flag_to_a_smaller_value && !extend_expiration) {
              if (intent_counts[i] != 0) {
                continue;
              }
            }
            i++;
          }
          return true;
        },
        MonoDelta::FromSeconds(60), "Waiting for all the tservers intent counts"));

    for (uint32_t i = 0; i < num_tservers; ++i) {
      if (set_flag_to_a_smaller_value && !extend_expiration) {
        ASSERT_EQ(intent_counts[i], 0);
      } else {
        ASSERT_GE(intent_counts[i], 0);
      }
    }

    // After time expired insert few more records
    if (set_flag_to_a_smaller_value && extend_expiration) {
      ASSERT_OK(WriteRowsHelper(10, 20, &test_cluster_, true));
      ASSERT_OK(test_client()->FlushTables(
          {table.table_id()}, /* add_indexes = */ false,
          /* timeout_secs = */ 30, /* is_compaction = */ false));

      SleepFor(MonoDelta::FromMilliseconds(100));

      change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
      uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
      uint32_t insert_count = 0;
      for (uint32_t idx = 0; idx < record_size; idx++) {
        const CDCSDKProtoRecordPB& record = change_resp.cdc_sdk_proto_records(idx);
        if (record.row_message().op() == RowMessage::INSERT) {
          insert_count += 1;
        }
      }
      ASSERT_GE(insert_count, 10);
      LOG(INFO) << "Got insert record after expiration: " << insert_count;
    }
  }

  void TestSetCDCCheckpoint(const uint32_t num_tservers, bool initial_checkpoint) {
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));
    auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(
        test_client()->GetTablets(table, 0, &tablets, /* partition_list_version = */ nullptr));

    TabletId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT));
    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());
    auto checkpoints = ASSERT_RESULT(GetCDCCheckpoint(stream_id, tablets));
    for (auto op_id : checkpoints) {
      ASSERT_EQ(OpId(0, 0), op_id);
    }

    resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId(1, 3)));
    ASSERT_FALSE(resp.has_error());

    checkpoints = ASSERT_RESULT(GetCDCCheckpoint(stream_id, tablets));

    for (auto op_id : checkpoints) {
      ASSERT_EQ(OpId(1, 3), op_id);
    }

    ASSERT_NOK(SetCDCCheckpoint(stream_id, tablets, OpId(1, -3)));

    ASSERT_NOK(SetCDCCheckpoint(stream_id, tablets, OpId(-2, 1)));
  }

  Result<GetChangesResponsePB> VerifyIfDDLRecordPresent(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      bool expect_ddl_record, bool is_first_call, const CDCSDKCheckpointPB* cp = nullptr) {
    GetChangesRequestPB req;
    GetChangesResponsePB resp;

    if (cp == nullptr) {
      PrepareChangeRequest(&req, stream_id, tablets, 0);
    } else {
      PrepareChangeRequest(&req, stream_id, tablets, *cp, 0);
    }

    // The default value for need_schema_info is false.
    if (expect_ddl_record) {
      req.set_need_schema_info(true);
    }

    RpcController get_changes_rpc;
    RETURN_NOT_OK(cdc_proxy_->GetChanges(req, &resp, &get_changes_rpc));

    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    for (const auto& record : resp.cdc_sdk_proto_records()) {
      if (record.row_message().op() == RowMessage::BEGIN) {
        continue;
      }

      // If it's the first call to GetChanges, we will get a DDL record irrespective of the
      // value of need_schema_info.
      if (is_first_call || expect_ddl_record) {
        EXPECT_EQ(record.row_message().op(), RowMessage::DDL);
      } else {
        EXPECT_NE(record.row_message().op(), RowMessage::DDL);
      }
      break;
    }

    return resp;
  }

  void PollForIntentCount(
      const int64& min_expected_num_intents, const uint32_t& tserver_index,
      const IntentCountCompareOption intentCountCompareOption, int64* num_intents) {
    ASSERT_OK(WaitFor(
        [this, &num_intents, &min_expected_num_intents, &tserver_index,
         &intentCountCompareOption]() -> Result<bool> {
          auto status = GetIntentCounts(tserver_index, num_intents);
          if (!status.ok()) {
            return false;
          }

          switch (intentCountCompareOption) {
            case IntentCountCompareOption::GreaterThan:
              return (*num_intents > min_expected_num_intents);
            case IntentCountCompareOption::GreaterThanOrEqualTo:
              return (*num_intents >= min_expected_num_intents);
            case IntentCountCompareOption::EqualTo:
              return (*num_intents == min_expected_num_intents);
          }

          return false;
        },
        MonoDelta::FromSeconds(120),
        "Getting Number of intents"));
  }

  Result<GetCDCDBStreamInfoResponsePB> GetDBStreamInfo(const CDCStreamId db_stream_id) {
    GetCDCDBStreamInfoRequestPB get_req;
    GetCDCDBStreamInfoResponsePB get_resp;
    get_req.set_db_stream_id(db_stream_id);

    RpcController get_rpc;
    get_rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));
    RETURN_NOT_OK(cdc_proxy_->GetCDCDBStreamInfo(get_req, &get_resp, &get_rpc));
    return get_resp;
  }

  void VerifyTablesInStreamMetadata(
      const std::string& stream_id, const std::unordered_set<std::string>& expected_table_ids,
      const std::string& timeout_msg) {
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          auto get_resp = GetDBStreamInfo(stream_id);
          if (get_resp.ok() && !get_resp->has_error()) {
            const uint64_t table_info_size = get_resp->table_info_size();
            if (table_info_size == expected_table_ids.size()) {
              std::unordered_set<std::string> table_ids;
              for (auto entry : get_resp->table_info()) {
                table_ids.insert(entry.table_id());
              }
              if (expected_table_ids == table_ids) return true;
            }
          }
          return false;
        },
        MonoDelta::FromSeconds(60), timeout_msg));
  }

  Status ChangeLeaderOfTablet(size_t new_leader_index, const TabletId tablet_id) {
    CHECK(!FLAGS_enable_load_balancing);

    string tool_path = GetToolPath("../bin", "yb-admin");
    vector<string> argv;
    argv.push_back(tool_path);
    argv.push_back("-master_addresses");
    argv.push_back(AsString(test_cluster_.mini_cluster_->mini_master(0)->bound_rpc_addr()));
    argv.push_back("leader_stepdown");
    argv.push_back(tablet_id);
    argv.push_back(
        test_cluster()->mini_tablet_server(new_leader_index)->server()->permanent_uuid());
    RETURN_NOT_OK(Subprocess::Call(argv));

    return Status::OK();
  }

  Status CreateSnapshot(const NamespaceName& ns) {
    string tool_path = GetToolPath("../bin", "yb-admin");
    vector<string> argv;
    argv.push_back(tool_path);
    argv.push_back("-master_addresses");
    argv.push_back(AsString(test_cluster_.mini_cluster_->mini_master(0)->bound_rpc_addr()));
    argv.push_back("create_database_snapshot");
    argv.push_back(ns);
    RETURN_NOT_OK(Subprocess::Call(argv));

    return Status::OK();
  }

  int CountEntriesInDocDB(std::vector<tablet::TabletPeerPtr> peers, const std::string& table_id) {
    int count = 0;
    for (const auto& peer : peers) {
      if (peer->tablet()->metadata()->table_id() != table_id) {
        continue;
      }
      auto db = peer->tablet()->regular_db();
      rocksdb::ReadOptions read_opts;
      read_opts.query_id = rocksdb::kDefaultQueryId;
      std::unique_ptr<rocksdb::Iterator> iter(db->NewIterator(read_opts));
      std::unordered_map<std::string, std::string> keys;

      for (iter->SeekToFirst(); EXPECT_RESULT(iter->CheckedValid()); iter->Next()) {
        Slice key = iter->key();
        EXPECT_OK(DocHybridTime::DecodeFromEnd(&key));
        LOG(INFO) << "key: " << iter->key().ToDebugString()
                  << "value: " << iter->value().ToDebugString();
        ++count;
      }
    }
    return count;
  }

  Status TriggerCompaction(const TabletId tablet_id) {
    string tool_path = GetToolPath("../bin", "yb-ts-cli");
    vector<string> argv;
    argv.push_back(tool_path);
    argv.push_back("-server_address");
    argv.push_back(AsString(test_cluster_.mini_cluster_->mini_tablet_server(0)->bound_rpc_addr()));
    argv.push_back("compact_tablet");
    argv.push_back(tablet_id);
    RETURN_NOT_OK(Subprocess::Call(argv));
    return Status::OK();
  }

  Status CompactSystemTable() {
    string tool_path = GetToolPath("../bin", "yb-admin");
    vector<string> argv;
    argv.push_back(tool_path);
    argv.push_back("-master_addresses");
    argv.push_back(AsString(test_cluster_.mini_cluster_->mini_master(0)->bound_rpc_addr()));
    argv.push_back("compact_sys_catalog");
    RETURN_NOT_OK(Subprocess::Call(argv));
    return Status::OK();
  }

  void GetTabletLeaderAndAnyFollowerIndex(
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      size_t* leader_index, size_t* follower_index) {
    for (auto replica : tablets[0].replicas()) {
      for (size_t i = 0; i < test_cluster()->num_tablet_servers(); i++) {
        if (test_cluster()->mini_tablet_server(i)->server()->permanent_uuid() ==
            replica.ts_info().permanent_uuid()) {
          if (replica.role() == PeerRole::LEADER) {
            *leader_index = i;
            LOG(INFO) << "Found leader index: " << i;
          } else if (replica.role() == PeerRole::FOLLOWER) {
            *follower_index = i;
            LOG(INFO) << "Found follower index: " << i;
          }
        }
      }
    }
  }
  void CompareExpirationTime(
      const TabletId& tablet_id, const CoarseTimePoint& prev_leader_expiry_time,
      size_t current_leader_idx, bool strictly_greater_than = false) {
    ASSERT_OK(WaitFor(
        [&]() {
          CoarseTimePoint current_expiry_time;
          while (true) {
            for (auto const& peer : test_cluster()->GetTabletPeers(current_leader_idx)) {
              if (peer->tablet_id() == tablet_id) {
                current_expiry_time = peer->cdc_sdk_min_checkpoint_op_id_expiration();
                break;
              }
            }
            if (strictly_greater_than) {
              if (current_expiry_time > prev_leader_expiry_time) {
                LOG(INFO) << "The expiration time for the current LEADER is: "
                          << current_expiry_time.time_since_epoch().count()
                          << ", and the previous LEADER expiration time should be: "
                          << prev_leader_expiry_time.time_since_epoch().count();
                return true;
              }
            } else {
              if (current_expiry_time >= prev_leader_expiry_time) {
                LOG(INFO) << "The expiration time for the current LEADER is: "
                          << current_expiry_time.time_since_epoch().count()
                          << ", and the previous LEADER expiration time should be: "
                          << prev_leader_expiry_time.time_since_epoch().count();
                return true;
              }
            }
          }
          return false;
        },
        MonoDelta::FromSeconds(60), "Waiting for active time to be updated"));
  }

  Result<int64_t> GetLastActiveTimeFromCdcStateTable(
      const CDCStreamId& stream_id, const TabletId& tablet_id, client::YBClient* client) {
    auto session = client->NewSession();
    client::TableHandle table;
    const client::YBTableName cdc_state_table(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
    RETURN_NOT_OK(table.Open(cdc_state_table, client));

    auto row = VERIFY_RESULT(FetchCdcStreamInfo(
        &table, session.get(), tablet_id, stream_id, {master::kCdcData}));

    const auto& last_active_time_string =
        row.column(0).map_value().values().Get(0).string_value();

    auto last_active_time = VERIFY_RESULT(CheckedStoInt<int64_t>(last_active_time_string));
    return last_active_time;
  }

  Result<std::tuple<uint64, std::string>> GetSnapshotDetailsFromCdcStateTable(
      const CDCStreamId& stream_id, const TabletId& tablet_id, client::YBClient* client) {
    auto session = client->NewSession();
    client::TableHandle table;
    const client::YBTableName cdc_state_table(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
    RETURN_NOT_OK(table.Open(cdc_state_table, client));

    auto row = VERIFY_RESULT(FetchCdcStreamInfo(
        &table, session.get(), tablet_id, stream_id, {master::kCdcData}));

    int32_t snapshot_time_index = -1;
    int32_t snasphot_key_index = -1;
    for (int32_t i = 0; i < row.column(0).map_value().keys().size(); ++i) {
      const auto& key_pb = row.column(0).map_value().keys().Get(i);
      if (key_pb.string_value() == kCDCSDKSafeTime) {
        snapshot_time_index = i;
      } else if (key_pb.string_value() == kCDCSDKSnapshotKey) {
        snasphot_key_index = i;
      }
    }

    if (snasphot_key_index == -1) {
      return STATUS(InvalidArgument, "Did not find snapshot key details in cdc_state table");
    }

    uint64 snapshot_time = 0;
    if (snapshot_time_index != -1) {
      const auto& snapshot_time_string =
          row.column(0).map_value().values().Get(snapshot_time_index).string_value();
      snapshot_time = VERIFY_RESULT(CheckedStol<uint64>(snapshot_time_string));
    }

    std::string snapshot_key = "";
    if (snasphot_key_index != -1) {
      snapshot_key = row.column(0).map_value().values().Get(snasphot_key_index).string_value();
    }

    return std::make_pair(snapshot_time, snapshot_key);
  }

  Result<int64_t> GetSafeHybridTimeFromCdcStateTable(
      const CDCStreamId& stream_id, const TabletId& tablet_id, client::YBClient* client) {
    auto session = client->NewSession();
    client::TableHandle table;
    const client::YBTableName cdc_state_table(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
    RETURN_NOT_OK(table.Open(cdc_state_table, client));

    auto row = VERIFY_RESULT(FetchCdcStreamInfo(
        &table, session.get(), tablet_id, stream_id, {master::kCdcData}));

    const auto& safe_hybrid_time_string =
        row.column(0).map_value().values().Get(1).string_value();

    auto safe_hybrid_time = VERIFY_RESULT(CheckedStoInt<int64_t>(safe_hybrid_time_string));
    return safe_hybrid_time;
  }

  void ValidateColumnCounts(const GetChangesResponsePB& resp, uint32_t excepted_column_counts) {
    uint32_t record_size = resp.cdc_sdk_proto_records_size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = resp.cdc_sdk_proto_records(idx);
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), excepted_column_counts);
      }
    }
  }

  void ValidateInsertCounts(const GetChangesResponsePB& resp, uint32_t excepted_insert_counts) {
    uint32_t record_size = resp.cdc_sdk_proto_records_size();
    uint32_t insert_count = 0;
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = resp.cdc_sdk_proto_records(idx);
      if (record.row_message().op() == RowMessage::INSERT) {
        insert_count += 1;
      }
    }
    ASSERT_EQ(insert_count, excepted_insert_counts);
  }

  void WaitUntilSplitIsSuccesful(
      const TabletId& tablet_id, const yb::client::YBTableName& table,
      const int expected_num_tablets = 2) {
    ASSERT_OK(WaitFor(
        [this, tablet_id, &table, &expected_num_tablets]() -> Result<bool> {
          auto status = SplitTablet(tablet_id, &test_cluster_);
          if (!status.ok()) {
            return false;
          }
          SleepFor(MonoDelta::FromSeconds(10));

          google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
          status = test_client()->GetTablets(table, 0, &tablets_after_split, nullptr);
          if (!status.ok()) {
            return false;
          }

          return (tablets_after_split.size() == expected_num_tablets);
        },
        MonoDelta::FromSeconds(120), "Tabelt Split not succesful"));
  }

  void CheckTabletsInCDCStateTable(
      const std::unordered_set<TabletId> expected_tablet_ids, client::YBClient* client,
      const CDCStreamId& stream_id = "") {
    const client::YBTableName cdc_state_table(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);

    client::TableIteratorOptions options;
    options.columns = std::vector<std::string>{master::kCdcTabletId, master::kCdcStreamId};

    ASSERT_OK(WaitFor(
        [&]() {
          client::TableHandle table;
          std::unordered_set<TabletId> seen_tablet_ids;
          auto s = table.Open(cdc_state_table, client);
          if (!s.ok()) {
            return false;
          }

          uint32_t seen_rows = 0;
          for (const auto& row : client::TableRange(table, options)) {
            const auto& cur_stream_id = row.column(master::kCdcStreamIdIdx).string_value();
            if (cur_stream_id != stream_id && stream_id != "") {
              continue;
            }
            const auto& tablet_id = row.column(master::kCdcTabletIdIdx).string_value();
            seen_tablet_ids.insert(tablet_id);
            seen_rows += 1;
          }

          return (
              expected_tablet_ids == seen_tablet_ids && seen_rows == expected_tablet_ids.size());
        },
        MonoDelta::FromSeconds(60),
        "Tablets in cdc_state table associated with the stream are not the same as expected"));
  }

  Result<std::vector<TableId>> GetCDCStreamTableIds(const CDCStreamId& stream_id) {
    NamespaceId ns_id;
    std::vector<TableId> stream_table_ids;
    std::unordered_map<std::string, std::string> options;
    StreamModeTransactional transactional(false);
    RETURN_NOT_OK(test_client()->GetCDCStream(
        stream_id, &ns_id, &stream_table_ids, &options, &transactional));
    return stream_table_ids;
  }

  uint32_t GetTotalNumRecordsInTablet(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp = nullptr) {
    uint32_t total_seen_records = 0;
    GetChangesResponsePB change_resp;
    bool first_iter = true;
    while (true) {
      auto result = (first_iter)
                        ? GetChangesFromCDC(stream_id, tablets, cp)
                        : GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint());

      if (result.ok()) {
        change_resp = *result;
        if (change_resp.cdc_sdk_proto_records_size() == 0) {
          break;
        }
        total_seen_records += change_resp.cdc_sdk_proto_records_size();
        first_iter = false;
      } else {
        LOG(ERROR) << "Encountered error while calling GetChanges on tablet: "
                   << tablets[0].tablet_id();
        break;
      }
    }

    return total_seen_records;
  }

  void CDCSDKAddColumnsWithImplictTransaction(bool packed_row) {
    const int num_tservers = 3;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));

    const uint32_t num_tablets = 1;
    auto table = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, num_tablets, true, false, 0, false, "",
        "public", 4, {kValue2ColumnName, kValue3ColumnName}));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);

    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(
        1 /* start */, 10 /* end */, &test_cluster_, {kValue2ColumnName, kValue3ColumnName}));
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, {kValue4ColumnName}));
    GetChangesResponsePB change_resp;
    change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    // Number of columns for the above insert records should be 4.
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 4);
      }
      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 10);

    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);
    ASSERT_OK(WriteRows(
        11 /* start */, 21 /* end */, &test_cluster_,
        {kValue2ColumnName, kValue3ColumnName, kValue4ColumnName}));

    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 5);
      }

      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 10);
    LOG(INFO) << "Total records read by GetChanges call, after alter table: " << record_size;
  }

  void CDCSDKAddColumnsWithExplictTransaction(bool packed_row) {
    const int num_tservers = 3;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));

    const uint32_t num_tablets = 1;
    auto table = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, num_tablets, true, false, 0, false, "",
        "public", 4, {kValue2ColumnName, kValue3ColumnName}));

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);

    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    // Insert some records in transaction.
    ASSERT_OK(WriteRowsHelper(
        0 /* start */, 11 /* end */, &test_cluster_, true, 4, kTableName,
        {kValue2ColumnName, kValue3ColumnName}));
    ASSERT_OK(test_client()->FlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));

    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue4ColumnName));

    GetChangesResponsePB change_resp;
    change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    // Number of columns for the above insert records should be 4.
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 4);
      }
      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 10);

    ASSERT_OK(WriteRowsHelper(
        11 /* start */, 21 /* end */, &test_cluster_, true, 5, kTableName,
        {kValue2ColumnName, kValue3ColumnName, kValue4ColumnName}));
    ASSERT_OK(test_client()->FlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 5);
      }

      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 10);
    LOG(INFO) << "Total records read by GetChanges call, after alter table: " << record_size;
  }

  void CDCSDKDropColumnsWithRestartTServer(bool packed_row) {
    FLAGS_enable_load_balancing = false;
    const int num_tservers = 3;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));

    const uint32_t num_tablets = 1;
    auto table = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, num_tablets, true, false, 0, false, "",
        "public", 3, {kValue2ColumnName}));
    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(1 /* start */, 10 /* end */, &test_cluster_, {kValue2ColumnName}));

    ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
    ASSERT_OK(WriteRows(11 /* start */, 20 /* end */, &test_cluster_));

    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    for (int idx = 0; idx < num_tservers; idx++) {
      test_cluster()->mini_tablet_server(idx)->Shutdown();
      ASSERT_OK(test_cluster()->mini_tablet_server(idx)->Start());
      ASSERT_OK(test_cluster()->mini_tablet_server(idx)->WaitStarted());
    }

    GetChangesResponsePB change_resp;
    auto result = GetChangesFromCDC(stream_id, tablets);
    if (!result.ok()) {
      ASSERT_OK(result);
    }
    change_resp = *result;

    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 20);
    LOG(INFO) << "Total records read by GetChanges call, after alter table: " << record_size;
  }

  void CDCSDKDropColumnsWithImplictTransaction(bool packed_row) {
    const int num_tservers = 3;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));

    const uint32_t num_tablets = 1;
    auto table = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, num_tablets, true, false, 0, false, "",
        "public", 4, {kValue2ColumnName, kValue3ColumnName}));

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);

    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(
        1 /* start */, 11 /* end */, &test_cluster_, {kValue2ColumnName, kValue3ColumnName}));
    ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));

    GetChangesResponsePB change_resp;
    change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    // Number of columns for the above insert records should be 4.
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 4);
      }
      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 10);

    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);
    ASSERT_OK(WriteRows(11 /* start */, 21 /* end */, &test_cluster_, {kValue3ColumnName}));

    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
      }

      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 10);
    LOG(INFO) << "Total records read by GetChanges call, alter table: " << record_size;
  }

  void CDCSDKDropColumnsWithExplictTransaction(bool packed_row) {
    const int num_tservers = 3;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));

    const uint32_t num_tablets = 1;
    auto table = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, num_tablets, true, false, 0, false, "",
        "public", 4, {kValue2ColumnName, kValue3ColumnName}));

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);

    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    // Insert some records in transaction.
    ASSERT_OK(WriteRowsHelper(
        1 /* start */, 11 /* end */, &test_cluster_, true, 4, kTableName,
        {kValue2ColumnName, kValue3ColumnName}));
    ASSERT_OK(test_client()->FlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
    GetChangesResponsePB change_resp;
    change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    // Number of columns for the above insert records should be 4.
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 4);
      }
      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }

    ASSERT_GE(record_size, 10);
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);
    ASSERT_OK(WriteRowsHelper(
        11 /* start */, 21 /* end */, &test_cluster_, true, 3, kTableName, {kValue3ColumnName}));
    ASSERT_OK(test_client()->FlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;

      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
      }

      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 10);
    LOG(INFO) << "Total records read by GetChanges call, after alter table: " << record_size;
  }

  void CDCSDKRenameColumnsWithImplictTransaction(bool packed_row) {
    const int num_tservers = 3;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));

    const uint32_t num_tablets = 1;
    auto table = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, num_tablets, true, false, 0, false, "",
        "public", 3, {kValue2ColumnName}));

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);

    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(1 /* start */, 10 /* end */, &test_cluster_, {kValue2ColumnName}));
    ASSERT_OK(RenameColumn(
        &test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, kValue3ColumnName));
    GetChangesResponsePB change_resp;
    change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    // Number of columns for the above insert records should be 3.
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
      }
      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }

    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);
    ASSERT_OK(WriteRows(11 /* start */, 21 /* end */, &test_cluster_, {kValue3ColumnName}));

    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
      }

      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " column_name: " << record.row_message().new_tuple(jdx).column_name()
          << " column_value: " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 10);
    LOG(INFO) << "Total records read by GetChanges call, alter table: " << record_size;
  }

  void CDCSDKRenameColumnsWithExplictTransaction(bool packed_row) {
    const int num_tservers = 3;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));

    const uint32_t num_tablets = 1;
    auto table = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, num_tablets, true, false, 0, false, "",
        "public", 3, {kValue2ColumnName}));

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);

    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    // Insert some records in transaction.
    ASSERT_OK(WriteRowsHelper(
        1 /* start */, 10 /* end */, &test_cluster_, true, 3, kTableName, {kValue2ColumnName}));
    ASSERT_OK(test_client()->FlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    ASSERT_OK(RenameColumn(
        &test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, kValue3ColumnName));

    GetChangesResponsePB change_resp;
    change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    // Number of columns for the above insert records should be 3.
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
      }
      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }

    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);
    ASSERT_OK(WriteRowsHelper(
        11 /* start */, 21 /* end */, &test_cluster_, true, 3, kTableName, {kValue3ColumnName}));
    ASSERT_OK(test_client()->FlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
      }

      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " column_name: " << record.row_message().new_tuple(jdx).column_name()
          << " column_value: " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 10);
    LOG(INFO) << "Total records read by GetChanges call, alter table: " << record_size;
  }

  void CDCSDKMultipleAlterWithRestartTServer(bool packed_row) {
    const int num_tservers = 3;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));
    // create table with 3 columns
    // insert some records.
    // add column
    // insert some records.
    // remove the column
    // insert some records.
    // add column 2 columns.
    // insert some records.
    // remove the one columns
    // insert some records
    const uint32_t num_tablets = 1;
    auto table = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, num_tablets, true, false, 0, false, "",
        "public", 3, {kValue2ColumnName}));
    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);
    FLAGS_timestamp_history_retention_interval_sec = 0;

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(1 /* start */, 6 /* end */, &test_cluster_, {kValue2ColumnName}));

    // Add a column
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue3ColumnName));
    ASSERT_OK(WriteRows(
        6 /* start */, 11 /* end */, &test_cluster_, {kValue2ColumnName, kValue3ColumnName}));

    // Drop one column
    ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
    ASSERT_OK(WriteRows(11 /* start */, 16 /* end */, &test_cluster_, {kValue3ColumnName}));

    // Add the 2 columns
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue4ColumnName));
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
    ASSERT_OK(WriteRows(
        16 /* start */, 21 /* end */, &test_cluster_,
        {kValue2ColumnName, kValue3ColumnName, kValue4ColumnName}));

    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    for (int idx = 0; idx < 1; idx++) {
      test_cluster()->mini_tablet_server(idx)->Shutdown();
      ASSERT_OK(test_cluster()->mini_tablet_server(idx)->Start());
      ASSERT_OK(test_cluster()->mini_tablet_server(idx)->WaitStarted());
    }
    LOG(INFO) << "All nodes restarted";
    SleepFor(MonoDelta::FromSeconds(10));

    GetChangesResponsePB change_resp;
    auto result = GetChangesFromCDC(stream_id, tablets);
    if (!result.ok()) {
      ASSERT_OK(result);
    }
    change_resp = *result;

    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage_Op::RowMessage_Op_INSERT) {
        auto key_value = record.row_message().new_tuple(0).datum_int32();
        // key no 1 to 5 should have 3 columns.
        if (key_value >= 1 && key_value < 6) {
          ASSERT_EQ(record.row_message().new_tuple_size(), 3);
        } else if (key_value >= 6 && key_value < 11) {
          // Added a new column
          ASSERT_EQ(record.row_message().new_tuple_size(), 4);
        } else if (key_value >= 11 && key_value < 16) {
          // Dropped a column
          ASSERT_EQ(record.row_message().new_tuple_size(), 3);
        } else {
          // Added 2 new columns
          ASSERT_EQ(record.row_message().new_tuple_size(), 5);
        }
      }
      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 20);
    LOG(INFO) << "Total records read by GetChanges call, after alter table: " << record_size;
  }

  void CDCSDKMultipleAlterWithTabletLeaderSwitch(bool packed_row) {
    const int num_tservers = 3;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    FLAGS_enable_load_balancing = false;
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));
    const uint32_t num_tablets = 1;
    auto table =
        ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);
    FLAGS_timestamp_history_retention_interval_sec = 0;

    // Create CDC stream.
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    ASSERT_OK(WriteRowsHelper(1 /* start */, 11 /* end */, &test_cluster_, true));
    // Call Getchanges
    ASSERT_OK(test_client()->FlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
    // Validate the columns and insert counts.
    ValidateColumnCounts(change_resp, 2);
    ValidateInsertCounts(change_resp, 10);

    // Insert 10 more records and do the LEADERship change
    ASSERT_OK(WriteRows(11, 21, &test_cluster_));
    size_t first_leader_index = -1;
    size_t first_follower_index = -1;
    GetTabletLeaderAndAnyFollowerIndex(tablets, &first_leader_index, &first_follower_index);
    if (first_leader_index == 0) {
      // We want to avoid the scenario where the first TServer is the leader, since we want to shut
      // the leader TServer down and call GetChanges. GetChanges will be called on the cdc_proxy
      // based on the first TServer's address and we want to avoid the network issues.
      ASSERT_OK(ChangeLeaderOfTablet(first_follower_index, tablets[0].tablet_id()));
    }
    ASSERT_OK(ChangeLeaderOfTablet(first_follower_index, tablets[0].tablet_id()));
    SleepFor(MonoDelta::FromSeconds(10));

    // Call GetChanges with new LEADER.
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    // Validate the columns and insert counts.
    ValidateColumnCounts(change_resp, 2);
    ValidateInsertCounts(change_resp, 10);

    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
    ASSERT_OK(WriteRowsHelper(
        21 /* start */, 31 /* end */, &test_cluster_, true, 3, kTableName, {kValue2ColumnName}));
    ASSERT_OK(test_client()->FlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    // Validate the columns and insert counts.
    ValidateColumnCounts(change_resp, 3);
    ValidateInsertCounts(change_resp, 10);

    // Add a new column and insert few more records.
    // Do LEADERship change.
    // Call Getchanges in the new leader.
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue3ColumnName));
    ASSERT_OK(WriteRows(31, 41, &test_cluster_, {kValue2ColumnName, kValue3ColumnName}));
    GetTabletLeaderAndAnyFollowerIndex(tablets, &first_leader_index, &first_follower_index);
    if (first_leader_index == 0) {
      // We want to avoid the scenario where the first TServer is the leader, since we want to shut
      // the leader TServer down and call GetChanges. GetChanges will be called on the cdc_proxy
      // based on the first TServer's address and we want to avoid the network issues.
      ASSERT_OK(ChangeLeaderOfTablet(first_follower_index, tablets[0].tablet_id()));
    }
    ASSERT_OK(ChangeLeaderOfTablet(first_follower_index, tablets[0].tablet_id()));
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    // Validate the columns and insert counts.
    ValidateColumnCounts(change_resp, 4);
    ValidateInsertCounts(change_resp, 10);
  }

  void CDCSDKAlterWithSysCatalogCompaction(bool packed_row) {
    const int num_tservers = 1;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));
    const uint32_t num_tablets = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 0;

    auto table = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, num_tablets, true, false, 0, false, "",
        "public", 3, {kValue2ColumnName}));
    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(1 /* start */, 101 /* end */, &test_cluster_, {kValue2ColumnName}));

    // Add a column
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue3ColumnName));
    ASSERT_OK(WriteRows(
        101 /* start */, 201 /* end */, &test_cluster_, {kValue2ColumnName, kValue3ColumnName}));

    // Drop one column
    ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
    ASSERT_OK(WriteRows(201 /* start */, 301 /* end */, &test_cluster_, {kValue3ColumnName}));

    // Add the 2 columns
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue4ColumnName));
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
    ASSERT_OK(WriteRows(
        301 /* start */, 401 /* end */, &test_cluster_,
        {kValue2ColumnName, kValue3ColumnName, kValue4ColumnName}));

    test_cluster()->mini_master(0)->tablet_peer()->tablet()->TEST_ForceRocksDBCompact();

    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    GetChangesResponsePB change_resp;
    auto result = GetChangesFromCDC(stream_id, tablets);
    if (!result.ok()) {
      ASSERT_OK(result);
    }
    change_resp = *result;

    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(idx);
      std::stringstream s;
      if (record.row_message().op() == RowMessage_Op::RowMessage_Op_INSERT) {
        auto key_value = record.row_message().new_tuple(0).datum_int32();
        // key no 1 to 5 should have 3 columns.
        if (key_value >= 1 && key_value < 101) {
          ASSERT_EQ(record.row_message().new_tuple_size(), 3);
        } else if (key_value >= 101 && key_value < 201) {
          // Added a new column
          ASSERT_EQ(record.row_message().new_tuple_size(), 4);
        } else if (key_value >= 201 && key_value < 301) {
          // Dropped a column
          ASSERT_EQ(record.row_message().new_tuple_size(), 3);
        } else {
          // Added 2 new columns
          ASSERT_EQ(record.row_message().new_tuple_size(), 5);
        }
      }
      for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
        s << " " << record.row_message().new_tuple(jdx).datum_int32();
      }
      LOG(INFO) << "row: " << idx << " : " << s.str();
    }
    ASSERT_GE(record_size, 400);
    LOG(INFO) << "Total records read by GetChanges call, after alter table: " << record_size;
  }
  void CDCSDKIntentsBatchReadWithAlterAndTabletLeaderSwitch(bool packed_row) {
    const int num_tservers = 3;
    FLAGS_enable_load_balancing = false;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    FLAGS_cdc_max_stream_intent_records = 10;
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));
    const uint32_t num_tablets = 1;
    auto table =
        ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);

    // Create CDC stream.
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    ASSERT_OK(WriteRowsHelper(1 /* start */, 101 /* end */, &test_cluster_, true));
    // Call Getchanges
    ASSERT_OK(test_client()->FlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
    // Validate the columns and insert counts.
    ValidateColumnCounts(change_resp, 2);

    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
    ASSERT_OK(WriteRowsHelper(
        101 /* start */, 201 /* end */, &test_cluster_, true, 3, kTableName, {kValue2ColumnName}));
    ASSERT_OK(test_client()->FlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));

    size_t first_leader_index = -1;
    size_t first_follower_index = -1;
    GetTabletLeaderAndAnyFollowerIndex(tablets, &first_leader_index, &first_follower_index);
    if (first_leader_index == 0) {
      // We want to avoid the scenario where the first TServer is the leader, since we want to shut
      // the leader TServer down and call GetChanges. GetChanges will be called on the cdc_proxy
      // based on the first TServer's address and we want to avoid the network issues.
      ASSERT_OK(ChangeLeaderOfTablet(first_follower_index, tablets[0].tablet_id()));
    }
    ASSERT_OK(ChangeLeaderOfTablet(first_follower_index, tablets[0].tablet_id()));
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    ValidateColumnCounts(change_resp, 2);
  }

  void EnableVerboseLoggingForModule(const std::string& module, int level) {
    if (!FLAGS_vmodule.empty()) {
      FLAGS_vmodule += Format(",$0=$1", module, level);
    } else {
      FLAGS_vmodule = Format("$0=$1", module, level);
    }
  }

  Result<std::string> GetValueFromMap(const QLMapValuePB& map_value, const std::string& key) {
    for (int index = 0; index < map_value.keys_size(); ++index) {
      if (map_value.keys(index).string_value() == key) {
        return map_value.values(index).string_value();
      }
    }
    return STATUS_FORMAT(NotFound, "Key not found in the map: $0", key);
  }

  template <class T>
  Result<T> GetIntValueFromMap(const QLMapValuePB& map_value, const std::string& key) {
    auto str_value = VERIFY_RESULT(GetValueFromMap(map_value, key));

    return CheckedStol<T>(str_value);
  }

  // Read the cdc_state table
  Result<CdcStateTableRow> ReadFromCdcStateTable(
      const CDCStreamId stream_id, const std::string& tablet_id) {
    // Read the cdc_state table safe should be set to valid value.
    CdcStateTableRow expected_row;
    client::TableHandle table_handle_cdc;
    client::YBTableName cdc_state_table(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
    if (!table_handle_cdc.Open(cdc_state_table, test_client()).ok()) {
      return STATUS_FORMAT(NotFound, "Failed to open cdc_state table");
    }

    for (const auto& row : client::TableRange(table_handle_cdc)) {
      auto read_tablet_id = row.column(master::kCdcTabletIdIdx).string_value();
      auto read_stream_id = row.column(master::kCdcStreamIdIdx).string_value();
      auto read_checkpoint = row.column(master::kCdcCheckpointIdx).string_value();
      auto result = OpId::FromString(read_checkpoint);
      if (!result.ok()) {
        return STATUS_FORMAT(NotFound, "Failed to decode the checkpoint.");
      }

      HybridTime cdc_sdk_safe_time = HybridTime::kInvalid;
      int64_t last_active_time_cdc_state_table = 0;
      if (!row.column(3).IsNull()) {
        auto& map_value = row.column(3).map_value();

        auto safe_time_result = GetIntValueFromMap<uint64_t>(map_value, kCDCSDKSafeTime);
        if (safe_time_result.ok()) {
          cdc_sdk_safe_time = HybridTime::FromPB(safe_time_result.get());
        }

        auto last_active_time_result = GetIntValueFromMap<int64_t>(map_value, kCDCSDKActiveTime);
        if (last_active_time_result.ok()) {
          last_active_time_cdc_state_table = last_active_time_result.get();
        }
      }
      if (read_tablet_id == tablet_id && read_stream_id == stream_id) {
        LOG(INFO) << "Read cdc_state table with tablet_id: " << read_tablet_id
                  << " stream_id: " << read_stream_id << " checkpoint is: " << read_checkpoint
                  << " last_active_time_cdc_state_table: " << last_active_time_cdc_state_table
                  << " cdc_sdk_safe_time: " << cdc_sdk_safe_time;
        expected_row.op_id = *result;
        expected_row.cdc_sdk_safe_time = cdc_sdk_safe_time;
        expected_row.cdc_sdk_latest_active_time = last_active_time_cdc_state_table;
      }
    }
    return expected_row;
  }

  void UpdateRecordCount(const CDCSDKProtoRecordPB& record, int* record_count) {
    switch (record.row_message().op()) {
      case RowMessage::DDL: {
        record_count[0]++;
      } break;
      case RowMessage::INSERT: {
        record_count[1]++;
      } break;
      case RowMessage::UPDATE: {
        record_count[2]++;
      } break;
      case RowMessage::DELETE: {
        record_count[3]++;
      } break;
      case RowMessage::READ: {
        record_count[4]++;
      } break;
      case RowMessage::TRUNCATE: {
        record_count[5]++;
      } break;
      case RowMessage::BEGIN:
        record_count[6]++;
        break;
      case RowMessage::COMMIT:
        record_count[7]++;
        break;
      default:
        ASSERT_FALSE(true);
        break;
    }
  }

  void CheckRecordsConsistency(const std::vector<CDCSDKProtoRecordPB>& records) {
    uint64_t prev_commit_time = 0;
    uint64_t prev_record_time = 0;
    bool in_transaction = false;
    bool first_record_in_transaction = false;
    for (auto& record : records) {
      if (record.row_message().op() == RowMessage::BEGIN) {
        in_transaction = true;
        first_record_in_transaction = true;
        ASSERT_TRUE(record.row_message().commit_time() >= prev_commit_time);
        prev_commit_time = record.row_message().commit_time();
      }

      if (record.row_message().op() == RowMessage::COMMIT) {
        in_transaction = false;
        ASSERT_TRUE(record.row_message().commit_time() >= prev_commit_time);
        prev_commit_time = record.row_message().commit_time();
      }

      if (record.row_message().op() == RowMessage::INSERT ||
          record.row_message().op() == RowMessage::UPDATE ||
          record.row_message().op() == RowMessage::DELETE) {
        ASSERT_TRUE(record.row_message().commit_time() >= prev_commit_time);
        prev_commit_time = record.row_message().commit_time();

        if (in_transaction) {
          if (!first_record_in_transaction) {
            ASSERT_TRUE(record.row_message().record_time() >= prev_record_time);
          }

          first_record_in_transaction = false;
          prev_record_time = record.row_message().record_time();
        }
      }
    }
  }

  void GetRecordsAndSplitCount(
      const CDCStreamId& stream_id, const TabletId& tablet_id, const TableId& table_id,
      int* record_count, int* total_records, int* total_splits) {
    std::vector<pair<TabletId, CDCSDKCheckpointPB>> tablets;
    tablets.push_back({tablet_id, {}});

    for (size_t i = 0; i < tablets.size(); ++i) {
      TabletId tablet_id = tablets[i].first;
      CDCSDKCheckpointPB checkpoint = tablets[i].second;

      auto change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablet_id, &checkpoint));
      for (const auto& record : change_resp.cdc_sdk_proto_records()) {
        UpdateRecordCount(record, record_count);
      }
      (*total_records) += change_resp.cdc_sdk_proto_records_size();

      auto change_result_2 =
          GetChangesFromCDC(stream_id, tablet_id, &change_resp.cdc_sdk_checkpoint());
      if (!change_result_2.ok()) {
        ASSERT_TRUE(change_result_2.status().IsTabletSplit());
        (*total_splits)++;

        // Wait until the 'cdc_parent_tablet_deletion_task_' has run.
        SleepFor(MonoDelta::FromSeconds(2));

        auto get_tablets_resp =
            ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id, tablet_id));
        for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
          auto new_tablet = tablet_checkpoint_pair.tablet_locations();
          auto new_checkpoint = tablet_checkpoint_pair.cdc_sdk_checkpoint();
          tablets.push_back({new_tablet.tablet_id(), new_checkpoint});
        }
      }
    }
  }

  void PerformSingleAndMultiShardInserts(
      const int& num_batches, const int& inserts_per_batch, int apply_update_latency = 0,
      const int& start_index = 0) {
    for (int i = 0; i < num_batches; i++) {
      int multi_shard_inserts = inserts_per_batch / 2;
      int curr_start_id = start_index + i * inserts_per_batch;

      FLAGS_TEST_txn_participant_inject_latency_on_apply_update_txn_ms = apply_update_latency;
      ASSERT_OK(WriteRowsHelper(
          curr_start_id, curr_start_id + multi_shard_inserts, &test_cluster_, true));

      FLAGS_TEST_txn_participant_inject_latency_on_apply_update_txn_ms = 0;
      ASSERT_OK(WriteRows(
          curr_start_id + multi_shard_inserts, curr_start_id + inserts_per_batch, &test_cluster_));
    }
  }

  void PerformSingleAndMultiShardQueries(
      const int& num_batches, const int& queries_per_batch, const string& query,
      int apply_update_latency = 0, const int& start_index = 0) {
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
    for (int i = 0; i < num_batches; i++) {
      int multi_shard_queries = queries_per_batch / 2;
      int curr_start_id = start_index + i * queries_per_batch;

      FLAGS_TEST_txn_participant_inject_latency_on_apply_update_txn_ms = apply_update_latency;
      ASSERT_OK(conn.Execute("BEGIN"));
      for (int i = 0; i < multi_shard_queries; i++) {
        ASSERT_OK(conn.ExecuteFormat(query, curr_start_id + i + 1));
      }
      ASSERT_OK(conn.Execute("COMMIT"));

      FLAGS_TEST_txn_participant_inject_latency_on_apply_update_txn_ms = 0;
      for (int i = 0; i < (queries_per_batch - multi_shard_queries); i++) {
        ASSERT_OK(conn.ExecuteFormat(query, curr_start_id + multi_shard_queries + i + 1));
      }
    }
  }

  OpId GetHistoricalMaxOpId(
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const int& tablet_idx = 0) {
    for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
      for (const auto& peer : test_cluster()->GetTabletPeers(i)) {
        if (peer->tablet_id() == tablets[tablet_idx].tablet_id()) {
          return peer->tablet()->transaction_participant()->GetHistoricalMaxOpId();
        }
      }
    }
    return OpId::Invalid();
  }

  TableId GetColocatedTableId(const std::string& req_table_name) {
    for (const auto& peer : test_cluster()->GetTabletPeers(0)) {
      for (const auto& table_id : peer->tablet_metadata()->GetAllColocatedTables()) {
        auto table_name = peer->tablet_metadata()->table_name(table_id);
        if (table_name == req_table_name) {
          return table_id;
        }
      }
    }
    return "";
  }

  void AssertSafeTimeAsExpectedInTabletPeers(
      const TabletId& tablet_id, const HybridTime expected_safe_time) {
    for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
      for (const auto& tablet_peer : test_cluster()->GetTabletPeers(i)) {
        if (tablet_peer->tablet_id() == tablet_id) {
          ASSERT_OK(WaitFor(
              [&]() -> bool { return tablet_peer->get_cdc_sdk_safe_time() == expected_safe_time; },
              MonoDelta::FromSeconds(60), "Safe_time is not as expected."));
        }
      }
    }
  }

  Status WaitForGetChangesToFetchRecords(
      GetChangesResponsePB* get_changes_resp, const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const int& expected_count, const CDCSDKCheckpointPB* cp = nullptr, const int& tablet_idx = 0,
      const int64& safe_hybrid_time = -1, const int& wal_segment_index = 0,
      const double& timeout_secs = 5) {
    int actual_count = 0;
    return WaitFor(
        [&]() -> Result<bool> {
          auto get_changes_resp_result = GetChangesFromCDC(
              stream_id, tablets, cp, tablet_idx, safe_hybrid_time, wal_segment_index);
          if (get_changes_resp_result.ok()) {
            *get_changes_resp = (*get_changes_resp_result);
            for (const auto& record : get_changes_resp->cdc_sdk_proto_records()) {
              if (record.row_message().op() == RowMessage::INSERT ||
                  record.row_message().op() == RowMessage::UPDATE ||
                  record.row_message().op() == RowMessage::DELETE) {
                actual_count += 1;
              }
            }
          }
          return actual_count == expected_count;
        },
        MonoDelta::FromSeconds(timeout_secs),
        "Waiting for GetChanges to fetch: " + std::to_string(expected_count) + " records");
  }

  Status XreplValidateSplitCandidateTable(const TableId& table_id) {
    auto& cm = test_cluster_.mini_cluster_->mini_master()->catalog_manager_impl();
    auto table = cm.GetTableInfo(table_id);
    return cm.XreplValidateSplitCandidateTable(*table);
  }
};

}  // namespace cdc
}  // namespace yb
