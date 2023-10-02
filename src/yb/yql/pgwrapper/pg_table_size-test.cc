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

#include "yb/consensus/consensus.h"
#include "yb/consensus/log.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/status.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_int32(tserver_heartbeat_metrics_interval_ms);

namespace yb {
namespace pgwrapper {
namespace {

class PgTableSizeTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 500;
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

// Expected size of each table in bytes
constexpr auto kTempTableSize = 464000;

std::string GetTableSizeQuery(const std::string& relationType, const std::string& table_name) {
  // query equivalent to \d+ in ysqlsh -E
  std::string relkind = "'r','p','v','m','S','f',''";
  if (relationType == "T") {
    relkind = "'r','p',''";
  } else if (relationType == "I") {
    relkind = "'I','i',''";
  } else if (relationType == "M") {
    relkind = "'m',''";
  }

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
      pg_catalog.pg_table_size(c.oid) as "Size",
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

  return query;
}


Status VerifyTableSize(MiniCluster* cluster,
                       PGConn* conn,
                       const std::string& relationType,
                       const std::string& table_name) {
  // Wait for heartbeat interval to ensure that the metrics are updated.
  sleep(5);
  auto query = GetTableSizeQuery(relationType, table_name);
  auto result = VERIFY_RESULT(conn->Fetch(query));
  auto pg_size = VERIFY_RESULT(GetValue<PGUint64>(result.get(), 0, 4));

  // Verify that the actual size in DocDB is the same.
  uint64_t size_from_ts = 0;
  auto peers = ListTabletPeers(cluster, ListPeersFilter::kLeaders);
  for (auto& peer : peers) {
    auto tablet = peer->shared_tablet();
    if (tablet && tablet->metadata()->table_name() == table_name) {
      // Get SST files size from tablet server.
      size_from_ts += tablet->GetCurrentVersionSstFilesAllSizes().first;
      // Get WAL files size from tablet server.
      size_from_ts += peer->log()->OnDiskSize();
    }
  }
  if (size_from_ts != pg_size) {
    return STATUS_FORMAT(IllegalState, "Size $0 does not match the size from tablet server $1",
                         pg_size, size_from_ts);
  }
  return Status::OK();
}

Result<uint64_t> GetTempTableSize(
    MiniCluster* cluster, PGConn* conn, const std::string& table_name) {
  auto result = VERIFY_RESULT(conn->Fetch(GetTableSizeQuery("T", table_name)));
  return GetValue<PGUint64>(result.get(), 0, 4);
}

// Verify that the size of the table is not set in the output of pg_table_size.
Status VerifyInvalidTableSize(MiniCluster* cluster,
                              PGConn* conn,
                              const std::string& relationType,
                              const std::string& table_name) {
  auto query = GetTableSizeQuery(relationType, table_name);
  auto result = VERIFY_RESULT(conn->Fetch(query));
  const auto size = std::string(PQgetvalue(result.get(), 0, 4));
  if (!size.empty()) {
    return STATUS_FORMAT(IllegalState,
        "Size of $0 is set to $1 in the output of pg_table_size when invalid expected",
        table_name, size);
  }
  return Status::OK();
}

Status CheckSizeExpectedRange(uint64_t actual_size, uint64_t expected_size) {
  const auto lower = expected_size - expected_size * kSizeBuffer;
  const auto upper = expected_size + expected_size * kSizeBuffer;

  if (actual_size < lower || actual_size > upper) {
    return STATUS_FORMAT(IllegalState, "Size $0 does not fall into the range $1 - $2",
                                     actual_size, lower, upper);
  }
  return Status::OK();
}

} // namespace

TEST_F(PgTableSizeTest, ColocatedTableSize) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("Create database $0 with colocated='true'", kColocatedDatabase));
  auto test_conn = ASSERT_RESULT(ConnectToDB(kColocatedDatabase));
  ASSERT_OK(test_conn.ExecuteFormat(
      "CREATE TABLE $0($1 INT, $2 INT, $3 INT)", kTable, kCol1, kCol2, kCol3));

