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

#include "yb/integration-tests/cdcsdk_ysql_test_base.h"
#include <cstddef>
#include <map>
#include <vector>
#include <gtest/gtest.h>

#include "yb/cdc/xrepl_types.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_state_table.h"

#include "yb/client/yb_table_name.h"
#include "yb/common/entity_ids_types.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/status.h"

DECLARE_bool(cdc_write_post_apply_metadata);

namespace yb {
namespace cdc {
Result<string> CDCSDKYsqlTest::GetUniverseId(PostgresMiniCluster* cluster) {
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

  void CDCSDKYsqlTest::VerifyCdcStateMatches(
      client::YBClient* client, const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      const uint64_t term, const uint64_t index) {
    CDCStateTable cdc_state_table(client);

    auto row = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
        {tablet_id, stream_id}, CDCStateTableEntrySelector().IncludeCheckpoint()));
    ASSERT_TRUE(row);

    LOG(INFO) << Format(
        "Verifying tablet: $0, stream: $1, op_id: $2", tablet_id, stream_id,
        OpId(term, index).ToString());

    OpId op_id = *row->checkpoint;

    ASSERT_EQ(op_id.term, term);
    ASSERT_EQ(op_id.index, index);
  }

  Status CDCSDKYsqlTest::WriteRowsToTwoTables(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, bool flag,
      const char* const first_table_name, const char* const second_table_name, uint32_t num_cols) {
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

  void CDCSDKYsqlTest::VerifyStreamDeletedFromCdcState(
      client::YBClient* client, const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      int timeout_secs) {
    CDCStateTable cdc_state_table(client);

    // The deletion of cdc_state rows for the specified stream happen in an asynchronous thread,
    // so even if the request has returned, it doesn't mean that the rows have been deleted yet.
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          auto row = VERIFY_RESULT(cdc_state_table.TryFetchEntry({tablet_id, stream_id}));
          return !row;
        },
        MonoDelta::FromSeconds(timeout_secs),
        "Failed to delete stream rows from cdc_state table."));
  }

  Result<OpId> CDCSDKYsqlTest::GetStreamCheckpointInCdcState(
      client::YBClient* client, const xrepl::StreamId& stream_id, const TabletId& tablet_id) {
    CDCStateTable cdc_state_table(client);
    auto row = VERIFY_RESULT(cdc_state_table.TryFetchEntry(
        {tablet_id, stream_id}, CDCStateTableEntrySelector().IncludeCheckpoint()));
    SCHECK(row, IllegalState, "Row not found in cdc_state table");

    return *row->checkpoint;
  }

  void CDCSDKYsqlTest::VerifyStreamCheckpointInCdcState(
      client::YBClient* client, const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      OpIdExpectedValue op_id_expected_value, int timeout_secs) {
    CDCStateTable cdc_state_table(client);

    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          auto row = VERIFY_RESULT(cdc_state_table.TryFetchEntry(
              {tablet_id, stream_id}, CDCStateTableEntrySelector().IncludeCheckpoint()));
          if (!row) {
            return false;
          }

          SCHECK(
              row->checkpoint, IllegalState, "Checkpoint not set in cdc_state table row: $0",
              row->ToString());
          auto& op_id = *row->checkpoint;

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

  void CDCSDKYsqlTest::VerifyTransactionParticipant(const TabletId& tablet_id, const OpId& opid) {
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

  Status CDCSDKYsqlTest::DropDB(PostgresMiniCluster* cluster) {
    const std::string db_name = "testdatabase";
    RETURN_NOT_OK(CreateDatabase(&test_cluster_, db_name, true));
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(db_name));
    RETURN_NOT_OK(conn.ExecuteFormat("DROP DATABASE $0", kNamespaceName));
    return Status::OK();
  }

  Status CDCSDKYsqlTest::TruncateTable(
      PostgresMiniCluster* cluster, const std::vector<string>& table_ids) {
    RETURN_NOT_OK(cluster->client_->TruncateTables(table_ids));
    return Status::OK();
  }

  // The range is exclusive of end i.e. [start, end)
  Status CDCSDKYsqlTest::WriteRows(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster,
      const vector<string>& optional_cols_name, pgwrapper::PGConn* conn) {
    if (conn == nullptr) {
      auto conn_obj = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
      return WriteRowsWithConn(start, end, cluster, &conn_obj, optional_cols_name);
    } else {
      return WriteRowsWithConn(start, end, cluster, conn, optional_cols_name);
    }
  }

  Status CDCSDKYsqlTest::WriteRowsWithConn(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster,
      pgwrapper::PGConn* conn, const vector<string>& optional_cols_name) {
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
        RETURN_NOT_OK(conn->ExecuteFormat(
            "INSERT INTO $0 $1 VALUES $2", kTableName, columns_name.str(), columns_value.str()));
      } else {
        RETURN_NOT_OK(conn->ExecuteFormat(
            "INSERT INTO $0($1, $2) VALUES ($3, $4)", kTableName, kKeyColumnName, kValueColumnName,
            i, i + 1));
      }
    }
    int retry_count = 1;
    return WaitFor(
        [&]() -> Result<bool> {
          LOG(INFO) << "Retry : " << retry_count++;
          auto rows = VERIFY_RESULT((conn->FetchRows<int32_t, int32_t>(Format(
              "SELECT $1, $2 FROM $0 WHERE $1 >= $3 AND $1 < $4", kTableName, kKeyColumnName,
              kValueColumnName, start, end))));

          bool write_completed = true;
          if (rows.size() != (end - start)) {
            write_completed = false;
          }
          return write_completed;
        },
        MonoDelta::FromSeconds(60), "Waiting for write to be completed");
  }

  // The range is exclusive of end i.e. [start, end)
  Status CDCSDKYsqlTest::WriteRows(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, uint32_t num_cols,
      std::string table_name) {
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
      RETURN_NOT_OK(conn.ExecuteFormat(statement, table_name));
    }
    return Status::OK();
  }

  void CDCSDKYsqlTest::DropTable(PostgresMiniCluster* cluster, const char* table_name) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(kNamespaceName));
    ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table_name));
  }

  Status CDCSDKYsqlTest::WriteRowsHelper(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, bool flag, uint32_t num_cols,
      const char* const table_name, const vector<string>& optional_cols_name,
      const bool transaction_enabled, pgwrapper::PGConn* conn) {
    if (conn == nullptr) {
      auto conn_obj = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
      return WriteRowsHelperWithConn(
          start, end, cluster, flag, &conn_obj, num_cols, table_name, optional_cols_name,
          transaction_enabled);
    } else {
      return WriteRowsHelperWithConn(
          start, end, cluster, flag, conn, num_cols, table_name, optional_cols_name,
          transaction_enabled);
    }
  }

  Status CDCSDKYsqlTest::WriteRowsHelperWithConn(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, bool flag,
      pgwrapper::PGConn* conn, uint32_t num_cols, const char* const table_name,
      const vector<string>& optional_cols_name, const bool transaction_enabled) {
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    if (transaction_enabled) {
      RETURN_NOT_OK(conn->Execute("BEGIN"));
    }

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
        RETURN_NOT_OK(conn->ExecuteFormat(
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
        RETURN_NOT_OK(conn->ExecuteFormat(statement, table_name));
      }
    }

    if (transaction_enabled) {
      if (flag) {
        RETURN_NOT_OK(conn->Execute("COMMIT"));
      } else {
        RETURN_NOT_OK(conn->Execute("ABORT"));
      }
    }

    return Status::OK();
  }

  Status CDCSDKYsqlTest::CreateTableWithoutPK(PostgresMiniCluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLE test1_no_pk(id1 int, id2 int)"));
    return Status::OK();
  }

  Status CDCSDKYsqlTest::WriteAndUpdateRowsHelper(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, bool flag,
      const std::multimap<uint32_t, uint32_t>& col_val_map, const std::string& table_id) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    for (uint32_t i = start; i < end; ++i) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "INSERT INTO $0($1, $2) VALUES ($3, $4)", kTableName, kKeyColumnName, kValueColumnName, i,
          i + 1));
    }
    RETURN_NOT_OK(WaitForFlushTables(
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
    RETURN_NOT_OK(WaitForFlushTables(
        {table_id}, /* add_indexes = */ false,
        /* timeout_secs = */ 30, /* is_compaction = */ false));

    if (flag) {
      RETURN_NOT_OK(conn.Execute("COMMIT"));
    } else {
      RETURN_NOT_OK(conn.Execute("ABORT"));
    }
    return Status::OK();
  }

  Status CDCSDKYsqlTest::CreateColocatedObjects(PostgresMiniCluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLEGROUP tg1"));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLE test1(id1 int primary key) TABLEGROUP tg1;"));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLE test2(id2 text primary key) TABLEGROUP tg1;"));
    return Status::OK();
  }

  Status CDCSDKYsqlTest::AddColocatedTable(
      PostgresMiniCluster* cluster, const TableName& table_name,
      const std::string& table_group_name) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0(id2 text primary key) TABLEGROUP $1;", table_name, table_group_name));
    return Status::OK();
  }

  Status CDCSDKYsqlTest::PopulateColocatedData(
      PostgresMiniCluster* cluster, int insert_count, bool transaction) {
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

  Status CDCSDKYsqlTest::WriteEnumsRows(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, const string& enum_suffix,
      string database_name, string table_name, string schema_name) {
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

  Result<YBTableName> CDCSDKYsqlTest::CreateCompositeTable(
      PostgresMiniCluster* cluster, const uint32_t num_tablets, const std::string& type_suffix) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TYPE composite_name$0 AS (first text, last text);", type_suffix));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE emp(id int primary key, name composite_name) "
        "SPLIT INTO $0 TABLETS",
        num_tablets));
    return GetTable(cluster, kNamespaceName, "emp");
  }

  Status CDCSDKYsqlTest::WriteCompositeRows(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster) {
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

  Result<YBTableName> CDCSDKYsqlTest::CreateNestedCompositeTable(
      PostgresMiniCluster* cluster, const uint32_t num_tablets, const std::string& type_suffix) {
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

  Status CDCSDKYsqlTest::WriteNestedCompositeRows(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster) {
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

  Result<YBTableName> CDCSDKYsqlTest::CreateArrayCompositeTable(
      PostgresMiniCluster* cluster, const uint32_t num_tablets, const std::string& type_suffix) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));

    RETURN_NOT_OK(
        conn.ExecuteFormat("CREATE TYPE emp_data$0 AS (name text[], phone int[]);", type_suffix));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE emp_array(id int primary key, data emp_data$0) "
        "SPLIT INTO $1 TABLETS",
        type_suffix, num_tablets));
    return GetTable(cluster, kNamespaceName, "emp_array");
  }

  Status CDCSDKYsqlTest::WriteArrayCompositeRows(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster) {
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

  Result<YBTableName> CDCSDKYsqlTest::CreateRangeCompositeTable(
      PostgresMiniCluster* cluster, const uint32_t num_tablets, const std::string& type_suffix) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE type range_composite$0 AS (r1 numrange, r2 int4range);", type_suffix));

    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE range_composite_table(id int primary key, data range_composite$0) "
        "SPLIT INTO $1 TABLETS",
        type_suffix, num_tablets));
    return GetTable(cluster, kNamespaceName, "range_composite_table");
  }

  Status CDCSDKYsqlTest::WriteRangeCompositeRows(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster) {
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

  Result<YBTableName> CDCSDKYsqlTest::CreateRangeArrayCompositeTable(
      PostgresMiniCluster* cluster, const uint32_t num_tablets, const std::string& type_suffix) {
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

  Status CDCSDKYsqlTest::WriteRangeArrayCompositeRows(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster) {
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

  Status CDCSDKYsqlTest::UpdateRows(
      uint32_t key, uint32_t value, PostgresMiniCluster* cluster, std::string table_name) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Updating row for key " << key << " with value " << value;
    RETURN_NOT_OK(conn.ExecuteFormat(
        "UPDATE $0 SET $1 = $2 WHERE $3 = $4", table_name, kValueColumnName, value, kKeyColumnName,
        key));
    return Status::OK();
  }

  Status CDCSDKYsqlTest::UpdatePrimaryKey(
      uint32_t key, uint32_t value, PostgresMiniCluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Updating primary key " << key << " with value " << value;
    RETURN_NOT_OK(conn.ExecuteFormat(
        "UPDATE $0 SET $1 = $2 WHERE $3 = $4", kTableName, kKeyColumnName, value, kKeyColumnName,
        key));
    return Status::OK();
  }

  Status CDCSDKYsqlTest::UpdateRows(
      uint32_t key, const std::map<std::string, uint32_t>& col_val_map,
      PostgresMiniCluster* cluster) {
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

  Status CDCSDKYsqlTest::UpdateRowsHelper(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, bool flag, uint32_t key,
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

  Status CDCSDKYsqlTest::UpdateDeleteRowsHelper(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, bool flag, uint32_t key,
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

  Status CDCSDKYsqlTest::DeleteRows(
      uint32_t key, PostgresMiniCluster* cluster, std::string table_name) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Deleting row for key " << key;
    RETURN_NOT_OK(
        conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = $2", table_name, kKeyColumnName, key));
    return Status::OK();
  }

  Status CDCSDKYsqlTest::SplitTablet(const TabletId& tablet_id, PostgresMiniCluster* cluster) {
    yb::master::SplitTabletRequestPB req;
    req.set_tablet_id(tablet_id);
    yb::master::SplitTabletResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(30.0) * kTimeMultiplier);
    auto& cm = cluster->mini_cluster_->mini_master()->catalog_manager();
    RETURN_NOT_OK(cm.SplitTablet(
        tablet_id, master::ManualSplit::kTrue,
        cm.GetLeaderEpochInternal()));

    if (resp.has_error()) {
      RETURN_NOT_OK(StatusFromPB(resp.error().status()));
    }
    return Status::OK();
  }

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>>
    CDCSDKYsqlTest::SetUpCluster() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_single_record_update) = false;
    RETURN_NOT_OK(SetUpWithParams(3, 1, false));
    auto table = EXPECT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
    return tablets;
  }

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>>
  CDCSDKYsqlTest::SetUpClusterMultiColumnUsecase(uint32_t num_cols) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_single_record_update) = true;
    RETURN_NOT_OK(SetUpWithParams(3, 1, false));
    auto table = EXPECT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, 1, true, false, 0, false, "", "public",
        num_cols));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
    return tablets;
  }

  Result<GetChangesResponsePB> CDCSDKYsqlTest::UpdateSnapshotDone(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const TableId table_id) {
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

  Result<GetChangesResponsePB> CDCSDKYsqlTest::UpdateCheckpoint(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const GetChangesResponsePB* change_resp,
      const TableId table_id) {

    return UpdateCheckpoint(stream_id, tablets, change_resp->cdc_sdk_checkpoint(), table_id);
  }

  Result<GetChangesResponsePB> CDCSDKYsqlTest::UpdateCheckpoint(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB& resp_checkpoint,
      const TableId table_id) {
    GetChangesRequestPB change_req2;
    GetChangesResponsePB change_resp2;
    PrepareChangeRequest(
        &change_req2, stream_id, tablets, 0, resp_checkpoint.index(),
        resp_checkpoint.term(), resp_checkpoint.key(),
        resp_checkpoint.write_id(),
        resp_checkpoint.snapshot_time(), table_id);
    RpcController get_changes_rpc;
    RETURN_NOT_OK(cdc_proxy_->GetChanges(change_req2, &change_resp2, &get_changes_rpc));
    if (change_resp2.has_error()) {
      return StatusFromPB(change_resp2.error().status());
    }

    return change_resp2;
  }

  std::unique_ptr<tserver::TabletServerAdminServiceProxy> CDCSDKYsqlTest::GetTServerAdminProxy(
      const uint32_t tserver_index) {
    auto tserver = test_cluster()->mini_tablet_server(tserver_index);
    return std::make_unique<tserver::TabletServerAdminServiceProxy>(
        &tserver->server()->proxy_cache(), HostPort::FromBoundEndpoint(tserver->bound_rpc_addr()));
  }

  Status CDCSDKYsqlTest::GetIntentCounts(const uint32_t tserver_index, int64* num_intents) {
    tserver::CountIntentsRequestPB req;
    tserver::CountIntentsResponsePB resp;
    RpcController rpc;

    auto ts_admin_service_proxy = GetTServerAdminProxy(tserver_index);
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(ts_admin_service_proxy->CountIntents(req, &resp, &rpc));
    *num_intents = resp.num_intents();
    return Status::OK();
  }

  void CDCSDKYsqlTest::PrepareChangeRequest(
      GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const int tablet_idx, int64 index, int64 term, std::string key, int32_t write_id,
      int64 snapshot_time, const TableId table_id, int64 safe_hybrid_time,
      int32_t wal_segment_index, const bool populate_checkpoint, const bool need_schema_info) {
    change_req->set_stream_id(stream_id.ToString());
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
    change_req->set_need_schema_info(need_schema_info);
  }

  void CDCSDKYsqlTest::PrepareChangeRequest(
      GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      const int tablet_idx, int64 index, int64 term, std::string key, int32_t write_id,
      int64 snapshot_time) {
    change_req->set_stream_id(stream_id.ToString());
    change_req->set_tablet_id(tablet_id);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_index(index);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_term(term);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_key(key);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(write_id);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_snapshot_time(snapshot_time);
  }

  void CDCSDKYsqlTest::PrepareChangeRequest(
      GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB& cp, const int tablet_idx, const TableId table_id,
      int64 safe_hybrid_time, int32_t wal_segment_index) {
    change_req->set_stream_id(stream_id.ToString());
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

  void CDCSDKYsqlTest::PrepareChangeRequest(
      GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      const CDCSDKCheckpointPB& cp, const int tablet_idx) {
    change_req->set_stream_id(stream_id.ToString());
    change_req->set_tablet_id(tablet_id);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_term(cp.term());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_index(cp.index());

    if (cp.has_key()) {
      change_req->mutable_from_cdc_sdk_checkpoint()->set_key(cp.key());
    }

    change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(cp.write_id());
  }

  void CDCSDKYsqlTest::PrepareChangeRequestWithExplicitCheckpoint(
      GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* from_op_id, const CDCSDKCheckpointPB* explicit_checkpoint,
      const TableId table_id, const int tablet_idx) {
    change_req->set_stream_id(stream_id.ToString());
    change_req->set_tablet_id(tablets.Get(tablet_idx).tablet_id());

    change_req->mutable_from_cdc_sdk_checkpoint()->set_term(from_op_id->term());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_index(from_op_id->index());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_key(from_op_id->key());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(from_op_id->write_id());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_snapshot_time(from_op_id->snapshot_time());

    if (explicit_checkpoint != nullptr) {
      change_req->mutable_explicit_cdc_sdk_checkpoint()->set_term(explicit_checkpoint->term());
      change_req->mutable_explicit_cdc_sdk_checkpoint()->set_index(explicit_checkpoint->index());
      change_req->mutable_explicit_cdc_sdk_checkpoint()->set_key(explicit_checkpoint->key());
      change_req->mutable_explicit_cdc_sdk_checkpoint()->set_write_id(
        explicit_checkpoint->write_id());
      change_req->mutable_explicit_cdc_sdk_checkpoint()->set_snapshot_time(
        explicit_checkpoint->snapshot_time());
    }

    if (!table_id.empty()) {
      change_req->set_table_id(table_id);
    }
  }

  void CDCSDKYsqlTest::PrepareSetCheckpointRequest(
      SetCDCCheckpointRequestPB* set_checkpoint_req,
      const xrepl::StreamId stream_id,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB>
          tablets,
      const int tablet_idx,
      const OpId& op_id,
      bool initial_checkpoint,
      const uint64_t cdc_sdk_safe_time,
      bool bootstrap) {
    set_checkpoint_req->set_stream_id(stream_id.ToString());
    set_checkpoint_req->set_initial_checkpoint(initial_checkpoint);
    set_checkpoint_req->set_cdc_sdk_safe_time(cdc_sdk_safe_time);
    set_checkpoint_req->set_bootstrap(bootstrap);
    set_checkpoint_req->set_tablet_id(tablets.Get(tablet_idx).tablet_id());
    set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_term(op_id.term);
    set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_index(op_id.index);
  }

  Result<SetCDCCheckpointResponsePB> CDCSDKYsqlTest::SetCDCCheckpoint(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const OpId& op_id, const uint64_t cdc_sdk_safe_time, bool initial_checkpoint,
      const int tablet_idx, bool bootstrap) {
    Status st;
    SetCDCCheckpointResponsePB set_checkpoint_resp_final;

    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          RpcController set_checkpoint_rpc;
          SetCDCCheckpointRequestPB set_checkpoint_req;

          SetCDCCheckpointResponsePB set_checkpoint_resp;
          auto deadline = CoarseMonoClock::now() + test_client()->default_rpc_timeout();
          set_checkpoint_rpc.set_deadline(deadline);
          PrepareSetCheckpointRequest(
              &set_checkpoint_req, stream_id, tablets, tablet_idx, op_id, initial_checkpoint,
              cdc_sdk_safe_time, bootstrap);
          st = cdc_proxy_->SetCDCCheckpoint(
              set_checkpoint_req, &set_checkpoint_resp, &set_checkpoint_rpc);

          if (set_checkpoint_resp.has_error() &&
              set_checkpoint_resp.error().code() != CDCErrorPB::TABLET_NOT_FOUND &&
              set_checkpoint_resp.error().code() != CDCErrorPB::LEADER_NOT_READY &&
              set_checkpoint_resp.error().status().message().find("TRY_AGAIN_CODE") ==
                  std::string::npos) {
            return STATUS_FORMAT(
                InternalError, "Response had error: $0", set_checkpoint_resp.DebugString());
          }
          if (st.ok() && !set_checkpoint_resp.has_error()) {
            set_checkpoint_resp_final.CopyFrom(set_checkpoint_resp);
            return true;
          }

          return false;
        },
        MonoDelta::FromSeconds(kRpcTimeout),
        "GetChanges timed out waiting for Leader to get ready"));

    return set_checkpoint_resp_final;
  }

  Result<std::vector<OpId>> CDCSDKYsqlTest::GetCDCCheckpoint(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
    GetCheckpointRequestPB get_checkpoint_req;

    std::vector<OpId> op_ids;
    op_ids.reserve(tablets.size());
    for (const auto& tablet : tablets) {
      get_checkpoint_req.set_stream_id(stream_id.ToString());
      get_checkpoint_req.set_tablet_id(tablet.tablet_id());

      RETURN_NOT_OK(WaitFor(
          [&]() -> Result<bool> {
            GetCheckpointResponsePB get_checkpoint_resp;
            RpcController get_checkpoint_rpc;
            RETURN_NOT_OK(cdc_proxy_->GetCheckpoint(
                get_checkpoint_req, &get_checkpoint_resp, &get_checkpoint_rpc));

            if (get_checkpoint_resp.has_error() &&
                get_checkpoint_resp.error().code() != CDCErrorPB::TABLET_NOT_FOUND &&
                get_checkpoint_resp.error().code() != CDCErrorPB::LEADER_NOT_READY) {
              return STATUS_FORMAT(
                  InternalError, "Response had error: $0", get_checkpoint_resp.DebugString());
            }
            if (!get_checkpoint_resp.has_error()) {
              op_ids.push_back(OpId::FromPB(get_checkpoint_resp.checkpoint().op_id()));
              return true;
            }

            return false;
          },
          MonoDelta::FromSeconds(kRpcTimeout),
          "GetChanges timed out waiting for Leader to get ready"));
    }

    return op_ids;
  }

  Result<GetCheckpointResponsePB> CDCSDKYsqlTest::GetCDCSnapshotCheckpoint(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, const TableId& table_id) {

    GetCheckpointRequestPB get_checkpoint_req;
    GetCheckpointResponsePB get_checkpoint_resp;
    auto deadline = CoarseMonoClock::now() + test_client()->default_rpc_timeout();

    get_checkpoint_req.set_stream_id(stream_id.ToString());

    if (!table_id.empty()) {
      get_checkpoint_req.set_table_id(table_id);
    }

    get_checkpoint_req.set_tablet_id(tablet_id);

    RETURN_NOT_OK(WaitFor(
          [&]() -> Result<bool> {
            RpcController get_checkpoint_rpc;
            get_checkpoint_rpc.set_deadline(deadline);

            RETURN_NOT_OK(cdc_proxy_->GetCheckpoint(
                get_checkpoint_req, &get_checkpoint_resp, &get_checkpoint_rpc));

            if (get_checkpoint_resp.has_error() &&
                get_checkpoint_resp.error().code() != CDCErrorPB::TABLET_NOT_FOUND &&
                get_checkpoint_resp.error().code() != CDCErrorPB::LEADER_NOT_READY) {
              return STATUS_FORMAT(
                  InternalError, "Response had error: $0", get_checkpoint_resp.DebugString());
            }
            if (!get_checkpoint_resp.has_error()) {
              return true;
            }

            return false;
          },
          MonoDelta::FromSeconds(kRpcTimeout),
          "GetCheckpoint timed out waiting for Leader to get ready"));

    return get_checkpoint_resp;
  }

  Result<CDCSDKCheckpointPB> CDCSDKYsqlTest::GetCDCSDKSnapshotCheckpoint(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, const TableId& table_id) {

    GetCheckpointRequestPB get_checkpoint_req;
    GetCheckpointResponsePB get_checkpoint_resp;
    auto deadline = CoarseMonoClock::now() + test_client()->default_rpc_timeout();

    get_checkpoint_req.set_stream_id(stream_id.ToString());

    if (!table_id.empty()) {
      get_checkpoint_req.set_table_id(table_id);
    }

    get_checkpoint_req.set_tablet_id(tablet_id);
    RETURN_NOT_OK(WaitFor(
          [&]() -> Result<bool> {
            RpcController get_checkpoint_rpc;
            get_checkpoint_rpc.set_deadline(deadline);

            RETURN_NOT_OK(cdc_proxy_->GetCheckpoint(
                get_checkpoint_req, &get_checkpoint_resp, &get_checkpoint_rpc));

            if (get_checkpoint_resp.has_error() &&
                get_checkpoint_resp.error().code() != CDCErrorPB::TABLET_NOT_FOUND &&
                get_checkpoint_resp.error().code() != CDCErrorPB::LEADER_NOT_READY) {
              return STATUS_FORMAT(
                  InternalError, "Response had error: $0", get_checkpoint_resp.DebugString());
            }
            if (!get_checkpoint_resp.has_error()) {
              return true;
            }

            return false;
          },
          MonoDelta::FromSeconds(kRpcTimeout),
          "GetCheckpoint timed out waiting for Leader to get ready"));;

    CDCSDKCheckpointPB checkpoint_resp;
    checkpoint_resp.set_index(get_checkpoint_resp.checkpoint().op_id().index());
    checkpoint_resp.set_term(get_checkpoint_resp.checkpoint().op_id().term());
    checkpoint_resp.set_write_id(-1);
    checkpoint_resp.set_snapshot_time(get_checkpoint_resp.snapshot_time());
    checkpoint_resp.set_key(get_checkpoint_resp.snapshot_key());

    return checkpoint_resp;
  }

  Result<GetTabletListToPollForCDCResponsePB> CDCSDKYsqlTest::GetTabletListToPollForCDC(
      const xrepl::StreamId& stream_id, const TableId& table_id, const TabletId& tablet_id) {
    RpcController rpc;
    GetTabletListToPollForCDCRequestPB get_tablet_list_req;
    GetTabletListToPollForCDCResponsePB get_tablet_list_resp;
    auto deadline = CoarseMonoClock::now() + test_client()->default_rpc_timeout();
    rpc.set_deadline(deadline);

    TableInfo table_info;
    table_info.set_table_id(table_id);
    table_info.set_stream_id(stream_id.ToString());

    get_tablet_list_req.mutable_table_info()->set_table_id(table_id);
    get_tablet_list_req.mutable_table_info()->set_stream_id(stream_id.ToString());
    get_tablet_list_req.set_tablet_id(tablet_id);

    RETURN_NOT_OK(
        cdc_proxy_->GetTabletListToPollForCDC(get_tablet_list_req, &get_tablet_list_resp, &rpc));

    return get_tablet_list_resp;
  }

  void CDCSDKYsqlTest::AssertKeyValue(
      const CDCSDKProtoRecordPB& record, const int32_t& key, const int32_t& value,
      const bool& validate_third_column, const int32_t& value2) {
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

  void CDCSDKYsqlTest::AssertKeyValue(
      const CDCSDKProtoRecordPB& record1, const CDCSDKProtoRecordPB& record2) {
    for (int index = 0; index < record1.row_message().new_tuple_size(); ++index) {
      ASSERT_EQ(
          record1.row_message().new_tuple(index).pg_ql_value().int32_value(),
          record2.row_message().new_tuple(index).pg_ql_value().int32_value());
    }
  }

  void CDCSDKYsqlTest::AssertCDCSDKProtoRecords(
      const CDCSDKProtoRecordPB& record1, const CDCSDKProtoRecordPB& record2) {
    ASSERT_EQ(record1.row_message().op(), record2.row_message().op());
    ASSERT_EQ(record1.row_message().pg_lsn(), record2.row_message().pg_lsn());
    ASSERT_EQ(record1.row_message().pg_transaction_id(), record2.row_message().pg_transaction_id());
    if (IsDMLRecord(record1) && IsDMLRecord(record2)) {
      ASSERT_EQ(record1.row_message().table_id(), record2.row_message().table_id());
      ASSERT_EQ(record1.row_message().primary_key(), record2.row_message().primary_key());
      AssertKeyValue(record1, record2);
    }

    ASSERT_EQ(record1.cdc_sdk_op_id().term(), record2.cdc_sdk_op_id().term());
    ASSERT_EQ(record1.cdc_sdk_op_id().index(), record2.cdc_sdk_op_id().index());
    ASSERT_EQ(record1.cdc_sdk_op_id().write_id(), record2.cdc_sdk_op_id().write_id());
    ASSERT_EQ(record1.cdc_sdk_op_id().write_id_key(), record2.cdc_sdk_op_id().write_id_key());
  }

  void CDCSDKYsqlTest::AssertBeforeImageKeyValue(
      const CDCSDKProtoRecordPB& record, const int32_t& key, const int32_t& value,
      const bool& validate_third_column, const int32_t& value2) {
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

  void CDCSDKYsqlTest::AssertKeyValues(
      const CDCSDKProtoRecordPB& record, const int32_t& key,
      const vector<std::pair<std::string, uint32_t>>& col_val_vec) {
    uint32_t iter = 1;
    ASSERT_EQ(key, record.row_message().new_tuple(0).datum_int32());
    for (auto vec_iter = col_val_vec.begin(); vec_iter != col_val_vec.end(); ++iter, ++vec_iter) {
      ASSERT_EQ(vec_iter->second, record.row_message().new_tuple(iter).datum_int32());
    }
  }

  void CDCSDKYsqlTest::EnableCDCServiceInAllTserver(uint32_t num_tservers) {
    for (uint32_t i = 0; i < num_tservers; ++i) {
      const auto& tserver = test_cluster()->mini_tablet_server(i)->server();
      auto cdc_service = dynamic_cast<CDCServiceImpl*>(
          tserver->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());
      cdc_service->SetCDCServiceEnabled();
    }
  }

  int CDCSDKYsqlTest::FindTserversWithCacheHit(
      const xrepl::StreamId stream_id, const TabletId tablet_id, uint32_t num_tservers) {
    int count = 0;
    // check the CDC Service Cache of all the tservers.
    for (uint32_t i = 0; i < num_tservers; ++i) {
      const auto& tserver = test_cluster()->mini_tablet_server(i)->server();
      auto cdc_service = dynamic_cast<CDCServiceImpl*>(
          tserver->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());
      auto status = cdc_service->TEST_GetTabletInfoFromCache({stream_id, tablet_id});
      if (status.ok()) {
        count += 1;
      }
    }
    return count;
  }

  void CDCSDKYsqlTest::CheckRecord(
      const CDCSDKProtoRecordPB& record, CDCSDKYsqlTest::ExpectedRecord expected_records,
      uint32_t* count, const bool& validate_old_tuple,
      CDCSDKYsqlTest::ExpectedRecord expected_before_image_records, std::string table_name,
      bool is_nothing_record) {
    // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
    switch (record.row_message().op()) {
      case RowMessage::DDL: {
        ASSERT_EQ(record.row_message().table(), table_name);
        count[0]++;
      } break;
      case RowMessage::INSERT: {
        AssertKeyValue(record, expected_records.key, expected_records.value);
        ASSERT_EQ(record.row_message().table(), table_name);
        count[1]++;
      } break;
      case RowMessage::UPDATE: {
        AssertKeyValue(record, expected_records.key, expected_records.value);
        if (validate_old_tuple) {
          AssertBeforeImageKeyValue(
              record, expected_before_image_records.key, expected_before_image_records.value);
        }
        ASSERT_EQ(record.row_message().table(), table_name);
        count[2]++;
      } break;
      case RowMessage::DELETE: {
        if (is_nothing_record) {
          ASSERT_EQ(record.row_message().old_tuple_size(), 0);
          ASSERT_EQ(record.row_message().new_tuple_size(), 0);
        } else {
          ASSERT_EQ(record.row_message().old_tuple(0).datum_int32(), expected_records.key);
          if (validate_old_tuple) {
            AssertBeforeImageKeyValue(
                record, expected_before_image_records.key, expected_before_image_records.value);
          }
        }
        ASSERT_EQ(record.row_message().table(), table_name);
        count[3]++;
      } break;
      case RowMessage::READ: {
        AssertKeyValue(record, expected_records.key, expected_records.value);
        ASSERT_EQ(record.row_message().table(), table_name);
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

  void CDCSDKYsqlTest::CheckRecordWithThreeColumns(
      const CDCSDKProtoRecordPB& record,
      CDCSDKYsqlTest::ExpectedRecordWithThreeColumns expected_records, uint32_t* count,
      const bool& validate_old_tuple,
      CDCSDKYsqlTest::ExpectedRecordWithThreeColumns expected_before_image_records,
      const bool& validate_third_column, const bool is_nothing_record) {
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
        if (is_nothing_record) {
          ASSERT_EQ(record.row_message().old_tuple_size(), 0);
          ASSERT_EQ(record.row_message().new_tuple_size(), 0);
        } else {
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

  void CDCSDKYsqlTest::CheckCount(const uint32_t* expected_count, uint32_t* count) {
    for (int i = 0; i < 6; i++) {
      ASSERT_EQ(expected_count[i], count[i]);
    }
  }

  void CDCSDKYsqlTest::CheckRecord(
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

  Status CDCSDKYsqlTest::InitVirtualWAL(
      const xrepl::StreamId& stream_id, const std::vector<TableId> table_ids,
      const uint64_t session_id) {
    InitVirtualWALForCDCRequestPB init_req;
    init_req.set_stream_id(stream_id.ToString());
    init_req.set_session_id(session_id);
    for (const auto& table_id : table_ids) {
      init_req.add_table_id(table_id);
    }

    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          InitVirtualWALForCDCResponsePB init_resp;
          RpcController init_rpc;
          auto status = cdc_proxy_->InitVirtualWALForCDC(init_req, &init_resp, &init_rpc);

          if (status.ok() && !init_resp.has_error()) {
            return true;
          }

          if (status.ok() && init_resp.has_error()) {
            status = StatusFromPB(init_resp.error().status());
            if (status.IsAlreadyPresent() || status.IsInvalidArgument()|| status.IsNotFound()) {
              RETURN_NOT_OK(status);
            }
          }

          return false;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "InitVirtualWal failed due to RPC timeout"));

    return Status::OK();
  }

  Status CDCSDKYsqlTest::DestroyVirtualWAL(const uint64_t session_id) {
    DestroyVirtualWALForCDCRequestPB req;
    req.set_session_id(session_id);

    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          DestroyVirtualWALForCDCResponsePB resp;
          RpcController init_rpc;
          auto status = cdc_proxy_->DestroyVirtualWALForCDC(req, &resp, &init_rpc);

          if (status.ok() && !resp.has_error()) {
            return true;
          }

          if (status.ok() && resp.has_error()) {
            status = StatusFromPB(resp.error().status());
            if (status.IsNotFound() || status.IsInvalidArgument()) {
              RETURN_NOT_OK(status);
            }
          }

          return false;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "DestroyVirtualWAL failed due to RPC timeout"));

    return Status::OK();
  }

  Result<GetConsistentChangesResponsePB> CDCSDKYsqlTest::GetConsistentChangesFromCDC(
      const xrepl::StreamId& stream_id, const uint64_t session_id) {
    GetConsistentChangesRequestPB change_req;
    GetConsistentChangesResponsePB final_resp;
    change_req.set_stream_id(stream_id.ToString());
    change_req.set_session_id(session_id);

    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          GetConsistentChangesResponsePB change_resp;
          RpcController get_changes_rpc;
          auto status =
              cdc_proxy_->GetConsistentChanges(change_req, &change_resp, &get_changes_rpc);

          if (status.ok() && !change_resp.has_error()) {
            final_resp = change_resp;
            return true;
          }

          if (status.ok() && change_resp.has_error()) {
            status = StatusFromPB(change_resp.error().status());
            if (status.IsNotFound() || status.IsInvalidArgument()) {
              RETURN_NOT_OK(status);
            }
          }

          return false;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "GetConsistentChanges failed due to RPC timeout"));

    return final_resp;
  }

  Status CDCSDKYsqlTest::UpdatePublicationTableList(
      const xrepl::StreamId& stream_id, const std::vector<TableId> table_ids,
      const uint64_t& session_id) {
    UpdatePublicationTableListRequestPB req;
    UpdatePublicationTableListResponsePB resp;

    req.set_stream_id(stream_id.ToString());
    for (const auto& table_id : table_ids) {
      req.add_table_id(table_id);
    }
    req.set_session_id(session_id);

    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          RpcController rpc;
          auto status = cdc_proxy_->UpdatePublicationTableList(req, &resp, &rpc);

          if (status.ok() && !resp.has_error()) {
            return true;
          }

          if (status.ok() && resp.has_error()) {
            status = StatusFromPB(resp.error().status());
            if (status.IsNotFound() || status.IsInvalidArgument()) {
              RETURN_NOT_OK(status);
            }
          }

          return false;
        },
        MonoDelta::FromSeconds(kRpcTimeout),
        "UpdatePublicationTableList failed due to RPC timeout"));

    return Status::OK();
  }

  Status CDCSDKYsqlTest::UpdateAndPersistLSN(
      const xrepl::StreamId& stream_id, const uint64_t confirmed_flush_lsn,
      const uint64_t restart_lsn, const uint64_t session_id) {
    UpdateAndPersistLSNRequestPB update_req;
    update_req.set_session_id(session_id);
    update_req.set_stream_id(stream_id.ToString());
    update_req.set_restart_lsn(restart_lsn);
    update_req.set_confirmed_flush_lsn(confirmed_flush_lsn);

    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          UpdateAndPersistLSNResponsePB update_resp;
          RpcController rpc;
          auto status = cdc_proxy_->UpdateAndPersistLSN(update_req, &update_resp, &rpc);

          if (status.ok() && !update_resp.has_error()) {
            return true;
          }

          if (status.ok() && update_resp.has_error()) {
            status = StatusFromPB(update_resp.error().status());
            if (status.IsNotFound() || status.IsInvalidArgument()) {
              RETURN_NOT_OK(status);
            }
          }

          return false;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "UpdateRestartLSN failed due to RPC timeout"));

    return Status::OK();
  }

  Result<GetChangesResponsePB> CDCSDKYsqlTest::GetChangesFromCDC(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp,
      int tablet_idx,
      int64 safe_hybrid_time,
      int wal_segment_index,
      const bool populate_checkpoint,
      const bool should_retry,
      const bool need_schema_info) {
    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;

    if (cp == nullptr) {
      PrepareChangeRequest(
          &change_req, stream_id, tablets, tablet_idx, 0, 0, "", 0, 0, "", safe_hybrid_time,
          wal_segment_index, populate_checkpoint, need_schema_info);
    } else {
      PrepareChangeRequest(
          &change_req, stream_id, tablets, *cp, tablet_idx, "", safe_hybrid_time,
          wal_segment_index);
    }

    // Retry only on LeaderNotReadyToServe or NotFound errors
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          RpcController get_changes_rpc;
          auto status = cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc);

          if (status.ok() && change_resp.has_error()) {
            status = StatusFromPB(change_resp.error().status());
          }

          if (should_retry && (status.IsLeaderNotReadyToServe() || status.IsNotFound())) {
            LOG(INFO) << "Retrying GetChanges in test";
            return false;
          }

          RETURN_NOT_OK(status);
          return true;
        },
        MonoDelta::FromSeconds(kRpcTimeout),
        "GetChanges timed out waiting for Leader to get ready"));

    return change_resp;
  }

  Result<GetChangesResponsePB> CDCSDKYsqlTest::GetChangesFromCDC(
      const GetChangesRequestPB& change_req, bool should_retry) {
    GetChangesResponsePB change_resp;
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          GetChangesResponsePB resp;
          RpcController get_changes_rpc;
          auto status = cdc_proxy_->GetChanges(change_req, &resp, &get_changes_rpc);

          if (status.ok() && change_resp.has_error()) {
            status = StatusFromPB(change_resp.error().status());
          }

          // Retry only on LeaderNotReadyToServe or NotFound errors
          if (should_retry && (status.IsLeaderNotReadyToServe() || status.IsNotFound())) {
            LOG(INFO) << "Retrying GetChanges in test";
            return false;
          }

          change_resp = resp;
          return true;
        },
        MonoDelta::FromSeconds(kRpcTimeout),
        "GetChanges timed out waiting for Leader to get ready"));

    return change_resp;
  }

  Result<GetChangesResponsePB> CDCSDKYsqlTest::GetChangesFromCDC(
      const xrepl::StreamId& stream_id,
      const TabletId& tablet_id,
      const CDCSDKCheckpointPB* cp,
      int tablet_idx) {
    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;

    if (cp == nullptr) {
      PrepareChangeRequest(&change_req, stream_id, tablet_id, tablet_idx);
    } else {
      PrepareChangeRequest(&change_req, stream_id, tablet_id, *cp, tablet_idx);
    }

    // Retry only on LeaderNotReadyToServe or NotFound errors
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          RpcController get_changes_rpc;
          auto status = cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc);

          if (status.ok() && change_resp.has_error()) {
            status = StatusFromPB(change_resp.error().status());
          }

          if (status.IsLeaderNotReadyToServe() || status.IsNotFound()) {
            return false;
          }

          RETURN_NOT_OK(status);
          return true;
        },
        MonoDelta::FromSeconds(kRpcTimeout),
        "GetChanges timed out waiting for Leader to get ready"));

    return change_resp;
  }

  Result<int64> CDCSDKYsqlTest::GetChangeRecordCount(
      const xrepl::StreamId& stream_id,
      const YBTableName& table,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint,
      const int64 expected_total_records,
      bool explicit_checkpointing_enabled,
      std::map<TabletId, std::vector<CDCSDKProtoRecordPB>> records) {
    std::vector<TabletId> tablet_ids;
    std::map<TabletId, CDCSDKCheckpointPB> explicit_checkpoints;
    for (int i = 0; i < tablets.size(); ++i) {
      tablet_ids.push_back(tablets.Get(i).tablet_id());
    }

    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;

    CDCSDKCheckpointPB explicit_checkpoint;
    explicit_checkpoint.set_term(0);
    explicit_checkpoint.set_index(0);
    explicit_checkpoint.set_write_id(0);
    explicit_checkpoint.set_key("");

    int64 total_record_count = 0;

    RETURN_NOT_OK(WaitFor([&]() -> Result<bool> {
      for (uint32_t i = 0; i < tablet_ids.size(); ++i) {
        auto cp = tablet_to_checkpoint.find(tablet_ids[i]);

        if (cp == tablet_to_checkpoint.end()) {
          PrepareChangeRequest(&change_req, stream_id, tablet_ids[i], i);
        } else {
          PrepareChangeRequest(&change_req, stream_id, tablet_ids[i], cp->second, i);
        }

        // If the stream is configured for explicit checkpointing, then we will populate the
        // explicit_cdc_sdk_checkpoint field as well.
        auto iter = explicit_checkpoints.find(tablet_ids[i]);
        CDCSDKCheckpointPB explicit_cp;
        if (explicit_checkpointing_enabled) {
          if (iter == explicit_checkpoints.end()) {
            change_req.mutable_explicit_cdc_sdk_checkpoint()->CopyFrom(explicit_checkpoint);
          } else {
            explicit_cp = iter->second;
            change_req.mutable_explicit_cdc_sdk_checkpoint()->CopyFrom(explicit_cp);

          }
        }

        rpc::RpcController get_changes_rpc;

        LOG(INFO) << "Calling GetChanges on " << tablet_ids[i] << " with "
                  << change_req.from_cdc_sdk_checkpoint().term() << ":"
                  << change_req.from_cdc_sdk_checkpoint().index();
        auto status = cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc);

        if (status.ok() && !change_resp.has_error()) {
          // Process the records here.
          for (auto record : change_resp.cdc_sdk_proto_records()) {
            if (IsDMLRecord(record)) {
              ++total_record_count;
            }

            if (record.row_message().op() != RowMessage::DDL) {
              records[tablet_ids[i]].push_back(record);
            }

            if (explicit_checkpointing_enabled) {
              explicit_cp.set_term(record.from_op_id().term());
              explicit_cp.set_index(record.from_op_id().index());
              explicit_cp.set_key(record.from_op_id().write_id_key());
              explicit_cp.set_write_id(record.from_op_id().write_id());
              explicit_cp.set_snapshot_time(record.row_message().commit_time() - 1);
            }
          }

          LOG(INFO) << "Received records for tablet " << tablet_ids[i] << ": "
                    << change_resp.cdc_sdk_proto_records_size() << " with response checkpoint "
                    << change_resp.cdc_sdk_checkpoint().term() << ":"
                    << change_resp.cdc_sdk_checkpoint().index();

          tablet_to_checkpoint[tablet_ids[i]] = change_resp.cdc_sdk_checkpoint();
        } else {
          status = StatusFromPB(change_resp.error().status());
          if (status.IsTabletSplit()) {
            LOG(INFO) << "Got a tablet split on tablet " << tablet_ids[i]
                      << ", fetching new tablets";

            auto get_tablets_resp = VERIFY_RESULT(
                GetTabletListToPollForCDC(stream_id, table.table_id(), tablet_ids[i]));

            VERIFY_EQ(get_tablets_resp.tablet_checkpoint_pairs_size(), 2);

            // Store the opIds for the children tablets.
            for (int j = 0; j < get_tablets_resp.tablet_checkpoint_pairs_size(); ++j) {
              auto pair = get_tablets_resp.tablet_checkpoint_pairs(j);
              tablet_to_checkpoint[pair.tablet_locations().tablet_id()] = pair.cdc_sdk_checkpoint();
              explicit_checkpoints[pair.tablet_locations().tablet_id()] = pair.cdc_sdk_checkpoint();

              tablet_ids.push_back(pair.tablet_locations().tablet_id());

              LOG(INFO) << "Assigned from_op_id " << pair.cdc_sdk_checkpoint().term() << ":"
                        << pair.cdc_sdk_checkpoint().index() << " to child "
                        << pair.tablet_locations().tablet_id();
            }

            tablet_ids.erase(find(tablet_ids.begin(), tablet_ids.end(), tablet_ids[i]));

            break;
          } else {
            RETURN_NOT_OK(status);
          }
        }
      }

      LOG(INFO) << "Total records consumed so far: " << total_record_count;

      return total_record_count >= expected_total_records;
    },
    MonoDelta::FromSeconds(300),
    "Timed out while fetching the changes"));

    return total_record_count;
  }

  Result<GetChangesResponsePB> CDCSDKYsqlTest::GetChangesFromCDCWithoutRetry(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp) {
    return GetChangesFromCDC(
        stream_id, tablets, cp, 0 /* tablet_idx */, -1 /* safe_hybrid_time */,
        0 /* wal_segment_index */, true /* populate_checkpoint*/, false /* should_retry */);
  }

  CDCSDKYsqlTest::GetAllPendingChangesResponse
  CDCSDKYsqlTest::GetAllPendingChangesWithRandomReqSafeTimeChanges(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp,
      int tablet_idx,
      int64 safe_hybrid_time,
      int wal_segment_index) {
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

  Result<uint64_t> FindLSNForSendingFeedback(const GetConsistentChangesResponsePB& change_resp) {
    bool found_commit = false;
    uint64_t commit_lsn;
    if (change_resp.cdc_sdk_proto_records_size() > 0) {
      for (const auto& record : change_resp.cdc_sdk_proto_records()) {
        if (record.row_message().op() == RowMessage_Op_COMMIT) {
          found_commit = true;
          commit_lsn = record.row_message().pg_lsn();
        }
      }
    }

    if (!found_commit) {
      LOG(INFO) << "Couldnt find a commit lsn for sending feedback";
      return STATUS_FORMAT(NotFound, "Couldnt find a commit lsn for sending feedback");
    }

    return commit_lsn;
  }

  Result<CDCSDKYsqlTest::GetAllPendingChangesResponse>
  CDCSDKYsqlTest::GetAllPendingTxnsFromVirtualWAL(
      const xrepl::StreamId& stream_id, std::vector<TableId> table_ids, int expected_dml_records,
      bool init_virtual_wal, const uint64_t session_id, bool allow_sending_feedback) {
    // We will keep on consuming changes until we get the entire txn i.e COMMIT record of the
    // last txn. This indicates that even though we might have received the expecpted DML
    // records, we might still continue calling GetConsistentChanges until we receive the
    // COMMIT record.

    GetAllPendingChangesResponse resp;
    if (init_virtual_wal) {
      Status s = InitVirtualWAL(stream_id, table_ids, session_id);
      if (!s.ok()) {
        LOG(ERROR) << "Error while trying to initialize virtual WAL";
        RETURN_NOT_OK(s);
      }
    }

    // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT
    // in that order.
    int count[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int begin_records = 0;
    int commit_records = 0;
    int dml_records = 0;
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          GetConsistentChangesResponsePB change_resp;
          auto get_changes_result = GetConsistentChangesFromCDC(stream_id, session_id);

          if (get_changes_result.ok()) {
            change_resp = *get_changes_result;
          } else {
            LOG(ERROR) << "Encountered error while calling GetConsistentChanges on stream: "
                       << stream_id << ", status: " << get_changes_result.status();
            RETURN_NOT_OK(get_changes_result);
          }

          if (change_resp.has_needs_publication_table_list_refresh() &&
          change_resp.needs_publication_table_list_refresh() &&
          change_resp.has_publication_refresh_time() &&
          change_resp.publication_refresh_time() > 0) {
            resp.has_publication_refresh_indicator = true;
          }

          for (int i = 0; i < change_resp.cdc_sdk_proto_records_size(); i++) {
            resp.records.push_back(change_resp.cdc_sdk_proto_records(i));
            UpdateRecordCount(change_resp.cdc_sdk_proto_records(i), count);
          }

          begin_records = count[6];
          commit_records = count[7];
          dml_records =
              count[1] + count[2] + count[3] + count[5];  // INSERT + UPDATE + DELETE + TRUNCATE
          LOG(INFO) << "Total Received records for stream " << resp.records.size();
          uint64_t restart_lsn = 0;
          uint64_t confirmed_flush_lsn = 0;
          bool send_feedback = false;

          if (allow_sending_feedback) {
            auto result = FindLSNForSendingFeedback(change_resp);
            if (result.ok()) {
              send_feedback = true;
              confirmed_flush_lsn = *result;
              restart_lsn = *result + 1;
            }

            if (send_feedback) {
              LOG(INFO) << "Sending feedback for stream " << stream_id
                        << " with restart_lsn: " << restart_lsn
                        << " and confirmed_flush_lsn: " << confirmed_flush_lsn;
              auto result =
                  UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn, session_id);
              if (!result.ok()) {
                LOG(ERROR) << "UpdateRestartLSN failed: " << result;
                RETURN_NOT_OK(result);
              }
            }
          }

          if (dml_records < expected_dml_records || commit_records < begin_records) {
            return false;
          }

          return true;
        },
        MonoDelta::FromSeconds(300), "Didnt receive expected records within time",
        MonoDelta::FromMilliseconds(kDefaultInitialWaitMs), 1));

    LOG(INFO) << "Record count array: ";
    for (int i = 0; i < 8; i++) {
      LOG(INFO) << "Count[" << i << "] = " << count[i];
      resp.record_count[i] = count[i];
    }

    return resp;
  }

  CDCSDKYsqlTest::GetAllPendingChangesResponse CDCSDKYsqlTest::GetAllPendingChangesFromCdc(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp,
      int tablet_idx,
      int64 safe_hybrid_time,
      int wal_segment_index) {
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

  Result<GetChangesResponsePB> CDCSDKYsqlTest::GetChangesFromCDCWithExplictCheckpoint(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* from_op_id,
      const CDCSDKCheckpointPB* explicit_checkpoint,
      const TableId& colocated_table_id,
      int tablet_idx) {
    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;

    if (from_op_id == nullptr) {
      PrepareChangeRequest(&change_req, stream_id, tablets, tablet_idx);
    } else {
      PrepareChangeRequestWithExplicitCheckpoint(&change_req, stream_id, tablets,
        from_op_id, explicit_checkpoint, colocated_table_id, tablet_idx);
    }

    // Retry only on LeaderNotReadyToServe or NotFound errors
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          RpcController get_changes_rpc;
          auto status = cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc);

          if (status.ok() && change_resp.has_error()) {
            status = StatusFromPB(change_resp.error().status());
          }

          if (status.IsLeaderNotReadyToServe() || status.IsNotFound()) {
            return false;
          }

          RETURN_NOT_OK(status);
          return true;
        },
        MonoDelta::FromSeconds(kRpcTimeout),
        "GetChanges timed out waiting for Leader to get ready"));

    return change_resp;
  }

  bool CDCSDKYsqlTest::DeleteCDCStream(const xrepl::StreamId& db_stream_id) {
    RpcController delete_rpc;
    delete_rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    DeleteCDCStreamRequestPB delete_req;
    DeleteCDCStreamResponsePB delete_resp;
    delete_req.add_stream_id(db_stream_id.ToString());

    // The following line assumes that cdc_proxy_ has been initialized in the test already
    auto result = cdc_proxy_->DeleteCDCStream(delete_req, &delete_resp, &delete_rpc);
    return result.ok() && !delete_resp.has_error();
  }

  Result<GetChangesResponsePB> CDCSDKYsqlTest::GetChangesFromCDCSnapshot(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const TableId& colocated_table_id) {
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

  void CDCSDKYsqlTest::TestGetChanges(
      const uint32_t replication_factor, bool add_tables_without_primary_key) {
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
    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

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

  void CDCSDKYsqlTest::TestIntentGarbageCollectionFlag(
      const uint32_t num_tservers,
      const bool set_flag_to_a_smaller_value,
      const uint32_t cdc_intent_retention_ms,
      CDCCheckpointType checkpoint_type,
      const bool extend_expiration) {
    if (set_flag_to_a_smaller_value) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_intent_retention_ms) = cdc_intent_retention_ms;
    }
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_update_local_peer_min_index) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;

    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));

    auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(
        test_client()->GetTablets(table, 0, &tablets, /* partition_list_version = */ nullptr));

    TabletId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(checkpoint_type));
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
    ASSERT_OK(WaitForPostApplyMetadataWritten(1 /* expected_num_transactions */));
    ASSERT_OK(FlushTable(table.table_id()));

    // Sleep for 60s for the background thread to update the consumer op_id so that garbage
    // collection can happen.
    vector<int64> intent_counts(num_tservers, -1);
    ASSERT_OK(WaitFor(
        [this, &num_tservers, &set_flag_to_a_smaller_value, &extend_expiration, &intent_counts,
         &stream_id, &tablets, &change_resp]() -> Result<bool> {
          for (uint32_t i = 0; i < num_tservers; ++i) {
            if (extend_expiration) {
              // Call GetChanges once to set the initial value in the cdc_state table.
              auto change_resp_2 = VERIFY_RESULT(GetChangesFromCDC(
                  stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
              if (change_resp_2.has_error()) {
                return false;
              }
              change_resp = change_resp_2;
            }

            RETURN_NOT_OK(GetIntentCounts(i, &intent_counts[i]));

            if (set_flag_to_a_smaller_value && !extend_expiration) {
              if (intent_counts[i] != 0) {
                return false;
              }
            }
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
      ASSERT_OK(WaitForFlushTables(
          {table.table_id()}, /* add_indexes = */ false,
          /* timeout_secs = */ 30, /* is_compaction = */ false));

      SleepFor(MonoDelta::FromMilliseconds(100));

      change_resp =
          ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
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

  void CDCSDKYsqlTest::TestSetCDCCheckpoint(CDCCheckpointType checkpoint_type) {
    ASSERT_OK(SetUpWithParams(1, 1, false));
    auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(
        test_client()->GetTablets(table, 0, &tablets, /* partition_list_version = */ nullptr));

    TabletId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    auto stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
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

  Result<GetChangesResponsePB> CDCSDKYsqlTest::VerifyIfDDLRecordPresent(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      bool expect_ddl_record, bool is_first_call, const CDCSDKCheckpointPB* cp) {
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

  void CDCSDKYsqlTest::PollForIntentCount(
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

  Result<GetCDCDBStreamInfoResponsePB> CDCSDKYsqlTest::GetDBStreamInfo(
      const xrepl::StreamId db_stream_id) {
    GetCDCDBStreamInfoRequestPB get_req;
    GetCDCDBStreamInfoResponsePB get_resp;
    get_req.set_db_stream_id(db_stream_id.ToString());

    RpcController get_rpc;
    get_rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));
    RETURN_NOT_OK(cdc_proxy_->GetCDCDBStreamInfo(get_req, &get_resp, &get_rpc));
    return get_resp;
  }

  void CDCSDKYsqlTest::VerifyTablesInStreamMetadata(
      const xrepl::StreamId& stream_id, const std::unordered_set<std::string>& expected_table_ids,
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

  Status CDCSDKYsqlTest::ChangeLeaderOfTablet(size_t new_leader_index, const TabletId tablet_id) {
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

  Status CDCSDKYsqlTest::StepDownLeader(size_t new_leader_index, const TabletId tablet_id) {
    Status status = yb_admin_client_->SetLoadBalancerEnabled(false);
    if (!status.ok()) {
      return status;
    }

    status = yb_admin_client_->LeaderStepDownWithNewLeader(
        tablet_id,
        test_cluster()->mini_tablet_server(new_leader_index)->server()->permanent_uuid());
    if (!status.ok()) {
      return status;
    }
    SleepFor(MonoDelta::FromMilliseconds(500));
    return Status::OK();
  }

  Status CDCSDKYsqlTest::CreateSnapshot(const NamespaceName& ns) {
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

  int CDCSDKYsqlTest::CountEntriesInDocDB(std::vector<tablet::TabletPeerPtr> peers,
    const std::string& table_id) {
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

  Status CDCSDKYsqlTest::TriggerCompaction(const TabletId tablet_id) {
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

  Status CDCSDKYsqlTest::CompactSystemTable() {
    string tool_path = GetToolPath("../bin", "yb-admin");
    vector<string> argv;
    argv.push_back(tool_path);
    argv.push_back("-master_addresses");
    argv.push_back(AsString(test_cluster_.mini_cluster_->mini_master(0)->bound_rpc_addr()));
    argv.push_back("compact_sys_catalog");
    RETURN_NOT_OK(Subprocess::Call(argv));
    return Status::OK();
  }

  Status CDCSDKYsqlTest::FlushTable(const TableId& table_id) {
    return WaitForFlushTables(
        {table_id}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false);
  }

  Status CDCSDKYsqlTest::WaitForPostApplyMetadataWritten(size_t expected_num_transactions) {
    if (!GetAtomicFlag(&FLAGS_cdc_write_post_apply_metadata)) {
      return Status::OK();
    }
    size_t num_intents = 0;
    return WaitFor(
        [&]() -> Result<bool> {
          auto peers = ListTabletPeers(test_cluster_.mini_cluster_.get(), ListPeersFilter::kAll);
          for (const auto &peer : peers) {
            auto tablet = peer->shared_tablet();
            auto participant = tablet ? tablet->transaction_participant() : nullptr;
            if (!participant) {
              continue;
            }
            auto result = VERIFY_RESULT(participant->TEST_CountIntents());
            LOG(INFO) << "Transactions: " << result.num_transactions
                      << " Post-apply: " << result.num_post_apply;
            if (result.num_transactions < expected_num_transactions ||
                result.num_transactions != result.num_post_apply) {
              return false;
            }
            num_intents += result.num_intents;
          }
          return true;
        },
        MonoDelta::FromSeconds(30), "Waiting for post apply metadata to be written");
  }

  void CDCSDKYsqlTest::GetTabletLeaderAndAnyFollowerIndex(
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
  void CDCSDKYsqlTest::CompareExpirationTime(
      const TabletId& tablet_id, const CoarseTimePoint& prev_leader_expiry_time,
      size_t current_leader_idx, bool strictly_greater_than) {
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

  Result<int64_t> CDCSDKYsqlTest::GetLastActiveTimeFromCdcStateTable(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, client::YBClient* client) {
    CDCStateTable cdc_state_table(client);

    auto row = VERIFY_RESULT(cdc_state_table.TryFetchEntry(
        {tablet_id, stream_id}, CDCStateTableEntrySelector().IncludeActiveTime()));
    SCHECK(
        row, IllegalState, "CDC state table entry for tablet $0 stream $1 not found", tablet_id,
        stream_id);

    return *row->active_time;
  }

  Result<std::tuple<uint64, std::string>> CDCSDKYsqlTest::GetSnapshotDetailsFromCdcStateTable(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, client::YBClient* client) {
    CDCStateTable cdc_state_table(client);
    auto row = VERIFY_RESULT(cdc_state_table.TryFetchEntry(
        {tablet_id, stream_id},
        CDCStateTableEntrySelector().IncludeCDCSDKSafeTime().IncludeSnapshotKey()));
    SCHECK(
        row, IllegalState, "CDC state table entry for tablet $0 stream $1 not found", tablet_id,
        stream_id);
    SCHECK(
        row->cdc_sdk_safe_time, IllegalState,
        "CDC SDK safe time not found for tablet $0 stream $1 not found", tablet_id, stream_id);
    SCHECK(
        row->snapshot_key, IllegalState,
        "CDC SDK snapshot key not found for tablet $0 stream $1 not found", tablet_id, stream_id);

    return std::make_pair(*row->cdc_sdk_safe_time, *row->snapshot_key);
  }

  Result<int64_t> CDCSDKYsqlTest::GetSafeHybridTimeFromCdcStateTable(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, client::YBClient* client) {
    CDCStateTable cdc_state_table(client);
    auto row = VERIFY_RESULT(cdc_state_table.TryFetchEntry(
        {tablet_id, stream_id}, CDCStateTableEntrySelector().IncludeCDCSDKSafeTime()));

    SCHECK(
        row, IllegalState, "CDC state table entry for tablet $0 stream $1 not found", tablet_id,
        stream_id);

    return *row->cdc_sdk_safe_time;
  }

  void CDCSDKYsqlTest::ValidateColumnCounts(const GetChangesResponsePB& resp,
    uint32_t excepted_column_counts) {
    uint32_t record_size = resp.cdc_sdk_proto_records_size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = resp.cdc_sdk_proto_records(idx);
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), excepted_column_counts);
      }
    }
  }

  void CDCSDKYsqlTest::ValidateColumnCounts(
      const GetAllPendingChangesResponse& resp, uint32_t excepted_column_counts) {
    size_t record_size = resp.records.size();
    for (uint32_t idx = 0; idx < record_size; idx++) {
      const CDCSDKProtoRecordPB record = resp.records[idx];
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), excepted_column_counts);
      }
    }
  }

  void CDCSDKYsqlTest::ValidateInsertCounts(const GetChangesResponsePB& resp,
    uint32_t excepted_insert_counts) {
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

  void CDCSDKYsqlTest::WaitUntilSplitIsSuccesful(
      const TabletId& tablet_id, const yb::client::YBTableName& table,
      const int expected_num_tablets) {
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
        MonoDelta::FromSeconds(120), "Tablet Split not succesful"));
  }

  void CDCSDKYsqlTest::CheckTabletsInCDCStateTable(
      const std::unordered_set<TabletId> expected_tablet_ids, client::YBClient* client,
      const xrepl::StreamId& stream_id) {
    CDCStateTable cdc_state_table(test_client());
    Status s;
    auto table_range = ASSERT_RESULT(cdc_state_table.GetTableRange({}, &s));

    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          std::unordered_set<TabletId> seen_tablet_ids;
          uint32_t seen_rows = 0;
          for (auto row_result : table_range) {
            RETURN_NOT_OK(row_result);
            auto& row = *row_result;
            if (stream_id && row.key.stream_id != stream_id) {
              continue;
            }
            seen_tablet_ids.insert(row.key.tablet_id);
            seen_rows += 1;
          }
          RETURN_NOT_OK(s);

          return (
              expected_tablet_ids == seen_tablet_ids && seen_rows == expected_tablet_ids.size());
        },
        MonoDelta::FromSeconds(60),
        "Tablets in cdc_state table associated with the stream are not the same as expected"));
  }

  Result<std::vector<TableId>> CDCSDKYsqlTest::GetCDCStreamTableIds(
      const xrepl::StreamId& stream_id) {
    NamespaceId ns_id;
    std::vector<TableId> stream_table_ids;
    std::unordered_map<std::string, std::string> options;
    StreamModeTransactional transactional(false);
    RETURN_NOT_OK(test_client()->GetCDCStream(
        stream_id, &ns_id, &stream_table_ids, &options, &transactional));
    return stream_table_ids;
  }

  uint32_t CDCSDKYsqlTest::GetTotalNumRecordsInTablet(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp) {
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

  void CDCSDKYsqlTest::CDCSDKAddColumnsWithImplictTransaction(bool packed_row) {
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
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(
        1 /* start */, 10 /* end */, &test_cluster_, {kValue2ColumnName, kValue3ColumnName}));
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, {kValue4ColumnName}, &conn));
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

  void CDCSDKYsqlTest::CDCSDKAddColumnsWithExplictTransaction(bool packed_row) {
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
    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

    // Insert some records in transaction.
    ASSERT_OK(WriteRowsHelper(
        0 /* start */, 11 /* end */, &test_cluster_, true, 4, kTableName,
        {kValue2ColumnName, kValue3ColumnName}));
    ASSERT_OK(WaitForFlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));

    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue4ColumnName, &conn));

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
    ASSERT_OK(WaitForFlushTables(
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

  void CDCSDKYsqlTest::CDCSDKDropColumnsWithRestartTServer(bool packed_row) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
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
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(1 /* start */, 10 /* end */, &test_cluster_, {kValue2ColumnName}));

    ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, &conn));
    ASSERT_OK(WriteRows(11 /* start */, 20 /* end */, &test_cluster_));

    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
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

  void CDCSDKYsqlTest::CDCSDKDropColumnsWithImplictTransaction(bool packed_row) {
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
    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(
        1 /* start */, 11 /* end */, &test_cluster_, {kValue2ColumnName, kValue3ColumnName}));
    ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, &conn));

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

  void CDCSDKYsqlTest::CDCSDKDropColumnsWithExplictTransaction(bool packed_row) {
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
    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

    // Insert some records in transaction.
    ASSERT_OK(WriteRowsHelper(
        1 /* start */, 11 /* end */, &test_cluster_, true, 4, kTableName,
        {kValue2ColumnName, kValue3ColumnName}));
    ASSERT_OK(WaitForFlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, &conn));
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
    ASSERT_OK(WaitForFlushTables(
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

  void CDCSDKYsqlTest::CDCSDKRenameColumnsWithImplictTransaction(bool packed_row) {
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
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));


    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(1 /* start */, 10 /* end */, &test_cluster_, {kValue2ColumnName}));
    ASSERT_OK(RenameColumn(
        &test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, kValue3ColumnName, &conn));
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

  void CDCSDKYsqlTest::CDCSDKRenameColumnsWithExplictTransaction(bool packed_row) {
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
    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

    // Insert some records in transaction.
    ASSERT_OK(WriteRowsHelper(
        1 /* start */, 10 /* end */, &test_cluster_, true, 3, kTableName, {kValue2ColumnName}));
    ASSERT_OK(WaitForFlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    ASSERT_OK(RenameColumn(
        &test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, kValue3ColumnName, &conn));

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
    ASSERT_OK(WaitForFlushTables(
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

  void CDCSDKYsqlTest::CDCSDKMultipleAlterWithRestartTServer(bool packed_row) {
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
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(1 /* start */, 6 /* end */, &test_cluster_, {kValue2ColumnName}));

    // Add a column
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue3ColumnName, &conn));
    ASSERT_OK(WriteRows(
        6 /* start */, 11 /* end */, &test_cluster_, {kValue2ColumnName, kValue3ColumnName}));

    // Drop one column
    ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, &conn));
    ASSERT_OK(WriteRows(11 /* start */, 16 /* end */, &test_cluster_, {kValue3ColumnName}));

    // Add the 2 columns
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue4ColumnName, &conn));
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, &conn));
    ASSERT_OK(WriteRows(
        16 /* start */, 21 /* end */, &test_cluster_,
        {kValue2ColumnName, kValue3ColumnName, kValue4ColumnName}));

    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
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

  void CDCSDKYsqlTest::CDCSDKMultipleAlterWithTabletLeaderSwitch(bool packed_row) {
    const int num_tservers = 3;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));
    const uint32_t num_tablets = 1;
    auto table =
        ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

    // Create CDC stream.
    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

    ASSERT_OK(WriteRowsHelper(1 /* start */, 11 /* end */, &test_cluster_, true));
    // Call Getchanges
    ASSERT_OK(WaitForFlushTables(
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
    StartYbAdminClient();
    if (first_leader_index == 0) {
      // We want to avoid the scenario where the first TServer is the leader, since we want to shut
      // the leader TServer down and call GetChanges. GetChanges will be called on the cdc_proxy
      // based on the first TServer's address and we want to avoid the network issues.
      ASSERT_OK(StepDownLeader(first_follower_index, tablets[0].tablet_id()));
    }
    ASSERT_OK(StepDownLeader(first_follower_index, tablets[0].tablet_id()));

    // Call GetChanges with new LEADER.
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    // Validate the columns and insert counts.
    ValidateColumnCounts(change_resp, 2);
    ValidateInsertCounts(change_resp, 10);

    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, &conn));
    ASSERT_OK(WriteRowsHelper(
        21 /* start */, 31 /* end */, &test_cluster_, true, 3, kTableName, {kValue2ColumnName}));
    ASSERT_OK(WaitForFlushTables(
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
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue3ColumnName, &conn));
    ASSERT_OK(WriteRows(31, 41, &test_cluster_, {kValue2ColumnName, kValue3ColumnName}));
    GetTabletLeaderAndAnyFollowerIndex(tablets, &first_leader_index, &first_follower_index);
    if (first_leader_index == 0) {
      // We want to avoid the scenario where the first TServer is the leader, since we want to shut
      // the leader TServer down and call GetChanges. GetChanges will be called on the cdc_proxy
      // based on the first TServer's address and we want to avoid the network issues.
      ASSERT_OK(StepDownLeader(first_follower_index, tablets[0].tablet_id()));
    }
    ASSERT_OK(StepDownLeader(first_follower_index, tablets[0].tablet_id()));
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    // Validate the columns and insert counts.
    ValidateColumnCounts(change_resp, 4);
    ValidateInsertCounts(change_resp, 10);
  }

  void CDCSDKYsqlTest::CDCSDKAlterWithSysCatalogCompaction(bool packed_row) {
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
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

    // Insert some records in transaction.
    ASSERT_OK(WriteRows(1 /* start */, 101 /* end */, &test_cluster_, {kValue2ColumnName}));

    // Add a column
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue3ColumnName, &conn));
    ASSERT_OK(WriteRows(
        101 /* start */, 201 /* end */, &test_cluster_, {kValue2ColumnName, kValue3ColumnName}));

    // Drop one column
    ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, &conn));
    ASSERT_OK(WriteRows(201 /* start */, 301 /* end */, &test_cluster_, {kValue3ColumnName}));

    // Add the 2 columns
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue4ColumnName, &conn));
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, &conn));
    ASSERT_OK(WriteRows(
        301 /* start */, 401 /* end */, &test_cluster_,
        {kValue2ColumnName, kValue3ColumnName, kValue4ColumnName}));

    CHECK_OK(test_cluster()->mini_master(0)->tablet_peer()->tablet()->ForceManualRocksDBCompact());

    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
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

  void CDCSDKYsqlTest::CDCSDKIntentsBatchReadWithAlterAndTabletLeaderSwitch(bool packed_row) {
    const int num_tservers = 3;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, packed_row));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 10;
    ASSERT_OK(SetUpWithParams(num_tservers, 1, false));
    const uint32_t num_tablets = 1;
    auto table =
        ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
    TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), num_tablets);
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

    // Create CDC stream.
    xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
    auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
    ASSERT_FALSE(resp.has_error());

    ASSERT_OK(WriteRowsHelper(1 /* start */, 101 /* end */, &test_cluster_, true));
    // Call Getchanges
    ASSERT_OK(WaitForFlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
    // Validate the columns and insert counts.
    ValidateColumnCounts(change_resp, 2);

    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName, &conn));
    ASSERT_OK(WriteRowsHelper(
        101 /* start */, 201 /* end */, &test_cluster_, true, 3, kTableName, {kValue2ColumnName}));
    ASSERT_OK(WaitForFlushTables(
        {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
        /* is_compaction = */ false));
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));

    size_t first_leader_index = -1;
    size_t first_follower_index = -1;
    GetTabletLeaderAndAnyFollowerIndex(tablets, &first_leader_index, &first_follower_index);
    StartYbAdminClient();
    if (first_leader_index == 0) {
      // We want to avoid the scenario where the first TServer is the leader, since we want to shut
      // the leader TServer down and call GetChanges. GetChanges will be called on the cdc_proxy
      // based on the first TServer's address and we want to avoid the network issues.
      ASSERT_OK(StepDownLeader(first_follower_index, tablets[0].tablet_id()));
    }
    ASSERT_OK(StepDownLeader(first_follower_index, tablets[0].tablet_id()));
    change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    ValidateColumnCounts(change_resp, 2);
  }

  void CDCSDKYsqlTest::WaitForCompaction(YBTableName table) {
    auto peers = ListTabletPeers(test_cluster(), ListPeersFilter::kLeaders);
    int count_before_compaction = CountEntriesInDocDB(peers, table.table_id());
    int count_after_compaction = 0;
    ASSERT_OK(WaitFor(
      [&]() {
        auto result = test_cluster_.mini_cluster_->CompactTablets();
        if (!result.ok()) {
          return false;
        }
        count_after_compaction = CountEntriesInDocDB(peers, table.table_id());
        if (count_after_compaction < count_before_compaction) {
          return true;
        }
        return false;
      },
      MonoDelta::FromSeconds(60), "Expected compaction did not happen"));
    LOG(INFO) << "count_before_compaction: " << count_before_compaction
            << " count_after_compaction: " << count_after_compaction;
  }

  void CDCSDKYsqlTest::VerifySnapshotOnColocatedTables(
      xrepl::StreamId stream_id,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets,
      const CDCSDKCheckpointPB& snapshot_bootstrap_checkpoint, const TableId& req_table_id,
      const TableName& table_name, int64_t snapshot_records_per_table) {
    bool first_call = true;
    GetChangesResponsePB next_change_resp;
    GetChangesResponsePB change_resp;
    uint64 expected_snapshot_time;
    int64_t seen_snapshot_records = 0;
    CDCSDKCheckpointPB explicit_checkpoint;

    while (true) {
      if (first_call) {
        next_change_resp = ASSERT_RESULT(GetChangesFromCDCWithExplictCheckpoint(
            stream_id, tablets, &snapshot_bootstrap_checkpoint, &explicit_checkpoint,
            req_table_id));
      } else {
        next_change_resp = ASSERT_RESULT(GetChangesFromCDCWithExplictCheckpoint(
            stream_id, tablets, &change_resp.cdc_sdk_checkpoint(), &explicit_checkpoint,
            req_table_id));
      }

      // count READ records
      for (const auto& record : next_change_resp.cdc_sdk_proto_records()) {
        if (record.row_message().op() == RowMessage::READ) {
          seen_snapshot_records += 1;
          ASSERT_EQ(record.row_message().table(), table_name);
        }
      }

      // Get the checkpoint from cdc_state table
      auto resp =
          ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream_id, tablets[0].tablet_id(), req_table_id));
      ASSERT_GE(resp.snapshot_time(), 0);

      if (first_call) {
        ASSERT_EQ(resp.checkpoint().op_id().term(), snapshot_bootstrap_checkpoint.term());
        ASSERT_EQ(resp.checkpoint().op_id().index(), snapshot_bootstrap_checkpoint.index());
        ASSERT_EQ(resp.snapshot_key(), "");
        expected_snapshot_time = resp.snapshot_time();
        first_call = false;
      } else {
        ASSERT_EQ(resp.checkpoint().op_id().term(), change_resp.cdc_sdk_checkpoint().term());
        ASSERT_EQ(resp.checkpoint().op_id().index(), change_resp.cdc_sdk_checkpoint().index());
        ASSERT_EQ(resp.snapshot_key(), change_resp.cdc_sdk_checkpoint().key());
        ASSERT_EQ(resp.snapshot_time(), expected_snapshot_time);
      }

      change_resp = next_change_resp;
      explicit_checkpoint = change_resp.cdc_sdk_checkpoint();

      if (change_resp.cdc_sdk_checkpoint().key().empty() &&
          change_resp.cdc_sdk_checkpoint().write_id() == 0 &&
          change_resp.cdc_sdk_checkpoint().snapshot_time() == 0) {
        ASSERT_EQ(seen_snapshot_records, snapshot_records_per_table);
        ASSERT_RESULT(UpdateSnapshotDone(stream_id, tablets, req_table_id));
        break;
      }
    }
  }

  Result<std::string> CDCSDKYsqlTest::GetValueFromMap(const QLMapValuePB& map_value,
    const std::string& key) {
    for (int index = 0; index < map_value.keys_size(); ++index) {
      if (map_value.keys(index).string_value() == key) {
        return map_value.values(index).string_value();
      }
    }
    return STATUS_FORMAT(NotFound, "Key not found in the map: $0", key);
  }

  template <class T>
  Result<T> CDCSDKYsqlTest::GetIntValueFromMap(const QLMapValuePB& map_value,
    const std::string& key) {
    auto str_value = VERIFY_RESULT(GetValueFromMap(map_value, key));

    return CheckedStol<T>(str_value);
  }

  // Read the cdc_state table
  Result<CDCSDKYsqlTest::CdcStateTableRow> CDCSDKYsqlTest::ReadFromCdcStateTable(
      const xrepl::StreamId stream_id, const std::string& tablet_id) {
    // Read the cdc_state table safe should be set to valid value.
    CdcStateTableRow expected_row;
    CDCStateTable cdc_state_table(test_client());
    Status s;
    auto table_range =
        VERIFY_RESULT(cdc_state_table.GetTableRange(CDCStateTableEntrySelector().IncludeAll(), &s));
    for (auto row_result : table_range) {
      RETURN_NOT_OK(row_result);
      auto& row = *row_result;

      HybridTime cdc_sdk_safe_time = HybridTime::kInvalid;
      int64_t last_active_time_cdc_state_table = 0;
      uint64_t confirmed_flush_lsn = 0;
      uint64_t restart_lsn = 0;
      uint64_t record_id_commit_time = 0;
      uint32_t xmin = 0;

      if (row.cdc_sdk_safe_time) {
        cdc_sdk_safe_time = HybridTime(*row.cdc_sdk_safe_time);
      }

      if (row.active_time) {
        last_active_time_cdc_state_table = *row.active_time;
      }

      if(row.confirmed_flush_lsn) {
        confirmed_flush_lsn = *row.confirmed_flush_lsn;
      }

      if(row.restart_lsn) {
        restart_lsn = *row.restart_lsn;
      }

      if(row.record_id_commit_time) {
        record_id_commit_time = *row.record_id_commit_time;
      }

      if(row.xmin) {
        xmin = *row.xmin;
      }

      if (row.key.tablet_id == tablet_id && row.key.stream_id == stream_id) {
        LOG(INFO) << "Read cdc_state table with tablet_id: " << row.key.tablet_id
                  << " stream_id: " << row.key.stream_id << " checkpoint is: " << *row.checkpoint
                  << " last_active_time_cdc_state_table: " << last_active_time_cdc_state_table
                  << " cdc_sdk_safe_time: " << cdc_sdk_safe_time;
        expected_row.op_id = *row.checkpoint;
        expected_row.cdc_sdk_safe_time = cdc_sdk_safe_time;
        expected_row.cdc_sdk_latest_active_time = last_active_time_cdc_state_table;
        expected_row.confirmed_flush_lsn = confirmed_flush_lsn;
        expected_row.restart_lsn = restart_lsn;
        expected_row.record_id_commit_time = record_id_commit_time;
        expected_row.xmin = xmin;
      }
    }
    RETURN_NOT_OK(s);
    return expected_row;
  }
  Result<std::optional<CDCSDKYsqlTest::CdcStateTableSlotRow>>
  CDCSDKYsqlTest::ReadSlotEntryFromStateTable(const xrepl::StreamId& stream_id) {
    std::optional<CdcStateTableSlotRow> slot_row = std::nullopt;
    CDCStateTable cdc_state_table(test_client());
    Status s;
    auto table_range =
        VERIFY_RESULT(cdc_state_table.GetTableRange(CDCStateTableEntrySelector().IncludeAll(), &s));

    for (auto row_result : table_range) {
      RETURN_NOT_OK(row_result);
      auto& row = *row_result;
      if (row.key.tablet_id == kCDCSDKSlotEntryTabletId && row.key.stream_id == stream_id) {
        slot_row = CdcStateTableSlotRow();
        slot_row->confirmed_flush_lsn = *(row.confirmed_flush_lsn);
        slot_row->restart_lsn = *(row.restart_lsn);
        slot_row->xmin = *(row.xmin);
        slot_row->record_id_commit_time = HybridTime(*(row.record_id_commit_time));
        slot_row->last_pub_refresh_time = HybridTime(*(row.last_pub_refresh_time));
        slot_row->pub_refresh_times = *(row.pub_refresh_times);
        slot_row->last_decided_pub_refresh_time = *(row.last_decided_pub_refresh_time);
        LOG(INFO) << "Read cdc_state table slot entry for slot with stream id: " << stream_id
                  << " confirmed_flush_lsn: " << slot_row->confirmed_flush_lsn
                  << " restart_lsn: " << slot_row->restart_lsn << " xmin: " << slot_row->xmin
                  << " record_id_commit_time: " << slot_row->record_id_commit_time.ToUint64()
                  << " last_pub_refresh_time: " << slot_row->last_pub_refresh_time.ToUint64()
                  << " pub_refresh_times: " << slot_row->pub_refresh_times
                  << " last_decided_pub_refresh_time: " << slot_row->last_decided_pub_refresh_time;
      }
    }
    RETURN_NOT_OK(s);
    return slot_row;
  }

  void CDCSDKYsqlTest::VerifyExplicitCheckpointingOnTablets(
      const xrepl::StreamId& stream_id,
      const std::unordered_map<TabletId, CdcStateTableRow>& initial_tablet_checkpoint,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets,
      const std::unordered_set<TabletId>& expected_tablet_ids_with_progress) {
    for (const auto& tablet : tablets) {
      auto result = ASSERT_RESULT(ReadFromCdcStateTable(stream_id, tablet.tablet_id()));
      ASSERT_GT(result.op_id, initial_tablet_checkpoint.at(tablet.tablet_id()).op_id);
      if (expected_tablet_ids_with_progress.contains(tablet.tablet_id())) {
        ASSERT_GT(
            result.cdc_sdk_safe_time,
            initial_tablet_checkpoint.at(tablet.tablet_id()).cdc_sdk_safe_time);
      } else {
        ASSERT_EQ(
            result.cdc_sdk_safe_time,
            initial_tablet_checkpoint.at(tablet.tablet_id()).cdc_sdk_safe_time);
      }
    }
  }

  void CDCSDKYsqlTest::VerifyLastRecordAndProgressOnSlot(
      const xrepl::StreamId& stream_id, const CDCSDKProtoRecordPB& last_record) {
    // Last record received from the Virtual WAL should always be a COMMIT record. While sending the
    // last feedback to the VWAL after receiving the last batch of records, VWAL will use the
    // last shipped commit record's metadata to update the slot entry in cdc_state table. Hence,
    // confirmed_flush_lsn & restart_lsn will be same and they'll be equal to last shipped commit
    // record' lsn.
    ASSERT_EQ(last_record.row_message().op(), RowMessage::Op::RowMessage_Op_COMMIT);
    auto commit_record_lsn = last_record.row_message().pg_lsn();
    auto commit_record_txn_id = last_record.row_message().pg_transaction_id();
    auto commit_record_commit_time = last_record.row_message().commit_time();

    CDCStateTable cdc_state_table(test_client());
    auto slot_entry = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
        {kCDCSDKSlotEntryTabletId, stream_id},
        CDCStateTableEntrySelector().IncludeData().IncludeCDCSDKSafeTime()));
    ASSERT_TRUE(slot_entry.has_value());
    ASSERT_EQ(slot_entry->confirmed_flush_lsn, commit_record_lsn);
    ASSERT_EQ(slot_entry->restart_lsn, commit_record_lsn);
    ASSERT_EQ(slot_entry->xmin, commit_record_txn_id);
    ASSERT_EQ(slot_entry->record_id_commit_time, commit_record_commit_time);
    ASSERT_EQ(slot_entry->cdc_sdk_safe_time, commit_record_commit_time);
  }

  void CDCSDKYsqlTest::UpdateRecordCount(const CDCSDKProtoRecordPB& record, int* record_count) {
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
      case RowMessage::SAFEPOINT:
        break;
      default:
        ASSERT_FALSE(true);
        break;
    }
  }

  void CDCSDKYsqlTest::CheckRecordCount(
      GetAllPendingChangesResponse resp, int expected_dml_records, int expected_ddl_records,
      int expected_min_txn_id) {
    // The record count array in GetAllPendingChangesResponse stores counts of DDL, INSERT, UPDATE,
    // DELETE, READ, TRUNCATE, BEGIN, COMMIT in that order.
    int dml_records =
        resp.record_count[1] + resp.record_count[2] + resp.record_count[3] + resp.record_count[5];
    ASSERT_EQ(dml_records, expected_dml_records);

    if (expected_ddl_records > 0) {
      int ddl_records = resp.record_count[0];
      ASSERT_EQ(ddl_records, expected_ddl_records);
    }

    size_t idx = resp.records.size() - 1;
    // Find the last COMMIT record in the response.
    while(resp.records[idx].row_message().op() != RowMessage_Op_COMMIT) {
      idx--;
    }
    ASSERT_EQ(resp.records[idx].row_message().op(), RowMessage::COMMIT);
    auto max_txn_id = resp.records[idx].row_message().pg_transaction_id();

    // Number of BEGIN & COMMIT should be equal to the txn_id of last received COMMIT record
    // - expected_min_txn_id + 1, since transaction generator starts from 2, so first record will
    // have txn_id as 2.
    int expected_boundary_records = max_txn_id - expected_min_txn_id + 1;
    int begin_records = resp.record_count[6];
    int commit_records = resp.record_count[7];
    ASSERT_EQ(begin_records, expected_boundary_records);
    ASSERT_EQ(commit_records, expected_boundary_records);
  }

  void CDCSDKYsqlTest::CheckRecordsConsistencyFromVWAL(
      const std::vector<CDCSDKProtoRecordPB>& records) {
    RowMessage_Op prev_op = RowMessage::UNKNOWN;
    uint64_t prev_commit_time = 0;
    std::string prev_docdb_txn_id = "";
    uint64_t prev_record_time = 0;
    uint32_t prev_write_id = 0;
    TableId prev_table_id = "";
    std::string prev_primary_key = "";
    uint64_t prev_lsn = 0;
    uint64_t prev_txn_id = 0;
    bool in_transaction = false;
    bool first_record_in_transaction = false;
    for (auto& record : records) {
      if (record.row_message().op() == RowMessage::BEGIN) {
        in_transaction = true;
        first_record_in_transaction = true;
        // BEGIN record should have strictly > commit_time than prev record' commit_time. Same
        // follows for PG txn_id.
        ASSERT_GT(record.row_message().commit_time(), prev_commit_time);
        ASSERT_GT(record.row_message().pg_lsn(), prev_lsn);
        ASSERT_GT(record.row_message().pg_transaction_id(), prev_txn_id);
        ASSERT_FALSE(record.row_message().has_table_id());
        ASSERT_FALSE(record.row_message().has_primary_key());
        prev_commit_time = record.row_message().commit_time();
        prev_lsn = record.row_message().pg_lsn();
        prev_txn_id = record.row_message().pg_transaction_id();
      }

      if (record.row_message().op() == RowMessage::COMMIT) {
        ASSERT_TRUE(in_transaction);
        in_transaction = false;
        ASSERT_EQ(record.row_message().commit_time(), prev_commit_time);
        ASSERT_GT(record.row_message().pg_lsn(), prev_lsn);
        // PG txn_id should be same as the BEGIN record of the current txn.
        ASSERT_EQ(record.row_message().pg_transaction_id(), prev_txn_id);
        ASSERT_FALSE(record.row_message().has_table_id());
        ASSERT_FALSE(record.row_message().has_primary_key());
        prev_commit_time = record.row_message().commit_time();
        prev_lsn = record.row_message().pg_lsn();
      }

      if (record.row_message().op() == RowMessage::INSERT ||
          record.row_message().op() == RowMessage::UPDATE ||
          record.row_message().op() == RowMessage::DELETE) {
        ASSERT_TRUE(in_transaction);
        ASSERT_TRUE(record.row_message().has_table_id());
        ASSERT_TRUE(record.row_message().has_primary_key());
        ASSERT_EQ(record.row_message().commit_time(), prev_commit_time);
        ASSERT_GT(record.row_message().pg_lsn(), prev_lsn);
        // PG txn_id should be same as the BEGIN record of the current txn.
        ASSERT_EQ(record.row_message().pg_transaction_id(), prev_txn_id);
        prev_lsn = record.row_message().pg_lsn();
        bool has_tie_broken = false;

        if (!first_record_in_transaction) {
          if (record.row_message().has_transaction_id()) {
            if (record.row_message().transaction_id() != prev_docdb_txn_id) {
              ASSERT_GT(record.row_message().transaction_id(), prev_docdb_txn_id);
              has_tie_broken = true;
              break;
            }
          }

          if (record.row_message().record_time() != prev_record_time) {
            ASSERT_GT(record.row_message().record_time(), prev_record_time);
            has_tie_broken = true;
            break;
          }
          if (record.cdc_sdk_op_id().write_id() != prev_write_id) {
            ASSERT_GT(record.cdc_sdk_op_id().write_id(), prev_write_id);
            has_tie_broken = true;
            break;
          }
          if (record.row_message().table_id() != prev_table_id) {
            ASSERT_GT(record.row_message().table_id(), prev_table_id);
            has_tie_broken = true;
            break;
          }
          if (record.row_message().primary_key() != prev_primary_key) {
            ASSERT_GT(record.row_message().primary_key(), prev_primary_key);
            has_tie_broken = true;
          }
        }

        if(!first_record_in_transaction) {
          ASSERT_TRUE(has_tie_broken);
        }


        first_record_in_transaction = false;
        prev_docdb_txn_id =
            record.row_message().has_transaction_id() ? record.row_message().transaction_id() : "";
        prev_record_time = record.row_message().record_time();
        prev_write_id = record.cdc_sdk_op_id().write_id();
        prev_table_id = record.row_message().table_id();
        prev_primary_key = record.row_message().primary_key();
      }

      if (record.row_message().op() == RowMessage::DDL) {
        if (prev_op == RowMessage::DDL) {
          // There can be two DDLs received back to back with the same commit_time.
          ASSERT_GE(record.row_message().commit_time(), prev_commit_time);
        } else {
          // Since DDL are not part of txn, they should have a strictly greater commit_time.
          ASSERT_GT(record.row_message().commit_time(), prev_commit_time);
        }
        ASSERT_FALSE(record.row_message().has_pg_lsn());
        ASSERT_FALSE(record.row_message().has_pg_transaction_id());
      }

      prev_op = record.row_message().op();

    }
  }

  void CDCSDKYsqlTest::CheckRecordsConsistency(const std::vector<CDCSDKProtoRecordPB>& records) {
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

  void CDCSDKYsqlTest::GetRecordsAndSplitCount(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, const TableId& table_id,
      CDCCheckpointType checkpoint_type,
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
          auto new_checkpoint = (checkpoint_type == CDCCheckpointType::EXPLICIT)
                                    ? change_resp.cdc_sdk_checkpoint()
                                    : tablet_checkpoint_pair.cdc_sdk_checkpoint();
          tablets.push_back({new_tablet.tablet_id(), new_checkpoint});
        }
      }
    }
  }

  void CDCSDKYsqlTest::PerformSingleAndMultiShardInserts(
      const int& num_batches, const int& inserts_per_batch, int apply_update_latency,
      const int& start_index) {
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
    for (int i = 0; i < num_batches; i++) {
      int multi_shard_inserts = inserts_per_batch / 2;
      int curr_start_id = start_index + i * inserts_per_batch;

      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_participant_inject_latency_on_apply_update_txn_ms) =
          apply_update_latency;
      ASSERT_OK(WriteRowsHelper(
          curr_start_id, curr_start_id + multi_shard_inserts, &test_cluster_, true, 2, kTableName,
          {}, true, &conn));

      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_participant_inject_latency_on_apply_update_txn_ms) =
          0;
      ASSERT_OK(WriteRows(
          curr_start_id + multi_shard_inserts, curr_start_id + inserts_per_batch, &test_cluster_,
          {}, &conn));
    }
  }

  void CDCSDKYsqlTest::PerformSingleAndMultiShardQueries(
      const int& num_batches, const int& queries_per_batch, const string& query,
      int apply_update_latency, const int& start_index) {
    auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
    for (int i = 0; i < num_batches; i++) {
      int multi_shard_queries = queries_per_batch / 2;
      int curr_start_id = start_index + i * queries_per_batch;

      ANNOTATE_UNPROTECTED_WRITE(
          FLAGS_TEST_txn_participant_inject_latency_on_apply_update_txn_ms) = apply_update_latency;
      ASSERT_OK(conn.Execute("BEGIN"));
      for (int i = 0; i < multi_shard_queries; i++) {
        ASSERT_OK(conn.ExecuteFormat(query, curr_start_id + i + 1));
      }
      ASSERT_OK(conn.Execute("COMMIT"));

      ANNOTATE_UNPROTECTED_WRITE(
          FLAGS_TEST_txn_participant_inject_latency_on_apply_update_txn_ms) = 0;
      for (int i = 0; i < (queries_per_batch - multi_shard_queries); i++) {
        ASSERT_OK(conn.ExecuteFormat(query, curr_start_id + multi_shard_queries + i + 1));
      }
    }
  }

  OpId CDCSDKYsqlTest::GetHistoricalMaxOpId(
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const int& tablet_idx) {
    for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
      for (const auto& peer : test_cluster()->GetTabletPeers(i)) {
        if (peer->tablet_id() == tablets[tablet_idx].tablet_id()) {
          return peer->tablet()->transaction_participant()->GetHistoricalMaxOpId();
        }
      }
    }
    return OpId::Invalid();
  }

  TableId CDCSDKYsqlTest::GetColocatedTableId(const std::string& req_table_name) {
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

  void CDCSDKYsqlTest::AssertSafeTimeAsExpectedInTabletPeers(
      const TabletId& tablet_id, const HybridTime expected_safe_time) {
    for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
      for (const auto& tablet_peer : test_cluster()->GetTabletPeers(i)) {
        if (tablet_peer->tablet_id() == tablet_id) {
          ASSERT_OK(WaitFor(
              [&]() -> bool { return tablet_peer->get_cdc_sdk_safe_time() <= expected_safe_time; },
              MonoDelta::FromSeconds(60), "Safe_time is not as expected."));
        }
      }
    }
  }

  Status CDCSDKYsqlTest::WaitForGetChangesToFetchRecords(
      GetChangesResponsePB* get_changes_resp, const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const int& expected_count, bool is_explicit_checkpoint,
      const CDCSDKCheckpointPB* cp, const int& tablet_idx,
      const int64& safe_hybrid_time, const int& wal_segment_index, const double& timeout_secs) {
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
          LOG_WITH_FUNC(INFO) << "Actual Count = " << actual_count
                              << ", Expected count = " << expected_count;

          bool result = actual_count == expected_count;
          // Reset the count back to zero for explicit checkpoint since we are going to receive
          // these records again as we are not forwarding the checkpoint in the next GetChanges
          // call based on the rows received.
          if (is_explicit_checkpoint) {
            actual_count = 0;
          }
          return result;
        },
        MonoDelta::FromSeconds(timeout_secs),
        "Waiting for GetChanges to fetch: " + std::to_string(expected_count) + " records");
  }

  Status CDCSDKYsqlTest::WaitForGetChangesToFetchRecordsAcrossTablets(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const int& expected_count, bool is_explicit_checkpoint, const CDCSDKCheckpointPB* cp,
      const int64& safe_hybrid_time, const int& wal_segment_index, const double& timeout_secs) {
    int actual_count = 0;
    return WaitFor(
        [&]() -> Result<bool> {
          // Call GetChanges for each tablet.
          for (int tablet_idx = 0; tablet_idx < tablets.size(); tablet_idx++) {
            auto get_changes_resp_result = GetChangesFromCDC(
              stream_id, tablets, cp, tablet_idx, safe_hybrid_time, wal_segment_index);
            if (get_changes_resp_result.ok()) {
              for (const auto& record : get_changes_resp_result->cdc_sdk_proto_records()) {
                if (record.row_message().op() == RowMessage::INSERT ||
                    record.row_message().op() == RowMessage::UPDATE ||
                    record.row_message().op() == RowMessage::DELETE) {
                  actual_count += 1;
                }
              }
            }
          }

          LOG_WITH_FUNC(INFO) << "Actual Count = " << actual_count
                              << ", Expected count = " << expected_count;

          bool result = actual_count == expected_count;

          // Reset the count back to zero for explicit checkpoint since we are going to receive
          // these records again as we are not forwarding the checkpoint in the next GetChanges
          // call based on the rows received.
          if (is_explicit_checkpoint) {
            actual_count = 0;
          }
          return result;
        },
        MonoDelta::FromSeconds(timeout_secs),
        "Waiting for GetChanges to fetch: " + std::to_string(expected_count) + " records");
  }

  Status CDCSDKYsqlTest::WaitForFlushTables(
      const std::vector<TableId>& table_ids, bool add_indexes, int timeout_secs,
      bool is_compaction) {
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          auto status = test_client()->FlushTables(
              table_ids, /* add_indexes = */ add_indexes,
              /* timeout_secs = */ timeout_secs, /* is_compaction = */ is_compaction);
          if (!status.ok()) {
            if (status.IsInternalError()) {
              return false;
            } else {
              RETURN_NOT_OK(status);
            }
          }
          return true;
        },
        MonoDelta::FromSeconds(timeout_secs), "Waiting for flush operation to complete"));
    return Status::OK();
  }

  Status CDCSDKYsqlTest::XReplValidateSplitCandidateTable(const TableId& table_id) {
    auto& cm = test_cluster_.mini_cluster_->mini_master()->catalog_manager_impl();
    return cm.XReplValidateSplitCandidateTable(table_id);
  }

  void CDCSDKYsqlTest::LogRetentionBarrierAndRelatedDetails(
      const GetCheckpointResponsePB& checkpoint_result,
      const tablet::TabletPeerPtr& tablet_peer) {

    LOG(INFO) << "Snapshot Time : " << checkpoint_result.snapshot_time();
    LOG(INFO) << "History cutoff: " << tablet_peer->get_cdc_sdk_safe_time();
    LOG(INFO) << "Snapshot Safe Opid: " <<  checkpoint_result.checkpoint().op_id()
              << ", WAL index protected from: " << tablet_peer->get_cdc_min_replicated_index()
              << ", Intents protected from: " << tablet_peer->cdc_sdk_min_checkpoint_op_id();
  }

  void CDCSDKYsqlTest::LogRetentionBarrierDetails(
      const tablet::TabletPeerPtr& tablet_peer) {

    LOG(INFO) << tablet_peer->LogPrefix()
              << " History cutoff: " << tablet_peer->get_cdc_sdk_safe_time()
              << ", WAL index protected from: " << tablet_peer->get_cdc_min_replicated_index()
              << ", Intents protected from: " << tablet_peer->cdc_sdk_min_checkpoint_op_id();
  }

  void CDCSDKYsqlTest::ConsumeSnapshotAndVerifyRecords(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB& cp_resp,
      const CDCSDKYsqlTest::ExpectedRecord* expected_records,
      const uint32_t* expected_count,
      uint32_t* count) {

    GetChangesResponsePB change_resp_updated =
    ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));

    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      CheckRecord(record, expected_records[i], count);
    }
    LOG(INFO) << "Got " << count[4] << " read record and " << count[0] << " ddl record";
    CheckCount(expected_count, count);
  }

  Result<uint32_t> CDCSDKYsqlTest::ConsumeSnapshotAndVerifyCounts(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB& cp_resp,
      GetChangesResponsePB* change_resp_after_snapshot) {

    uint32_t reads_snapshot = 0;
    bool end_snapshot = false;
    bool first_read = true;
    GetChangesResponsePB change_resp;
    GetChangesResponsePB change_resp_updated;

    while (true) {
      if (first_read) {
        change_resp_updated = VERIFY_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));
        first_read = false;
      } else {
        change_resp_updated = VERIFY_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
      }

      uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
      uint32_t read_count = 0;
      for (uint32_t i = 0; i < record_size; ++i) {
        const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
        if (record.row_message().op() == RowMessage::READ) {
          read_count++;
        } else if (record.row_message().op() == RowMessage::INSERT) {
          end_snapshot = true;
          break;
        }
      }
      if (end_snapshot) {
        break;
      }
      reads_snapshot += read_count;
      change_resp = change_resp_updated;
    }

    *change_resp_after_snapshot = change_resp;
    return reads_snapshot;
  }

  Result<uint32_t> CDCSDKYsqlTest::ConsumeInsertsAndVerifyCounts(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const GetChangesResponsePB& change_resp_after_snapshot) {

    uint32_t inserts_snapshot = 0;
    GetChangesResponsePB change_resp = change_resp_after_snapshot;

    while (true) {
      GetChangesResponsePB change_resp1 =
          VERIFY_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
      uint32_t record_size_after_snapshot = change_resp1.cdc_sdk_proto_records_size();
      if (record_size_after_snapshot == 0) {
        break;
      }
      uint32_t insert_count = 0;
      for (uint32_t i = 0; i < record_size_after_snapshot; ++i) {
        const CDCSDKProtoRecordPB record = change_resp1.cdc_sdk_proto_records(i);
        if (record.row_message().op() == RowMessage::INSERT) {
          insert_count++;
        }
      }
      inserts_snapshot += insert_count;
      change_resp = change_resp1;
    }

    return inserts_snapshot;
  }

  void CDCSDKYsqlTest::ConsumeSnapshotAndPerformDML(
      xrepl::StreamId stream_id, YBTableName table,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets,
      CDCSDKCheckpointPB checkpoint, GetChangesResponsePB* change_resp) {
    auto peers = ListTabletPeers(test_cluster(), ListPeersFilter::kLeaders);
    uint32_t reads_snapshot = 0;
    bool do_update = true;
    bool first_read = true;
    // GetChangesResponsePB change_resp;
    GetChangesResponsePB change_resp_updated;
    CDCSDKCheckpointPB explicit_checkpoint;
    vector<int> excepted_result(2);
    vector<int> actual_result(2);
    while (true) {
      if (do_update) {
        ASSERT_OK(UpdateRows(100, 1001, &test_cluster_));
        ASSERT_OK(DeleteRows(1, &test_cluster_));
        ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
        ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
        do_update = false;
      }
      if (first_read) {
        change_resp_updated = ASSERT_RESULT(GetChangesFromCDCWithExplictCheckpoint(
            stream_id, tablets, &checkpoint, &explicit_checkpoint));
        first_read = false;
        // No changes in DocDB entries should be seen because retention barriers are held by
        // snapshot.
        auto count_before_compaction = CountEntriesInDocDB(peers, table.table_id());
        ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
        auto count_after_compaction = CountEntriesInDocDB(peers, table.table_id());
        ASSERT_EQ(count_before_compaction, count_after_compaction);
      } else {
        change_resp_updated = ASSERT_RESULT(GetChangesFromCDCWithExplictCheckpoint(
            stream_id, tablets, &change_resp->cdc_sdk_checkpoint(), &explicit_checkpoint));
      }
      uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
      uint32_t read_count = 0;
      for (uint32_t i = 0; i < record_size; ++i) {
        const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
        std::stringstream s;

        if (record.row_message().op() == RowMessage::READ) {
          for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
            s << " " << record.row_message().new_tuple(jdx).datum_int32();
            if (record.row_message().new_tuple(jdx).column_name() == kKeyColumnName) {
              actual_result[0] = record.row_message().new_tuple(jdx).datum_int32();
            } else if (record.row_message().new_tuple(jdx).column_name() == kValueColumnName) {
              actual_result[1] = record.row_message().new_tuple(jdx).datum_int32();
            }
          }
          LOG(INFO) << "row: " << i << " : " << s.str();
          // we should only get row values w.r.t snapshot, not changed values during snapshot.
          if (actual_result[0] == 100) {
            excepted_result[0] = 100;
            excepted_result[1] = 101;
            ASSERT_EQ(actual_result, excepted_result);
          } else if (actual_result[0] == 1) {
            excepted_result[0] = 1;
            excepted_result[1] = 2;
            ASSERT_EQ(actual_result, excepted_result);
          }
          read_count++;
        }
      }
      reads_snapshot += read_count;
      change_resp = &change_resp_updated;
      explicit_checkpoint = change_resp->cdc_sdk_checkpoint();

      // received snapshot complete marker
      if (change_resp->cdc_sdk_checkpoint().has_snapshot_time() &&
          change_resp->cdc_sdk_checkpoint().snapshot_time() == 0 &&
          change_resp->cdc_sdk_checkpoint().has_key() &&
          change_resp->cdc_sdk_checkpoint().key() == "" &&
          change_resp->cdc_sdk_checkpoint().has_write_id() &&
          change_resp->cdc_sdk_checkpoint().write_id() == 0) {
        ASSERT_RESULT(UpdateSnapshotDone(stream_id, tablets));
        break;
      }
    }
    ASSERT_EQ(reads_snapshot, 100);
  }

  void CDCSDKYsqlTest::VerifyTableIdAndPkInCDCRecords(
      GetChangesResponsePB* resp, std::unordered_set<std::string>* record_primary_key,
      std::unordered_set<std::string>* record_table_id) {
    for (int i = 0; i < resp->cdc_sdk_proto_records_size(); i++) {
      auto record = resp->cdc_sdk_proto_records(i);
      if (IsDMLRecord(record)) {
        ASSERT_TRUE(record.row_message().has_table_id());
        ASSERT_TRUE(record.row_message().has_primary_key());
        record_table_id->insert(record.row_message().table_id());
        record_primary_key->insert(record.row_message().primary_key());
      } else if (record.row_message().op() == RowMessage_Op_DDL) {
        ASSERT_TRUE(record.row_message().has_table_id());
        ASSERT_FALSE(record.row_message().has_primary_key());
        record_table_id->insert(record.row_message().table_id());
      } else {
        ASSERT_FALSE(record.row_message().has_table_id());
        ASSERT_FALSE(record.row_message().has_primary_key());
      }
    }
  }

  std::string CDCSDKYsqlTest::GetPubRefreshTimesString(vector<uint64_t> pub_refresh_times) {
    if (pub_refresh_times.empty()) {
      return "";
    }

    std::ostringstream oss;
    for (size_t i = 0; i < pub_refresh_times.size(); ++i) {
      if (i > 0) {
        oss << ",";
      }
      oss << pub_refresh_times[i];
    }
    return oss.str();
  }

  Status CDCSDKYsqlTest::ExecuteYBAdminCommand(
      const std::string& command_name, const std::vector<string>& command_args) {
    string tool_path = GetToolPath("../bin", "yb-admin");
    vector<string> argv;
    argv.push_back(tool_path);
    argv.push_back("--master_addresses");
    argv.push_back(AsString(test_cluster_.mini_cluster_->GetMasterAddresses()));
    argv.push_back(command_name);
    for (const auto& command_arg : command_args) {
      argv.push_back(command_arg);
    }

    RETURN_NOT_OK(Subprocess::Call(argv));

    return Status::OK();
  }

  Status CDCSDKYsqlTest::DisableDynamicTableAdditionOnCDCSDKStream(
      const xrepl::StreamId& stream_id) {
    std::string yb_admin_command = "disable_dynamic_table_addition_on_change_data_stream";
    vector<string> command_args;
    command_args.push_back(stream_id.ToString());
    RETURN_NOT_OK(ExecuteYBAdminCommand(yb_admin_command, command_args));
    return Status::OK();
  }

  Status CDCSDKYsqlTest::RemoveUserTableFromCDCSDKStream(
      const xrepl::StreamId& stream_id, const TableId& table_id) {
    std::string yb_admin_command = "remove_user_table_from_change_data_stream";
    vector<string> command_args;
    command_args.push_back(stream_id.ToString());
    command_args.push_back(table_id);
    RETURN_NOT_OK(ExecuteYBAdminCommand(yb_admin_command, command_args));

    return Status::OK();
  }

  Status CDCSDKYsqlTest::ValidateAndSyncCDCStateEntriesForCDCSDKStream(
      const xrepl::StreamId& stream_id) {
    std::string yb_admin_command =
        "validate_and_sync_cdc_state_table_entries_on_change_data_stream";
    vector<string> command_args;
    command_args.push_back(stream_id.ToString());
    RETURN_NOT_OK(ExecuteYBAdminCommand(yb_admin_command, command_args));

    return Status::OK();
  }

} // namespace cdc
} // namespace yb
