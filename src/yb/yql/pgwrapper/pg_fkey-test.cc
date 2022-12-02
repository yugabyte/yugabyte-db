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

#include <memory>
#include <string>

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/metrics.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Read);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_PgClientService_Perform);
DECLARE_uint64(ysql_session_max_batch_size);
DECLARE_bool(TEST_ysql_ignore_add_fk_reference);

namespace yb {
namespace pgwrapper {
namespace {

const std::string kPKTable = "pk_table";
const std::string kFKTable = "fk_table";
const std::string kConstraintName = "fk2pk";

class PgFKeyTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    PgMiniTestBase::SetUp();
    const auto& tserver = *cluster_->mini_tablet_server(0)->server();
    read_rpc_watcher_ = std::make_unique<MetricWatcher>(
        tserver,
        METRIC_handler_latency_yb_tserver_TabletServerService_Read);
    perform_rpc_watcher_ = std::make_unique<MetricWatcher>(
        tserver,
        METRIC_handler_latency_yb_tserver_PgClientService_Perform);
  }

  size_t NumTabletServers() override {
    return 1;
  }

  std::unique_ptr<MetricWatcher> read_rpc_watcher_;
  std::unique_ptr<MetricWatcher> perform_rpc_watcher_;
};

Status InsertItems(
    PGConn* conn, const std::string& table, size_t first_item, size_t last_item) {
  return conn->ExecuteFormat(
      "INSERT INTO $0 SELECT s, s FROM generate_series($1, $2) AS s", table, first_item, last_item);
}

struct Options {
  std::string fk_type = "INT";
  bool temp_tables = false;
  size_t last_item = 100;
};

Status PrepareTables(PGConn* conn, const Options& options = Options()) {
  const char* table_type = options.temp_tables ? "TEMP TABLE" : "TABLE";
  RETURN_NOT_OK(conn->ExecuteFormat(
      "CREATE $0 $1(k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS",
      table_type, kPKTable));
  RETURN_NOT_OK(conn->ExecuteFormat(
      "CREATE $0 $1(k INT PRIMARY KEY, pk $2) SPLIT INTO 1 TABLETS",
      table_type, kFKTable, options.fk_type));
  RETURN_NOT_OK(InsertItems(conn, kPKTable, 1, options.last_item));
  return InsertItems(conn, kFKTable, 1, options.last_item);
}

Status AddFKConstraint(PGConn* conn, bool skip_check = false) {
  return conn->ExecuteFormat(
      "ALTER TABLE $0 ADD CONSTRAINT $1 FOREIGN KEY(pk) REFERENCES $2(k)$3",
      kFKTable, kConstraintName, kPKTable, skip_check ? " NOT VALID" : "");
}

Status CheckAddFKCorrectness(PGConn* conn, bool temp_tables) {
  const size_t pk_fk_item_delta = 10;
  const auto last_pk_item = FLAGS_ysql_session_max_batch_size - pk_fk_item_delta / 2;
  RETURN_NOT_OK(PrepareTables(conn,
                              Options {
                                  .temp_tables = temp_tables,
                                  .last_item = last_pk_item
                              }));
  const size_t first_fk_extra_item = last_pk_item + 1;
  // Add items into FK table which are absent in PK table. All attempts to add FK constraint must
  // fail until all corresponding items will be added into PK table.
  RETURN_NOT_OK(InsertItems(
      conn, kFKTable, first_fk_extra_item, first_fk_extra_item + pk_fk_item_delta - 1));
  for (size_t i = 0; i < pk_fk_item_delta; ++i) {
    if (AddFKConstraint(conn).ok()) {
      return STATUS(IllegalState, "AddFKConstraint should fail, but it doesn't");
    }
    const size_t new_pk_item = last_pk_item + 1 + i;
    RETURN_NOT_OK(InsertItems(conn, kPKTable, new_pk_item, new_pk_item));
  }
  return AddFKConstraint(conn);
}

class PgFKeyTestNoFKCache : public PgFKeyTest {
 protected:
  void SetUp() override {
    FLAGS_TEST_ysql_ignore_add_fk_reference = true;
    PgFKeyTest::SetUp();
  }
};

Status PrepareTablesForMultipleFKs(PGConn* conn) {
  for (size_t i = 0; i < 3; ++i) {
    RETURN_NOT_OK(conn->ExecuteFormat("CREATE TABLE $0_$1(k INT PRIMARY KEY)", kPKTable, i + 1));
  }
  return conn->ExecuteFormat("CREATE TABLE $0(k INT PRIMARY KEY, "
                             "                pk_1 INT REFERENCES $1_1(k),"
                             "                pk_2 INT REFERENCES $1_2(k),"
                             "                pk_3 INT REFERENCES $1_3(k))",
                             kFKTable,
                             kPKTable);
}

} // namespace

// Test checks the number of RPC in case adding foreign key constraint to non empty table.
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(AddFKConstraintRPCCount)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTables(&conn));
  const auto add_fk_rpc_count = ASSERT_RESULT(read_rpc_watcher_->Delta([&conn]() {
    return AddFKConstraint(&conn);
  }));
  ASSERT_EQ(add_fk_rpc_count, 2);
}

