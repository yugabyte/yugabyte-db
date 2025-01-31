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

import static org.yb.AssertionWrappers.*;

import org.hamcrest.CoreMatchers;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import com.yugabyte.util.PSQLException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBCluster;
import org.yb.util.ThrowingRunnable;
import org.yb.YBTestRunner;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.IntStream;

@RunWith(value = YBTestRunner.class)
public class TestPgCacheConsistency extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgCacheConsistency.class);

  @Test
  public void testBasicDDLOperations() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement statement1 = connection1.createStatement();
         Statement statement2 = connection2.createStatement()) {
      Set<Row> expectedRows = new HashSet<>();

      // Create a table with connection 1.
      statement1.execute("CREATE TABLE cache_test1(a int)");

      // Ensure table is usable from both connections.
      statement1.execute("INSERT INTO cache_test1(a) VALUES (1)");
      expectedRows.add(new Row(1));
      statement2.execute("INSERT INTO cache_test1(a) VALUES (2)");
      expectedRows.add(new Row(2));

      // Check values.
      try (ResultSet rs = statement1.executeQuery("SELECT * FROM cache_test1")) {
        assertEquals(expectedRows, getRowSet(rs));
      }
      expectedRows.clear();

      // Drop table from connection 2.
      statement2.execute("DROP TABLE cache_test1");

      // Check that insert now fails on both connections.
      runInvalidQuery(statement1, "INSERT INTO cache_test1(a) VALUES (3)", "does not exist");
      runInvalidQuery(statement2, "INSERT INTO cache_test1(a) VALUES (4)", "does not exist");

      // Create and use a new table on connection 1.
      statement1.execute("CREATE TABLE cache_test2(a int)");
      statement1.execute("INSERT INTO cache_test2(a) VALUES (1)");

      // Drop and create a same-name table on connection 2.
      statement2.execute("DROP TABLE cache_test2");
      statement2.execute("CREATE TABLE cache_test2(a float)");

      // Check that can use new table on both connections.
      statement1.execute("INSERT INTO cache_test2(a) VALUES (1)");
      expectedRows.add(new Row(1.0));
      statement2.execute("INSERT INTO cache_test2(a) VALUES (2)");
      expectedRows.add(new Row(2.0));

      // Check values.
      try (ResultSet rs = statement1.executeQuery("SELECT * FROM cache_test2")) {
        assertEquals(expectedRows, getRowSet(rs));
      }
      expectedRows.clear();

      // Drop and create a same-name table on connection 1.
      statement1.execute("DROP TABLE cache_test2");
      statement1.execute("CREATE TABLE cache_test2(a bool)");

      // Check that we cannot still insert a float (but that bool will work).
      runInvalidQuery(statement2, "INSERT INTO cache_test2(a) VALUES (1.0)",
          "type boolean but expression is of type numeric");
      statement2.execute("INSERT INTO cache_test2(a) VALUES (true)");
      expectedRows.add(new Row(true));
      statement1.execute("INSERT INTO cache_test2(a) VALUES (false)");
      expectedRows.add(new Row(false));

      // Check values.
      try (ResultSet rs = statement2.executeQuery("SELECT * FROM cache_test2")) {
        assertEquals(expectedRows, getRowSet(rs));
      }
      expectedRows.clear();

      // Alter the table to add a column.
      statement1.execute("ALTER TABLE cache_test2 ADD COLUMN b int");
      expectedRows.add(new Row(true, null));
      expectedRows.add(new Row(false, null));

      // This may or may not fail, depending on whether the catalog version has been
      // propagated to the associated tablet server. If so, we expect a refresh prior
      // to execution, otherwise a failure will occur during execution.
      try {
        statement2.execute("INSERT INTO cache_test2(a,b) VALUES (true, 11)");
        expectedRows.add(new Row(true, 11));
      } catch (PSQLException psqle) {
        // Any failure should be due to a catalog version mismatch.
        assertThat(
            psqle.getMessage(),
            CoreMatchers.containsString("Catalog Version Mismatch")
        );
      }

      // Second attempt should always succeed.
      statement2.execute("INSERT INTO cache_test2(a,b) VALUES (false, 12)");
      expectedRows.add(new Row(false, 12));

      // Check values.
      try (ResultSet rs = statement1.executeQuery("SELECT * FROM cache_test2")) {
        assertEquals(expectedRows, getRowSet(rs));
      }
      expectedRows.clear();

      // Test functions.

      // Create a function on connection 1.
      statement1.execute("create or replace function inc(n in integer)\n" +
                                 "  returns integer\n" +
                                 "  language 'plpgsql'\n" +
                                 "as $$\n" +
                                 "begin\n" +
                                 "  return n + 1;\n" +
                                 "end\n" +
                                 "$$");

      // Check result from connection 1.
      expectedRows.add(new Row( 11));
      try (ResultSet rs = statement1.executeQuery("SELECT inc(10)")) {
        assertEquals(expectedRows, getRowSet(rs));
      }
      expectedRows.clear();

      // Check result from connection 2.
      expectedRows.add(new Row( 16));
      try (ResultSet rs = statement2.executeQuery("SELECT inc(15)")) {
        assertEquals(expectedRows, getRowSet(rs));
      }
      expectedRows.clear();

      // Alter (replace) the function (increment 1 -> 101) from connection 2.
      statement2.execute("create or replace function inc(n in integer)\n" +
                                 "  returns integer\n" +
                                 "  language 'plpgsql'\n" +
                                 "as $$\n" +
                                 "begin\n" +
                                 "  return n + 101;\n" +
                                 "end\n" +
                                 "$$");

      // Wait for tserver heartbeat to propagate the catalog version.
      waitForTServerHeartbeat();

      // Check result from connection 1.
      expectedRows.add(new Row( 111));
      try (ResultSet rs = statement1.executeQuery("SELECT inc(10)")) {
        assertEquals(expectedRows, getRowSet(rs));
      }
      expectedRows.clear();

      // Check result from connection 2.
      expectedRows.add(new Row( 116));
      try (ResultSet rs = statement2.executeQuery("SELECT inc(15)")) {
        assertEquals(expectedRows, getRowSet(rs));
      }
      expectedRows.clear();
    }
  }

  @Test
  public void testVersionMismatchWithoutRetry() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement statement1 = connection1.createStatement();
         Statement statement2 = connection2.createStatement()) {
      statement1.execute("CREATE TABLE test_table(id int, PRIMARY KEY (id))");
      statement1.execute("INSERT INTO test_table(id) VALUES (1), (2), (3)");

      waitForTServerHeartbeat();

      // Select refreshes cache and discovers new table.
      assertQuery(
          statement2,
          "SELECT * FROM test_table",
          new Row(1),
          new Row(2),
          new Row(3)
      );

      statement2.execute("ALTER TABLE test_table ADD COLUMN c1 int");

      waitForTServerHeartbeat();

      // Select refreshes cache and discovers new column.
      assertQuery(
          statement1,
          "SELECT * FROM test_table",
          new Row(1, null),
          new Row(2, null),
          new Row(3, null)
      );

      statement1.execute("ALTER TABLE test_table ADD COLUMN c2 int");

      waitForTServerHeartbeat();

      // Insert refreshes cache and discovers new column.
      statement2.execute("INSERT INTO test_table(id, c1, c2) VALUES (4, 5, 6)");

      assertQuery(
          statement1,
          "SELECT * FROM test_table WHERE id = 4",
          new Row(4, 5, 6)
      );

      statement1.execute("DROP TABLE test_table");

      waitForTServerHeartbeat();

      // Create table refreshes cache and succeeds.
      statement2.execute("CREATE TABLE test_table(id int, PRIMARY KEY (id))");
    }
  }

  @Test
  public void testVersionMismatchWithFailedRetry() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement statement1 = connection1.createStatement();
         Statement statement2 = connection2.createStatement()) {
      // Create table from connection 1.
      statement1.execute("CREATE TABLE test_table(id int)");

      waitForTServerHeartbeat();

      // Force a cache refresh on connection 2.
      statement2.execute("SELECT * FROM test_table");

      final int attempts = 5;
      List<Throwable> errors = IntStream.range(0, attempts)
          .boxed()
          .map((i) -> captureThrow(() -> {
            // Add some artificial delay to space out attempts.
            Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS / attempts);

            // Add new row from connection 1.
            statement1.execute("ALTER TABLE test_table ADD COLUMN x" + i + " int");

            // Immediately try selecting row from connection 2.
            statement2.execute("SELECT x" + i + " FROM test_table");
          }))
          .filter(Optional::isPresent)
          .map(Optional::get)
          .collect(Collectors.toList());

      if (isTestRunningWithConnectionManager()) {
        // Due to DDL sleeps with Connection Manager, all select statements
        // should succeed.
        assertEquals("No failures are expected with Connection Manager enabled",
         errors.size(),
         0);
      } else {
        // At least half the select statements should fail.
        assertGreaterThanOrEqualTo(
            String.format(
                "Expected at least %d failures out of %d attempts, got %d",
                attempts / 2,
                attempts,
                errors.size()
              ),
            errors.size(),
            attempts / 2
          );
      }

      // All errors should be catalog version mismatches.
      for (Throwable error : errors) {
        assertThat(
            error.getMessage(),
            CoreMatchers.containsString("Catalog Version Mismatch")
        );
      }
    }
  }

  @Ignore // TODO enable after #1502
  public void testUndetectedSelectVersionMismatch() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement statement1 = connection1.createStatement();
         Statement statement2 = connection2.createStatement()) {
      // Create table from connection 1.
      statement1.execute("CREATE TABLE test_table(id int, PRIMARY KEY (id))");

      waitForTServerHeartbeat();

      // Force a cache refresh on connection 2.
      assertQuery(statement2, "SELECT * FROM test_table");

      // Add a column and insert a row from connection 1.
      statement1.execute("ALTER TABLE test_table ADD COLUMN b int");
      statement1.execute("INSERT INTO test_table(id, b) VALUES (1, 2)");

      // Select statement immediately observes the new column, without waiting for a heartbeat.
      assertQuery(statement2, "SELECT * FROM test_table", new Row(1, 2));
    }
  }

  @Test
  public void testConsistentNonRetryableTransactions() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement statement1 = connection1.createStatement();
         Statement statement2 = connection2.createStatement()) {
      // Create table from connection 1.
      statement1.execute("CREATE TABLE test_table(id int, PRIMARY KEY (id))");

      waitForTServerHeartbeat();

      statement2.execute("BEGIN");
      // Perform a DDL operation, which cannot (as of 07/01/2019) be rolled back.
      statement2.execute("CREATE TABLE other_table(id int)");

      statement2.execute("SELECT * FROM test_table");

      // Modify table from connection 2.
      statement1.execute("ALTER TABLE test_table ADD COLUMN c int");

      waitForTServerHeartbeat();

      // Select should fail because the alter modified the table (catalog version mismatch).
      runInvalidQuery(statement2,"SELECT * FROM test_table", "Catalog Version Mismatch");

      // COMMIT will succeed as a command but will rollback the transaction due to the error above.
      statement2.execute("COMMIT");

      // Check that the other table was created.
      statement2.execute("SELECT * FROM other_table");
    }
  }

  @Test
  public void testConsistentPreparedStatements() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement statement1 = connection1.createStatement();
         Statement statement2 = connection2.createStatement()) {
      // Create table from connection 1.
      statement1.execute("CREATE TABLE test_table(id int, PRIMARY KEY (id))");

      waitForTServerHeartbeat();

      // Force a cache refresh on connection 2.
      statement2.execute("SELECT * FROM test_table");

      // Alter table from connection 1.
      statement1.execute("ALTER TABLE test_table ADD COLUMN b int");
      statement1.execute("INSERT INTO test_table(id, b) VALUES (0, 0)");

      waitForTServerHeartbeat();

      // Preparing from connection 2 includes column added from connection 1.
      statement2.execute("PREPARE plan (int) AS SELECT * FROM test_table where id=$1");
      assertQuery(statement2, "EXECUTE plan(0)", new Row(0, 0));

      // Alter table from connection 1.
      statement1.execute("ALTER TABLE test_table ADD COLUMN c int");
      statement1.execute("INSERT INTO test_table(id, b, c) VALUES (1, 2, 3), (2, 3, 4)");

      waitForTServerHeartbeat();

      // TODO enable after #1502
      if (false) {
        // Cache reload from connection 2 reveals prepared statement is no longer valid.
        runInvalidQuery(statement2, "EXECUTE plan(1)", "cached plan");
      }

      // Re-create plan.
      statement2.execute("PREPARE plan_new (int) AS SELECT * FROM test_table where id=$1");

      // New execution is successful.
      assertQuery(statement2, "EXECUTE plan_new(1)", new Row(1, 2, 3));
    }
  }

  @Test
  public void testConsistentExplain() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement statement1 = connection1.createStatement();
         Statement statement2 = connection2.createStatement()) {
      // Create table with unique column from connection 1.
      statement1.execute("CREATE TABLE test_table(id int, u int)");
      statement1.execute("ALTER TABLE test_table ADD CONSTRAINT unq UNIQUE (u)");

      waitForTServerHeartbeat();

      // Force a cache refresh on connection 2.
      statement2.execute("SELECT * FROM test_table");

      assertQuery(
          statement2,
          "EXPLAIN (COSTS OFF) SELECT u FROM test_table WHERE u = 1",
          new Row("Index Only Scan using unq on test_table"),
          new Row("  Index Cond: (u = 1)")
      );

      // Remove unique constraint from connection 1.
      statement1.execute("ALTER TABLE test_table DROP CONSTRAINT unq");

      waitForTServerHeartbeat();

      // Cache is refreshed, so unique constraint is not used.
      assertQuery(
          statement2,
          "EXPLAIN (COSTS OFF) SELECT u FROM test_table WHERE u = 1",
          new Row("Seq Scan on test_table"),
          new Row("  Storage Filter: (u = 1)")
      );
    }
  }

  @Test
  public void testConsistentGUCWrites() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement statement1 = connection1.createStatement();
         Statement statement2 = connection2.createStatement()) {
      statement1.execute("CREATE ROLE some_role");

      // Update roles cache on connection 2.
      statement2.execute("SET ROLE some_role");
      statement2.execute("RESET ROLE");

      statement1.execute("DROP ROLE some_role");

      waitForTServerHeartbeat();

      // Connection 2 refreshes its cache before setting the guc var.
      runInvalidQuery(statement2, "SET ROLE some_role", "role \"some_role\" does not exist");
    }
  }

  @Test
  public void testInvalidationCallbacksWhenInsertingIntoList() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement statement1 = connection1.createStatement();
         Statement statement2 = connection2.createStatement()) {
      statement1.execute("CREATE ROLE some_role CREATEROLE");

      statement2.execute("SET SESSION AUTHORIZATION some_role");

      // Populate membership roles cache from connection 2.
      statement2.execute("CREATE ROLE inaccessible");
      runInvalidQuery(statement2, "SET ROLE inaccessible", "permission denied");

      // Invalidate membership roles cache from connection 1.
      statement1.execute("CREATE ROLE some_group ROLE some_role");

      waitForTServerHeartbeat();

      // Connection 2 observes the new membership roles list.
      statement2.execute("SET ROLE some_group");
    }
  }

  /** Test case inspired by #6317 and #6352, this caused SIGSERV crash. */
  @Test
  public void testAddedDefaults1() throws Exception {
    try (Connection conn1 = getConnectionBuilder().connect();
         Connection conn2 = getConnectionBuilder().connect();
         Statement stmt1 = conn1.createStatement();
         Statement stmt2 = conn2.createStatement()) {
      stmt1.executeUpdate("CREATE ROLE application");

      stmt1.executeUpdate("CREATE TABLE with_default(id int PRIMARY KEY)");

      // This sequence just needs to exist, we don't even have to use it.
      stmt1.executeUpdate("CREATE SEQUENCE some_seq");

      stmt1.executeUpdate("INSERT INTO with_default(id) VALUES (1)");
      stmt1.executeUpdate("ALTER TABLE with_default ADD COLUMN def1 int DEFAULT 10");

      // Mixing in some "concurrent" DDLs to invalidate cache.
      stmt2.executeUpdate("CREATE TABLE t()");
      stmt2.executeUpdate("DROP TABLE t");
      // Make sure other connections will detect catalog version change.
      waitForTServerHeartbeat();

      stmt1.executeUpdate("GRANT SELECT, INSERT, UPDATE, DELETE ON with_default TO application");

      // Default on existing rows isn't properly set, see #4415
      for (Statement stmt : Arrays.asList(stmt1, stmt2)) {
        assertQuery(stmt, "SELECT COUNT(*) FROM with_default", new Row(1));
      }
    }
  }

  /** Test case inspired by #6317 and #6352, this caused SIGSERV crash. */
  @Test
  public void testAddedDefaults2() throws Exception {
    try (Connection conn1 = getConnectionBuilder().connect();
         Connection conn2 = getConnectionBuilder().connect();
         Statement stmt1 = conn1.createStatement();
         Statement stmt2 = conn2.createStatement()) {
      stmt1.executeUpdate("CREATE TABLE with_default(id int PRIMARY KEY)");

      stmt1.executeUpdate("CREATE SEQUENCE some_seq");
      stmt1.executeUpdate("ALTER SEQUENCE some_seq OWNED BY with_default.id");

      stmt1.executeUpdate("INSERT INTO with_default(id) VALUES (1)");
      stmt1.executeUpdate("ALTER TABLE with_default ADD COLUMN def1 int DEFAULT 10");

      // Mixing in some "concurrent" DDLs to invalidate cache.
      stmt2.executeUpdate("CREATE TABLE t()");
      stmt2.executeUpdate("DROP TABLE t");
      // Make sure other connections will detect catalog version change.
      waitForTServerHeartbeat();

      stmt1.executeUpdate("DROP TABLE with_default");
      waitForTServerHeartbeat();

      for (Statement stmt : Arrays.asList(stmt1, stmt2)) {
        runInvalidQuery(stmt, "SELECT * FROM with_default",
            "relation \"with_default\" does not exist");
        runInvalidQuery(stmt, "SELECT nextval('some_seq'::regclass)",
            "relation \"some_seq\" does not exist");
      }
    }
  }

  @Test
  public void testPgInheritsCacheConsistency() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement stmt1 = connection1.createStatement();
         Statement stmt2 = connection2.createStatement()) {

      // Create a partitioned table and some partitions in connection1.
      stmt1.executeUpdate("CREATE TABLE prt (a int, b varchar) PARTITION BY RANGE(a)");
      stmt1.executeUpdate("CREATE TABLE prt_p1 PARTITION OF prt FOR VALUES FROM (0) TO (10)");

      // Create additional partitions in connection2 and perform operations ensuring that the cache
      // is populated in connection2.
      stmt2.executeUpdate("CREATE TABLE prt_p2 PARTITION OF prt FOR VALUES FROM (10) TO (20)");

      // Now in a loop, create a new partition and insert data into it in connection1.
      // The cache in connection2 must refresh itself each time a partition is created in
      // connection1, therefore the data fetched using SELECT query in both connections
      // must be the same.
      final int numIterations = 25;
      final String query = "SELECT * FROM prt WHERE a>0";
      for (int ii = 2; ii < numIterations; ++ii) {
        final int startPartition = 10 * ii;
        final int endPartition = 10 * (ii + 1);
        stmt1.executeUpdate(String.format(
              "CREATE TABLE prt_p%d PARTITION OF prt FOR VALUES FROM (%d) TO (%d)",
              ii + 1, startPartition, endPartition));
        stmt1.executeUpdate(String.format("INSERT INTO prt_p%d(a,b) VALUES (%d, 'abc')",
                                          ii + 1, startPartition + 1));
        waitForTServerHeartbeat();
        assertEquals(getRowList(stmt1, query).size(), getRowList(stmt2, query).size());
      }

      // Now repeat the same test as above, but start a transaction in stmt2 in
      // the loop. Snapshot isolation guarantees here should ensure that the
      // transaction in stmt2 should not see the new partition or the data inserted
      // in stmt1.
      for (int ii = numIterations; ii < numIterations * 2; ++ii) {
        stmt2.execute("BEGIN");
        final int startPartition = 10 * ii;
        final int endPartition = 10 * (ii + 1);
        stmt1.executeUpdate(String.format(
              "CREATE TABLE prt_p%d PARTITION OF prt FOR VALUES FROM (%d) TO (%d)",
              ii + 1, startPartition, endPartition));
        stmt1.executeUpdate(String.format(
              "INSERT INTO prt_p%d(a,b) VALUES (%d, 'abc')",
              ii + 1, startPartition + 1));
        waitForTServerHeartbeat();
        assertEquals(getRowList(stmt1, query).size() - 1, getRowList(stmt2, query).size());
        stmt2.execute("END");
      }

      // Test that dropping a partition in one connection is reflected in the other.
      stmt1.executeUpdate("DROP TABLE prt_p1");
      waitForTServerHeartbeat();
      final String error_msg = "no partition of relation \"prt\" found for row";
      runInvalidQuery(stmt2, "INSERT INTO prt VALUES (1)", error_msg);

      // Test that attaching a partition in one connection is reflected in the other.
      stmt1.executeUpdate("CREATE TABLE prt_p1 (a int, b varchar)");
      stmt1.executeUpdate("ALTER TABLE prt ATTACH PARTITION prt_p1 FOR VALUES FROM (0) TO (10)");
      waitForTServerHeartbeat();
      stmt2.executeUpdate("INSERT INTO prt VALUES (1)");

      // Test whether detaching a partition in one connection is reflected in the other.
      stmt1.executeUpdate("ALTER TABLE prt DETACH PARTITION prt_p1");
      waitForTServerHeartbeat();
      runInvalidQuery(stmt2, "INSERT INTO prt VALUES (1)", error_msg);

      // Test that adding a default partition in one connection is reflected in the other.
      stmt1.executeUpdate("CREATE TABLE prt_default (a int, b varchar)");
      stmt1.executeUpdate("ALTER TABLE prt ATTACH PARTITION prt_default DEFAULT");
      waitForTServerHeartbeat();
      stmt2.executeUpdate("INSERT INTO prt VALUES (1)");

      // Test that dropping a default partition in one connection is reflected in the other.
      stmt1.executeUpdate("DROP TABLE prt_default");
      waitForTServerHeartbeat();
      runInvalidQuery(stmt2, "INSERT INTO prt VALUES (1)", error_msg);

      // Test adding a new partitioned table as a partition.
      stmt1.executeUpdate("CREATE TABLE prt2 (a int, b varchar) PARTITION BY RANGE(a)");
      stmt1.executeUpdate("CREATE TABLE prt2_p1 PARTITION OF prt2 FOR VALUES FROM (2) TO (8)");
      stmt1.executeUpdate("ALTER TABLE prt ATTACH PARTITION prt2 FOR VALUES FROM (0) TO (10)");
      waitForTServerHeartbeat();
      stmt2.executeUpdate("INSERT INTO prt VALUES (3)");

      // Test adding a subpartition.
      stmt1.executeUpdate("CREATE TABLE prt2_p2 PARTITION OF prt2 FOR VALUES FROM (8) TO (10)");
      waitForTServerHeartbeat();
      stmt2.executeUpdate("INSERT INTO prt VALUES (9)");

      // Test dropping a subpartition.
      stmt1.executeUpdate("DROP TABLE prt2_p2");
      waitForTServerHeartbeat();
      final String error_msg2 = "no partition of relation \"prt2\" found for row";
      runInvalidQuery(stmt2, "INSERT INTO prt VALUES (9)", error_msg2);

      // Test adding a default subpartition
      stmt1.executeUpdate("CREATE TABLE prt2_default PARTITION OF prt2 DEFAULT");
      waitForTServerHeartbeat();
      stmt2.executeUpdate("INSERT INTO prt VALUES (9)");

      // Test dropping a default subpartition
      stmt1.executeUpdate("ALTER TABLE prt2 DETACH PARTITION prt2_default");
      waitForTServerHeartbeat();
      runInvalidQuery(stmt2, "INSERT INTO prt VALUES (9)", error_msg2);

      // Test dropping child partitioned table.
      stmt1.executeUpdate("DROP TABLE prt2");
      waitForTServerHeartbeat();
      runInvalidQuery(stmt2, "INSERT INTO prt VALUES (3)", error_msg);
    }
  }

  @Test
  public void testDefaultPartitionConsistency() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement stmt1 = connection1.createStatement();
         Statement stmt2 = connection2.createStatement()) {

      // Create a partitioned table and and a default partition in connection1.
      stmt1.executeUpdate("CREATE TABLE prt (a int, b varchar) PARTITION BY RANGE(a)");
      stmt1.executeUpdate("CREATE TABLE prt_default PARTITION OF prt DEFAULT");

      // Concurrently create/attach a new partition in connection 2 while inserting data into the
      // parent partitioned table in connection 1. Verify that the insert transaction gets aborted
      // due to concurrent DDL.
      final String[] abort_errors =
        { "expired or aborted by a conflict", "Transaction aborted: kAborted" };
      for (int part_idx = 0; part_idx < 50; ++part_idx) {
        final int startPartition = 10 * part_idx;
        final int endPartition = 10 * (part_idx + 1);

        stmt1.execute("BEGIN");
        stmt1.executeUpdate(String.format("INSERT INTO prt(a,b) VALUES (%d, 'abc')",
                                          startPartition + 1));

        // Alternatively test creating a new partition and attaching a new partition.
        if (part_idx % 2 == 0) {
          stmt2.executeUpdate(String.format("CREATE TABLE prt_p%d (a int, b varchar)",
                                            part_idx + 1));
          stmt2.executeUpdate(String.format(
              "ALTER TABLE prt ATTACH PARTITION prt_p%d FOR VALUES FROM (%d) TO (%d)",
              part_idx + 1, startPartition, endPartition));
        } else {
          stmt2.executeUpdate(String.format(
                  "CREATE TABLE prt_p%d PARTITION OF prt FOR VALUES FROM (%d) TO (%d)",
                  part_idx + 1, startPartition, endPartition));
        }

        runInvalidQuery(stmt1, "COMMIT", abort_errors);

        // There should be no rows in the default partition.
        assertEquals(getRowList(stmt1, "SELECT * FROM prt_default WHERE a>0").size(), 0);
      }
    }
  }

  private static Optional<Throwable> captureThrow(ThrowingRunnable action) {
    try {
      action.run();
      return Optional.empty();
    } catch (Throwable t) {
      return Optional.of(t);
    }
  }

  // Check that an error message contains all the relevant error information with the Catalog
  // Version Mismatch message appended.
  @Test
  public void testCatalogVersionLogging() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement statement1 = connection1.createStatement();
         Statement statement2 = connection2.createStatement()) {

      statement1.execute("CREATE TABLE test_table(id int, x0 int)");

      waitForTServerHeartbeat();

      // Force a cache refresh on connection 2.
      statement2.execute("SELECT * FROM test_table");

      statement1.execute("ALTER TABLE test_table ADD COLUMN x1 int");

      try {
        // Immediately try selecting row from connection 2.
        String query = "SELECT x1 FROM test_table";
        statement2.execute(query);

        // Connection Manager enabled runs will not fail due to DDL sleeps.
        if (!isTestRunningWithConnectionManager()) {
          fail(String.format("Statement did not fail: %s", query));
        }
      } catch (SQLException error) {
        if (isTestRunningWithConnectionManager()) {
          fail("Connection Manager enabled run should not have failed");
        }
        LOG.info(error.getMessage());
        assertThat(
          error.getMessage(),
          CoreMatchers.containsString("Catalog Version Mismatch")
        );
        assertThat(
          error.getMessage(),
          CoreMatchers.containsString("Hint: Perhaps you meant to reference the column " +
            "\"test_table.x0\"")
        );
        assertThat(
          error.getMessage(),
          CoreMatchers.not(CoreMatchers.containsString("(null)"))
        );
      }
    }
  }
}
