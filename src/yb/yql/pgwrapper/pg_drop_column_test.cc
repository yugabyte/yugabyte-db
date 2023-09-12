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

#include "yb/yql/pgwrapper/libpq_test_base.h"

using std::string;

DECLARE_bool(ysql_ddl_rollback_enabled);

namespace yb {
namespace pgwrapper {

const std::string table = "testtable";

class PgDropColumnSanityTest : public LibPqTestBase {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
      "--allowed_preview_flags_csv=ysql_ddl_rollback_enabled");
    options->extra_tserver_flags.push_back("--ysql_ddl_rollback_enabled=true");
    options->extra_tserver_flags.push_back("--report_ysql_ddl_txn_status_to_master=false");
  }

 public:
  void TestMarkColForDeletion();

 protected:
  virtual void SetupTables();

  void SelectTests();
  void InsertTests();
  void UpdateTests();
  void DeleteTests();
  Status TestDroppedColError(PGConn *conn, string query);
  virtual Result<PGConn> Connect() {
    return LibPqTestBase::Connect();
  }
};

void PgDropColumnSanityTest::TestMarkColForDeletion() {
  SetupTables();
  SelectTests();
  InsertTests();
  UpdateTests();
  DeleteTests();
}

void PgDropColumnSanityTest::SetupTables() {
  auto conn = ASSERT_RESULT(Connect());

  // Create table.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (id INT, col_to_test INT)", table));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0(col_to_test)", table));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0(id) INCLUDE (col_to_test)", table));
  ASSERT_OK(conn.ExecuteFormat(
    "INSERT INTO $0 VALUES (1, 1), (2, 2), (3, 3), (4, 4), (5, 5)", table));
  // Disable rollback.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_disable_ysql_ddl_txn_verification", "true"));
  // Issue Alter Table Drop column.
  ASSERT_OK(conn.TestFailDdl(Format("ALTER TABLE $0 DROP COLUMN col_to_test", table)));
}

void PgDropColumnSanityTest::SelectTests() {
  // Selecting the dropped column must fail.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(TestDroppedColError(&conn, Format("SELECT col_to_test FROM $0", table)));

  // Selecting all columns (using '*') must fail.
  ASSERT_OK(TestDroppedColError(&conn, Format("SELECT * FROM $0", table)));

  // Using the dropped column in the WHERE clause must fail.
  ASSERT_OK(TestDroppedColError(&conn, Format("SELECT id FROM $0 WHERE col_to_test < 3", table)));

  // Merely querying the columns not being deleted must succeed.
  ASSERT_EQ(PQntuples(ASSERT_RESULT(conn.Fetch(Format("SELECT id FROM $0", table))).get()), 5);

  // Querying the dropped column which only involves scan on the index succeeds.
  // (TODO: Deepthi) This should be fixed when tables/indexes can be marked for drop as well as part
  // of #13358.
  string query = Format("SELECT col_to_test FROM $0 WHERE col_to_test=4", table);
  ASSERT_EQ(PQntuples(ASSERT_RESULT(conn.Fetch(query)).get()), 1);

  // Query from the index that includes the deleted column. This passes now because the index is not
  // marked for deletion now, but will be fixed as part of #13358.
  query = Format("SELECT * FROM $0 WHERE id=4", table);
  ASSERT_EQ(PQntuples(ASSERT_RESULT(conn.Fetch(query)).get()), 1);
}

void PgDropColumnSanityTest::InsertTests() {
  auto conn = ASSERT_RESULT(Connect());
  // Creating new rows should fail.
  ASSERT_OK(TestDroppedColError(&conn, Format("INSERT INTO $0 VALUES (6)", table)));
}

void PgDropColumnSanityTest::UpdateTests() {
  auto conn = ASSERT_RESULT(Connect());
  // Update dropped column.
  ASSERT_OK(TestDroppedColError(&conn, Format("UPDATE $0 SET col_to_test=4", table)));

  // Update non-dropped column, with dropped column in WHERE clause.
  ASSERT_OK(TestDroppedColError(&conn, Format("UPDATE $0 SET id=3 WHERE col_to_test=4", table)));

  // Update non-dropped column with expression involving the dropped column.
  ASSERT_OK(TestDroppedColError(&conn, Format("UPDATE $0 SET id=col_to_test+1", table)));

  // Update dropped column with expression on dropped column.
  ASSERT_OK(TestDroppedColError(&conn, Format("UPDATE $0 SET col_to_test=col_to_test+1", table)));

  // Update column not being dropped.
  ASSERT_OK(TestDroppedColError(&conn, Format("UPDATE $0 SET id=3", table)));

  // Update column with WHERE clause not referencing the dropped column.
  ASSERT_OK(TestDroppedColError(&conn, Format("UPDATE $0 SET id=3 WHERE id=1", table)));
}