// Test checks the number of RPC in case adding foreign key constraint with delayed validation
// to non empty table.
TEST_F(PgFKeyTest,
       YB_DISABLE_TEST_IN_TSAN(AddFKConstraintDelayedValidationRPCCount)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTables(&conn));
  const auto add_fk_rpc_count = ASSERT_RESULT(read_rpc_watcher_->Delta([&conn]() {
    return AddFKConstraint(&conn, true /* skip_check */);
  }));
  ASSERT_EQ(add_fk_rpc_count, 0);

  /* Note: VALIDATE CONSTRAINT is not yet supported. Uncomment next lines after fixing of #3946
  const auto validate_fk_rpc_count = ASSERT_RESULT(ReadRPCCountDelta([&conn]() {
    return conn.Execute("ALTER TABLE child VALIDATE CONSTRAINT child2parent");
  }));

  ASSERT_EQ(validate_fk_rpc_count, 2);*/

  // Check that VALIDATE CONSTRAINT is not supported
  ASSERT_STR_CONTAINS(
      conn.ExecuteFormat(
          "ALTER TABLE $0 VALIDATE CONSTRAINT $1", kFKTable, kConstraintName).ToString(),
      "not supported yet");
}

// Test checks FK correctness in case of FK check requires type casting.
// In this case RPC optimization can't be used.
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(AddFKConstraintWithTypeCast)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTables(&conn,
                          Options {
                              .fk_type = "BIGINT",
                              .last_item = 20
                          }));
  ASSERT_OK(InsertItems(&conn, kFKTable, 21, 21));
  ASSERT_NOK(AddFKConstraint(&conn));
  ASSERT_OK(InsertItems(&conn, kPKTable, 21, 21));
  const auto add_fk_rpc_count = ASSERT_RESULT(read_rpc_watcher_->Delta([&conn]() {
    return AddFKConstraint(&conn);
  }));
  ASSERT_EQ(add_fk_rpc_count, 43);
}

// Test checks FK check correctness with respect to internal buffering
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(AddFKCorrectness)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CheckAddFKCorrectness(&conn, false /* temp_tables */));
}

// Test checks FK check correctness on temp tables (no optimizations is used in this case)
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(AddFKCorrectnessOnTempTables)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CheckAddFKCorrectness(&conn, true /* temp_tables */));
}

// Test checks the number of RPC in case of multiple FK on same table.
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(MultipleFKConstraintRPCCount)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTablesForMultipleFKs(&conn));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0_1 VALUES(11), (12), (13);"
                               "INSERT INTO $0_2 VALUES(21), (22), (23);"
                               "INSERT INTO $0_3 VALUES(31), (32), (33);", kPKTable));
  // Warmup catalog cache to load info related for triggers before estimating RPC count.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(1, 11, 21, 31)", kFKTable));
  ASSERT_OK(conn.ExecuteFormat("TRUNCATE $0", kFKTable));
  const auto insert_fk_rpc_count = ASSERT_RESULT(perform_rpc_watcher_->Delta([&conn]() {
    return conn.ExecuteFormat(
      "INSERT INTO $0 VALUES(1, 11, 21, 31), (2, 12, 22, 32), (3, 13, 23, 33)", kFKTable);
  }));
  ASSERT_EQ(insert_fk_rpc_count, 1);
}

// Test checks that insertion into table with large number of foreign keys doesn't fail.
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(InsertWithLargeNumberOfFK)) {
  auto conn = ASSERT_RESULT(Connect());
  constexpr size_t fk_count = 3;
  constexpr size_t insert_count = 100;
  const auto parent_table_prefix = kPKTable + "_";
  std::string fk_keys, insert_fk_columns;
  fk_keys.reserve(255);
  insert_fk_columns.reserve(255);
  for (size_t i = 1; i <= fk_count; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0$1(k INT PRIMARY KEY)", parent_table_prefix, i));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0$1 SELECT s FROM generate_series(1, $2) AS s",
        parent_table_prefix, i, insert_count));
    if (!fk_keys.empty()) {
      fk_keys += ", ";
      insert_fk_columns += ", ";
    }
    fk_keys += Format("fk_$0 INT REFERENCES $1$0(k)", i, parent_table_prefix);
    insert_fk_columns += "s";
  }

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(k INT PRIMARY KEY, $1)", kFKTable, fk_keys));
  for (size_t i = 0; i < 50; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 SELECT $1 + s, $2 FROM generate_series(1, $3) AS s",
      kFKTable, i * insert_count, insert_fk_columns, insert_count));
  }
}

// Test checks rows written by buffered write operations are read successfully while
// performing FK constraint check.
TEST_F_EX(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(BufferedWriteOfReferencedRows), PgFKeyTestNoFKCache) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTablesForMultipleFKs(&conn));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE PROCEDURE test(start_idx INT, end_idx INT) LANGUAGE plpgsql AS $$$$ "
      "BEGIN"
      "  INSERT INTO $0_1 SELECT s FROM generate_series(start_idx, end_idx) AS s;"
      "  INSERT INTO $0_2 SELECT s FROM generate_series(start_idx, end_idx) AS s;"
      "  INSERT INTO $0_3 SELECT s FROM generate_series(start_idx, end_idx) AS s;"
      "  INSERT INTO $1 SELECT s, s, s, s FROM generate_series(start_idx, end_idx) AS s;"
      "END;$$$$",
      kPKTable, kFKTable));
  for (size_t i = 0; i < 10; ++i) {
    const size_t start_idx = i * 1000 + 1;
    const size_t end_idx = start_idx + 100;
    ASSERT_OK(conn.ExecuteFormat("CALL test($0, $1)", start_idx, end_idx));
  }
}

} // namespace pgwrapper
} // namespace yb
