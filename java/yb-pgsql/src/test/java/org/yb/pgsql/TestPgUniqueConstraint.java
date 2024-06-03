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
//

package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Statement;

import com.google.common.net.HostAndPort;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

@RunWith(YBTestRunner.class)
public class TestPgUniqueConstraint extends BasePgSQLTest {

  @Test
  public void indexIsManaged_unnamed() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test("
          + "i1 int UNIQUE, "
          + "i2 int, "
          + "UNIQUE(i1, i2)"
          + ")");

      // Check that indexes has been created
      assertQuery(stmt,
          "SELECT indexname FROM pg_indexes WHERE tablename='test'",
          new Row("test_i1_key"),
          new Row("test_i1_i2_key"));

      // They cannot be dropped manually...
      runInvalidQuery(stmt, "DROP INDEX test_i1_key", "cannot drop index");
      runInvalidQuery(stmt, "DROP INDEX test_i1_i2_key", "cannot drop index");

      // But are dropped automatically when constraint is dropped
      stmt.execute("ALTER TABLE test DROP CONSTRAINT test_i1_key");
      stmt.execute("ALTER TABLE test DROP CONSTRAINT test_i1_i2_key");
      assertNoRows(stmt, "SELECT indexname FROM pg_indexes WHERE tablename='test'");
    }
  }

  @Test
  public void indexIsManaged_named() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test("
          + "i1 int CONSTRAINT my_constraint_name_1 UNIQUE, "
          + "i2 int, "
          + "CONSTRAINT my_constraint_name_2 UNIQUE(i1, i2)"
          + ")");

      // Check that indexes has been created
      assertQuery(stmt,
          "SELECT indexname FROM pg_indexes WHERE tablename='test'",
          new Row("my_constraint_name_1"),
          new Row("my_constraint_name_2"));

      // They cannot be dropped manually...
      runInvalidQuery(stmt, "DROP INDEX my_constraint_name_1", "cannot drop index");
      runInvalidQuery(stmt, "DROP INDEX my_constraint_name_2", "cannot drop index");

      // But are dropped automatically when constraint is dropped
      stmt.execute("ALTER TABLE test DROP CONSTRAINT my_constraint_name_1");
      stmt.execute("ALTER TABLE test DROP CONSTRAINT my_constraint_name_2");
      assertNoRows(stmt, "SELECT indexname FROM pg_indexes WHERE tablename='test'");
    }
  }

  @Test
  public void indexIsDroppedWithTheTable() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test("
          + "i1 int UNIQUE, "
          + "i2 int, "
          + "UNIQUE(i1, i2)"
          + ")");

      // Check that indexes has been created
      assertQuery(stmt,
          "SELECT indexname FROM pg_indexes WHERE tablename='test'",
          new Row("test_i1_key"),
          new Row("test_i1_i2_key"));

      // But are dropped automatically when table is dropped
      stmt.execute("DROP TABLE test;");
      assertNoRows(stmt, "SELECT indexname FROM pg_indexes WHERE tablename='test'");
      runInvalidQuery(stmt, "DROP INDEX test_i1_key", "does not exist");
      runInvalidQuery(stmt, "DROP INDEX test_i1_i2_key", "does not exist");
    }
  }

  @Test
  public void guaranteesUniqueness() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test("
          + "i1 int UNIQUE, "
          + "i2 int, "
          + "i3 int, "
          + "UNIQUE(i2, i3)"
          + ")");

      stmt.execute("INSERT INTO test(i1, i2, i3) VALUES (1, 1, 1)");
      runInvalidQuery(stmt, "INSERT INTO test(i1, i2, i3) VALUES (1, 2, 2)",
          "duplicate key value violates unique constraint \"test_i1_key\"");
      runInvalidQuery(stmt, "INSERT INTO test(i1, i2, i3) VALUES (2, 1, 1)",
          "duplicate key value violates unique constraint \"test_i2_i3_key\"");
      runInvalidQuery(stmt, "INSERT INTO test(i1, i2, i3) VALUES (1, 1, 1)",
          "duplicate key value violates unique constraint \"test_i1_key\"");
      stmt.execute("INSERT INTO test(i1, i2, i3) VALUES (2, 2, 2)");
      assertQuery(stmt,
          "SELECT * FROM test ORDER BY i1, i2, i3",
          new Row(1, 1, 1),
          new Row(2, 2, 2));
    }
  }

  @Test
  public void multipleNullsAreAllowed() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Singular column UNIQUE constraint
      stmt.execute("CREATE TABLE test1("
          + "i int UNIQUE"
          + ")");
      stmt.execute("INSERT INTO test1 VALUES (NULL)");
      stmt.execute("INSERT INTO test1 VALUES (NULL)");
      assertQuery(stmt,
          "SELECT * FROM test1",
          new Row((Comparable<?>)null),
          new Row((Comparable<?>)null));

      // Multi-column UNIQUE constraint
      stmt.execute("CREATE TABLE test2("
          + "i2 int, "
          + "i3 int, "
          + "UNIQUE(i2, i3)"
          + ")");
      stmt.execute("INSERT INTO test2 VALUES (1, 1)");
      stmt.execute("INSERT INTO test2 VALUES (NULL, 1)");
      stmt.execute("INSERT INTO test2 VALUES (NULL, 1)");
      stmt.execute("INSERT INTO test2 VALUES (1, NULL)");
      stmt.execute("INSERT INTO test2 VALUES (1, NULL)");
      runInvalidQuery(stmt, "INSERT INTO test2 VALUES (1, 1)", "duplicate");
      assertQuery(stmt,
          "SELECT * FROM test2 ORDER BY i2, i3",
          new Row(1, 1),
          new Row(1, null),
          new Row(1, null),
          new Row(null, 1),
          new Row(null, 1));
    }
  }

  @Test
  public void constraintDropAllowsDuplicates() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test("
          + "i1 int UNIQUE, "
          + "i2 int, "
          + "i3 int, "
          + "UNIQUE(i2, i3)"
          + ")");

      stmt.execute("INSERT INTO test(i1, i2, i3) VALUES (1, 1, 1)");

      stmt.execute("ALTER TABLE test DROP CONSTRAINT test_i1_key");
      stmt.execute("ALTER TABLE test DROP CONSTRAINT test_i2_i3_key");

      stmt.execute("INSERT INTO test(i1, i2, i3) VALUES (1, 2, 2)");
      stmt.execute("INSERT INTO test(i1, i2, i3) VALUES (2, 1, 1)");
      stmt.execute("INSERT INTO test(i1, i2, i3) VALUES (1, 1, 1)");
      assertQuery(stmt,
          "SELECT * FROM test ORDER BY i1, i2, i3",
          new Row(1, 1, 1),
          new Row(1, 1, 1),
          new Row(1, 2, 2),
          new Row(2, 1, 1));
    }
  }

  @Test
  public void addConstraint() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test("
          + "i1 int, "
          + "i2 int"
          + ")");
      stmt.execute("ALTER TABLE test ADD CONSTRAINT test_constr UNIQUE (i1, i2)");

      // Index is created
      assertQuery(stmt,
          "SELECT indexname FROM pg_indexes WHERE tablename='test'",
          new Row("test_constr"));

      // Uniqueness should work
      stmt.execute("INSERT INTO test(i1, i2) VALUES (1, 1)");
      stmt.execute("INSERT INTO test(i1, i2) VALUES (2, 2)");
      runInvalidQuery(stmt, "INSERT INTO test(i1, i2) VALUES (1, 1)", "duplicate");
      assertQuery(stmt,
          "SELECT * FROM test ORDER BY i1, i2",
          new Row(1, 1),
          new Row(2, 2));

      // Index should only be dropped when dropping constraint
      runInvalidQuery(stmt, "DROP INDEX test_constr", "cannot drop index");
      stmt.execute("ALTER TABLE test DROP CONSTRAINT test_constr");
      assertNoRows(stmt, "SELECT indexname FROM pg_indexes WHERE tablename='test'");

      // Uniqueness is no longer enforced
      stmt.execute("INSERT INTO test(i1, i2) VALUES (1, 1)");
      assertQuery(stmt,
          "SELECT * FROM test ORDER BY i1, i2",
          new Row(1, 1),
          new Row(1, 1),
          new Row(2, 2));
    }
  }

  @Test
  public void addConstraintUsingExistingIndex() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test("
          + "i1 int, "
          + "i2 int"
          + ")");
      stmt.execute("CREATE UNIQUE INDEX test_idx ON test (i1 ASC, i2)");
      // This will also rename index "test_idx" to "test_constr"
      stmt.execute("ALTER TABLE test ADD CONSTRAINT test_constr UNIQUE USING INDEX test_idx");

      // Index should be renamed
      assertQuery(stmt,
          "SELECT indexname FROM pg_indexes WHERE tablename='test'",
          new Row("test_constr"));

      // Uniqueness should work
      stmt.execute("INSERT INTO test(i1, i2) VALUES (1, 1)");
      stmt.execute("INSERT INTO test(i1, i2) VALUES (2, 2)");
      runInvalidQuery(stmt, "INSERT INTO test(i1, i2) VALUES (1, 1)", "duplicate");
      assertQuery(stmt,
          "SELECT * FROM test ORDER BY i1, i2",
          new Row(1, 1),
          new Row(2, 2));

      // Index should only be dropped when dropping constraint
      runInvalidQuery(stmt, "DROP INDEX test_constr", "cannot drop index");
      stmt.execute("ALTER TABLE test DROP CONSTRAINT test_constr");
      assertNoRows(stmt, "SELECT indexname FROM pg_indexes WHERE tablename='test'");

      // Uniqueness is no longer enforced
      stmt.execute("INSERT INTO test(i1, i2) VALUES (1, 1)");
      assertQuery(stmt,
          "SELECT * FROM test ORDER BY i1, i2",
          new Row(1, 1),
          new Row(1, 1),
          new Row(2, 2));
    }
  }

  @Test
  public void addUniqueWithInclude() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test(i1 int, i2 int, i3 int)");
      stmt.execute("ALTER TABLE test ADD CONSTRAINT test_constr UNIQUE (i3) INCLUDE (i1, i2)");

      // Check that index is created properly
      assertQuery(
          stmt,
          "SELECT pg_get_indexdef(i.indexrelid)" +
              " FROM pg_index i" +
              " WHERE i.indrelid = 'test'::regclass",
          new Row("CREATE UNIQUE INDEX test_constr ON public.test " +
                      "USING lsm (i3 HASH) INCLUDE (i1, i2)")
      );

      // Valid DMLs
      stmt.execute("INSERT INTO test(i3, i2) VALUES (1, 3), (2, 4), (3, 3), (4, NULL)");
      stmt.execute("UPDATE test SET i2 = 2 WHERE i2 = 4");
      stmt.execute("UPDATE test SET i1 = 1 WHERE i1 IS NULL AND i3 = 4");

      // Invalid DMLs
      runInvalidQuery(stmt, "INSERT INTO test(i3, i2) VALUES (1, 3)", "duplicate");
      runInvalidQuery(stmt, "INSERT INTO test(i3, i2) VALUES (4, NULL)", "duplicate");
      runInvalidQuery(stmt, "INSERT INTO test(i3) VALUES (4)", "duplicate");

      // Check the resulting table content
      assertQuery(stmt, "SELECT * FROM test ORDER BY i3, i2, i1",
          new Row(null, 3, 1),
          new Row(null, 2, 2),
          new Row(null, 3, 3),
          new Row(1, null, 4));

      // Selection containing inequality (<) on i1 is a full table scan until we support proper
      // range scan on index.
      assertQuery(
          stmt,
          "EXPLAIN (COSTS OFF) SELECT * FROM test WHERE (i3, i2) < (4, 4)",
          new Row("Seq Scan on test"),
          new Row("  Filter: (ROW(i3, i2) < ROW(4, 4))")
      );

      // Switch to a unique index without importing i2
      stmt.execute("ALTER TABLE test DROP CONSTRAINT test_constr");
      stmt.execute("ALTER TABLE test ADD CONSTRAINT test_constr UNIQUE (i3)");

      // Selection containing inequality (<) on i1 is a full table scan until we support proper
      // range scan on index.
      assertQuery(
          stmt,
          "EXPLAIN (COSTS OFF) SELECT * FROM test WHERE (i3, i2) < (4, 4)",
          new Row("Seq Scan on test"),
          new Row("  Filter: (ROW(i3, i2) < ROW(4, 4))")
      );
    }
  }

  @Test
  public void addUniqueWithExistingDuplicateFails() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test(i1 int, i2 int)");

      // Add duplicate values for i1
      stmt.execute("INSERT INTO test(i1, i2) VALUES (1, 1), (1, 2)");

      // Constraint cannot be added
      runInvalidQuery(stmt, "ALTER TABLE test ADD CONSTRAINT test_constr UNIQUE (i1)", "duplicate");
    }
  }

  @Test
  public void addUniqueConstraintAttributes() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test(i1 int, i2 int)");
      stmt.execute("CREATE UNIQUE INDEX test_idx ON test (i2 ASC)");

      runInvalidQuery(
          stmt,
          "ALTER TABLE test ADD CONSTRAINT test_constr UNIQUE " +
              "USING INDEX test_idx DEFERRABLE INITIALLY DEFERRED",
          "DEFERRABLE unique constraints are not supported yet"
      );
      runInvalidQuery(
          stmt,
          "ALTER TABLE test ADD CONSTRAINT test_constr UNIQUE " +
              "USING INDEX test_idx DEFERRABLE INITIALLY IMMEDIATE",
          "DEFERRABLE unique constraints are not supported yet"
      );
      runInvalidQuery(
          stmt,
          "ALTER TABLE test ADD CONSTRAINT test_constr UNIQUE " +
              "USING INDEX test_idx INITIALLY DEFERRED",
          "DEFERRABLE unique constraints are not supported yet"
      );
      stmt.execute("ALTER TABLE test ADD CONSTRAINT test_constr UNIQUE " +
          "USING INDEX test_idx NOT DEFERRABLE");

      stmt.execute("INSERT INTO test(i1, i2) VALUES (1, 1)");

      stmt.execute("BEGIN");
      // Insert fails immediately
      runInvalidQuery(stmt, "INSERT INTO test(i1, i2) VALUES (2, 1)", "duplicate");
      stmt.execute("COMMIT");
    }
  }

  @Test
  public void createIndexViolatingUniqueness() throws Exception {
    long tableOid;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test(id int PRIMARY KEY, v int)");

      // Get the OID of the 'test' table from pg_class
      tableOid = getRowList(
          stmt.executeQuery("SELECT oid FROM pg_class WHERE relname = 'test'")).get(0).getLong(0);

      // Two entries in pg_depend table, one for pg_type and the other for pg_constraint
      assertQuery(stmt, "SELECT COUNT(*) FROM pg_depend WHERE refobjid=" + tableOid,
          new Row(2));

      stmt.executeUpdate("INSERT INTO test VALUES (1, 1)");
      stmt.executeUpdate("INSERT INTO test VALUES (2, 1)");
    }

    // Disabling transactional DDL GC flag to avoid CREATE UNIQUE INDEX
    // failure from triggering table deletion. The deletion occurs
    // as a background thread which can produce inconsistent schema
    // verison between the Postgres and TServer side. To avoid this issue
    // DDL transaction will only rollback if it fails but skip the master side
    // clean up work.
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(miniCluster.getClient().setFlag(hp,
          "enable_transactional_ddl_gc", "false"));
    }
    for (HostAndPort hp : miniCluster.getTabletServers().keySet()) {
      if (isTestRunningWithConnectionManager())
        assertTrue(miniCluster.getClient().setFlag(hp,
            "allowed_preview_flags_csv", "ysql_yb_ddl_rollback_enabled,enable_ysql_conn_mgr"));
      else
        assertTrue(miniCluster.getClient().setFlag(hp,
            "allowed_preview_flags_csv", "ysql_yb_ddl_rollback_enabled=true"));
      assertTrue(miniCluster.getClient().setFlag(hp,
          "ysql_yb_ddl_rollback_enabled", "false"));
    }
    try (Statement stmt = connection.createStatement()) {
      runInvalidQuery(
          stmt,
          "CREATE UNIQUE INDEX NONCONCURRENTLY test_v on test(v)",
          "duplicate key"
      );
    }
    // Reset flags.
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(miniCluster.getClient().setFlag(hp,
          "enable_transactional_ddl_gc", "true"));
    }

    try (Statement stmt = connection.createStatement()) {
      // Make sure index has no leftovers
      runInvalidQuery(stmt, "DROP INDEX test_v", "does not exist");
      assertNoRows(stmt, "SELECT oid FROM pg_class WHERE relname = 'test_v'");
      assertQuery(stmt, "SELECT COUNT(*) FROM pg_depend WHERE refobjid=" + tableOid,
          new Row(2));
      assertQuery(stmt, "SELECT * FROM test WHERE v = 1",
          new Row(1, 1), new Row(2, 1));

      // We can still insert duplicate elements
      stmt.executeUpdate("INSERT INTO test VALUES (3, 1)");
      assertQuery(stmt, "SELECT * FROM test WHERE v = 1",
          new Row(1, 1), new Row(2, 1), new Row(3, 1));
    }
  }
}
