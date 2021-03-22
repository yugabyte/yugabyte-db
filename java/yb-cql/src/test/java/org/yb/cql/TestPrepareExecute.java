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
package org.yb.cql;

import java.util.Arrays;
import java.util.Vector;
import java.lang.reflect.Field;

import com.datastax.driver.core.ColumnDefinitions;
import com.datastax.driver.core.DataType;
import com.datastax.driver.core.PreparedId;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.exceptions.NoHostAvailableException;
import com.datastax.driver.core.exceptions.QueryValidationException;

import org.junit.BeforeClass;
import org.junit.Test;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import org.yb.YBTestRunner;
import org.yb.minicluster.BaseMiniClusterTest;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestPrepareExecute extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPrepareExecute.class);

  @BeforeClass
  public static void SetUpBeforeClass() throws Exception {
    // Set up prepare statement cache size. Each SELECT statement below takes about 16kB (two 4kB
    // memory page for the parse tree and two for semantic analysis results). A 64kB cache
    // should be small enough to force prepared statements to be freed and reprepared.
    // Note: add "--v=1" below to see the prepared statement cache usage in trace output.
    BaseMiniClusterTest.tserverArgs.add(
        "--cql_service_max_prepared_statement_size_bytes=65536");
    BaseCQLTest.setUpBeforeClass();
  }

  @Test
  public void testBasicPrepareExecute() throws Exception {
    LOG.info("Begin test");

    // Setup table.
    setupTable("test_prepare", 0 /* num_rows */);

    String insert_stmt =
        "insert into test_prepare (h1, h2, r1, r2, v1, v2) values (1, 'a', 2, 'b', 3, 'c');";
    session.execute(insert_stmt);

    // Prepare and execute statement.
    String select_stmt =
        "select * from test_prepare where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';";
    PreparedStatement stmt = session.prepare(select_stmt);
    ResultSet rs = session.execute(stmt.bind());
    Row row = rs.one();

    // Assert that the row is returned.
    assertNotNull(row);
    assertEquals(1, row.getInt("h1"));
    assertEquals("a", row.getString("h2"));
    assertEquals(2, row.getInt("r1"));
    assertEquals("b", row.getString("r2"));
    assertEquals(3, row.getInt("v1"));
    assertEquals("c", row.getString("v2"));

    LOG.info("End test");
  }

  @Test
  public void testMultiplePrepareExecute() throws Exception {
    LOG.info("Begin test");

    // Setup table.
    setupTable("test_prepare", 0 /* num_rows */);

    // Insert 10 rows.
    for (int i = 0; i < 10; i++) {
      String insert_stmt = String.format(
          "insert into test_prepare (h1, h2, r1, r2, v1, v2) values (%d, 'a', 2, 'b', %d, 'c');",
          i, i + 1);
      session.execute(insert_stmt);
    }

    // Prepare 10 statements, each for each of the 10 rows.
    Vector<PreparedStatement> stmts = new Vector<PreparedStatement>();
    for (int i = 0; i < 10; i++) {
      String select_stmt = String.format(
          "select * from test_prepare where h1 = %d and h2 = 'a' and r1 = 2 and r2 = 'b';", i);
      stmts.add(i, session.prepare(select_stmt));
    }

    // Execute the 10 prepared statements round-robin. Loop for 3 times.
    for (int j = 0; j < 3; j++) {
      for (int i = 0; i < 10; i++) {
        ResultSet rs = session.execute(stmts.get(i).bind());
        Row row = rs.one();

        // Assert that the expected row is returned.
        assertNotNull(row);
        assertEquals(i, row.getInt("h1"));
        assertEquals("a", row.getString("h2"));
        assertEquals(2, row.getInt("r1"));
        assertEquals("b", row.getString("r2"));
        assertEquals(i + 1, row.getInt("v1"));
        assertEquals("c", row.getString("v2"));
      }
    }

    LOG.info("End test");
  }

  @Test
  public void testExecuteAfterTableDrop() throws Exception {
    LOG.info("Begin test");

    // Setup table.
    setupTable("test_prepare", 1 /* num_rows */);

    // Prepare statement.
    PreparedStatement stmt = session.prepare("select * from test_prepare;");

    // Drop the table.
    session.execute("drop table test_prepare;");

    // Execute the prepared statement. Expect failure because of the table drop.
    try {
      ResultSet rs = session.execute(stmt.bind());
      fail("Prepared statement did not fail to execute after table is dropped");
    } catch (NoHostAvailableException e) {
      LOG.info("Expected exception caught: " + e.getMessage());
    }

    LOG.info("End test");
  }

  @Test
  public void testExecuteWithUnknownSystemTable() throws Exception {
    LOG.info("Begin test");

    // Prepare statement.
    PreparedStatement stmt = session.prepare("select * from system.unknown_table;");

    // Execute the prepared statement. Expect empty row set.
    ResultSet rs = session.execute(stmt.bind());
    assertNull(rs.one());

    LOG.info("End test");
  }

  private void testPreparedDDL(String stmt) {
    session.execute(session.prepare(stmt).bind());
  }

  private void testInvalidDDL(String stmt) {
    try {
      session.execute(session.prepare(stmt).bind());
      fail("Prepared statement did not fail to execute");
    } catch (QueryValidationException e) {
      LOG.info("Expected exception caught: " + e.getMessage());
    }
  }

  private void testInvalidPrepare(String stmt) {
    try {
      PreparedStatement pstmt = session.prepare(stmt);
      fail("Prepared statement did not fail to prepare");
    } catch (QueryValidationException e) {
      LOG.info("Expected exception caught: " + e.getMessage());
    }
  }

  @Test
  public void testDDL() throws Exception {
    LOG.info("Begin test");

    // Test execute prepared CREATE/DROP KEYSPACE and TABLE.
    testPreparedDDL("CREATE KEYSPACE k;");
    assertQuery("SELECT keyspace_name FROM system_schema.keyspaces " +
                "WHERE keyspace_name = 'k';",
                "Row[k]");

    testPreparedDDL("CREATE TABLE k.t (k int PRIMARY KEY);");
    assertQuery("SELECT keyspace_name, table_name FROM system_schema.tables "+
                "WHERE keyspace_name = 'k' AND table_name = 't';",
                "Row[k, t]");

    testPreparedDDL("DROP TABLE k.t;");
    assertQuery("SELECT keyspace_name, table_name FROM system_schema.tables "+
                "WHERE keyspace_name = 'k' AND table_name = 't';",
                "");

    testPreparedDDL("DROP KEYSPACE k;");
    assertQuery("SELECT keyspace_name FROM system_schema.keyspaces " +
                "WHERE keyspace_name = 'k';",
                "");

    // Test USE keyspace.
    testPreparedDDL("CREATE KEYSPACE k;");
    testPreparedDDL("USE k;");
    testPreparedDDL("CREATE TABLE t (k int PRIMARY KEY);");
    assertQuery("SELECT keyspace_name, table_name FROM system_schema.tables "+
                "WHERE keyspace_name = 'k' AND table_name = 't';",
                "Row[k, t]");

    // Test invalid DDL: invalid syntax and non-existent keyspace.
    testInvalidDDL("CREATE TABLE k.t2;");
    testInvalidDDL("CREATE TABLE k2.t (k int PRIMARY KEY);");

    LOG.info("End test");
  }

  // Assert metadata returned from a prepared statement.
  private void assertHashKeysMetadata(String stmt, DataType hashKeys[]) throws Exception {
    PreparedStatement pstmt = session.prepare(stmt);
    PreparedId id = pstmt.getPreparedId();

    // TODO: routingKeyIndexes in Cassandra's PreparedId class is not exposed (see JAVA-195).
    // Make it accessible via an API in our fork.

    // Assert routingKeyIndexes have the same number of entries as the hash columns.
    Field f = id.getClass().getDeclaredField("routingKeyIndexes");
    f.setAccessible(true);
    int hashIndices[] = (int[])f.get(id);
    if (hashKeys == null) {
      assertNull(hashIndices);
      return;
    }
    assertEquals(hashKeys.length, hashIndices.length);

    // Assert routingKeyIndexes's order and datatypes in metadata are the same as in the table hash
    // key definition.
    ColumnDefinitions variables = pstmt.getVariables();
    assertTrue(hashKeys.length <= variables.size());
    for (int i = 0; i < hashKeys.length; i++) {
      assertEquals(hashKeys[i], variables.getType(hashIndices[i]));
    }
  }

  @Test
  public void testHashKeysMetadata() throws Exception {

    LOG.info("Create test table.");
    session.execute("CREATE TABLE test_pk_indices " +
                    "(h1 int, h2 text, h3 timestamp, r int, v int, " +
                    "PRIMARY KEY ((h1, h2, h3), r));");

    // Expected hash key columns in the cardinal order.
    DataType hashKeys[] = new DataType[]{DataType.cint(), DataType.text(), DataType.timestamp()};

    LOG.info("Test prepared statements.");

    // Test bind markers "?" in different query and DMLs and in different hash key column orders.
    // The hash key metadata returned should be the same regardless.
    assertHashKeysMetadata("SELECT * FROM test_pk_indices WHERE h3 = ? AND h1 = ? AND h2 = ?;",
                           hashKeys);
    assertHashKeysMetadata("SELECT * FROM test_pk_indices WHERE h2 = ? AND h3 = ? AND h1 = ? " +
                           "AND r < ?;", hashKeys);
    assertHashKeysMetadata("INSERT INTO test_pk_indices (h2, h1, h3, r) VALUES (?, ?, ?, ?);",
                           hashKeys);
    assertHashKeysMetadata("UPDATE test_pk_indices SET v = ? " +
                           "WHERE h1 = ? AND h3 = ? AND r = ? AND h2 = ?;", hashKeys);
    assertHashKeysMetadata("DELETE FROM test_pk_indices " +
                           "WHERE h2 = ? AND h1 = ? AND h3 = ? AND r = ?;", hashKeys);

    // Test bind markers ":x" in different query and DMLs and in different hash key column orders.
    // The hash key metadata returned should be the same regardless still.
    assertHashKeysMetadata("SELECT * FROM test_pk_indices " +
                           "WHERE h3 = :h3 AND h1 = :h1 AND h2 = :h2;", hashKeys);
    assertHashKeysMetadata("SELECT * FROM test_pk_indices " +
                           "WHERE h2 = :h2 AND h3 = :h3 AND h1 = :h1 " +
                           "AND r < :r1;", hashKeys);
    assertHashKeysMetadata("INSERT INTO test_pk_indices (h1, h2, h3, r) " +
                           "VALUES (:h1, :h2, :h3, :r);", hashKeys);
    assertHashKeysMetadata("UPDATE test_pk_indices SET v = :v " +
                           "WHERE h1 = :h1 AND h3 = :h3 AND h2 = :h2 AND r = :r;", hashKeys);
    assertHashKeysMetadata("DELETE FROM test_pk_indices " +
                           "WHERE h2 = :h2 AND h1 = :h1 AND h3 = :h3 AND r = :r;", hashKeys);

    // Test SELECT with partial hash column list. In this case, it becomes a full-table scan.
    // Verify no hash key metadata is returned.
    assertHashKeysMetadata("SELECT * FROM test_pk_indices WHERE h1 = ?;", null);

    // Test with hash column list partially bound. In this case, verify an empty hash key list is
    // returned.
    assertHashKeysMetadata("SELECT * FROM test_pk_indices WHERE h1 = ? and h2 = ? and h3 = 1;",
                           null);
    assertHashKeysMetadata("INSERT INTO test_pk_indices (h1, h2, h3, r) VALUES (1, ?, ?, ?);",
                           null);

    // Test DML with incomplete hash key columns. Verify that the statements fail to prepare at all.
    testInvalidPrepare("INSERT INTO test_pk_indices (h1, h2, r) VALUES (?, ?, ?);");
    testInvalidPrepare("UPDATE test_pk_indices SET v = ? WHERE h1 = ?;");
    testInvalidPrepare("DELETE FROM test_pk_indices WHERE h2 = ? AND h1 = ?;");
  }

  @Test
  public void testDDLKeyspaceResolution() throws Exception {
    // Create 2 test keyspaces.
    session.execute("CREATE KEYSPACE test_k1;");
    session.execute("CREATE KEYSPACE test_k2;");

    // Prepare create DDLs in k1.
    session.execute("USE test_k1;");
    PreparedStatement createTable = session.prepare("CREATE TABLE test_table (h INT PRIMARY KEY);");
    PreparedStatement createType = session.prepare("CREATE TYPE test_type (n INT);");

    // Execute prepared DDLs in k2.
    session.execute("USE test_k2;");
    session.execute(createTable.bind());
    session.execute(createType.bind());

    // Verify that the objects are not created in k2.
    runInvalidStmt("DROP TABLE test_k2.test_table;");
    runInvalidStmt("DROP TYPE test_k2.test_type;");

    // Prepare alter/drop DDLs in k1.
    session.execute("USE test_k1;");
    PreparedStatement alterTable = session.prepare("ALTER TABLE test_table ADD c INT;");
    PreparedStatement dropTable = session.prepare("DROP TABLE test_table;");
    PreparedStatement dropType = session.prepare("DROP TYPE test_type;");

    // Execute prepared DDLs in k2.
    session.execute("USE test_k2;");
    session.execute(alterTable.bind());
    session.execute(dropTable.bind());
    session.execute(dropType.bind());
  }
}
