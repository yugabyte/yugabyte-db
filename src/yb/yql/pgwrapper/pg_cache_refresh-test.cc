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

#include "yb/util/scope_exit.h"
#include "yb/util/tsan_util.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"

DECLARE_int32(heartbeat_interval_ms);

using namespace std::literals;
using std::string;

namespace yb {
namespace pgwrapper {

namespace {
const auto kTableName = "test"s;
}

class PgCacheRefreshTest : public LibPqTestBase {
 protected:
  void TestSetup(PGConn* conn) {
    ASSERT_OK(conn->ExecuteFormat("CREATE TABLE $0(id int)", kTableName));
    // Perform an insert to ensure that caches get populated.
    ASSERT_OK(executeInsert(conn));
  }

  void testConcurrentDDL(const string& col_name) {
    auto aux_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(aux_conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 TEXT", kTableName, col_name));
  }

  void testConcurrentSchemaVersionIncrement(const string& col_name) {
    auto aux_conn = ASSERT_RESULT(Connect());
    // The following statement will fail because the table is currently not empty. The NOT NULL
    // constraint will be violated. This will cause the Alter operation at the DocDB side to roll
    // back. Thus although the catalog version is not incremented due to this failed alter, the
    // DocDB schema_version will get incremented twice - once to add the column and once to drop
    // the column during rollback.
    ASSERT_NOK(aux_conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 TEXT NOT NULL",
                                      kTableName, col_name));
  }

  using DdlFunction = std::function<void(void)>;

  void runOnDifferentNode(const DdlFunction& ddl_func) {
    auto pg_ts_restorer = ScopeExit([this, pg_ts_old = pg_ts] {
      pg_ts = pg_ts_old;
    });
    pg_ts = cluster_->tablet_server(1);
    ddl_func();
  }

  void testConcurrentDDLFromDifferentNode(const string& col_name) {
    runOnDifferentNode([this, &col_name] {
      testConcurrentDDL(col_name);
    });
  }

  void testConcurrentSchemaVersionIncrementFromDifferentNode(const string& col_name) {
    runOnDifferentNode([this, &col_name] {
      testConcurrentSchemaVersionIncrement(col_name);
    });
  }

  Status executeInsert() {
    auto conn = VERIFY_RESULT(Connect());
    return executeInsert(&conn);
  }

  Status executeInsert(PGConn* conn) {
    return conn->ExecuteFormat("INSERT INTO $0 VALUES (1)", kTableName);
  }

  void testTxnConcurrentWithDDL(const DdlFunction& ddl_func) {
    auto conn = ASSERT_RESULT(Connect());
    testTxnConcurrentWithDDL(&conn, ddl_func);
  }

  void testTxnConcurrentWithDDL(PGConn* conn, const DdlFunction& ddl_func) {
    // Start a DML transaction.
    ASSERT_OK(conn->Execute("BEGIN"));
    ASSERT_OK(executeInsert(conn));

    // Run concurrent DDL.
    ddl_func();

    // Commit fails due to concurrent DDL.
    ASSERT_NOK(conn->Execute("COMMIT"));
    ASSERT_OK(conn->Execute("ROLLBACK"));

    // Retrying transaction works.
    ASSERT_OK(conn->Execute("BEGIN"));
    ASSERT_OK(executeInsert(conn));
    ASSERT_OK(conn->Execute("COMMIT"));
  }

  void testSuccessfulTxnSchemaVersionMismatch(const DdlFunction& ddl_func) {
    auto conn = ASSERT_RESULT(Connect());

    // Run concurrent DDL.
    ddl_func();

    // No schema version mismatch. This is because Alter operation issued to the TServer
    // invalidates the table cache.
    ASSERT_OK(conn.Execute("BEGIN"));
    ASSERT_OK(executeInsert(&conn));
    ASSERT_OK(conn.Execute("COMMIT"));
  }

