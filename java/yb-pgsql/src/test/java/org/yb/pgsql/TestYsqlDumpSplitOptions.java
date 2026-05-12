// Copyright (c) YugabyteDB, Inc.
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

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Arrays;
import java.util.List;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.TestUtils;
import org.yb.util.ProcessUtil;
import org.yb.util.YBTestRunnerNonTsanAsan;

/**
 * Tests that SPLIT INTO / SPLIT AT VALUES clauses are correctly
 * stored and preserved across ysql_dump operations.
 */
@RunWith(value = YBTestRunnerNonTsanAsan.class)
public class TestYsqlDumpSplitOptions extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYsqlDumpSplitOptions.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return super.getTestMethodTimeoutSec() * 5;
  }

  /**
   * Helper to get the tablet count for a relation using yb_table_properties.
   */
  private int getTabletCount(Statement stmt, String relationName) throws Exception {
    try (ResultSet rs = stmt.executeQuery(
        String.format("SELECT num_tablets FROM yb_table_properties('%s'::regclass)",
            relationName))) {
      assertTrue("Expected result from yb_table_properties", rs.next());
      return rs.getInt("num_tablets");
    }
  }

  /**
   * Helper to get the reloptions for a relation.
   */
  private String getReloptions(Statement stmt, String relationName) throws Exception {
    try (ResultSet rs = stmt.executeQuery(
        String.format("SELECT reloptions FROM pg_class WHERE relname = '%s'", relationName))) {
      if (rs.next()) {
        return rs.getString("reloptions");
      }
      return null;
    }
  }

  /**
   * Helper to get the range split clause for a relation using yb_get_range_split_clause.
   */
  private String getRangeSplitClause(Statement stmt, String relationName) throws Exception {
    try (ResultSet rs = stmt.executeQuery(
        String.format("SELECT yb_get_range_split_clause('%s'::regclass) as split_clause",
            relationName))) {
      if (rs.next()) {
        return rs.getString("split_clause");
      }
      return null;
    }
  }

  /**
   * Test that ysql_dump outputs SPLIT INTO / SPLIT AT VALUES clauses.
   */
  @Test
  public void testDumpSplitOptions() throws Exception {
    final String sourceDb = "split_source_db";
    int tserverIndex = 0;

    // Create source database
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("CREATE DATABASE " + sourceDb);
    }

    // Connect to source database and create test objects
    try (Connection conn = getConnectionBuilder().withDatabase(sourceDb).connect();
         Statement stmt = conn.createStatement()) {

      // Create table with SPLIT INTO
      stmt.executeUpdate(
          "CREATE TABLE split_table (k INT PRIMARY KEY, v INT) SPLIT INTO 5 TABLETS");

      // Create table with yb_presplit reloption directly
      stmt.executeUpdate(
          "CREATE TABLE split_table_reloption (k INT PRIMARY KEY, v INT) WITH (yb_presplit='7')");

      // Create table with index that has SPLIT INTO
      stmt.executeUpdate("CREATE TABLE table_with_split_index (k INT PRIMARY KEY, v INT)");
      stmt.executeUpdate(
          "CREATE INDEX split_index ON table_with_split_index(v) SPLIT INTO 4 TABLETS");

      // Create materialized view with yb_presplit reloption
      stmt.executeUpdate("CREATE TABLE mv_source (k INT, v INT)");
      stmt.executeUpdate("INSERT INTO mv_source VALUES (1, 10), (2, 20)");
      stmt.executeUpdate(
          "CREATE MATERIALIZED VIEW split_mv WITH (yb_presplit='3') AS SELECT * FROM mv_source");

      // Verify initial tablet counts
      assertEquals(5, getTabletCount(stmt, "split_table"));
      assertEquals(7, getTabletCount(stmt, "split_table_reloption"));
      assertEquals(4, getTabletCount(stmt, "split_index"));
      assertEquals(3, getTabletCount(stmt, "split_mv"));

      // Verify yb_presplit is in reloptions
      String tableReloptions = getReloptions(stmt, "split_table");
      assertNotNull("split_table should have reloptions", tableReloptions);
      assertTrue("split_table reloptions should contain yb_presplit",
          tableReloptions.contains("yb_presplit=5"));

      String indexReloptions = getReloptions(stmt, "split_index");
      assertNotNull("split_index should have reloptions", indexReloptions);
      assertTrue("split_index reloptions should contain yb_presplit",
          indexReloptions.contains("yb_presplit=4"));
    }

    // Run ysql_dump with --include-yb-metadata
    File pgBinDir = PgRegressBuilder.getPgBinDir();
    File ysqlDumpExec = new File(pgBinDir, "ysql_dump");
    File dumpFile = File.createTempFile("split_options_dump", ".sql");
    dumpFile.deleteOnExit();

    List<String> dumpArgs = Arrays.asList(
        ysqlDumpExec.toString(),
        "-h", getPgHost(tserverIndex),
        "-p", Integer.toString(getPgPort(tserverIndex)),
        "-U", DEFAULT_PG_USER,
        "-d", sourceDb,
        "-f", dumpFile.toString(),
        "--include-yb-metadata");

    LOG.info("Running ysql_dump: " + dumpArgs);
    ProcessUtil.executeSimple(dumpArgs, "ysql_dump");

    // Read and verify the dump contains yb_presplit
    String dumpContent = new String(Files.readAllBytes(dumpFile.toPath()), StandardCharsets.UTF_8);
    LOG.info("Dump file size: " + dumpContent.length() + " bytes");
    // ysql_dump outputs reloptions with quoted values like yb_presplit='5'
    assertTrue("Dump should contain yb_presplit for split_table",
        dumpContent.contains("yb_presplit=5") || dumpContent.contains("yb_presplit='5'"));
    assertTrue("Dump should contain yb_presplit for split_table_reloption",
        dumpContent.contains("yb_presplit=7") || dumpContent.contains("yb_presplit='7'"));
    assertTrue("Dump should contain yb_presplit for split_index",
        dumpContent.contains("yb_presplit=4") || dumpContent.contains("yb_presplit='4'"));
    assertTrue("Dump should contain yb_presplit for split_mv",
        dumpContent.contains("yb_presplit=3") || dumpContent.contains("yb_presplit='3'"));

    // Cleanup
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("DROP DATABASE " + sourceDb);
    }
  }

  /**
   * Test that tables without SPLIT INTO don't have yb_presplit in the dump.
   */
  @Test
  public void testDumpWithoutSplitOptions() throws Exception {
    final String testDb = "no_split_db";
    int tserverIndex = 0;

    // Create test database
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("CREATE DATABASE " + testDb);
    }

    // Create table without SPLIT INTO
    try (Connection conn = getConnectionBuilder().withDatabase(testDb).connect();
         Statement stmt = conn.createStatement()) {
      stmt.executeUpdate("CREATE TABLE no_split_table (k INT PRIMARY KEY, v INT)");

      // Verify no yb_presplit in reloptions
      String reloptions = getReloptions(stmt, "no_split_table");
      assertTrue("Table without SPLIT INTO should not have yb_presplit in reloptions",
          reloptions == null || !reloptions.contains("yb_presplit"));
    }

    // Run ysql_dump
    File pgBinDir = PgRegressBuilder.getPgBinDir();
    File ysqlDumpExec = new File(pgBinDir, "ysql_dump");
    File dumpFile = File.createTempFile("no_split_dump", ".sql");
    dumpFile.deleteOnExit();

    List<String> dumpArgs = Arrays.asList(
        ysqlDumpExec.toString(),
        "-h", getPgHost(tserverIndex),
        "-p", Integer.toString(getPgPort(tserverIndex)),
        "-U", DEFAULT_PG_USER,
        "-d", testDb,
        "-f", dumpFile.toString(),
        "--include-yb-metadata");

    ProcessUtil.executeSimple(dumpArgs, "ysql_dump");

    // Verify dump doesn't contain yb_presplit for this table
    String dumpContent = new String(Files.readAllBytes(dumpFile.toPath()), StandardCharsets.UTF_8);

    // The dump should not contain yb_presplit for no_split_table
    // (it might contain it for system tables, so we check specifically)
    assertFalse("Dump should not contain yb_presplit for no_split_table",
        dumpContent.contains("no_split_table") &&
        dumpContent.contains("yb_presplit") &&
        dumpContent.indexOf("no_split_table") <
            dumpContent.indexOf("yb_presplit", dumpContent.indexOf("no_split_table")));

    // Cleanup
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("DROP DATABASE " + testDb);
    }
  }

  /**
   * Test that SPLIT AT VALUES is correctly stored in yb_presplit reloption.
   */
  @Test
  public void testSplitAtValuesReloption() throws Exception {
    final String testDb = "split_at_db";

    // Create test database
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("CREATE DATABASE " + testDb);
    }

    try (Connection conn = getConnectionBuilder().withDatabase(testDb).connect();
         Statement stmt = conn.createStatement()) {

      // Create table with SPLIT AT VALUES (must use range partitioning with ASC/DESC)
      stmt.executeUpdate(
          "CREATE TABLE split_at_table (k INT, v INT, PRIMARY KEY(k ASC)) " +
          "SPLIT AT VALUES ((100), (200), (300))");

      // Verify tablet count (4 tablets: before 100, 100-200, 200-300, after 300)
      assertEquals("Tablet count should be 4", 4, getTabletCount(stmt, "split_at_table"));

      // Verify yb_presplit is in reloptions
      String reloptions = getReloptions(stmt, "split_at_table");
      assertNotNull("split_at_table should have reloptions", reloptions);
      assertTrue("Reloptions should contain yb_presplit",
          reloptions.contains("yb_presplit"));

      // Verify split points can be retrieved
      String splitClause = getRangeSplitClause(stmt, "split_at_table");
      assertNotNull("Should have split clause", splitClause);
      assertTrue("Split clause should contain expected values",
          splitClause.contains("100") &&
          splitClause.contains("200") &&
          splitClause.contains("300"));
    }

    // Cleanup
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("DROP DATABASE " + testDb);
    }
  }

  /**
   * Test that SPLIT AT VALUES on indexes is correctly stored in yb_presplit reloption.
   */
  @Test
  public void testSplitAtValuesIndexReloption() throws Exception {
    final String testDb = "split_at_index_db";

    // Create test database
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("CREATE DATABASE " + testDb);
    }

    try (Connection conn = getConnectionBuilder().withDatabase(testDb).connect();
         Statement stmt = conn.createStatement()) {

      // Create table and index with SPLIT AT VALUES
      stmt.executeUpdate("CREATE TABLE idx_table (k INT PRIMARY KEY, v INT)");
      stmt.executeUpdate(
          "CREATE INDEX split_at_idx ON idx_table(v ASC) SPLIT AT VALUES ((10), (20), (30))");

      // Verify tablet count
      assertEquals("Index tablet count should be 4", 4, getTabletCount(stmt, "split_at_idx"));

      // Verify yb_presplit is in reloptions
      String reloptions = getReloptions(stmt, "split_at_idx");
      assertNotNull("split_at_idx should have reloptions", reloptions);
      assertTrue("Index reloptions should contain yb_presplit",
          reloptions.contains("yb_presplit"));

      // Verify split points can be retrieved
      String splitClause = getRangeSplitClause(stmt, "split_at_idx");
      assertNotNull("Index should have split clause", splitClause);
      assertTrue("Index split clause should contain expected values",
          splitClause.contains("10") &&
          splitClause.contains("20") &&
          splitClause.contains("30"));
    }

    // Cleanup
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("DROP DATABASE " + testDb);
    }
  }

  /**
   * Test that ALTER TABLE to modify yb_presplit is reflected in ysql_dump output.
   */
  @Test
  public void testAlterTableDump() throws Exception {
    final String testDb = "alter_dump_db";
    int tserverIndex = 0;

    // Create test database
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("CREATE DATABASE " + testDb);
    }

    try (Connection conn = getConnectionBuilder().withDatabase(testDb).connect();
         Statement stmt = conn.createStatement()) {

      // Create table with initial SPLIT INTO
      stmt.executeUpdate(
          "CREATE TABLE alter_test (k INT PRIMARY KEY, v INT) SPLIT INTO 4 TABLETS");

      // Verify initial yb_presplit
      String initialReloptions = getReloptions(stmt, "alter_test");
      assertNotNull("alter_test should have reloptions", initialReloptions);
      assertTrue("Initial reloptions should contain yb_presplit=4",
          initialReloptions.contains("yb_presplit=4"));

      // ALTER TABLE to change yb_presplit
      stmt.executeUpdate("ALTER TABLE alter_test SET (yb_presplit='8')");

      // Verify yb_presplit is updated
      String alteredReloptions = getReloptions(stmt, "alter_test");
      assertNotNull("alter_test should have reloptions after ALTER", alteredReloptions);
      assertTrue("Altered reloptions should contain yb_presplit=8",
          alteredReloptions.contains("yb_presplit=8"));
    }

    // Run ysql_dump and verify the altered value is in the dump
    File pgBinDir = PgRegressBuilder.getPgBinDir();
    File ysqlDumpExec = new File(pgBinDir, "ysql_dump");
    File dumpFile = File.createTempFile("alter_dump", ".sql");
    dumpFile.deleteOnExit();

    List<String> dumpArgs = Arrays.asList(
        ysqlDumpExec.toString(),
        "-h", getPgHost(tserverIndex),
        "-p", Integer.toString(getPgPort(tserverIndex)),
        "-U", DEFAULT_PG_USER,
        "-d", testDb,
        "-f", dumpFile.toString(),
        "--include-yb-metadata");

    LOG.info("Running ysql_dump: " + dumpArgs);
    ProcessUtil.executeSimple(dumpArgs, "ysql_dump");

    // Read and verify the dump contains the altered yb_presplit value
    String dumpContent = new String(Files.readAllBytes(dumpFile.toPath()), StandardCharsets.UTF_8);

    // The dump should contain the altered value (8), not the original (4)
    assertTrue("Dump should contain altered yb_presplit=8",
        dumpContent.contains("yb_presplit=8") || dumpContent.contains("yb_presplit='8'"));
    assertFalse("Dump should NOT contain original yb_presplit=4",
        dumpContent.contains("yb_presplit=4") || dumpContent.contains("yb_presplit='4'"));

    // Cleanup
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("DROP DATABASE " + testDb);
    }
  }
}
