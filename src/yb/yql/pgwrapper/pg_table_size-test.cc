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

#include <chrono>
#include <string>

#include "yb/client/client.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"

#include "yb/util/status.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
namespace yb {
namespace pgwrapper {
namespace {

class PgTableSizeTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    PgMiniTestBase::SetUp();
  }
};

const std::string kTable = "test_table";
const std::string kDatabase = "test_database";
const std::string kIndex = "test_index";
const std::string kCol1 = "test_column_1";
const std::string kCol2 = "test_column_2";
const std::string kCol3 = "test_column_3";
const std::string kView = "test_view";

const std::string kColocatedDatabase = "test_colocated_database";

constexpr double kSizeBuffer = 0.4;

constexpr int kByteStringSize = 6; // size of string " bytes", used to parse table size string
constexpr int kByteUnitStringSize = 3; // size of byte suffix strings (' kB',' MB',' GB',' TB')

// Expected size of each table in bytes
constexpr int kColocatedTableSize = 0;
constexpr int kColocatedIndexSize = 0;
constexpr int kSimpleTableSize = 4763000;
constexpr int kSimpleIndexSize = 4122000;
constexpr int kSimplePrimaryIndexSize = 0;
constexpr int kTempTableSize = 464000;
constexpr int kPartitionParentTableSize = 3072000;
constexpr int kPartitionTableSize = 3563000;
constexpr int kMaterializedViewSize = 3638000;
constexpr int kMaterializedViewRefreshSize = 4396000;

Result<std::string> GetTableSize(PGConn* conn,
                                 const std::string& relationType,
                                 const std::string& table_name) {
  // query equivalent to \d+ in ysqlsh -E
  std::string relkind = "'r','p','v','m','S','f',''";
  if (relationType == "T") {
    relkind = "'r','p',''";
  } else if (relationType == "I") {
    relkind = "'I','i',''";
  } else if (relationType == "M") {
    relkind = "'m',''";
  }

  // wait for master heartbeat service to run
  sleep(2);

  auto query = Format(R"#(
    SELECT
      n.nspname as "Schema",
      c.relname as "Name",
      CASE c.relkind
        WHEN 'r' THEN 'table'
        WHEN 'v' THEN 'view'
        WHEN 'm' THEN 'materialized view'
        WHEN 'i' THEN 'index'
        WHEN 'S' THEN 'sequence'
        WHEN 's' THEN 'special'
        WHEN 'f' THEN 'foreign table'
        WHEN 'p' THEN 'table'
        WHEN 'I' THEN 'index'
      END as "Type",
      pg_catalog.pg_get_userbyid(c.relowner) as "Owner",
      pg_catalog.pg_size_pretty(pg_catalog.pg_table_size(c.oid)) as "Size",
      pg_catalog.obj_description(c.oid, 'pg_class') as "Description"
    FROM
      pg_catalog.pg_class c LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
    WHERE c.relkind IN ($0)
      AND n.nspname <> 'pg_catalog'
      AND n.nspname <> 'information_schema'
      AND n.nspname !~ '^pg_toast'
      AND pg_catalog.pg_table_is_visible(c.oid)
      AND c.relname = '$1'
    ORDER BY 1,2)#",
    relkind,
    table_name);

  auto result = VERIFY_RESULT(conn->Fetch(query));

  return PQgetvalue(result.get(), 0, 4);
}

Status CheckSizeExpectedRange(const std::string& actual_size_string, int64 expected_size) {
  // actual_size_string: value returned from getTableSize query
  // expected_size: expected number of the actual_size_string
  int64 actual_size = 0;

  if (!(actual_size_string.size() > kByteStringSize && actual_size_string.substr(
        actual_size_string.size() - kByteStringSize, kByteStringSize) == " bytes")) {
    if (actual_size_string.size() > 0) {
      std::string units = actual_size_string.substr(
        actual_size_string.size() - kByteUnitStringSize, kByteUnitStringSize);
      actual_size =
        stoi(actual_size_string.substr(0, actual_size_string.size() - kByteUnitStringSize));
      if (units == " kB") {
        actual_size <<= 10;
      } else if (units == " MB") {
        actual_size <<= 20;
      } else if (units == " GB") {
        actual_size <<= 30;
      } else if (units == " TB") {
        actual_size <<= 40;
      }
    }
  } else {
    actual_size = stoi(actual_size_string.substr(0, actual_size_string.size() - kByteStringSize));
  }

  int64 lower = expected_size - expected_size * kSizeBuffer;
  int64 upper = expected_size + expected_size * kSizeBuffer;

  EXPECT_GE(actual_size, lower);
  EXPECT_LE(actual_size, upper);

  return Status::OK();
}

} // namespace

TEST_F(PgTableSizeTest, YB_DISABLE_TEST_IN_TSAN(ColocatedTableSize)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 500;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("Create database $0 with colocated='true'", kColocatedDatabase));
  auto test_conn = ASSERT_RESULT(ConnectToDB(kColocatedDatabase));
  ASSERT_OK(test_conn.ExecuteFormat(
      "CREATE TABLE $0($1 INT, $2 INT, $3 INT)", kTable, kCol1, kCol2, kCol3));

  ASSERT_OK(test_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT t1.i, 1, 1 FROM generate_series(1, 10000) as t1(i)", kTable));
  ASSERT_OK(test_conn.ExecuteFormat("create index $0 on $1($2)", kIndex, kTable, kCol1));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());

  auto table_size = ASSERT_RESULT(GetTableSize(&test_conn, "T", kTable));
  auto index_size = ASSERT_RESULT(GetTableSize(&test_conn, "I", kIndex));

  ASSERT_OK(CheckSizeExpectedRange(table_size, kColocatedTableSize));
  ASSERT_OK(CheckSizeExpectedRange(index_size, kColocatedIndexSize));
}