  void testTxnRetryAfterSchemaVersionMismatch(PGConn* conn, const DdlFunction& ddl_func) {
    ddl_func();

    ASSERT_OK(conn->Execute("BEGIN"));
    // This will fail due to schema version mismatch.
    ASSERT_NOK(executeInsert(conn));
    ASSERT_OK(conn->Execute("ROLLBACK"));

    // Retrying transaction works.
    ASSERT_OK(conn->Execute("BEGIN"));
    ASSERT_OK(executeInsert(conn));
    ASSERT_OK(conn->Execute("COMMIT"));
  }
};

TEST_F(PgCacheRefreshTest, ExistingConnectionTransparentRetry) {
  // Create a table.
  auto stmt_conn = ASSERT_RESULT(Connect());
  TestSetup(&stmt_conn);

  // The connection has the table schema cached.
  // Now in a different connection, run a DDL statement. The insert fails due to schema version
  // mismatch, but will internally retry the operation and succeed, transparently to the client.
  testConcurrentDDL("col1");
  ASSERT_OK(executeInsert(&stmt_conn));

  // In a different connection to a different node, run a DDL statement.
  // We expect same behavior as above.
  testConcurrentDDLFromDifferentNode("col2");
  ASSERT_OK(executeInsert(&stmt_conn));

  // Run failed alter in a different connection.
  // We expect same behavior as above.
  testConcurrentSchemaVersionIncrement("col3");
  ASSERT_OK(executeInsert(&stmt_conn));

  // Run failed alter in a different connection to a different node.
  // We expect same behavior as above.
  testConcurrentSchemaVersionIncrement("col4");
  ASSERT_OK(executeInsert(&stmt_conn));
}

TEST_F(PgCacheRefreshTest, NewConnectionTransparentRetry) {
  auto stmt_conn = ASSERT_RESULT(Connect());
  TestSetup(&stmt_conn);

  // Note that this test looks the same as the previous test, but differs in the fact that every
  // DML operation takes place on a new connection.
  // In a different connection, run a DDL statement.
  testConcurrentDDL("col1");
  // Run Insert from a new connection.
  ASSERT_OK(executeInsert());

  // In a different connection to a different node, run a DDL statement.
  testConcurrentDDLFromDifferentNode("col2");
  // TODO: Test commented out due to #14327
  // ASSERT_OK(executeInsert());

  // Cause schema version increment in a different connection to the same TServer.
  testConcurrentSchemaVersionIncrement("col3");
  ASSERT_OK(executeInsert());

  // Cause schema version increment in a different connection to a different TServer.
  testConcurrentSchemaVersionIncrementFromDifferentNode("col4");
  ASSERT_OK(executeInsert());
}

TEST_F(PgCacheRefreshTest, ExistingConnTransparentRetryTxn) {
  auto stmt_conn = ASSERT_RESULT(Connect());
  TestSetup(&stmt_conn);

  // Test DML transaction interleaved with a DDL operation running on a new connection to the
  // same TServer.
  testTxnConcurrentWithDDL(&stmt_conn, [this] {
    testConcurrentDDL("col1");
  });

  // Test DML transaction interleaved with a DDL operation running on a new connection to a
  // different TServer.
  testTxnConcurrentWithDDL(&stmt_conn, [this] {
    testConcurrentDDLFromDifferentNode("col2");
  });

  // Test DML transaction interleaved with an operation that only causes schema version mismatch
  // through a connection to the same TServer.
  testTxnRetryAfterSchemaVersionMismatch(&stmt_conn, [this] {
    testConcurrentSchemaVersionIncrement("col3");
  });

  // Test DML transaction interleaved with an operation that only causes schema version mismatch
  // through a connection to a different TServer.
  testTxnRetryAfterSchemaVersionMismatch(&stmt_conn, [this] {
    testConcurrentSchemaVersionIncrementFromDifferentNode("col4");
  });
}

TEST_F(PgCacheRefreshTest, NewConnectionTransparentRetryTxn) {
  auto stmt_conn = ASSERT_RESULT(Connect());
  TestSetup(&stmt_conn);

  // Note that this test looks the same as the previous test, but differs in the fact that every
  // DML operation takes place on a new connection.
  // Test DML transaction interleaved with a DDL operation running on a new connection to the
  // same TServer.
  testTxnConcurrentWithDDL([this] {
    testConcurrentDDL("col1");
  });

  // Test DML transaction interleaved with a DDL operation running on a new connection to a
  // different TServer.
  testTxnConcurrentWithDDL([this] {
    testConcurrentDDLFromDifferentNode("col2");
  });

  // Wait for heartbeat to propagate across all the TServers and invalidate the table cache
  // across all nodes.
  SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_heartbeat_interval_ms));

  // Test DML transaction interleaved with an operation that only causes schema version mismatch
  // through a connection to the same TServer. Here the alter operation causes table cache
  // invalidation on the TServer, therefore no retry is needed.
  testSuccessfulTxnSchemaVersionMismatch([this] {
    testConcurrentSchemaVersionIncrement("col3");
  });

  // Test DML transaction interleaved with an operation that only causes schema version mismatch
  // through a connection to a different TServer.
  auto conn = ASSERT_RESULT(Connect());
  testTxnRetryAfterSchemaVersionMismatch(&conn, [this] {
    testConcurrentSchemaVersionIncrementFromDifferentNode("col4");
  });
}

} // namespace pgwrapper
} // namespace yb