  ASSERT_OK(test_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT t1.i, 1, 1 FROM generate_series(1, 10000) as t1(i)", kTable));
  ASSERT_OK(test_conn.ExecuteFormat("create index $0 on $1($2)", kIndex, kTable, kCol1));

  ASSERT_OK(cluster_->CompactTablets());

  ASSERT_OK(VerifyInvalidTableSize(cluster_.get(), &test_conn, "T", kTable));
  ASSERT_OK(VerifyInvalidTableSize(cluster_.get(), &test_conn, "I", kIndex));
}

TEST_F(PgTableSizeTest, SimpleTableSize) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("Create database $0", kDatabase));

  auto test_conn = ASSERT_RESULT(ConnectToDB(kDatabase));
  ASSERT_OK(test_conn.ExecuteFormat(
      "CREATE TABLE $0($1 INT PRIMARY KEY, $2 INT, $3 INT)", kTable, kCol1, kCol2, kCol3));
  ASSERT_OK(test_conn.ExecuteFormat(
    "INSERT INTO $0 SELECT t1.i, 1, 1 FROM generate_series(1, 20000) as t1(i)", kTable));

  ASSERT_OK(test_conn.ExecuteFormat("create index $0 on $1($2)", kIndex, kTable, kCol1));

  ASSERT_OK(cluster_->CompactTablets());

  ASSERT_OK(VerifyTableSize(cluster_.get(), &test_conn, "T", kTable));
  ASSERT_OK(VerifyTableSize(cluster_.get(), &test_conn, "I", kIndex));
  ASSERT_OK(VerifyInvalidTableSize(cluster_.get(), &test_conn, "I", kTable + "_pkey"));
}

TEST_F(PgTableSizeTest, TempTableSize) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("Create database $0", kDatabase));

  auto test_conn = ASSERT_RESULT(ConnectToDB(kDatabase));
  ASSERT_OK(test_conn.ExecuteFormat(
      "CREATE TEMP TABLE $0(id INT, value1 INT, value2 INT)", kTable));
  ASSERT_OK(test_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT t1.i, 1, 1 FROM generate_series(1, 10000) as t1(i)", kTable));

  auto table_size = ASSERT_RESULT(GetTempTableSize(cluster_.get(), &test_conn, kTable));
  ASSERT_OK(CheckSizeExpectedRange(table_size, kTempTableSize));
}

TEST_F(PgTableSizeTest, PartitionedTableSize) {
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
  ASSERT_OK(cluster_->CompactTablets());

  for (int i = 0; i < num_partitions; ++i) {
    std::string partition_name = Format("$0_$1", kTable, i);
    ASSERT_OK(VerifyTableSize(cluster_.get(), &test_conn, "T", partition_name));
  }
  ASSERT_OK(VerifyTableSize(cluster_.get(), &test_conn, "T", kTable));
}

TEST_F(PgTableSizeTest, MaterializedViewSize) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("Create database $0", kDatabase));

  auto test_conn = ASSERT_RESULT(ConnectToDB(kDatabase));
  ASSERT_OK(test_conn.ExecuteFormat(
      "CREATE TABLE $0($1 INT, $2 INT, $3 INT)", kTable, kCol1, kCol2, kCol3));
  ASSERT_OK(test_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT t1.i, 1, 1 FROM generate_series(1, 10000) as t1(i)", kTable));

  ASSERT_OK(test_conn.ExecuteFormat(
      "CREATE MATERIALIZED VIEW $0 AS SELECT * FROM $1 WHERE $2 > 5000", kView, kTable, kCol1));

  ASSERT_OK(cluster_->CompactTablets());

  ASSERT_OK(VerifyTableSize(cluster_.get(), &test_conn, "M", kView));

  ASSERT_OK(test_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT t1.i, 1, 1 FROM generate_series(10001, 20000) as t1(i)", kTable));
  ASSERT_OK(test_conn.ExecuteFormat("REFRESH MATERIALIZED VIEW $0", kView));

  ASSERT_OK(cluster_->CompactTablets());

  ASSERT_OK(VerifyTableSize(cluster_.get(), &test_conn, "M", kView));
} // namespace

} // namespace pgwrapper
} // namespace yb