TEST_F(PgTableSizeTest, YB_DISABLE_TEST_IN_TSAN(TableSize)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 500;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("Create database $0", kDatabase));

  auto test_conn = ASSERT_RESULT(ConnectToDB(kDatabase));
  ASSERT_OK(test_conn.ExecuteFormat(
      "CREATE TABLE $0($1 INT PRIMARY KEY, $2 INT, $3 INT)", kTable, kCol1, kCol2, kCol3));
  ASSERT_OK(test_conn.ExecuteFormat(
    "INSERT INTO $0 SELECT t1.i, 1, 1 FROM generate_series(1, 20000) as t1(i)", kTable));

  ASSERT_OK(test_conn.ExecuteFormat("create index $0 on $1($2)", kIndex, kTable, kCol1));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());

  auto table_size = ASSERT_RESULT(GetTableSize(&test_conn, "T", kTable));
  auto index_size = ASSERT_RESULT(GetTableSize(&test_conn, "I", kIndex));
  auto primary_index_size = ASSERT_RESULT(GetTableSize(&test_conn, "I", kTable + "_pkey"));

  ASSERT_OK(CheckSizeExpectedRange(table_size, kSimpleTableSize));
  ASSERT_OK(CheckSizeExpectedRange(index_size, kSimpleIndexSize));
  ASSERT_OK(CheckSizeExpectedRange(primary_index_size, kSimplePrimaryIndexSize));
}

TEST_F(PgTableSizeTest, YB_DISABLE_TEST_IN_TSAN(TempTableSize)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 500;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("Create database $0", kDatabase));

  auto test_conn = ASSERT_RESULT(ConnectToDB(kDatabase));
  ASSERT_OK(test_conn.ExecuteFormat(
      "CREATE TEMP TABLE $0(id INT, value1 INT, value2 INT)", kTable));
  ASSERT_OK(test_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT t1.i, 1, 1 FROM generate_series(1, 10000) as t1(i)", kTable));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());

  auto table_size = ASSERT_RESULT(GetTableSize(&test_conn, "T", kTable));

  ASSERT_OK(CheckSizeExpectedRange(table_size, kTempTableSize));
}

TEST_F(PgTableSizeTest, YB_DISABLE_TEST_IN_TSAN(PartitionedTableSize)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 500;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("Create database $0", kDatabase));

  auto test_conn = ASSERT_RESULT(ConnectToDB(kDatabase));
  ASSERT_OK(test_conn.ExecuteFormat(
      "CREATE TABLE $0($1 INT, $2 INT, $3 INT) PARTITION BY RANGE ($4)",
      kTable, kCol1, kCol2, kCol3, kCol1));

  const int num_partitions = 5;
  for (int i = 0; i < num_partitions; ++i) {
    int beginning = i * 1000 + 1;
    int end = (i + 1) * 1000 + 1;
    std::string partition_name = Format("$0_$1", kTable, i);
    ASSERT_OK(test_conn.ExecuteFormat(
        "CREATE TABLE $0 partition of $1 FOR VALUES FROM ($2) TO ($3)",
        partition_name, kTable, beginning, end));
  }

  ASSERT_OK(test_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT t1.i, t2.i, 1 FROM generate_series(1, 5000)" \
      "as t1(i), generate_series(1, 4) as t2(i)", kTable));
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());

  for (int i = 0; i < num_partitions; ++i) {
    std::string partition_name = Format("$0_$1", kTable, i);
    auto partition_size = ASSERT_RESULT(GetTableSize(&test_conn, "T", partition_name));
    ASSERT_OK(CheckSizeExpectedRange(partition_size, kPartitionTableSize));
  }

  auto parent_table_size = ASSERT_RESULT(GetTableSize(&test_conn, "T", kTable));
  ASSERT_OK(CheckSizeExpectedRange(parent_table_size, kPartitionParentTableSize));
}

TEST_F(PgTableSizeTest, YB_DISABLE_TEST_IN_TSAN(MaterializedViewSize)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 500;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("Create database $0", kDatabase));

  auto test_conn = ASSERT_RESULT(ConnectToDB(kDatabase));
  ASSERT_OK(test_conn.ExecuteFormat(
      "CREATE TABLE $0($1 INT, $2 INT, $3 INT)", kTable, kCol1, kCol2, kCol3));
  ASSERT_OK(test_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT t1.i, 1, 1 FROM generate_series(1, 10000) as t1(i)", kTable));

  ASSERT_OK(test_conn.ExecuteFormat(
      "CREATE MATERIALIZED VIEW $0 AS SELECT * FROM $1 WHERE $2 > 5000", kView, kTable, kCol1));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());

  auto view_size = ASSERT_RESULT(GetTableSize(&test_conn, "M", kView));
  ASSERT_OK(CheckSizeExpectedRange(view_size, kMaterializedViewSize));

  ASSERT_OK(test_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT t1.i, 1, 1 FROM generate_series(10001, 20000) as t1(i)", kTable));
  ASSERT_OK(test_conn.ExecuteFormat("REFRESH MATERIALIZED VIEW $0", kView));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());

  auto refreshed_view_size = ASSERT_RESULT(GetTableSize(&test_conn, "M", kView));
  ASSERT_OK(CheckSizeExpectedRange(refreshed_view_size,
      kMaterializedViewRefreshSize));

} // namespace

} // namespace pgwrapper
} // namespace yb
