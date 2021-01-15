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

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBBackupException;
import org.yb.util.YBBackupUtil;
import org.yb.util.YBTestRunnerNonSanitizersOrMac;

import static org.yb.AssertionWrappers.fail;

@RunWith(value=YBTestRunnerNonSanitizersOrMac.class)
public class TestYbBackup extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYbBackup.class);

  @Before
  public void initYBBackupUtil() {
    YBBackupUtil.setMasterAddresses(masterAddresses);
    YBBackupUtil.setPostgresContactPoint(miniCluster.getPostgresContactPoints().get(0));
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 360; // Usual time for a test ~90 seconds. But can be much more on Jenkins.
  }

  @Override
  protected int overridableNumShardsPerTServer() {
    return 2;
  }

  @Test
  public void testAlteredYSQLTableBackup() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE  test_tbl (h INT PRIMARY KEY, a INT, b FLOAT)");

      for (int i = 1; i <= 2000; ++i) {
        stmt.execute("INSERT INTO test_tbl (h, a, b) VALUES" +
          " (" + String.valueOf(i) +                     // h
          ", " + String.valueOf(100 + i) +               // a
          ", " + String.valueOf(2.14 + (float)i) + ")"); // b
      }

      stmt.execute("ALTER TABLE test_tbl DROP a");
      YBBackupUtil.runYbBackupCreate("--keyspace", "ysql.yugabyte");

      stmt.execute("INSERT INTO test_tbl (h, b) VALUES (9999, 8.9)");

      YBBackupUtil.runYbBackupRestore("--keyspace", "ysql.yb2");

      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 3.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=2000", new Row(2000, 2002.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=9999", new Row(9999, 8.9));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = connection2.createStatement()) {
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=1", new Row(1, 3.14));
      assertQuery(stmt, "SELECT b FROM test_tbl WHERE h=1", new Row(3.14));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=2000", new Row(2000, 2002.14));
      assertQuery(stmt, "SELECT h FROM test_tbl WHERE h=2000", new Row(2000));
      assertQuery(stmt, "SELECT * FROM test_tbl WHERE h=9999");
    }
  }

  @Test
  public void testAlteredYSQLTableBackupInOriginalCluster() throws Exception {
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
  public void testAlteredYSQLTableBackupWithNotNull() throws Exception {
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
}
