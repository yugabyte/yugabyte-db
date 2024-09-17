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

import static org.yb.AssertionWrappers.assertArrayEquals;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertLessThan;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;
import static org.junit.Assume.*;

import java.io.File;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.sql.Timestamp;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.json.JSONObject;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.apache.commons.io.FileUtils;
import org.yb.client.LocatedTablet;
import org.yb.client.TestUtils;
import org.yb.client.YBTable;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.SystemUtil;
import org.yb.util.TableProperties;
import org.yb.util.YBBackupException;
import org.yb.util.YBBackupUtil;
import org.yb.util.YBTestRunnerNonTsanAsan;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.util.PSQLException;

import static org.yb.AssertionWrappers.assertArrayEquals;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertLessThan;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

@RunWith(value=YBTestRunnerNonTsanAsan.class)
public class TestYbBackup extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYbBackup.class);

  private static final String PERMISSION_DENIED = "permission denied";

  // This constant must be synced with the same variable in `yb_backup.py` script.
  // This temporary constant enables the features:
  // 1. "STOP_ON_ERROR" mode in 'ysqlsh' tool
  // 2. New API: 'backup_tablespaces'/'restore_tablespaces'+'use_tablespaces'
  //    instead of old 'use_tablespaces'
  // 3. If the new API 'backup_roles' is NOT used - the YSQL Dump is generated
  //    with '--no-privileges' flag.
  private static final boolean ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR = false;

  @Before
  public void initYBBackupUtil() throws Exception {
    YBBackupUtil.setTSAddresses(miniCluster.getTabletServers());
    YBBackupUtil.setMasterAddresses(masterAddresses);
    YBBackupUtil.setPostgresContactPoint(miniCluster.getPostgresContactPoints().get(0));
    YBBackupUtil.maybeStartYbControllers(miniCluster);
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("ysql_legacy_colocated_database_creation", "false");
    return flagMap;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);

    List<Map<String, String>> perTserverZonePlacementFlags = Arrays.asList(
        ImmutableMap.of("placement_cloud", "cloud1",
                        "placement_region", "region1",
                        "placement_zone", "zone1"),
        ImmutableMap.of("placement_cloud", "cloud2",
                        "placement_region", "region2",
                        "placement_zone", "zone2"),
        ImmutableMap.of("placement_cloud", "cloud3",
                        "placement_region", "region3",
                        "placement_zone", "zone3"));
    builder.perTServerFlags(perTserverZonePlacementFlags);
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 900; // Usual time for a test ~90 seconds. But can be much more on Jenkins.
  }

  @Override
  protected int getNumShardsPerTServer() {
    return 2;
  }

  public void doAlteredTableBackup(String dbName, TableProperties tp) throws Exception {
    String colocString = tp.isColocated() ? "TRUE" : "FALSE";
    String initialDBName = dbName + "1";
    String restoreDBName = dbName + "2";

    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATION=%s", initialDBName, colocString));
    }
    try (Connection connection2 = getConnectionBuilder().withDatabase(initialDBName).connect();
         Statement stmt = connection2.createStatement()) {
      stmt.execute("CREATE TABLE test_tbl (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   String.format("WITH (colocation = %s)", colocString));

      for (int i = 1; i <= 2000; ++i) {
        stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES" +
          " (" + String.valueOf(i) +                     // h
          ", " + String.valueOf(100 + i) +               // a
          ", " + String.valueOf(2.14 + (float)i) + ")"); // b
      }

      stmt.execute("ALTER TABLE test_tbl DROP a");
      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql." + initialDBName);
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }

      stmt.execute("INSERT INTO test_tbl (h, b) VALUES (9999, 8.9)");

      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql." + restoreDBName);

      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 3.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=2000", new Row(2000, 2002.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=9999", new Row(9999, 8.9));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(restoreDBName).connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 3.14));
      assertQuery(stmt, "SELECT b FROM test_tbl WHERE h=1", new Row(3.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=2000", new Row(2000, 2002.14));
      assertQuery(stmt, "SELECT h FROM test_tbl WHERE h=2000", new Row(2000));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=9999");
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE " + initialDBName);
      stmt.execute("DROP DATABASE " + restoreDBName);
    }
  }

  @Test
  public void testAlteredTable() throws Exception {
    doAlteredTableBackup("altered_db", new TableProperties(
        TableProperties.TP_YSQL | TableProperties.TP_NON_COLOCATED));
  }

  @Test
  public void testAlteredTableInColocatedDB() throws Exception {
    doAlteredTableBackup("altered_colocated_db", new TableProperties(
        TableProperties.TP_YSQL | TableProperties.TP_COLOCATED));
  }

  @Test
  public void testAlteredTableInLegacyColocatedDB() throws Exception {
    markClusterNeedsRecreation();
    restartClusterWithFlags(Collections.singletonMap("ysql_legacy_colocated_database_creation",
                                                     "true"),
                            Collections.emptyMap());
    initYBBackupUtil();
    doAlteredTableBackup("altered_colocated_db", new TableProperties(
        TableProperties.TP_YSQL | TableProperties.TP_COLOCATED));
  }

  private void doMixedColocatedDatabaseBackup() throws Exception {
    String initialDBName = "yb_colocated";
    String restoreDBName = "yb_colocated2";

    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATION=TRUE", initialDBName));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(initialDBName).connect();
         Statement stmt = connection2.createStatement()) {
      // Create 3 tables, 2 colocated and 1 non colocated but in the same db.
      stmt.execute("CREATE TABLE test_tbl1 (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   "WITH (COLOCATION=TRUE)");
      stmt.execute("CREATE TABLE test_tbl2 (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   "WITH (COLOCATION=TRUE)");
      stmt.execute("CREATE TABLE test_tbl3 (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   "WITH (COLOCATION=FALSE)");

      // Insert random rows/values for tables to snapshot
      for (int j = 1; j <= 3; ++j) {
        for (int i = 1; i <= 2000; ++i) {
          stmt.execute("INSERT INTO test_tbl" + String.valueOf(j) + " (h, a, b) VALUES" +
            " (" + String.valueOf(i * j) +                       // h
            ", " + String.valueOf((100 + i) * j) +               // a
            ", " + String.valueOf((2.14 + (float)i) * j) + ")"); // b
        }
      }

      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql." + initialDBName);
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }

      // Insert more rows after taking the snapshot.
      for (int j = 1; j <= 3; ++j) {
        stmt.execute("INSERT INTO test_tbl" + String.valueOf(j) + " (h, a, b) VALUES" +
        " (" + String.valueOf(9999 * j) +                        // h
        ", " + String.valueOf((100 + 9999) * j) +                // a
        ", " + String.valueOf((2.14 + 9999f) * j) + ")");        // b
      }

      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql." + restoreDBName);

      // Verify the original database has the rows we previously inserted.
      for (int j = 1; j <= 3; ++j) {
        for (int i : new int[] {1, 2000, 9999}) {
          assertQuery(stmt, String.format("SELECT * FROM test_tbl%d WHERE h=%d", j, i * j),
            new Row(i * j, (100 + i) * j, (2.14 + (float)i) * j));
        }
      }
    }

    // Verify that the new database and tables are properly configured.
    List<String> tbl1Tablets = YBBackupUtil.getTabletsForTable("ysql." + restoreDBName,
                                                                "test_tbl1");
    List<String> tbl2Tablets = YBBackupUtil.getTabletsForTable("ysql." + restoreDBName,
                                                                "test_tbl2");
    List<String> tbl3Tablets = YBBackupUtil.getTabletsForTable("ysql." + restoreDBName,
                                                                "test_tbl3");
    // test_tbl1 and test_tbl2 are colocated and so should share the exact same tablet.
    assertEquals("test_tbl1 is not colocated", 1, tbl1Tablets.size());
    assertEquals("test_tbl2 is not colocated", 1, tbl2Tablets.size());
    assertArrayEquals("test_tbl1 and test_tbl2 do not share the same colocated tablet",
                      tbl1Tablets.toArray(), tbl2Tablets.toArray());
    // test_tbl3 is not colocated so it should have more tablets.
    assertTrue("test_tbl3 should have more than 1 tablet", tbl3Tablets.size() > 1);
    assertFalse("test_tbl3 uses the colocated tablet", tbl3Tablets.contains(tbl1Tablets.get(0)));

    try (Connection connection2 = getConnectionBuilder().withDatabase(restoreDBName).connect();
         Statement stmt = connection2.createStatement()) {
      // Verify the new database contains all the rows we inserted before taking the snapshot.
      for (int j = 1; j <= 3; ++j) {
        for (int i : new int[] {1, 500, 2000}) {
          assertQuery(stmt, String.format("SELECT * FROM test_tbl%d WHERE h=%d", j, i * j),
            new Row(i * j, (100 + i) * j, (2.14 + (float)i) * j));
          assertQuery(stmt, String.format("SELECT h FROM test_tbl%d WHERE h=%d", j, i * j),
            new Row(i * j));
          assertQuery(stmt, String.format("SELECT a FROM test_tbl%d WHERE h=%d", j, i * j),
            new Row((100 + i) * j));
          assertQuery(stmt, String.format("SELECT b FROM test_tbl%d WHERE h=%d", j, i * j),
            new Row((2.14 + (float)i) * j));
        }
        // Assert that this returns no rows.
        assertQuery(stmt, String.format("SELECT * FROM test_tbl%d WHERE h=%d", j, 9999 * j));
      }
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute(String.format("DROP DATABASE %s", initialDBName));
      stmt.execute(String.format("DROP DATABASE %s", restoreDBName));
    }
  }

  @Test
  public void testMixedColocatedDatabase() throws Exception {
    doMixedColocatedDatabaseBackup();
  }

  @Test
  public void testMixedLegacyColocatedDatabase() throws Exception {
    markClusterNeedsRecreation();
    restartClusterWithFlags(Collections.singletonMap("ysql_legacy_colocated_database_creation",
                                                     "true"),
                            Collections.emptyMap());
    initYBBackupUtil();
    doMixedColocatedDatabaseBackup();
  }

  @Test
  public void testAlteredTableInOriginalCluster() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE  test_tbl (h INT PRIMARY KEY, a INT, b FLOAT)");

      for (int i = 1; i <= 2000; ++i) {
        stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES" +
          " (" + String.valueOf(i) +                     // h
          ", " + String.valueOf(100 + i) +               // a
          ", " + String.valueOf(2.14 + (float)i) + ")"); // b
      }

      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
      stmt.execute("ALTER TABLE test_tbl DROP a");
      stmt.execute("INSERT INTO test_tbl (h, b) VALUES (9999, 8.9)");

      try {
        YBBackupUtil.runYbBackupRestore(backupDir);
        fail("Backup restoring did not fail as expected");
      } catch (YBBackupException ex) {
        LOG.info("Expected exception", ex);
      }

      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 3.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=2000", new Row(2000, 2002.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=9999", new Row(9999, 8.9));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT b FROM test_tbl WHERE h=1", new Row(3.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=2000", new Row(2000, 2100, 2002.14));
      assertQuery(stmt, "SELECT h FROM test_tbl WHERE h=2000", new Row(2000));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=9999");
    }
  }

  @Test
  public void testColocatedDBWithColocationIdAlreadySet() throws Exception {
    String ybTablePropsSql = "SELECT c.relname, tg.grpname, props.colocation_id"
        + " FROM pg_class c, yb_table_properties(c.oid) props"
        + " LEFT JOIN pg_yb_tablegroup tg ON tg.oid = props.tablegroup_oid"
        + " WHERE c.oid >= " + FIRST_NORMAL_OID
        + " ORDER BY c.relname";
    String uniqueIndexOnlySql = "SELECT b FROM test_tbl WHERE b = 3.14";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE DATABASE yb1 COLOCATION=TRUE");
    }
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb1").connect();
         Statement stmt = connection2.createStatement()) {
      // Create a table with a set colocation_id.
      stmt.execute("CREATE TABLE test_tbl ("
          + "  h INT PRIMARY KEY,"
          + "  a INT,"
          + "  b FLOAT CONSTRAINT test_tbl_uniq UNIQUE WITH (colocation_id=654321)"
          + ") WITH (colocation_id=123456)");
      stmt.execute("CREATE INDEX test_tbl_a_idx ON test_tbl (a ASC) WITH (colocation_id=11223344)");
      stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES (1, 101, 3.14)");

      runInvalidQuery(stmt, "INSERT INTO test_tbl (h, a, b) VALUES (2, 202, 3.14)",
          "violates unique constraint \"test_tbl_uniq\"");

      assertQuery(stmt, ybTablePropsSql,
          new Row("test_tbl", "default", 123456),
          new Row("test_tbl_a_idx", "default", 11223344),
          new Row("test_tbl_pkey", null, null),
          new Row("test_tbl_uniq", "default", 654321));

      assertTrue(isIndexOnlyScan(stmt, uniqueIndexOnlySql, "test_tbl_uniq"));

      // Check that backup and restore works fine.
      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yb1");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");
    }
    // Verify data is correct.
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT b FROM test_tbl WHERE h=1", new Row(3.14));

      assertQuery(stmt, ybTablePropsSql,
          new Row("test_tbl", "default", 123456),
          new Row("test_tbl_a_idx", "default", 11223344),
          new Row("test_tbl_pkey", null, null),
          new Row("test_tbl_uniq", "default", 654321));

      runInvalidQuery(stmt, "INSERT INTO test_tbl (h, a, b) VALUES (2, 202, 3.14)",
          "violates unique constraint \"test_tbl_uniq\"");

      assertTrue(isIndexOnlyScan(stmt, uniqueIndexOnlySql, "test_tbl_uniq"));
      assertQuery(stmt, uniqueIndexOnlySql, new Row(3.14));

      // Now try to do a backup/restore of the restored db.
      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yb2");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb3");
    }
    // Verify data is correct.
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb3").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT b FROM test_tbl WHERE h=1", new Row(3.14));

      assertQuery(stmt, ybTablePropsSql,
          new Row("test_tbl", "default", 123456),
          new Row("test_tbl_a_idx", "default", 11223344),
          new Row("test_tbl_pkey", null, null),
          new Row("test_tbl_uniq", "default", 654321));

      runInvalidQuery(stmt, "INSERT INTO test_tbl (h, a, b) VALUES (2, 202, 3.14)",
          "violates unique constraint \"test_tbl_uniq\"");

      assertTrue(isIndexOnlyScan(stmt, uniqueIndexOnlySql, "test_tbl_uniq"));
      assertQuery(stmt, uniqueIndexOnlySql, new Row(3.14));
    }
    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb1");
      stmt.execute("DROP DATABASE yb2");
      stmt.execute("DROP DATABASE yb3");
    }
  }

  @Test
  public void testLegacyColocatedDBWithColocationIdAlreadySet() throws Exception {
    markClusterNeedsRecreation();
    restartClusterWithFlags(Collections.singletonMap("ysql_legacy_colocated_database_creation",
                                                     "true"),
                            Collections.emptyMap());
    initYBBackupUtil();
    String ybTablePropsSql = "SELECT c.relname, props.colocation_id"
        + " FROM pg_class c, yb_table_properties(c.oid) props"
        + " WHERE c.oid >= " + FIRST_NORMAL_OID
        + " ORDER BY c.relname";
    String uniqueIndexOnlySql = "SELECT b FROM test_tbl WHERE b = 3.14";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE DATABASE yb1 COLOCATION=TRUE");
    }
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb1").connect();
         Statement stmt = connection2.createStatement()) {
      // Create a table with a set colocation_id.
      stmt.execute("CREATE TABLE test_tbl ("
          + "  h INT PRIMARY KEY,"
          + "  a INT,"
          + "  b FLOAT CONSTRAINT test_tbl_uniq UNIQUE WITH (colocation_id=654321)"
          + ") WITH (colocation_id=123456)");
      stmt.execute("CREATE INDEX test_tbl_a_idx ON test_tbl (a ASC) WITH (colocation_id=11223344)");
      stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES (1, 101, 3.14)");

      runInvalidQuery(stmt, "INSERT INTO test_tbl (h, a, b) VALUES (2, 202, 3.14)",
          "violates unique constraint \"test_tbl_uniq\"");

      assertQuery(stmt, ybTablePropsSql,
          new Row("test_tbl", 123456),
          new Row("test_tbl_a_idx", 11223344),
          new Row("test_tbl_pkey", null),
          new Row("test_tbl_uniq", 654321));

      assertTrue(isIndexOnlyScan(stmt, uniqueIndexOnlySql, "test_tbl_uniq"));

      // Check that backup and restore works fine.
      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yb1");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");
    }
    // Verify data is correct.
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT b FROM test_tbl WHERE h=1", new Row(3.14));

      assertQuery(stmt, ybTablePropsSql,
          new Row("test_tbl", 123456),
          new Row("test_tbl_a_idx", 11223344),
          new Row("test_tbl_pkey", null),
          new Row("test_tbl_uniq", 654321));

      runInvalidQuery(stmt, "INSERT INTO test_tbl (h, a, b) VALUES (2, 202, 3.14)",
          "violates unique constraint \"test_tbl_uniq\"");

      assertTrue(isIndexOnlyScan(stmt, uniqueIndexOnlySql, "test_tbl_uniq"));
      assertQuery(stmt, uniqueIndexOnlySql, new Row(3.14));

      // Now try to do a backup/restore of the restored db.
      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yb2");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb3");
    }
    // Verify data is correct.
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb3").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT b FROM test_tbl WHERE h=1", new Row(3.14));

      assertQuery(stmt, ybTablePropsSql,
          new Row("test_tbl", 123456),
          new Row("test_tbl_a_idx", 11223344),
          new Row("test_tbl_pkey", null),
          new Row("test_tbl_uniq", 654321));

      runInvalidQuery(stmt, "INSERT INTO test_tbl (h, a, b) VALUES (2, 202, 3.14)",
          "violates unique constraint \"test_tbl_uniq\"");

      assertTrue(isIndexOnlyScan(stmt, uniqueIndexOnlySql, "test_tbl_uniq"));
      assertQuery(stmt, uniqueIndexOnlySql, new Row(3.14));
    }
    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb1");
      stmt.execute("DROP DATABASE yb2");
      stmt.execute("DROP DATABASE yb3");
    }
  }

  @Test
  public void testTablegroup() throws Exception {
    String ybTablePropsSql = "SELECT c.relname, tg.grpname, props.colocation_id"
        + " FROM pg_class c, yb_table_properties(c.oid) props"
        + " LEFT JOIN pg_yb_tablegroup tg ON tg.oid = props.tablegroup_oid"
        + " WHERE c.oid >= " + FIRST_NORMAL_OID
        + " ORDER BY c.relname";
    String uniqueIndexOnlySql = "SELECT b FROM test_tbl WHERE b = 3.14";
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE DATABASE yb1");
    }
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb1").connect();
         Statement stmt = connection2.createStatement()) {
      stmt.execute("CREATE TABLEGROUP tg1");
      stmt.execute("CREATE TABLE test_tbl ("
          + "  h INT PRIMARY KEY,"
          + "  a INT,"
          + "  b FLOAT CONSTRAINT test_tbl_uniq UNIQUE WITH (colocation_id=654321)"
          + ") WITH (colocation_id=123456)"
          + " TABLEGROUP tg1");
      stmt.execute("CREATE INDEX test_tbl_idx ON test_tbl (a ASC)"
          + " WITH (colocation_id=11223344)");
      stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES (1, 101, 3.14)");

      runInvalidQuery(stmt, "INSERT INTO test_tbl (h, a, b) VALUES (2, 202, 3.14)",
          "violates unique constraint \"test_tbl_uniq\"");

      assertQuery(stmt, ybTablePropsSql,
          new Row("test_tbl", "tg1", 123456),
          new Row("test_tbl_idx", "tg1", 11223344),
          new Row("test_tbl_pkey", null, null),
          new Row("test_tbl_uniq", "tg1", 654321));

      assertTrue(isIndexOnlyScan(stmt, uniqueIndexOnlySql, "test_tbl_uniq"));

      // Check that backup and restore works fine.
      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yb1");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");
    }
    // Verify data is correct.
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT b FROM test_tbl WHERE h=1", new Row(3.14));

      assertQuery(stmt, ybTablePropsSql,
          new Row("test_tbl", "tg1", 123456),
          new Row("test_tbl_idx", "tg1", 11223344),
          new Row("test_tbl_pkey", null, null),
          new Row("test_tbl_uniq", "tg1", 654321));

      runInvalidQuery(stmt, "INSERT INTO test_tbl (h, a, b) VALUES (2, 202, 3.14)",
          "violates unique constraint \"test_tbl_uniq\"");

      assertTrue(isIndexOnlyScan(stmt, uniqueIndexOnlySql, "test_tbl_uniq"));
      assertQuery(stmt, uniqueIndexOnlySql, new Row(3.14));

      // Now try to do a backup/restore of the restored db.
      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yb2");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb3");
    }
    // Verify data is correct.
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb3").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT b FROM test_tbl WHERE h=1", new Row(3.14));

      assertQuery(stmt, ybTablePropsSql,
          new Row("test_tbl", "tg1", 123456),
          new Row("test_tbl_idx", "tg1", 11223344),
          new Row("test_tbl_pkey", null, null),
          new Row("test_tbl_uniq", "tg1", 654321));

      runInvalidQuery(stmt, "INSERT INTO test_tbl (h, a, b) VALUES (2, 202, 3.14)",
          "violates unique constraint \"test_tbl_uniq\"");

      assertTrue(isIndexOnlyScan(stmt, uniqueIndexOnlySql, "test_tbl_uniq"));
      assertQuery(stmt, uniqueIndexOnlySql, new Row(3.14));
    }
    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb1");
      stmt.execute("DROP DATABASE yb2");
      stmt.execute("DROP DATABASE yb3");
    }
  }

  @Test
  public void testMultipleTablegroups() throws Exception {
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE dummy (k INT PRIMARY KEY, v INT)");
      stmt.execute("CREATE TABLEGROUP tg1");
      stmt.execute("CREATE TABLEGROUP tg2");
      stmt.execute("CREATE TABLE tbl1_in_tg1 (k INT PRIMARY KEY, v INT) TABLEGROUP tg1");
      stmt.execute("INSERT INTO tbl1_in_tg1 VALUES (1, 1), (2, 2)");
      stmt.execute("CREATE TABLE tbl2_in_tg1 (k TEXT PRIMARY KEY, v TEXT) TABLEGROUP tg1");
      stmt.execute("INSERT INTO tbl2_in_tg1 VALUES ('a', 'b'), ('c', 'd')");
      stmt.execute("CREATE TABLE tbl3_in_tg2 (k INT PRIMARY KEY, v TEXT) TABLEGROUP tg2");
      stmt.execute("INSERT INTO tbl3_in_tg2 VALUES (1, 'a'), (2, 'b')");
      stmt.execute("CREATE TABLE tbl4_in_tg2 (k TEXT PRIMARY KEY, v INT) TABLEGROUP tg2");
      stmt.execute("INSERT INTO tbl4_in_tg2 VALUES ('x', 100), ('y', 200)");

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM tbl1_in_tg1 ORDER BY k",
                  new Row(1, 1), new Row(2, 2));
      assertQuery(stmt, "SELECT * FROM tbl2_in_tg1 ORDER BY k",
                  new Row("a", "b"), new Row("c", "d"));
      assertQuery(stmt, "SELECT * FROM tbl3_in_tg2 ORDER BY k",
                  new Row(1, "a"), new Row(2, "b"));
      assertQuery(stmt, "SELECT * FROM tbl4_in_tg2 ORDER BY k",
                  new Row("x", 100), new Row("y", 200));

      String ybTablePropsSql = "SELECT c.relname, tg.grpname"
          + " FROM pg_class c, yb_table_properties(c.oid) props"
          + " JOIN pg_yb_tablegroup tg ON tg.oid = props.tablegroup_oid"
          + " WHERE c.oid >= " + FIRST_NORMAL_OID
          + " ORDER BY c.relname";
      assertQuery(stmt, ybTablePropsSql,
                  new Row("tbl1_in_tg1", "tg1"),
                  new Row("tbl2_in_tg1", "tg1"),
                  new Row("tbl3_in_tg2", "tg2"),
                  new Row("tbl4_in_tg2", "tg2"));
    }
  }

  private void doColocatedDatabaseRestoreToOriginalDB() throws Exception {
    String initialDBName = "yb_colocated";
    int num_tables = 2;

    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATION=TRUE", initialDBName));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(initialDBName).connect();
         Statement stmt = connection2.createStatement()) {
      stmt.execute("CREATE TABLE test_tbl1 (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   "WITH (COLOCATION=TRUE)");
      stmt.execute("CREATE TABLE test_tbl2 (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   "WITH (COLOCATION=TRUE)");

      // Insert random rows/values for tables to snapshot
      for (int j = 1; j <= num_tables; ++j) {
        for (int i = 1; i <= 2000; ++i) {
          stmt.execute("INSERT INTO test_tbl" + String.valueOf(j) + " (h, a, b) VALUES" +
            " (" + String.valueOf(i * j) +                       // h
            ", " + String.valueOf((100 + i) * j) +               // a
            ", " + String.valueOf((2.14 + (float)i) * j) + ")"); // b
        }
      }

      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql." + initialDBName);
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }

      // Delete all rows from the tables after taking a snapshot.
      for (int j = 1; j <= num_tables; ++j) {
        stmt.execute("DELETE FROM test_tbl" + String.valueOf(j));
      }

      // Restore back into this same database, this way all the ids will happen to be the same.
      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql." + initialDBName);

      // Verify rows.
      for (int j = 1; j <= num_tables; ++j) {
        for (int i : new int[] {1, 500, 2000}) {
          assertQuery(stmt, String.format("SELECT * FROM test_tbl%d WHERE h=%d", j, i * j),
            new Row(i * j, (100 + i) * j, (2.14 + (float)i) * j));
          assertQuery(stmt, String.format("SELECT h FROM test_tbl%d WHERE h=%d", j, i * j),
            new Row(i * j));
          assertQuery(stmt, String.format("SELECT a FROM test_tbl%d WHERE h=%d", j, i * j),
            new Row((100 + i) * j));
          assertQuery(stmt, String.format("SELECT b FROM test_tbl%d WHERE h=%d", j, i * j),
            new Row((2.14 + (float)i) * j));
        }
      }
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute(String.format("DROP DATABASE %s", initialDBName));
    }
  }

  @Test
  public void testColocatedDatabaseRestoreToOriginalDB() throws Exception {
    doColocatedDatabaseRestoreToOriginalDB();
  }

  @Test
  public void testLegacyColocatedDatabaseRestoreToOriginalDB() throws Exception {
    markClusterNeedsRecreation();
    restartClusterWithFlags(Collections.singletonMap("ysql_legacy_colocated_database_creation",
                                                     "true"),
                            Collections.emptyMap());
    initYBBackupUtil();
    doColocatedDatabaseRestoreToOriginalDB();
  }

  @Test
  public void testAlteredTableWithNotNull() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test_tbl(id int)");

      stmt.execute("ALTER TABLE test_tbl ADD a int NOT NULL");
      stmt.execute("ALTER TABLE test_tbl ADD b int NULL");

      stmt.execute("INSERT INTO test_tbl(id, a, b) VALUES (1, 2, 3)");
      stmt.execute("INSERT INTO test_tbl(id, a) VALUES (2, 4)");

      runInvalidQuery(
          stmt, "INSERT INTO test_tbl(id, b) VALUES(3, 6)",
          "null value in column \"a\" of relation \"test_tbl\" violates not-null constraint");

      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
      stmt.execute("INSERT INTO test_tbl (id, a, b) VALUES (9999, 9, 9)");

      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE id=1", new Row(1, 2, 3));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE id=2", new Row(2, 4, (Integer) null));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE id=3");
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE id=9999", new Row(9999, 9, 9));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE id=1", new Row(1, 2, 3));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE id=2", new Row(2, 4, (Integer) null));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE id=3");
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE id=9999");
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testIndex() throws Exception {
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test_tbl (h INT PRIMARY KEY, a INT, b FLOAT)");
      stmt.execute("CREATE INDEX test_idx ON test_tbl (a)");

      for (int i = 1; i <= 1000; ++i) {
        stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES" +
          " (" + String.valueOf(i) +                     // h
          ", " + String.valueOf(100 + i) +               // a
          ", " + String.valueOf(2.14 + (float)i) + ")"); // b
      }

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }

      stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES (9999, 8888, 8.9)");

      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1000", new Row(1000, 1100, 1002.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=9999", new Row(9999, 8888, 8.9));

      // Select via the index.
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE a=101", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE a=1100", new Row(1000, 1100, 1002.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE a=8888", new Row(9999, 8888, 8.9));
    }

    // Add a new node.
    miniCluster.startTServer(getTServerFlags());
    // Wait for node list refresh.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 2 * 1000);
    YBBackupUtil.setTSAddresses(miniCluster.getTabletServers());
    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1000", new Row(1000, 1100, 1002.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=9999");

      // Select via the index.
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE a=101", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE a=1100", new Row(1000, 1100, 1002.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE a=8888");
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testIndexTypes() throws Exception {
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test_tbl (h INT PRIMARY KEY, c1 INT, c2 INT, c3 INT, c4 INT)");
      stmt.execute("CREATE INDEX test_idx1 ON test_tbl (c1)");
      stmt.execute("CREATE INDEX test_idx2 ON test_tbl (c2 HASH)");
      stmt.execute("CREATE INDEX test_idx3 ON test_tbl (c3 ASC)");
      stmt.execute("CREATE INDEX test_idx4 ON test_tbl (c4 DESC)");

      stmt.execute("INSERT INTO test_tbl (h, c1, c2, c3, c4) VALUES (1, 11, 12, 13, 14)");

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }

      stmt.execute("INSERT INTO test_tbl (h, c1, c2, c3, c4) VALUES (9, 21, 22, 23, 24)");

      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 11, 12, 13, 14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c1=11", new Row(1, 11, 12, 13, 14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c2=12", new Row(1, 11, 12, 13, 14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c3=13", new Row(1, 11, 12, 13, 14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c4=14", new Row(1, 11, 12, 13, 14));

      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=9", new Row(9, 21, 22, 23, 24));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c1=21", new Row(9, 21, 22, 23, 24));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c2=22", new Row(9, 21, 22, 23, 24));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c3=23", new Row(9, 21, 22, 23, 24));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c4=24", new Row(9, 21, 22, 23, 24));
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 11, 12, 13, 14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c1=11", new Row(1, 11, 12, 13, 14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c2=12", new Row(1, 11, 12, 13, 14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c3=13", new Row(1, 11, 12, 13, 14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c4=14", new Row(1, 11, 12, 13, 14));

      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=9");
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c1=21");
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c2=22");
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c3=23");
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c4=24");
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  private void verifyCollationIndexData(Statement stmt, int test_step) throws Exception {
    Row expectedLowerCaseRow = new Row("a", "b", "c", "d", "e");
    Row expectedUpperCaseRow = new Row("A", "B", "C", "D", "E");
    List<Row> expectedRows = test_step == 2 ? Arrays.asList(new Row("a", "b", "c", "d", "e"),
                                                            new Row("A", "B", "C", "D", "E"),
                                                            new Row("f", "g", "h", "i", "j"))
                                            : Arrays.asList(new Row("a", "b", "c", "d", "e"),
                                                            new Row("A", "B", "C", "D", "E"));
    assertRowList(stmt, "SELECT * FROM test_tbl ORDER BY h", expectedRows);
    assertRowList(stmt, "SELECT * FROM test_tbl ORDER BY c1", expectedRows);
    assertRowList(stmt, "SELECT * FROM test_tbl ORDER BY c2", expectedRows);
    assertRowList(stmt, "SELECT * FROM test_tbl ORDER BY c3", expectedRows);
    assertRowList(stmt, "SELECT * FROM test_tbl ORDER BY c4", expectedRows);
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE h='a'", expectedLowerCaseRow);
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE c1='b'", expectedLowerCaseRow);
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE c2='c'", expectedLowerCaseRow);
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE c3='d'", expectedLowerCaseRow);
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE c4='e'", expectedLowerCaseRow);
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE h='A'", expectedUpperCaseRow);
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE c1='B'", expectedUpperCaseRow);
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE c2='C'", expectedUpperCaseRow);
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE c3='D'", expectedUpperCaseRow);
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE c4='E'", expectedUpperCaseRow);
    if (test_step == 2) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h='f'", new Row("f", "g", "h", "i", "j"));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c1='g'", new Row("f", "g", "h", "i", "j"));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c2='h'", new Row("f", "g", "h", "i", "j"));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c3='i'", new Row("f", "g", "h", "i", "j"));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE c4='j'", new Row("f", "g", "h", "i", "j"));
    } else {
      assertNoRows(stmt, "SELECT * FROM test_tbl WHERE h='f'");
      assertNoRows(stmt, "SELECT * FROM test_tbl WHERE c1='g'");
      assertNoRows(stmt, "SELECT * FROM test_tbl WHERE c2='h'");
      assertNoRows(stmt, "SELECT * FROM test_tbl WHERE c3='i'");
      assertNoRows(stmt, "SELECT * FROM test_tbl WHERE c4='j'");
    }
  }

  private void testCollationIndexTypesHelper(String collName) throws Exception {
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test_tbl");
      stmt.execute("CREATE TABLE test_tbl (h TEXT PRIMARY KEY COLLATE \"" + collName + "\", " +
                   "c1 TEXT COLLATE \"" + collName + "\", " +
                   "c2 TEXT COLLATE \"" + collName + "\", " +
                   "c3 TEXT COLLATE \"" + collName + "\", " +
                   "c4 TEXT COLLATE \"" + collName + "\")");
      stmt.execute("CREATE INDEX test_idx1 ON test_tbl (c1)");
      stmt.execute("CREATE INDEX test_idx2 ON test_tbl (c2 HASH)");
      stmt.execute("CREATE INDEX test_idx3 ON test_tbl (c3 ASC)");
      stmt.execute("CREATE INDEX test_idx4 ON test_tbl (c4 DESC)");

      stmt.execute("INSERT INTO test_tbl (h, c1, c2, c3, c4) VALUES ('a', 'b', 'c', 'd', 'e')");
      stmt.execute("INSERT INTO test_tbl (h, c1, c2, c3, c4) VALUES ('A', 'B', 'C', 'D', 'E')");

      verifyCollationIndexData(stmt, 1);

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }

      stmt.execute("INSERT INTO test_tbl (h, c1, c2, c3, c4) VALUES ('f', 'g', 'h', 'i', 'j')");

      verifyCollationIndexData(stmt, 2);
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      verifyCollationIndexData(stmt, 3);
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testCollationIndexTypesEn() throws Exception {
    testCollationIndexTypesHelper("en-US-x-icu");
  }

  @Test
  public void testCollationIndexTypesSv() throws Exception {
    testCollationIndexTypesHelper("sv-x-icu");
  }

  @Test
  public void testCollationIndexTypesTr() throws Exception {
    testCollationIndexTypesHelper("tr-x-icu");
  }

  private void verifyPartialIndexData(Statement stmt) throws Exception {
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 11, 22));
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE c1=11", new Row(1, 11, 22));
    assertQuery(stmt, "SELECT * FROM test_tbl WHERE c2=22", new Row(1, 11, 22));

    assertQuery(stmt, "SELECT * FROM \"WHERE_tbl\" WHERE h=1", new Row(1, 11, 22));
    assertQuery(stmt, "SELECT * FROM \"WHERE_tbl\" WHERE \"WHERE_c1\"=11", new Row(1, 11, 22));
    assertQuery(stmt, "SELECT * FROM \"WHERE_tbl\" WHERE \" WHERE c2\"=22", new Row(1, 11, 22));
  }

  @Test
  public void testPartialIndexes() throws Exception {
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test_tbl (h INT PRIMARY KEY, c1 INT, c2 INT)");
      stmt.execute("CREATE INDEX test_idx1 ON test_tbl (c1) WHERE (c1 IS NOT NULL)");
      stmt.execute("CREATE INDEX test_idx2 ON test_tbl (c2 ASC) WHERE (c2 IS NOT NULL)");

      stmt.execute("INSERT INTO test_tbl (h, c1, c2) VALUES (1, 11, 22)");

      stmt.execute("CREATE TABLE \"WHERE_tbl\" " +
                     "(h INT PRIMARY KEY, \"WHERE_c1\" INT, \" WHERE c2\" INT)");
      stmt.execute("CREATE INDEX ON \"WHERE_tbl\" " +
                     "(\"WHERE_c1\") WHERE (\"WHERE_c1\" IS NOT NULL)");
      stmt.execute("CREATE INDEX ON \"WHERE_tbl\" " +
                     "(\" WHERE c2\") WHERE (\" WHERE c2\" IS NOT NULL)");

      stmt.execute("INSERT INTO \"WHERE_tbl\" (h, \"WHERE_c1\", \" WHERE c2\") VALUES (1, 11, 22)");

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
      verifyPartialIndexData(stmt);
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      verifyPartialIndexData(stmt);
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testRestoreWithRestoreTime() throws Exception {
    // disabling this test since Yb Controller doesn't support
    // --restore_time
    if(TestUtils.useYbController()){
      return;
    }
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE  test_tbl (h INT PRIMARY KEY, a INT, b FLOAT)");

      for (int i = 1; i <= 2000; ++i) {
        stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES" +
            " (" + String.valueOf(i) +                     // h
            ", " + String.valueOf(100 + i) +               // a
            ", " + String.valueOf(2.14 + (float)i) + ")"); // b
      }

      // Get the current timestamp in microseconds.
      String ts = Long.toString(ChronoUnit.MICROS.between(Instant.EPOCH, Instant.now()));

      // Insert additional values into the table before taking the backup.
      stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES (9999, 789, 8.9)");

      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      backupDir = new JSONObject(output).getString("snapshot_url");

      // Backup using --restore_time.
      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2", "--restore_time", ts);
    }

    // Verify we only restore the original rows.
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
        Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=2000", new Row(2000, 2100, 2002.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=9999");  // Should not exist.
    }
  }

  @Test
  public void testBackupCreateGetBackupSize() throws Exception {
    // output from yb controller doesn't contain backup size data in json format so
    // this test doesn't make much sense
    if (TestUtils.useYbController()) {
      return;
    }
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE  test_tbl (h INT PRIMARY KEY, a INT, b FLOAT)");

      for (int i = 1; i <= 2000; ++i) {
        stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES" +
            " (" + String.valueOf(i) +                     // h
            ", " + String.valueOf(100 + i) +               // a
            ", " + String.valueOf(2.14 + (float)i) + ")"); // b
      }

      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte", "--pg_based_backup", "--disable_checksum");
      JSONObject json = new JSONObject(output);
      long expectedBackupSize = json.getLong("backup_size_in_bytes");
      long actualBackupSize = FileUtils.sizeOfDirectory(new File(json.getString("snapshot_url")));
      LOG.info("Expected size = " + expectedBackupSize + "  ActualSize = " + actualBackupSize);
      long allowedDelta = 1 * 1024;     // 1 KB

      // On MAC the expected size can be zero - not calculated.
      // On MAC 'du -sb' does not work: '-b' is not supported.
      // The issue on MAC is ignored for now because MacOS is not a production OS.
      // https://github.com/yugabyte/yugabyte-db/issues/14724
      if (SystemUtil.IS_LINUX) {
        assertLessThan(Math.abs(expectedBackupSize - actualBackupSize), allowedDelta);
      }
    }
  }

  @Test
  public void testSedRegExpForYSQLDump() throws Exception {
    // disabling this test since Yb Controller doesn't support
    // --edit_ysql_dump_sed_reg_exp
    if(TestUtils.useYbController()){
      return;
    }
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE ROLE  admin");
      // Default DB & table owner is ROLE 'yugabyte'.
      stmt.execute("CREATE TABLE  test_tbl (h INT PRIMARY KEY, a INT)");

      String backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      backupDir = new JSONObject(output).getString("snapshot_url");

      // Restore with the table owner renaming on fly.
      YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2",
          "--edit_ysql_dump_sed_reg_exp", "s|OWNER TO yugabyte_test|OWNER TO admin|");

      // In this DB the table owner was not changed.
      assertEquals("yugabyte_test", getOwnerForTable(stmt, "test_tbl"));
    }

    // Verify the changed table owner for the restored table.
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertEquals("admin", getOwnerForTable(stmt, "test_tbl"));
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  public Set<String> subDirs(String path) throws Exception {
    Set<String> dirs = new HashSet<String>();
    for (File f : new File(path).listFiles(File::isDirectory)) {
      dirs.add(f.getName());
    }
    return dirs;
  }

  public void checkTabletsInDir(String path, List<String>... tabletLists) throws Exception {
    Set<String> dirs = subDirs(path);
    for(List<String> tablets : tabletLists) {
      for (String tabletID : tablets) {
        assertTrue(dirs.contains("tablet-" + tabletID));
      }
    }
  }

  public String doCreateGeoPartitionedBackup(int numRegions, boolean backupTablespaces)
      throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(
          " CREATE TABLESPACE region1_ts " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");
      stmt.execute(
          " CREATE TABLESPACE region2_ts " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud2\",\"region\":\"region2\",\"zone\":\"zone2\"," +
          "\"min_num_replicas\":1}]}')");
      stmt.execute(
          " CREATE TABLESPACE region3_ts " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud3\",\"region\":\"region3\",\"zone\":\"zone3\"," +
          "\"min_num_replicas\":1}]}')");

      stmt.execute("CREATE TABLE tbl (id INT, geo VARCHAR) PARTITION BY LIST (geo)");
      stmt.execute("CREATE TABLE tbl_r1 PARTITION OF tbl (id, geo, PRIMARY KEY (id HASH, geo))" +
                   "FOR VALUES IN ('R1') TABLESPACE region1_ts");
      stmt.execute("CREATE TABLE tbl_r2 PARTITION OF tbl (id, geo, PRIMARY KEY (id HASH, geo))" +
                   "FOR VALUES IN ('R2') TABLESPACE region2_ts");
      stmt.execute("CREATE TABLE tbl_r3 PARTITION OF tbl (id, geo, PRIMARY KEY (id HASH, geo))" +
                   "FOR VALUES IN ('R3') TABLESPACE region3_ts");
      // Check tablespaces for tables.
      assertEquals(null, getTablespaceForTable(stmt, "tbl"));
      assertEquals("region1_ts", getTablespaceForTable(stmt, "tbl_r1"));
      assertEquals("region2_ts", getTablespaceForTable(stmt, "tbl_r2"));
      assertEquals("region3_ts", getTablespaceForTable(stmt, "tbl_r3"));

      for (int i = 1; i <= 2000; ++i) {
        stmt.execute("INSERT INTO tbl (id, geo) VALUES" +
          " (" + String.valueOf(i) +                  // id
          ", 'R" + String.valueOf(1 + i % 3) + "')"); // geo
      }

      List<String> tblTablets = getTabletsForTable("yugabyte", "tbl");
      List<String> tblR1Tablets = getTabletsForTable("yugabyte", "tbl_r1");
      List<String> tblR2Tablets = getTabletsForTable("yugabyte", "tbl_r2");
      List<String> tblR3Tablets = getTabletsForTable("yugabyte", "tbl_r3");

      String backupDir = YBBackupUtil.getTempBackupDir(), output = null;
      List<String> args = new ArrayList<>(Arrays.asList("--keyspace", "ysql.yugabyte"));
      if (backupTablespaces) {
        args.add("--backup_tablespaces");

        if (!ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR) {
          args.add("--use_tablespaces");
        }
      }

      switch (numRegions) {
        case 0:
          args.addAll(Arrays.asList("--backup_location", backupDir));
          output = YBBackupUtil.runYbBackupCreate(args);
          backupDir = new JSONObject(output).getString("snapshot_url");
          checkTabletsInDir(backupDir, tblTablets, tblR1Tablets, tblR2Tablets, tblR3Tablets);
          break;
        case 1:
          args.addAll(Arrays.asList(
              "--region", "region1", "--region_location", backupDir + "_reg1",
              "--backup_location", backupDir));
          output = YBBackupUtil.runYbBackupCreate(args);
          backupDir = new JSONObject(output).getString("snapshot_url");
          checkTabletsInDir(backupDir, tblR2Tablets, tblR3Tablets);
          checkTabletsInDir(backupDir + "_reg1", tblR1Tablets);
          break;
        case 3:
          args.addAll(Arrays.asList(
            "--region", "region1", "--region_location", backupDir + "_reg1",
            "--region", "region2", "--region_location", backupDir + "_reg2",
            "--region", "region3", "--region_location", backupDir + "_reg3",
            "--backup_location", backupDir));
          output = YBBackupUtil.runYbBackupCreate(args);
          backupDir = new JSONObject(output).getString("snapshot_url");
          assertTrue(subDirs(backupDir).isEmpty());
          checkTabletsInDir(backupDir + "_reg1", tblR1Tablets);
          checkTabletsInDir(backupDir + "_reg2", tblR2Tablets);
          checkTabletsInDir(backupDir + "_reg3", tblR3Tablets);
          break;
        default:
          throw new IllegalArgumentException("Unexpected numRegions: " + numRegions);
      }

      return output;
    }
  }

  public String doTestGeoPartitionedBackupAndRestore(
      String targetDB, int numRegions, boolean backupAndRestoreTablespaces, boolean dropTablespaces,
      String restoreFlag, boolean useTablespaces) throws Exception {
    String output = null;
    try (Statement stmt = connection.createStatement()) {
      output = doCreateGeoPartitionedBackup(numRegions, backupAndRestoreTablespaces);
      JSONObject json = new JSONObject(output);
      String backupDir = json.getString("snapshot_url");

      stmt.execute("INSERT INTO tbl (id, geo) VALUES (9999, 'R1')");
      assertQuery(stmt, "SELECT * FROM tbl WHERE id=1", new Row(1, "R2"));
      assertQuery(stmt, "SELECT * FROM tbl WHERE id=2000", new Row(2000, "R3"));
      assertQuery(stmt, "SELECT * FROM tbl WHERE id=9999", new Row(9999, "R1"));
      assertQuery(stmt, "SELECT COUNT(*) FROM tbl", new Row(2001));

      List<String> args = new ArrayList<>();
      if (backupAndRestoreTablespaces) {
        args.add("--restore_tablespaces");
      }

      if (restoreFlag != null) {
        args.add(restoreFlag);
      }

      if (!ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR) {
        useTablespaces = backupAndRestoreTablespaces;
      }

      if (useTablespaces) {
        args.add("--use_tablespaces");
      }

      if (!targetDB.equals("yugabyte")) {
        // Drop TABLEs and TABLESPACEs.
        stmt.execute("DROP TABLE tbl_r1");
        stmt.execute("DROP TABLE tbl_r2");
        stmt.execute("DROP TABLE tbl_r3");
        stmt.execute("DROP TABLE tbl");
        if (dropTablespaces) {
          stmt.execute("DROP TABLESPACE region1_ts");
          stmt.execute("DROP TABLESPACE region2_ts");
          stmt.execute("DROP TABLESPACE region3_ts");

          // Check global TABLESPACEs.
          assertRowSet(stmt, "SELECT spcname FROM pg_tablespace",
              asSet(new Row("pg_default"), new Row("pg_global")));
        }

        args.addAll(Arrays.asList("--keyspace", "ysql." + targetDB));
      }
      // else - overwriting existing tables in DB "yugabyte".

      YBBackupUtil.runYbBackupRestore(backupDir, args);
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(targetDB).connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM tbl WHERE id=1", new Row(1, "R2"));
      assertQuery(stmt, "SELECT * FROM tbl WHERE id=2000", new Row(2000, "R3"));
      assertQuery(stmt, "SELECT COUNT(*) FROM tbl", new Row(2000));
      // This row was inserted after backup so it is absent here.
      assertNoRows(stmt, "SELECT * FROM tbl WHERE id=9999");

      assertEquals(null, getTablespaceForTable(stmt, "tbl"));
      // Check global TABLESPACEs.
      Set<Row> expectedTablespaces = asSet(new Row("pg_default"), new Row("pg_global"));
      if (backupAndRestoreTablespaces || targetDB.equals("yugabyte")) {
        expectedTablespaces.addAll(
            asSet(new Row("region1_ts"), new Row("region2_ts"), new Row("region3_ts")));
      }

      if ((backupAndRestoreTablespaces && useTablespaces) || targetDB.equals("yugabyte")) {
        assertEquals("region1_ts", getTablespaceForTable(stmt, "tbl_r1"));
        assertEquals("region2_ts", getTablespaceForTable(stmt, "tbl_r2"));
        assertEquals("region3_ts", getTablespaceForTable(stmt, "tbl_r3"));
      } else {
        assertEquals(null, getTablespaceForTable(stmt, "tbl_r1"));
        assertEquals(null, getTablespaceForTable(stmt, "tbl_r2"));
        assertEquals(null, getTablespaceForTable(stmt, "tbl_r3"));
      }
      assertRowSet(stmt, "SELECT spcname FROM pg_tablespace", expectedTablespaces);
    }

    if (!targetDB.equals("yugabyte")) {
      // Cleanup.
      try (Statement stmt = connection.createStatement()) {
        if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
        stmt.execute("DROP DATABASE " + targetDB);
      }
    }

    return output;
  }

  public String doTestGeoPartitionedBackupAndRestore(
      String targetDB, int numRegions, boolean backupAndRestoreTablespaces) throws Exception {
    return doTestGeoPartitionedBackupAndRestore(
        targetDB, numRegions, backupAndRestoreTablespaces, /* dropTablespaces */ true,
        /* restoreFlag */ null, /* useTablespaces */ true);
  }

  @Test
  public void testGeoPartitioning() throws Exception {
    if (disableGeoPartitionedTests()) {
      return;
    }
    doTestGeoPartitionedBackupAndRestore("db2", 3, false);
  }

  @Test
  public void testGeoPartitioningNoRegions() throws Exception {
    if (disableGeoPartitionedTests()) {
      return;
    }
    doTestGeoPartitionedBackupAndRestore("db2", 0, false);
  }

  @Test
  public void testGeoPartitioningOneRegion() throws Exception {
    if (disableGeoPartitionedTests()) {
      return;
    }
    doTestGeoPartitionedBackupAndRestore("db2", 1, false);
  }

  @Test
  public void testGeoPartitioningRestoringIntoExisting() throws Exception {
    // The test fails with Connection Manager as it is expected that a new
    // session would latch onto a new physical connection. Instead, two logical
    // connections use the same physical connection, leading to unexpected
    // results as per the expectations of the test.
    assumeFalse(BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED, isTestRunningWithConnectionManager());

    if (disableGeoPartitionedTests()) {
      return;
    }
    doTestGeoPartitionedBackupAndRestore("yugabyte", 3, false);
  }

  @Test
  public void testGeoPartitioningWithTablespaces() throws Exception {
    if (disableGeoPartitionedTests()) {
      return;
    }
    doTestGeoPartitionedBackupAndRestore("db2", 3, true);
  }

  @Test
  public void testGeoPartitioningNoRegionsWithTablespaces() throws Exception {
    if (disableGeoPartitionedTests()) {
      return;
    }
    doTestGeoPartitionedBackupAndRestore("db2", 0, true);
  }

  @Test
  public void testGeoPartitioningOneRegionWithTablespaces() throws Exception {
    if (disableGeoPartitionedTests()) {
      return;
    }
    doTestGeoPartitionedBackupAndRestore("db2", 1, true);
  }

  @Test
  public void testGeoPartitioningRestoringIntoExistingWithTablespaces() throws Exception {
    // The test fails with Connection Manager as it is expected that a new
    // session would latch onto a new physical connection. Instead, two logical
    // connections use the same physical connection, leading to unexpected
    // results as per the expectations of the test.
    assumeFalse(BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED, isTestRunningWithConnectionManager());

    if (disableGeoPartitionedTests()) {
      return;
    }
    doTestGeoPartitionedBackupAndRestore("yugabyte", 3, true);
  }

  @Test
  public void testFailureOnExistingTablespaces() throws Exception {
    if (disableGeoPartitionedTests()) {
      return;
    }
    try {
      doTestGeoPartitionedBackupAndRestore(
          "db2", 3, /* backupAndRestoreTablespaces */ true, /* dropTablespaces */ false,
          /* restoreFlag */ null, /* useTablespaces */ true);

      if (ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR) {
        fail("Backup restoring did not fail as expected");
      }
    } catch (YBBackupException ex) {
      LOG.info("Expected exception", ex);
      assertTrue(ex.getMessage().contains("tablespace \"region1_ts\" already exists"));
    }
  }

  @Test
  public void testIgnoreExistingTablespaces() throws Exception {
    if (disableGeoPartitionedTests()) {
      return;
    }
    doTestGeoPartitionedBackupAndRestore(
        "db2", 3, /* backupAndRestoreTablespaces */ true, /* dropTablespaces */ false,
        "--ignore_existing_tablespaces", /* useTablespaces */ true);
  }

  @Test
  public void testBackupRestoreTablespaces() throws Exception {
    if (disableGeoPartitionedTests()) {
      return;
    }
    doTestGeoPartitionedBackupAndRestore(
        "db2", 3, /* backupAndRestoreTablespaces */ true, /* dropTablespaces */ true,
        /* restoreFlag */ null, /* useTablespaces */ false);
    // Case with useTablespaces = true: testGeoPartitioningWithTablespaces().
  }

  @Test
  public void testGeoPartitioningDeleteBackup() throws Exception {
    if (disableGeoPartitionedTests()) {
      return;
    }
    String output = doCreateGeoPartitionedBackup(3, /* backupTablespaces */ false);
    JSONObject json = new JSONObject(output);
    String backupDir = json.getString("snapshot_url");
    List<String> backupDirs = new ArrayList<String>(Arrays.asList(
        backupDir, json.getString("region1"),
        json.getString("region2"), json.getString("region3")));
    LOG.info("Backup folders:" + backupDirs);

    // Ensure the backup folders exist.
    for (String dirName : backupDirs) {
      LOG.info("Checking existing folder: " + dirName);
      File dir = new File(dirName);
      assertTrue(dir.exists());
      assertGreaterThan(dir.length(), 0L);
    }

    YBBackupUtil.runYbBackupDelete(backupDir);
    // Ensure the backup folders were deleted.
    for (String dirName : backupDirs) {
      LOG.info("Checking deleted folder: " + dirName);
      File dir = new File(dirName);
      assertFalse(dir.exists());
      assertEquals(dir.length(), 0L);
    }
  }

  public void doTestBackupRestoreRoles(boolean restoreRoles, boolean useRoles)
      throws Exception {
    // ybc doesn't support --ignore_existing_roles currently
    if(TestUtils.useYbController()){
      return;
    }
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test_table(id INT PRIMARY KEY)");
      stmt.execute("INSERT INTO test_table (id) VALUES (1)");

      stmt.execute("CREATE ROLE admin LOGIN NOINHERIT");
      stmt.execute("REVOKE ALL ON TABLE test_table FROM admin");
      stmt.execute("GRANT SELECT ON TABLE test_table TO admin");

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte", "--backup_roles");
      backupDir = new JSONObject(output).getString("snapshot_url");
    }

    try (Connection connection2 = getConnectionBuilder().withUser("admin").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_table WHERE id=1", new Row(1));

      runInvalidQuery(stmt, "INSERT INTO test_table (id) VALUES (9)", PERMISSION_DENIED);
    }

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("REVOKE ALL ON TABLE test_table FROM admin");
      stmt.execute("DROP ROLE admin");
    }

    List<String> args = new ArrayList<>(Arrays.asList(
        "--keyspace", "ysql.yb2", "--ignore_existing_roles"));
    if (restoreRoles) {
      args.add("--restore_roles");
    }
    if (useRoles) {
      args.add("--use_roles");
    }

    try {
      YBBackupUtil.runYbBackupRestore(backupDir, args);
    } catch (YBBackupException ex) {
      if (ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR && !restoreRoles) {
        LOG.info("Expected exception", ex);
        assertTrue(ex.getMessage().contains("ERROR:  role \"admin\" does not exist"));
        return;
      } else {
        throw ex;
      }
    }

    try (Connection connection3 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection3.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_table WHERE id=1", new Row(1));
    }

    try (Connection connection4 =
             getConnectionBuilder().withDatabase("yb2").withUser("admin").connect();
         Statement stmt = connection4.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_table WHERE id=1", new Row(1));

      runInvalidQuery(stmt, "INSERT INTO test_table (id) VALUES (9)", PERMISSION_DENIED);
    } catch (PSQLException ex) {
      if (restoreRoles) {
        throw ex;
      } else {
        LOG.info("Expected exception", ex);
        assertTrue(ex.getMessage().contains("FATAL: role \"admin\" does not exist"));
     }
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testBackupRestoreRoles() throws Exception {
    doTestBackupRestoreRoles(/* restoreRoles */ true, /* useRoles */ true);
  }

  @Test
  public void testBackupRolesWithoutUseRoles() throws Exception {
    doTestBackupRestoreRoles(/* restoreRoles */ true, /* useRoles */ false);
  }

  @Test
  public void testBackupRolesWithoutRestoreRoles() throws Exception {
    doTestBackupRestoreRoles(/* restoreRoles */ false, /* useRoles */ false);
  }

  @Test
  public void testUserDefinedTypes() throws Exception {
    // TODO(myang): Add ALTER TYPE test after #1893 is fixed.
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      // A enum type.
      stmt.execute("CREATE TYPE e_t AS ENUM('c', 'b', 'a')");

      // Table column of enum type.
      stmt.execute("CREATE TABLE test_tb1(c1 e_t)");
      stmt.execute("INSERT INTO test_tb1 VALUES ('b'), ('c')");

      // Table column of enum type with default value.
      stmt.execute("CREATE TABLE test_tb2(c1 INT, c2 e_t DEFAULT 'a')");
      stmt.execute("INSERT INTO test_tb2 VALUES(1)");

      // A user-defined type.
      stmt.execute("CREATE TYPE udt1 AS (f1 INT, f2 TEXT, f3 e_t)");

      // Table column of user-defined type.
      stmt.execute("CREATE TABLE test_tb3(c1 udt1)");
      stmt.execute("INSERT INTO test_tb3 VALUES((1, '1', 'a'))");
      stmt.execute("INSERT INTO test_tb3 VALUES((1, '2', 'b'))");
      stmt.execute("INSERT INTO test_tb3 VALUES((1, '2', 'c'))");

      // Table column of user-defined type and enum type with default values.
      stmt.execute("CREATE TABLE test_tb4(c1 INT, c2 udt1 DEFAULT (1, '2', 'b'), " +
                   "c3 e_t DEFAULT 'b')");
      stmt.execute("INSERT INTO test_tb4 VALUES (1)");

      // Table column of enum array type.
      stmt.execute("CREATE TABLE test_tb5 (c1 e_t[])");
      stmt.execute("INSERT INTO test_tb5 VALUES (ARRAY['a', 'b', 'c']::e_t[])");

      // nested user-defined type and enum type.
      stmt.execute("CREATE TYPE udt2 AS (f1 INT, f2 udt1, f3 e_t)");

      // Table column of nested user-defined type and enum type.
      stmt.execute("CREATE TABLE test_tb6(c1 INT, c2 udt2, c3 e_t)");
      stmt.execute("INSERT INTO test_tb6 VALUES (1, (1, (1, '1', 'a'), 'b'), 'c')");

      // Table column of array of nested user-defined type and enum type.
      stmt.execute("CREATE TABLE test_tb7 (c1 udt2[])");
      stmt.execute("INSERT INTO test_tb7 VALUES (ARRAY[" +
                   "(1, (1, (1, '1', 'a'), 'b'), 'c')," +
                   "(2, (2, (2, '2', 'b'), 'a'), 'c')," +
                   "(3, (3, (3, '3', 'a'), 'c'), 'b')]::udt2[])");

      // A domain type.
      stmt.execute("CREATE DOMAIN dom AS TEXT " +
                   "check(value ~ '^\\d{5}$'or value ~ '^\\d{5}-\\d{4}$')");
      // Table column of array of domain type.
      stmt.execute("CREATE TABLE test_tb8(c1 dom[])");
      stmt.execute("INSERT INTO test_tb8 VALUES (ARRAY['32768', '65536']::dom[])");

      // A range type.
      stmt.execute("CREATE TYPE inetrange AS RANGE(subtype = inet)");
      // Table column of range type.
      stmt.execute("CREATE TABLE test_tb9(c1 inetrange)");
      stmt.execute("INSERT INTO test_tb9 VALUES ('[10.0.0.1,10.0.0.2]'::inetrange)");
      // Table column of array of range type.
      stmt.execute("CREATE TABLE test_tb10(c1 inetrange[])");
      stmt.execute("INSERT INTO test_tb10 VALUES (ARRAY[" +
                   "'[10.0.0.1,10.0.0.2]'::inetrange, '[10.0.0.3,10.0.0.8]'::inetrange])");

      // Test drop column in the middle.
      stmt.execute("CREATE TABLE test_tb11(c1 e_t, c2 e_t, c3 e_t, c4 e_t)");
      stmt.execute("INSERT INTO test_tb11 VALUES (" +
                   "'a', 'b', 'c', 'a'), ('a', 'c', 'b', 'b'), ('b', 'a', 'c', 'c')");
      stmt.execute("ALTER TABLE test_tb11 DROP COLUMN c2");

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }

      stmt.execute("INSERT INTO test_tb1 VALUES ('a')");
      stmt.execute("INSERT INTO test_tb2 VALUES(2)");
      stmt.execute("INSERT INTO test_tb3 VALUES((2, '1', 'a'))");
      stmt.execute("INSERT INTO test_tb3 VALUES((2, '2', 'b'))");
      stmt.execute("INSERT INTO test_tb3 VALUES((2, '2', 'c'))");
      stmt.execute("INSERT INTO test_tb4 VALUES (2)");
      stmt.execute("INSERT INTO test_tb5 VALUES (ARRAY['c', 'b', 'a']::e_t[])");
      stmt.execute("INSERT INTO test_tb6 VALUES (2, (2, (2, '2', 'c'), 'b'), 'a')");
      stmt.execute("INSERT INTO test_tb7 VALUES (ARRAY[" +
                   "(4, (4, (4, '4', 'c'), 'b'), 'a')]::udt2[])");
      stmt.execute("INSERT INTO test_tb8 VALUES (ARRAY['16384', '81920']::dom[])");
      stmt.execute("INSERT INTO test_tb9 VALUES ('[10.0.0.3,10.0.0.8]'::inetrange)");
      stmt.execute("INSERT INTO test_tb10 VALUES (array['[10.0.0.9,10.0.0.12]'::inetrange])");
      stmt.execute("ALTER TABLE test_tb11 DROP COLUMN c3");

      List<Row> expectedRows1 = Arrays.asList(new Row("c"),
                                              new Row("b"),
                                              new Row("a"));
      List<Row> expectedRows2 = Arrays.asList(new Row(1, "a"),
                                              new Row(2, "a"));
      List<Row> expectedRows3 = Arrays.asList(new Row("(1,1,a)"),
                                              new Row("(1,2,c)"),
                                              new Row("(1,2,b)"),
                                              new Row("(2,1,a)"),
                                              new Row("(2,2,c)"),
                                              new Row("(2,2,b)"));
      List<Row> expectedRows4 = Arrays.asList(new Row(1, "(1,2,b)", "b"),
                                              new Row(2, "(1,2,b)", "b"));
      List<Row> expectedRows5 = Arrays.asList(new Row("{a,b,c}"),
                                              new Row("{c,b,a}"));
      List<Row> expectedRows6 = Arrays.asList(new Row(1, "(1,\"(1,1,a)\",b)", "c"),
                                              new Row(2, "(2,\"(2,2,c)\",b)", "a"));
      List<Row> expectedRows7 = Arrays.asList(
        new Row("{\"(1,\\\"(1,\\\"\\\"(1,1,a)\\\"\\\",b)\\\",c)\"," +
                 "\"(2,\\\"(2,\\\"\\\"(2,2,b)\\\"\\\",a)\\\",c)\"," +
                 "\"(3,\\\"(3,\\\"\\\"(3,3,a)\\\"\\\",c)\\\",b)\"}"),
        new Row("{\"(4,\\\"(4,\\\"\\\"(4,4,c)\\\"\\\",b)\\\",a)\"}"));
      List<Row> expectedRows8 = Arrays.asList(new Row("{16384,81920}"),
                                              new Row("{32768,65536}"));
      List<Row> expectedRows9 = Arrays.asList(new Row("[10.0.0.1,10.0.0.2]"),
                                              new Row("[10.0.0.3,10.0.0.8]"));
      List<Row> expectedRows10 = Arrays.asList(
        new Row("{\"[10.0.0.1,10.0.0.2]\",\"[10.0.0.3,10.0.0.8]\"}"),
        new Row("{\"[10.0.0.9,10.0.0.12]\"}"));

      // Column c2 and c3 are dropped from test_tbl1 so we only expect to see rows for
      // column c1 and c4.
      List<Row> expectedRows11 = Arrays.asList(new Row("b", "c"),
                                               new Row("a", "b"),
                                               new Row("a", "a"));
      assertRowList(stmt, "SELECT * FROM test_tb1 ORDER BY c1", expectedRows1);
      assertRowList(stmt, "SELECT * FROM test_tb1 ORDER BY c1", expectedRows1);
      assertRowList(stmt, "SELECT * FROM test_tb2 ORDER BY c1", expectedRows2);
      assertRowList(stmt, "SELECT * FROM test_tb3 ORDER BY c1", expectedRows3);
      assertRowList(stmt, "SELECT * FROM test_tb4 ORDER BY c1", expectedRows4);
      assertRowList(stmt, "SELECT c1::TEXT FROM test_tb5 ORDER BY c1", expectedRows5);
      assertRowList(stmt, "SELECT * FROM test_tb6 ORDER BY c1", expectedRows6);
      assertRowList(stmt, "SELECT c1::TEXT FROM test_tb7 ORDER BY c1", expectedRows7);
      assertRowList(stmt, "SELECT c1::TEXT FROM test_tb8 ORDER BY c1", expectedRows8);
      assertRowList(stmt, "SELECT * FROM test_tb9 ORDER BY c1", expectedRows9);
      assertRowList(stmt, "SELECT c1::TEXT FROM test_tb10 ORDER BY c1", expectedRows10);
      assertRowList(stmt, "SELECT * FROM test_tb11 ORDER BY c1, c4", expectedRows11);
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      List<Row> expectedRows1 = Arrays.asList(new Row("c"),
                                              new Row("b"));
      List<Row> expectedRows2 = Arrays.asList(new Row(1, "a"));
      List<Row> expectedRows3 = Arrays.asList(new Row("(1,1,a)"),
                                              new Row("(1,2,c)"),
                                              new Row("(1,2,b)"));
      List<Row> expectedRows4 = Arrays.asList(new Row(1, "(1,2,b)", "b"));
      List<Row> expectedRows5 = Arrays.asList(new Row("{a,b,c}"));
      List<Row> expectedRows6 = Arrays.asList(new Row(1, "(1,\"(1,1,a)\",b)", "c"));
      List<Row> expectedRows7 = Arrays.asList(
        new Row("{\"(1,\\\"(1,\\\"\\\"(1,1,a)\\\"\\\",b)\\\",c)\"," +
                 "\"(2,\\\"(2,\\\"\\\"(2,2,b)\\\"\\\",a)\\\",c)\"," +
                 "\"(3,\\\"(3,\\\"\\\"(3,3,a)\\\"\\\",c)\\\",b)\"}"));
      List<Row> expectedRows8 = Arrays.asList(new Row("{32768,65536}"));
      List<Row> expectedRows9 = Arrays.asList(new Row("[10.0.0.1,10.0.0.2]"));
      List<Row> expectedRows10 = Arrays.asList(
        new Row("{\"[10.0.0.1,10.0.0.2]\",\"[10.0.0.3,10.0.0.8]\"}"));

      // Only column c2 is dropped from test_tbl1 before backup, the column c3 was dropped
      // after backup and it should be restored. Therefore we expect to see rows for column
      // c1, c3 and c4.
      List<Row> expectedRows11 = Arrays.asList(new Row("b", "c", "c"),
                                               new Row("a", "c", "a"),
                                               new Row("a", "b", "b"));
      assertRowList(stmt, "SELECT * FROM test_tb1 ORDER BY c1", expectedRows1);
      assertRowList(stmt, "SELECT * FROM test_tb2 ORDER BY c1", expectedRows2);
      assertRowList(stmt, "SELECT * FROM test_tb3 ORDER BY c1", expectedRows3);
      assertRowList(stmt, "SELECT * FROM test_tb4 ORDER BY c1", expectedRows4);
      assertRowList(stmt, "SELECT c1::TEXT FROM test_tb5 ORDER BY c1", expectedRows5);
      assertRowList(stmt, "SELECT * FROM test_tb6 ORDER BY c1", expectedRows6);
      assertRowList(stmt, "SELECT c1::TEXT FROM test_tb7 ORDER BY c1", expectedRows7);
      assertRowList(stmt, "SELECT c1::TEXT FROM test_tb8 ORDER BY c1", expectedRows8);
      assertRowList(stmt, "SELECT * FROM test_tb9 ORDER BY c1", expectedRows9);
      assertRowList(stmt, "SELECT c1::TEXT FROM test_tb10 ORDER BY c1", expectedRows10);
      assertRowList(stmt, "SELECT * FROM test_tb11 ORDER BY c1, c3, c4", expectedRows11);
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  private void testMaterializedViewsHelper(boolean matviewOnMatview, String dbName)
      throws Exception {
    String backupDir = null;
    try (Connection connection = getConnectionBuilder().withDatabase(dbName).connect();
         Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test_tbl");
      stmt.execute("CREATE TABLE test_tbl (t int)");
      stmt.execute("CREATE MATERIALIZED VIEW test_mv AS SELECT * FROM test_tbl");
      if (matviewOnMatview) {
        stmt.execute("CREATE MATERIALIZED VIEW test_mv_2 AS SELECT * FROM test_mv");
      }
      stmt.execute("INSERT INTO test_tbl VALUES (1)");
      stmt.execute("REFRESH MATERIALIZED VIEW test_mv");
      if (matviewOnMatview) {
        stmt.execute("REFRESH MATERIALIZED VIEW test_mv_2");
      }
      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", String.format("ysql.%s", dbName));
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
        assertQuery(stmt, "SELECT * FROM test_mv WHERE t=1", new Row(1));
        if (matviewOnMatview) {
          assertQuery(stmt, "SELECT * FROM test_mv_2 WHERE t=1", new Row(1));
        }
    }
  }

  @Test
  public void testRefreshedMaterializedViewsBackup() throws Exception {
    testMaterializedViewsHelper(false, "yugabyte");
  }

  @Test
  public void testRefreshedMaterializedViewsOnMaterializedViewsBackup() throws Exception {
    testMaterializedViewsHelper(true, "yugabyte");
  }

  @Test
  public void testColocatedMateralizedViewBackup() throws Exception {
    String dbName = "colocated_db";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATION=true", dbName));
    }
    testMaterializedViewsHelper(false, dbName);
  }

  @Test
  public void testOnlyColocatedMateralizedViewBackup() throws Exception {
    // The difference between this test and the test above is in this test only
    // a materialized view is created without creating any other table first.
    // This is to test if we correctly preserve tablegroup oid before the
    // creation of the materialized view.
    String backupDir = null;
    String dbName = "colocated_db";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATION=true", dbName));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(dbName).connect();
         Statement stmt = connection2.createStatement()) {
      stmt.execute("CREATE MATERIALIZED VIEW mtv AS SELECT * FROM pg_class");

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", String.format("ysql.%s", dbName));
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT COUNT(*) FROM mtv",
                  getSingleRow(stmt, "SELECT COUNT(*) FROM pg_class"));
      assertQuery(stmt, "SELECT relname, relnamespace, relhasindex, relkind " +
                  "FROM mtv WHERE relname = 'pg_class'", new Row("pg_class", 11, true, "r"));
    }
  }

  @Test
  public void testLegacyColocatedMateralizedViewBackup() throws Exception {
    markClusterNeedsRecreation();
    restartClusterWithFlags(Collections.singletonMap("ysql_legacy_colocated_database_creation",
                                                     "true"),
                            Collections.emptyMap());
    initYBBackupUtil();
    String dbName = "colocated_db";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATION=true", dbName));
    }
    testMaterializedViewsHelper(false, dbName);
  }

  @Test
  public void testSequence() throws Exception {
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test_tbl (k SERIAL PRIMARY KEY, v INT)");
      stmt.execute("INSERT INTO test_tbl(v) SELECT * FROM generate_series(1, 50)");

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      stmt.execute("INSERT INTO test_tbl(v) VALUES (200)");
      // In YB, default sequence cache size is 100,
      // so the expected number generated from test_tbl's sequence after restore should be 101.
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE v = 200", new Row(101, 200));
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testPgHintPlan() throws Exception {
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      // Create tables, pg_hint_plan extension, and insert data.
      stmt.execute("CREATE EXTENSION pg_hint_plan");
      stmt.execute("CREATE TABLE tbl1 (k INT PRIMARY KEY, v INT)");
      stmt.execute("CREATE TABLE tbl2 (k INT PRIMARY KEY, v INT)");
      stmt.execute("INSERT INTO tbl1 SELECT i, i FROM generate_series(1,100) i");
      stmt.execute("INSERT INTO tbl2 SELECT i, i FROM generate_series(3,102) i");

      // Test pg_hint_plan before backup.
      stmt.execute("SET pg_hint_plan.enable_hint_table = on");
      stmt.execute("INSERT INTO hint_plan.hints(norm_query_string, application_name, hints)" +
                   " VALUES ('EXPLAIN (COSTS false) SELECT * FROM tbl1 WHERE tbl1.k = ?'," +
                   " '', 'SeqScan(tbl1)')");
      assertQuery(stmt, "EXPLAIN (COSTS false) SELECT * FROM tbl1 WHERE tbl1.k = 1",
                  new Row("Seq Scan on tbl1"),
                  new Row("  Storage Filter: (k = 1)"));
      stmt.execute("SET pg_hint_plan.enable_hint_table = off");
      assertQuery(stmt, "EXPLAIN (COSTS false) SELECT * FROM tbl1 WHERE tbl1.k = 1",
                  new Row("Index Scan using tbl1_pkey on tbl1"),
                  new Row("  Index Cond: (k = 1)"));

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      // Test pg_hint_plan after restore.
      stmt.execute("INSERT INTO hint_plan.hints(norm_query_string, application_name, hints)" +
                   " VALUES ('EXPLAIN (COSTS false) SELECT * FROM tbl2 WHERE tbl2.k = ?'," +
                   " '', 'SeqScan(tbl2)')");
      stmt.execute("SET pg_hint_plan.enable_hint_table = on");
      assertQuery(stmt, "EXPLAIN (COSTS false) SELECT * FROM tbl1 WHERE tbl1.k = 1",
                  new Row("Seq Scan on tbl1"),
                  new Row("  Storage Filter: (k = 1)"));
      assertQuery(stmt, "EXPLAIN (COSTS false) SELECT * FROM tbl2 WHERE tbl2.k = 3",
                  new Row("Seq Scan on tbl2"),
                  new Row("  Storage Filter: (k = 3)"));
      stmt.execute("SET pg_hint_plan.enable_hint_table = off");
      assertQuery(stmt, "EXPLAIN (COSTS false) SELECT * FROM tbl1 WHERE tbl1.k = 1",
                  new Row("Index Scan using tbl1_pkey on tbl1"),
                  new Row("  Index Cond: (k = 1)"));
      assertQuery(stmt, "EXPLAIN (COSTS false) SELECT * FROM tbl2 WHERE tbl2.k = 3",
                  new Row("Index Scan using tbl2_pkey on tbl2"),
                  new Row("  Index Cond: (k = 3)"));

      // Test unique index: hints_norm_and_app.
      runInvalidQuery(stmt, "INSERT INTO hint_plan.hints" +
                      "(norm_query_string, application_name, hints)" +
                      " VALUES ('EXPLAIN (COSTS false) SELECT * FROM tbl1 WHERE tbl1.k = ?'," +
                      " '', 'SeqScan(tbl1)')",
                      "duplicate key value violates unique constraint " +
                      "\"hints_norm_and_app\"");

      // Test pg_extension catalog info regarding configuration relations is correct.
      stmt.execute("SELECT ARRAY['hint_plan.hints'::regclass::oid, " +
                   "'hint_plan.hints_id_seq'::regclass::oid]::TEXT");
      ResultSet rs = stmt.getResultSet();
      rs.next();
      String extconfig = rs.getString(1);
      assertQuery(stmt, "SELECT extconfig::TEXT FROM pg_extension WHERE extname = 'pg_hint_plan'",
                  new Row(extconfig));

      // Test whether extension membership is set correctly after restoration.
      stmt.execute("DROP EXTENSION pg_hint_plan CASCADE");
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_class WHERE relname = 'hints')",
                  new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_class " +
                  "WHERE relname = 'hints_norm_and_app')", new Row(false));

    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testOrafce() throws Exception {
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      // Create orafce extension.
      stmt.execute("CREATE EXTENSION orafce");
      // Test functions created in oracle schema.
      assertQuery(stmt, "SELECT oracle.to_char(100)", new Row("100"));
      assertQuery(stmt, "SELECT oracle.substr('abcdef', 3)", new Row("cdef"));
      // Test operator.
      stmt.execute("SET search_path TO oracle, \"$user\", public");
      assertQuery(stmt, "SELECT oracle.to_date('2014-07-02 10:08:55','YYYY-MM-DD HH:MI:SS')" +
                        " + 9::integer", new Row(Timestamp.valueOf("2014-07-11 10:08:55")));
      // Test domain.
      stmt.execute("CREATE TABLE tbl_use_domain (k utl_file.file_type)");
      stmt.execute("INSERT INTO tbl_use_domain (k) VALUES (1), (2)");
      // Test base type.
      stmt.execute("CREATE TABLE tbl_varchar2 (a VARCHAR2(5))");
      stmt.execute("INSERT INTO tbl_varchar2(a) VALUES ('a'), ('bc')");
      // Test cast.
      assertQuery(stmt, "SELECT CAST('abc'::TEXT AS VARCHAR2)", new Row("abc"));
      // Test aggregate.
      stmt.execute("CREATE TABLE tbl (v INT)");
      stmt.execute("INSERT INTO tbl SELECT generate_series(1,100)");
      assertQuery(stmt, "SELECT median(v) FROM tbl", new Row(50.5));
      // Test view.
      assertQuery(stmt, "SELECT COUNT(*) FROM oracle.user_tables", new Row(77));

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      // Test orafce after restore.
      // Test functions created in oracle schema.
      assertQuery(stmt, "SELECT oracle.to_char(100)", new Row("100"));
      assertQuery(stmt, "SELECT oracle.substr('abcdef', 3)", new Row("cdef"));
      // Test operator.
      stmt.execute("SET search_path TO oracle, \"$user\", public");
      assertQuery(stmt, "SELECT oracle.to_date('2014-07-02 10:08:55','YYYY-MM-DD HH:MI:SS')" +
                        " + 9::integer", new Row(Timestamp.valueOf("2014-07-11 10:08:55")));
      // Test domain.
      assertQuery(stmt, "SELECT * FROM tbl_use_domain ORDER BY k",
                  new Row(1), new Row(2));
      // Test base type.
      assertQuery(stmt, "SELECT * FROM tbl_varchar2 ORDER BY a",
                  new Row("a"), new Row("bc"));
      // Test cast.
      assertQuery(stmt, "SELECT CAST('abc'::TEXT AS VARCHAR2)", new Row("abc"));
      // Test aggregate.
      stmt.execute("INSERT INTO tbl SELECT generate_series(101,200)");
      assertQuery(stmt, "SELECT median(v) FROM tbl", new Row(100.5));
      // Test view.
      assertQuery(stmt, "SELECT COUNT(*) FROM oracle.user_tables", new Row(77));

      // Test whether extension membership is set correctly after restoration.
      stmt.execute("DROP EXTENSION orafce CASCADE");
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_proc " +
                  "WHERE proname = 'to_char' AND prosrc = 'orafce_to_char_int4')", new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_proc " +
                  "WHERE proname = 'substr' AND prosrc = 'oracle_substr2')", new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_operator " +
                  "WHERE oprname = '+' AND oprresult = 'timestamp'::regtype::oid " +
                  "AND oprright = 'integer'::regtype::oid)", new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_type " +
                  "WHERE typname = 'file_type')", new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_type " +
                  "WHERE typname = 'varchar2')", new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_cast JOIN pg_type ON " +
                  "pg_cast.casttarget = pg_type.oid WHERE castsource = " +
                  "'text'::regtype::oid AND typname = 'varchar2')", new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_proc " +
                  "WHERE proname = 'median')", new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_class " +
                  "WHERE relname = 'user_tables')", new Row(false));
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testPostgresfdw() throws Exception {
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      // Create postgres_fdw extension, schema, table and insert data.
      stmt.execute("CREATE EXTENSION postgres_fdw");
      stmt.execute("CREATE SCHEMA test_schema");
      stmt.execute("CREATE TABLE test_schema.test_tbl (k INT, v INT)");
      stmt.execute("INSERT INTO test_schema.test_tbl VALUES (1, 1)");
      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      // Test postgres_fdw functionality after restore.
      // Create foreign server.
      stmt.execute("SELECT current_setting('listen_addresses')");
      ResultSet rs = stmt.getResultSet();
      rs.next();
      String host = rs.getString(1);
      stmt.execute("SELECT current_setting('port')");
      rs = stmt.getResultSet();
      rs.next();
      String port = rs.getString(1);
      stmt.execute(String.format("CREATE SERVER test_server FOREIGN DATA WRAPPER" +
                                 " postgres_fdw OPTIONS (host '%s', port '%s', dbname 'yb2')",
                                 host, port));
      stmt.execute("CREATE USER MAPPING FOR CURRENT_USER SERVER test_server" +
                   " OPTIONS (user 'yugabyte')");
      stmt.execute("CREATE FOREIGN TABLE foreign_tbl (k INT, v INT) SERVER test_server" +
                   " OPTIONS (schema_name 'test_schema', table_name 'test_tbl')");
      stmt.execute("INSERT INTO foreign_tbl VALUES (2, 2)");
      assertQuery(stmt, "SELECT * FROM foreign_tbl ORDER BY k",
                  new Row(1, 1), new Row(2, 2));

      // Test whether extension membership is set correctly after restoration.
      stmt.execute("DROP EXTENSION postgres_fdw CASCADE");
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_foreign_data_wrapper " +
                  "WHERE fdwname = 'postgres_fdw')", new Row(false));
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testExtensionBackupUsingTestExtension() throws Exception {
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      // Create test extension.
      stmt.execute("CREATE EXTENSION yb_test_extension");
      // Set up tables to test extension type backup.
      stmt.execute("CREATE TABLE enum_tbl (v test_enum)");
      stmt.execute("INSERT INTO enum_tbl(v) VALUES ('a'), ('c'), ('b')");
      stmt.execute("CREATE TABLE range_tbl (v test_range)");
      stmt.execute("INSERT INTO range_tbl(v) VALUES ('(1,4)'::test_range)");
      stmt.execute("CREATE TABLE composite_tbl (v test_composite)");
      stmt.execute("INSERT INTO composite_tbl(v) VALUES (('a', 'c')), (('b', 'd'))");

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      // Test trigger.
      stmt.execute("INSERT INTO tbl VALUES (1, 1), (2, 2)");
      assertQuery(stmt, "SELECT * FROM tbl ORDER BY k",
                  new Row(1, 2), new Row(2, 3));

      // Test event trigger.
      stmt.execute("CREATE TABLE dummy_tbl1 (k INT, v INT)");
      stmt.execute("CREATE TABLE dummy_tbl2 (k INT, v INT)");
      stmt.execute("DROP TABLE dummy_tbl1");
      stmt.execute("DROP TABLE dummy_tbl2");
      assertQuery(stmt, "SELECT * FROM command_tag ORDER BY k",
                  new Row(1, "DROP TABLE"), new Row(2, "DROP TABLE"));

      // Test sequence.
      assertQuery(stmt, "SELECT nextval('test_sequence')", new Row(101));

      // Test privilege.
      stmt.execute("SET ROLE yb_fdw");
      assertQuery(stmt, "SELECT nextval('test_sequence')", new Row(102));
      stmt.execute("RESET ROLE");

      // Test TS parser.
      assertQuery(stmt, "SELECT * FROM ts_parse('default', '123')",
                  new Row(22, "123"));
      assertQuery(stmt, "SELECT * FROM ts_parse('test_parser', '123')",
                  new Row(22, "123"));

      // Test TS dictionary.
      assertQuery(stmt, "SELECT ts_lexize('test_dict', 'YeS')::text",
                  new Row("{yes}"));

      // Test TS configuration.
      assertQuery(stmt, "SELECT alias, token, lexemes::text FROM ts_debug('english', 'cat')",
                  new Row("asciiword", "cat", "{cat}"));
      assertQuery(stmt, "SELECT alias, token, lexemes::text FROM" +
                  " ts_debug('test_config', 'cat')",
                  new Row("asciiword", "cat", "{cat}"));

      // Test TS template.
      // Create a dictionary using test_template.
      stmt.execute("CREATE TEXT SEARCH DICTIONARY dict (" +
                   "  TEMPLATE = test_template," +
                   "  STOPWORDS = english" +
                   ")");
      assertQuery(stmt, "SELECT ts_lexize('dict', 'YeS')::text",
                  new Row("{yes}"));
      stmt.execute("DROP TEXT SEARCH DICTIONARY dict");

      // Test collation.
      stmt.execute("CREATE TABLE tbl_collation (k SERIAL, v TEXT COLLATE test_collation)");
      stmt.execute("INSERT INTO tbl_collation(v) VALUES ('bbc'), ('abc')");
      assertQuery(stmt, "SELECT * FROM tbl_collation ORDER BY v",
                  new Row(2, "abc"), new Row(1, "bbc"));

      // Test language.
      stmt.execute("CREATE FUNCTION one()" +
                   "  RETURNS INT AS $$" +
                   "  BEGIN " +
                   "    RETURN 1;" +
                   "  END;" +
                   "  $$ LANGUAGE test_language");
      assertQuery(stmt, "SELECT one()", new Row(1));

      // Test enum type.
      assertQuery(stmt, "SELECT * FROM enum_tbl ORDER BY v",
                  new Row("c"), new Row("b"), new Row("a"));

      // Test range type.
      assertQuery(stmt, "SELECT * FROM range_tbl ORDER BY v",
                  new Row("(1,4)"));

      // Test composite type.
      assertQuery(stmt, "SELECT * FROM composite_tbl ORDER BY v",
                  new Row("(a,c)"), new Row("(b,d)"));

      // Test whether extension membership is set correctly after restoration.
      stmt.execute("DROP EXTENSION yb_test_extension CASCADE");
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_trigger WHERE tgname = 'increment_v')",
                  new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_event_trigger " +
                  "WHERE evtname = 'record_drop_command')", new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_class WHERE relname = 'test_sequence')",
                  new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_ts_parser WHERE prsname = 'test_parser')",
                  new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_ts_dict WHERE dictname = 'test_dict')",
                  new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_ts_config WHERE cfgname = 'test_config')",
                  new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_ts_template " +
                  "WHERE tmplname = 'test_template')", new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_collation " +
                  "WHERE collname = 'test_collation')", new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_language WHERE lanname = 'test_language')",
                  new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_type WHERE typname = 'test_enum')",
                  new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_type WHERE typname = 'test_range')",
                  new Row(false));
      assertQuery(stmt, "SELECT EXISTS(SELECT 1 FROM pg_type WHERE typname = 'test_composite')",
                  new Row(false));
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testIncludedColumns() throws Exception {
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE range_tbl_pk_with_include_clause (" +
                   " k2 TEXT," +
                   " v DOUBLE PRECISION," +
                   " k1 INT," +
                   " PRIMARY KEY (k1 ASC, k2 ASC) INCLUDE (v)" +
                   ") SPLIT AT VALUES((1, '1'), (100, '100'))");
      stmt.execute("CREATE UNIQUE INDEX unique_idx_with_include_clause ON " +
                   "range_tbl_pk_with_include_clause (k1, k2) INCLUDE (v)");
      stmt.execute("CREATE TABLE hash_tbl_pk_with_include_clause (" +
                   " k2 TEXT," +
                   " v DOUBLE PRECISION," +
                   " k1 INT," +
                   " PRIMARY KEY ((k1, k2) HASH) INCLUDE (v)" +
                   ") SPLIT INTO 8 TABLETS");
      stmt.execute("CREATE UNIQUE INDEX non_unique_idx_with_include_clause ON " +
                   "hash_tbl_pk_with_include_clause (k1, k2) INCLUDE (v);");

      stmt.execute("INSERT INTO range_tbl_pk_with_include_clause VALUES ('a', 1.1, 100), " +
                   "('b', 2.2, 200), ('c', 3.3, 300)");
      stmt.execute("INSERT INTO hash_tbl_pk_with_include_clause VALUES ('c', 3.3, 300), " +
                   "('b', 2.2, 200), ('a', 1.1, 100)");

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", "ysql.yugabyte");
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM range_tbl_pk_with_include_clause ORDER BY k2",
                  new Row("a", 1.1, 100), new Row("b", 2.2, 200), new Row("c", 3.3, 300));
      assertQuery(stmt, "SELECT * FROM hash_tbl_pk_with_include_clause ORDER BY k1",
                  new Row("a", 1.1, 100), new Row("b", 2.2, 200), new Row("c", 3.3, 300));
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE yb2");
    }
  }

  private byte[] getColocatedTableTabletId(String exactTableName) throws Exception {
    List<YBTable> colocatedTableList = getTablesFromName(exactTableName);
    YBTable colocatedTable = colocatedTableList.get(0);
    for (YBTable table : colocatedTableList) {
      if (exactTableName.equals(table.getName())) {
        colocatedTable = table;
      }
    }
    List<LocatedTablet> colocatedTabletList = colocatedTable.getTabletsLocations(30_000);
    assertEquals(1, colocatedTabletList.size());
    return colocatedTabletList.get(0).getTabletId();
  }

  @Test
  public void testLegacyColocatedDBMigration() throws Exception {
    markClusterNeedsRecreation();
    // Create a backup of a legacy colocated database.
    restartClusterWithFlags(Collections.singletonMap("ysql_legacy_colocated_database_creation",
                                                     "true"),
                            Collections.emptyMap());
    initYBBackupUtil();
    String dbname = "colocated_db";
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATION=true", dbname));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(dbname).connect();
         Statement stmt = connection2.createStatement()) {
      stmt.execute("CREATE TABLE tbl_col1 (k INT, v TEXT, PRIMARY KEY(k ASC))");
      stmt.execute("CREATE TABLE tbl_col2 (k INT, v INT, PRIMARY KEY(k DESC))");
      stmt.execute("CREATE INDEX tbl_col1_idx on tbl_col1(v ASC)");
      stmt.execute("CREATE UNIQUE INDEX tbl_col2_uniq_idx ON tbl_col2(v DESC)");
      stmt.execute("CREATE TABLE tbl_non_col (k INT PRIMARY KEY, v INT) WITH (COLOCATION = false) "
                   + "SPLIT INTO 5 TABLETS");

      for (int i = 0; i < 1000; ++i) {
        stmt.execute(String.format("INSERT INTO tbl_col1 VALUES (%d,'%s')", i, "text" + i));
        stmt.execute(String.format("INSERT INTO tbl_col2 VALUES (%d,%d)", i, i + 123));
        stmt.execute(String.format("INSERT INTO tbl_non_col VALUES (%d,%d)", i, i + 456));
      }

      // Verify the existence of legacy colocated database parent table.
      List<YBTable> legacyDBParentTablesList = getTablesFromName(".colocated.parent.tablename");
      assertEquals(1, legacyDBParentTablesList.size());
      List<YBTable> ColocationParentTablesList = getTablesFromName(".colocation.parent.tablename");
      assertEquals(0, ColocationParentTablesList.size());

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", String.format("ysql.%s", dbname));
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    // Restore/Migrate to a colocation database.
    restartClusterWithFlags(Collections.singletonMap("ysql_legacy_colocated_database_creation",
                                                     "false"),
                            Collections.emptyMap());
    initYBBackupUtil();
    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", String.format("ysql.%s", dbname));

    // Verify the correctness of migration.
    try (Connection connection2 = getConnectionBuilder().withDatabase(dbname).connect();
         Statement stmt = connection2.createStatement()) {
      // Verify the existence of colocation parent table.
      List<YBTable> legacyDBParentTablesList = getTablesFromName(".colocated.parent.tablename");
      assertEquals(0, legacyDBParentTablesList.size());
      List<YBTable> colocationParentTablesList = getTablesFromName(".colocation.parent.tablename");
      assertEquals(1, colocationParentTablesList.size());

      // Verify colocated tables share the same tablet with the parent table.
      YBTable colocationParentTable = colocationParentTablesList.get(0);
      List<LocatedTablet> colocationParentTableTablets =
          colocationParentTable.getTabletsLocations(30_000);
      assertEquals(1, colocationParentTableTablets.size());
      byte[] parentTableTabletId = colocationParentTableTablets.get(0).getTabletId();
      byte[] tblCol1TabletId = getColocatedTableTabletId("tbl_col1");
      byte[] tblCol2TabletId = getColocatedTableTabletId("tbl_col2");
      assertArrayEquals(parentTableTabletId, tblCol1TabletId);
      assertArrayEquals(tblCol1TabletId, tblCol2TabletId);

      // Verify data.
      assertQuery(stmt, "SELECT grpname FROM pg_yb_tablegroup", new Row("default"));
      for (int i : new int[] {9, 99, 999}) {
        assertQuery(stmt, String.format("SELECT * FROM tbl_col1 WHERE k=%d", i),
                    new Row(i, "text" + i));
        assertQuery(stmt, String.format("SELECT * FROM tbl_col2 WHERE k=%d", i),
                    new Row(i, i + 123));
        assertQuery(stmt, String.format("SELECT * FROM tbl_non_col WHERE k=%d", i),
                    new Row(i, i + 456));
      }
    }
  }

  @Test
  public void testLegacyColocatedDBWithNoColocatedTablesMigration() throws Exception {
    // This unit test tests for an edge case where a legacy colocated database doesn't contain any
    // colocated table.
    markClusterNeedsRecreation();
    // Create a backup of a legacy colocated database.
    restartClusterWithFlags(Collections.singletonMap("ysql_legacy_colocated_database_creation",
                                                     "true"),
                            Collections.emptyMap());
    initYBBackupUtil();
    String dbname = "colocated_db";
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATION=true", dbname));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(dbname).connect();
         Statement stmt = connection2.createStatement()) {
      stmt.execute("CREATE TABLE tbl_non_col1 (k INT, v TEXT, PRIMARY KEY(k ASC))"
                   + " WITH (COLOCATION = false)");
      stmt.execute("CREATE TABLE tbl_non_col2 (k INT PRIMARY KEY, v INT)"
                   + " WITH (COLOCATION = false)");
      stmt.execute("CREATE INDEX tbl_non_col1_idx on tbl_non_col1(v ASC)");
      stmt.execute("CREATE UNIQUE INDEX tbl_non_col2_uniq_idx ON tbl_non_col2(v DESC)");

      for (int i = 0; i < 1000; ++i) {
        stmt.execute(String.format("INSERT INTO tbl_non_col1 VALUES (%d,'%s')", i, "text" + i));
        stmt.execute(String.format("INSERT INTO tbl_non_col2 VALUES (%d,%d)", i, i + 123));
      }

      // Verify the existence of legacy colocated database parent table.
      List<YBTable> legacyDBParentTablesList = getTablesFromName(".colocated.parent.tablename");
      assertEquals(1, legacyDBParentTablesList.size());
      List<YBTable> colocationParentTablesList = getTablesFromName(".colocation.parent.tablename");
      assertEquals(0, colocationParentTablesList.size());

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", String.format("ysql.%s", dbname));
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    // Restore/Migrate to a colocation database.
    restartClusterWithFlags(Collections.singletonMap("ysql_legacy_colocated_database_creation",
                                                     "false"),
                            Collections.emptyMap());
    initYBBackupUtil();
    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", String.format("ysql.%s", dbname));

    // Verify the correctness of migration.
    try (Connection connection2 = getConnectionBuilder().withDatabase(dbname).connect();
         Statement stmt = connection2.createStatement()) {
      // Verify both legacy colocated database parent table and colocation database parent table
      // don't exist.
      List<YBTable> legacyDBParentTablesList = getTablesFromName(".colocated.parent.tablename");
      assertEquals(0, legacyDBParentTablesList.size());
      List<YBTable> colocationParentTablesList = getTablesFromName(".colocation.parent.tablename");
      assertEquals(0, colocationParentTablesList.size());

      // Verify data.
      assertQuery(stmt, "SELECT grpname FROM pg_yb_tablegroup");
      for (int i : new int[] {9, 99, 999}) {
        assertQuery(stmt, String.format("SELECT * FROM tbl_non_col1 WHERE k=%d", i),
                    new Row(i, "text" + i));
        assertQuery(stmt, String.format("SELECT * FROM tbl_non_col2 WHERE k=%d", i),
                    new Row(i, i + 123));
      }
    }
  }

  @Test
  public void testColocatedPartialIndex() throws Exception {
    String dbname = "colocated_db";
    String restore_dbname = "colocated_db2";
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATION=true", dbname));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(dbname).connect();
         Statement stmt = connection2.createStatement()) {
      stmt.execute("CREATE TABLE tbl (k INT PRIMARY KEY, v INT, v2 TEXT)");
      stmt.execute("CREATE INDEX tbl_partial_idx on tbl(v ASC) WHERE v < 300 AND v >= 100");
      stmt.execute("INSERT INTO tbl VALUES (1, 100, '100'), (2, 200, '200'), (3, 300, '300')");

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", String.format("ysql.%s", dbname));
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", String.format("ysql.%s",
                                                                           restore_dbname));

    try (Connection connection2 = getConnectionBuilder().withDatabase(restore_dbname).connect();
         Statement stmt = connection2.createStatement()) {
      // Index Only Scan
      String query = "SELECT v FROM tbl WHERE v < 300 AND v > 100";
      assertTrue(isIndexOnlyScan(stmt, query, "tbl_partial_idx"));

      // Verify data.
      assertQuery(stmt, query, new Row(200));
    }
  }

  /**
   * Disabling geo partitioning tests when backups are run using YB Controller.
   * This is because yb-controller-cli currently doesn't support such backups
   * using the yb-controller-cli.
   * Geo-partitioned backups via YB Controller are tested as part of YBA iTests.
   */
  private boolean disableGeoPartitionedTests() {
    return TestUtils.useYbController();
  }
}