void PgDropColumnSanityTest::DeleteTests() {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(TestDroppedColError(&conn, Format("DELETE FROM $0 WHERE id=2", table)));
  ASSERT_OK(TestDroppedColError(&conn, Format("DELETE FROM $0 WHERE col_to_test=2", table)));
}

Status PgDropColumnSanityTest::TestDroppedColError(PGConn *conn, string query) {
  Status s = MoveStatus(conn->Execute(query));
  if (s.ok()) {
    return STATUS(IllegalState, "Query succeeded when failure expected", query);
  }

  const std::string msg = s.message().ToBuffer();
  if (s.message().ToBuffer().find("marked for deletion") == std::string::npos) {
    return STATUS_FORMAT(IllegalState, "Query '$0' failed with unexpected error '$1'",
                         query, s.message());
  }
  return Status::OK();
}

TEST_F(PgDropColumnSanityTest, SanityTest) {
  TestMarkColForDeletion();
}

class PgDropReferencingColumnFKTest : public PgDropColumnSanityTest {
 protected:
  void SetupTables() override {
    auto conn = ASSERT_RESULT(Connect());

    // Create table.
    const string ref_table = "ref_table";
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (id INT, col_to_test INT UNIQUE)", table));

    // Wait for DDL verification to complete.
    sleep(2);

    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (id INT, col INT REFERENCES $1(col_to_test))",
                                 ref_table, table));
    // Wait for transaction verification to complete.
    sleep(2);
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 1), (2, 2), (3, 3), (4, 4),"
        " (5, 5), (6, 6), (7, 7)", table));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (6, 6), (7, 7)", ref_table));
    // Disable rollback.
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_disable_ysql_ddl_txn_verification", "true"));
    // Issue Alter Table Drop column.
    ASSERT_OK(conn.TestFailDdl(Format("ALTER TABLE $0 DROP COLUMN col", ref_table)));
  }
};

TEST_F(PgDropReferencingColumnFKTest, FKTest) {
  SetupTables();
  // Test operations on the referenced table that can result in lookups to the
  // referencing table with a column to be deleted.
  DeleteTests();

  auto conn = ASSERT_RESULT(Connect());
  // Updating col_to_test will result in a lookup to the referencing column which is marked for
  // deletion.
  ASSERT_OK(TestDroppedColError(&conn, Format("UPDATE $0 SET col_to_test=9 WHERE id=1", table)));
  ASSERT_OK(TestDroppedColError(&conn, Format("UPDATE $0 SET col_to_test=col_to_test+7", table)));

  ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET id=3 WHERE col_to_test=4", table));
  ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET id=col_to_test+1", table));
  ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET id=3", table));
}

// TODO (deepthi) : Enable the following tests when 2-phase drop column is supported for
// colocated tables and tablegroups.
class PgDropColumnColocatedTableTest : public PgDropColumnSanityTest {
  void SetupTables() override {
    auto conn_init = ASSERT_RESULT(LibPqTestBase::Connect());
    ASSERT_OK(conn_init.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", kDatabaseName));

    PGConn conn = ASSERT_RESULT(Connect());

    // Create table.
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (id INT, col_to_test INT)", table));
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0(col_to_test)", table));
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0(id) INCLUDE (col_to_test)", table));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 1), (2, 2), (3, 3), (4, 4),"
        " (5, 5)", table));
    // Disable rollback.
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_disable_ysql_ddl_txn_verification", "true"));
    // Issue Alter Table Drop column.
    ASSERT_OK(conn.TestFailDdl(Format("ALTER TABLE $0 DROP COLUMN col_to_test", table)));
  }

  Result<PGConn> Connect() override {
    return ConnectToDB(kDatabaseName);
  }

 private:
  std::string kDatabaseName = "colocateddbtest";
};

TEST_F(PgDropColumnColocatedTableTest, ColocatedTest) {
  TestMarkColForDeletion();
}

class PgDropColumnTablegroupTest : public PgDropColumnSanityTest {
 protected:
  void SetupTables() override {
    auto conn = ASSERT_RESULT(Connect());

    // Create table.
    const string& kTablegroup = "tgroup";
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLEGROUP $0", kTablegroup));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (id INT, col_to_test INT) TABLEGROUP $1",
                                table, kTablegroup));
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0(col_to_test)", table));
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0(id) INCLUDE (col_to_test)", table));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 1), (2, 2), (3, 3), (4, 4),"
        " (5, 5)", table));
    // Disable rollback.
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_disable_ysql_ddl_txn_verification", "true"));
    // Issue Alter Table Drop column.
    ASSERT_OK(conn.TestFailDdl(Format("ALTER TABLE $0 DROP COLUMN col_to_test", table)));
  }
};

TEST_F(PgDropColumnTablegroupTest, TablegroupTest) {
  TestMarkColForDeletion();
}

} // namespace pgwrapper
} // namespace yb
