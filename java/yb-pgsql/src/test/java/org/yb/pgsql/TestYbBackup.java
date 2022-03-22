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

import java.sql.Connection;
import java.sql.Statement;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.List;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.minicluster.MiniYBCluster;
import org.yb.util.TableProperties;
import org.yb.util.YBBackupException;
import org.yb.util.YBBackupUtil;
import org.yb.util.YBTestRunnerNonSanitizersOrMac;

import static org.yb.AssertionWrappers.assertArrayEquals;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

@RunWith(value=YBTestRunnerNonSanitizersOrMac.class)
public class TestYbBackup extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYbBackup.class);

  @Before
  public void initYBBackupUtil() {
    YBBackupUtil.setTSAddresses(miniCluster.getTabletServers());
    YBBackupUtil.setMasterAddresses(masterAddresses);
    YBBackupUtil.setPostgresContactPoint(miniCluster.getPostgresContactPoints().get(0));
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 600; // Usual time for a test ~90 seconds. But can be much more on Jenkins.
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
      stmt.execute(String.format("CREATE DATABASE %s COLOCATED=%s", initialDBName, colocString));
    }
    try (Connection connection2 = getConnectionBuilder().withDatabase(initialDBName).connect();
         Statement stmt = connection2.createStatement()) {
      stmt.execute("CREATE TABLE test_tbl (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   String.format("WITH (colocated = %s)", colocString));

      for (int i = 1; i <= 2000; ++i) {
        stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES" +
          " (" + String.valueOf(i) +                     // h
          ", " + String.valueOf(100 + i) +               // a
          ", " + String.valueOf(2.14 + (float)i) + ")"); // b
      }

      stmt.execute("ALTER TABLE test_tbl DROP a");
      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql." + initialDBName);

      stmt.execute("INSERT INTO test_tbl (h, b) VALUES (9999, 8.9)");

      YBBackupUtil.runYbBackupRestore("--keyspace", "ysql." + restoreDBName);

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
  public void testMixedColocatedDatabase() throws Exception {
    String initialDBName = "yb_colocated";
    String restoreDBName = "yb_colocated2";

    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATED=TRUE", initialDBName));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(initialDBName).connect();
         Statement stmt = connection2.createStatement()) {
      // Create 3 tables, 2 colocated and 1 non colocated but in the same db.
      stmt.execute("CREATE TABLE test_tbl1 (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   "WITH (COLOCATED=TRUE)");
      stmt.execute("CREATE TABLE test_tbl2 (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   "WITH (COLOCATED=TRUE)");
      stmt.execute("CREATE TABLE test_tbl3 (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   "WITH (COLOCATED=FALSE)");

      // Insert random rows/values for tables to snapshot
      for (int j = 1; j <= 3; ++j) {
        for (int i = 1; i <= 2000; ++i) {
          stmt.execute("INSERT INTO test_tbl" + String.valueOf(j) + " (h, a, b) VALUES" +
            " (" + String.valueOf(i * j) +                       // h
            ", " + String.valueOf((100 + i) * j) +               // a
            ", " + String.valueOf((2.14 + (float)i) * j) + ")"); // b
        }
      }

      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql." + initialDBName);

      // Insert more rows after taking the snapshot.
      for (int j = 1; j <= 3; ++j) {
        stmt.execute("INSERT INTO test_tbl" + String.valueOf(j) + " (h, a, b) VALUES" +
        " (" + String.valueOf(9999 * j) +                        // h
        ", " + String.valueOf((100 + 9999) * j) +                // a
        ", " + String.valueOf((2.14 + 9999f) * j) + ")");        // b
      }

      YBBackupUtil.runYbBackupRestore("--keyspace", "ysql." + restoreDBName);

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
      stmt.execute(String.format("DROP DATABASE %s", initialDBName));
      stmt.execute(String.format("DROP DATABASE %s", restoreDBName));
    }
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

      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql.yugabyte");
      stmt.execute("ALTER TABLE test_tbl DROP a");
      stmt.execute("INSERT INTO test_tbl (h, b) VALUES (9999, 8.9)");

      try {
        YBBackupUtil.runYbBackupRestore();
        fail("Backup restoring did not fail as expected");
      } catch (YBBackupException ex) {
        LOG.info("Expected exception", ex);
      }

      YBBackupUtil.runYbBackupRestore("--keyspace", "ysql.yb2");
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
  public void testColocatedWithTableOidAlreadySet() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE DATABASE yb1 COLOCATED=TRUE");
    }
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb1").connect();
         Statement stmt = connection2.createStatement()) {
      // Create a table with a set table_oid.
      stmt.execute("SET yb_enable_create_with_table_oid = true");
      stmt.execute("CREATE TABLE test_tbl (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   "WITH (table_oid = 123456)");
      stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES (1, 101, 3.14)");

      // Check that backup and restore works fine.
      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql.yb1");
      YBBackupUtil.runYbBackupRestore("--keyspace", "ysql.yb2");
    }
    // Verify data is correct.
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT b FROM test_tbl WHERE h=1", new Row(3.14));

      // Now try to do a backup/restore of the restored db.
      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql.yb2");
      YBBackupUtil.runYbBackupRestore("--keyspace", "ysql.yb3");
    }
    // Verify data is correct.
    try (Connection connection2 = getConnectionBuilder().withDatabase("yb3").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 101, 3.14));
      assertQuery(stmt, "SELECT b FROM test_tbl WHERE h=1", new Row(3.14));
    }
    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP DATABASE yb1");
      stmt.execute("DROP DATABASE yb2");
      stmt.execute("DROP DATABASE yb3");
    }
  }

  @Test
  public void testColocatedDatabaseRestoreToOriginalDB() throws Exception {
    String initialDBName = "yb_colocated";
    int num_tables = 2;

    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATED=TRUE", initialDBName));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(initialDBName).connect();
         Statement stmt = connection2.createStatement()) {
      stmt.execute("CREATE TABLE test_tbl1 (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   "WITH (COLOCATED=TRUE)");
      stmt.execute("CREATE TABLE test_tbl2 (h INT PRIMARY KEY, a INT, b FLOAT) " +
                   "WITH (COLOCATED=TRUE)");

      // Insert random rows/values for tables to snapshot
      for (int j = 1; j <= num_tables; ++j) {
        for (int i = 1; i <= 2000; ++i) {
          stmt.execute("INSERT INTO test_tbl" + String.valueOf(j) + " (h, a, b) VALUES" +
            " (" + String.valueOf(i * j) +                       // h
            ", " + String.valueOf((100 + i) * j) +               // a
            ", " + String.valueOf((2.14 + (float)i) * j) + ")"); // b
        }
      }

      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql." + initialDBName);

      // Truncate tables after taking the snapshot.
      for (int j = 1; j <= num_tables; ++j) {
        stmt.execute("TRUNCATE TABLE test_tbl" + String.valueOf(j));
      }

      // Restore back into this same database, this way all the ids will happen to be the same.
      YBBackupUtil.runYbBackupRestore("--keyspace", "ysql." + initialDBName);

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
      stmt.execute(String.format("DROP DATABASE %s", initialDBName));
    }
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
          "null value in column \"a\" violates not-null constraint");

      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql.yugabyte");
      stmt.execute("INSERT INTO test_tbl (id, a, b) VALUES (9999, 9, 9)");

      YBBackupUtil.runYbBackupRestore("--keyspace", "ysql.yb2");
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
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testIndex() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test_tbl (h INT PRIMARY KEY, a INT, b FLOAT)");
      stmt.execute("CREATE INDEX test_idx ON test_tbl (a)");

      for (int i = 1; i <= 1000; ++i) {
        stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES" +
          " (" + String.valueOf(i) +                     // h
          ", " + String.valueOf(100 + i) +               // a
          ", " + String.valueOf(2.14 + (float)i) + ")"); // b
      }

      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql.yugabyte");

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
    YBBackupUtil.runYbBackupRestore("--keyspace", "ysql.yb2");

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
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testIndexTypes() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test_tbl (h INT PRIMARY KEY, c1 INT, c2 INT, c3 INT, c4 INT)");
      stmt.execute("CREATE INDEX test_idx1 ON test_tbl (c1)");
      stmt.execute("CREATE INDEX test_idx2 ON test_tbl (c2 HASH)");
      stmt.execute("CREATE INDEX test_idx3 ON test_tbl (c3 ASC)");
      stmt.execute("CREATE INDEX test_idx4 ON test_tbl (c4 DESC)");

      stmt.execute("INSERT INTO test_tbl (h, c1, c2, c3, c4) VALUES (1, 11, 12, 13, 14)");

      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql.yugabyte");

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

    YBBackupUtil.runYbBackupRestore("--keyspace", "ysql.yb2");

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
      stmt.execute("DROP DATABASE yb2");
    }
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

      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql.yugabyte");
      verifyPartialIndexData(stmt);
    }

    YBBackupUtil.runYbBackupRestore("--keyspace", "ysql.yb2");

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      verifyPartialIndexData(stmt);
    }

    // Cleanup.
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testRestoreWithRestoreTime() throws Exception {
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

      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql.yugabyte");

      // Backup using --restore_time.
      YBBackupUtil.runYbBackupRestore("--keyspace", "ysql.yb2", "--restore_time", ts);
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
  public void testUserDefinedTypes() throws Exception {
    // TODO(myang): Add ALTER TYPE test after #1893 is fixed.
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

      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql.yugabyte");

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

    YBBackupUtil.runYbBackupRestore("--keyspace", "ysql.yb2");

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
      stmt.execute("DROP DATABASE yb2");
    }
  }
}
