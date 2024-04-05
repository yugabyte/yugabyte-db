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
import java.util.Map;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.json.JSONObject;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBCluster;
import org.yb.util.TableProperties;
import org.yb.util.YBBackupException;
import org.yb.util.YBBackupUtil;
import org.yb.util.YBTestRunnerNonTsanAsan;

import static org.yb.AssertionWrappers.assertArrayEquals;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

@RunWith(value=YBTestRunnerNonTsanAsan.class)
public class TestYsqlPartitionedBackup extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYsqlPartitionedBackup.class);

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
  public int getTestMethodTimeoutSec() {
    return 700;
  }

  @Override
  protected int getNumShardsPerTServer() {
    return 2;
  }

  private void partitionedTestVerifyDuplicateConstraintHelper(String tablename) throws Exception {
    try (Connection conn = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = conn.createStatement()) {
      final String error_msg = "duplicate key value violates unique constraint";
      runInvalidQuery(stmt, "INSERT INTO " + tablename + " VALUES " +
                            "(1, 2, 3, 4, 5), (1, 2, 3, 4, 5)", error_msg);
    }
  }

  private void partitionedTestVerifyDuplicateInsert(String tablename) throws Exception {
    try (Connection conn = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("INSERT INTO " + tablename + " VALUES (2, 2, 3, 4, 5), (2, 2, 3, 4, 5)");
    }
  }

  private void partitionedTestInsertSampleDataHelper() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO htest VALUES (1, 2, 3, 4, 5)");
      stmt.execute("INSERT INTO ltest VALUES (1, 2, 3, 4, 5)");
      stmt.execute("INSERT INTO rtest VALUES (1, 2, 3, 4, 5)");
    }
  }

  private void partitionedTestVerifyInsertDataHelper() throws Exception {
    try (Connection conn = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = conn.createStatement()) {
      partitionedTestVerifyInsertedDataHelper(stmt, "htest");
      partitionedTestVerifyInsertedDataHelper(stmt, "rtest");
      partitionedTestVerifyInsertedDataHelper(stmt, "ltest");
    }
  }

  private void partitionedTestVerifyInsertedDataHelper(Statement stmt, String tablename)
    throws Exception {

    // Verify that only the first partition has any data.
    String query = String.format("SELECT tableoid::regclass, * FROM %s WHERE k1=1", tablename);
    Row row = new Row (tablename + "_1", 1, "2", 3, 4, "5");
    assertQuery(stmt, query, row);
  }

  private void partitionedTestBackupAndRestoreHelper() throws Exception {
    partitionedTestInsertSampleDataHelper();
    String backupDir = YBBackupUtil.getTempBackupDir();
    String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
        "--keyspace", "ysql.yugabyte");
    if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");
    partitionedTestVerifyInsertDataHelper();
  }

  private void partitionedTestCleanupHelper() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP DATABASE yb2");
    }
  }

  @Test
  public void testPartitionedTableWithParentPrimaryKey() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      createPartitionedTable(stmt, "htest", YSQLPartitionType.HASH, "k1, k2");
      createPartition(stmt, "htest", YSQLPartitionType.HASH, 1);

      createPartitionedTable(stmt, "ltest", YSQLPartitionType.LIST, "(k1, k2) HASH");
      createPartition(stmt, "ltest", YSQLPartitionType.LIST, 1);

      createPartitionedTable(stmt, "rtest", YSQLPartitionType.RANGE, "k1 ASC, k2 DESC");
      createPartition(stmt, "rtest", YSQLPartitionType.RANGE, 1);

      // Perform backup and restore.
      partitionedTestBackupAndRestoreHelper();

      // Verify that duplicate keys cannot be inserted.
      partitionedTestVerifyDuplicateConstraintHelper("htest");
      partitionedTestVerifyDuplicateConstraintHelper("ltest");
      partitionedTestVerifyDuplicateConstraintHelper("rtest");
    }

    // Verify that the range keys are indeed used in the query plans.
    try (Connection conn = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = conn.createStatement()) {
      isPartitionedOrderedIndexScan(stmt,
          "SELECT * FROM htest WHERE k1=1 ORDER BY k2",
          "htest_1_pkey");
      isPartitionedOrderedIndexScan(stmt,
          "SELECT * FROM rtest ORDER BY k1",
          "rtest_1_pkey");
      isPartitionedOrderedIndexScan(stmt,
          "SELECT * FROM rtest WHERE k1=1 ORDER BY k2 DESC",
          "rtest_1_pkey");
    }
    partitionedTestCleanupHelper();
  }

  @Test
  public void testPartitionedTableWithChildPrimaryKey() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Create a partitioned table. Create one child with primary key and one child
      // without a primary key.
      createPartitionedTable(stmt, "htest", YSQLPartitionType.HASH);
      createPartition(stmt, "htest", YSQLPartitionType.HASH, 1, "k1, k2");
      createPartition(stmt, "htest", YSQLPartitionType.HASH, 2);

      createPartitionedTable(stmt, "ltest", YSQLPartitionType.LIST);
      createPartition(stmt, "ltest", YSQLPartitionType.LIST, 1, "(k1, k2) HASH");
      createPartition(stmt, "ltest", YSQLPartitionType.LIST, 2);

      createPartitionedTable(stmt, "rtest", YSQLPartitionType.RANGE);
      createPartition(stmt, "rtest", YSQLPartitionType.RANGE, 1, "k1 ASC, k2 DESC");
      createPartition(stmt, "rtest", YSQLPartitionType.RANGE, 2);

      // Perform backup and restore.
      partitionedTestBackupAndRestoreHelper();

      // Fail duplicate key insertion on child partitions with primary key.
      partitionedTestVerifyDuplicateConstraintHelper("htest");
      partitionedTestVerifyDuplicateConstraintHelper("ltest");
      partitionedTestVerifyDuplicateConstraintHelper("rtest");
    }
    try (Connection conn = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = conn.createStatement()) {
      // Insert of duplicate keys succeeds on children without primary key.
      stmt.execute("INSERT INTO htest VALUES (3, 2, 3, 4, 5), (3, 2, 3, 4, 5)");
      partitionedTestVerifyDuplicateInsert("ltest");
      partitionedTestVerifyDuplicateInsert("rtest");

      isOrderedIndexScan(stmt, "SELECT * FROM htest_1 WHERE k1=1 ORDER BY k2",
          "htest_1_pkey");
      isOrderedIndexScan(stmt, "SELECT * FROM rtest_1 ORDER BY k1",
          "rtest_1_pkey");
      isOrderedIndexScan(stmt, "SELECT * FROM rtest_1 WHERE k1=1 ORDER BY k2 DESC",
          "rtest_1_pkey");
    }
    partitionedTestCleanupHelper();
  }

  @Test
  public void testPartitionedTableWithParentUniqueKey() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      createPartitionedTable(stmt, "htest", YSQLPartitionType.HASH,
                             "k1, k2" /* unique_key */, "k3" /* unique_include */);
      createPartition(stmt, "htest", YSQLPartitionType.HASH, 1);

      createPartitionedTable(stmt, "ltest", YSQLPartitionType.LIST);
      stmt.execute("CREATE UNIQUE INDEX ON ltest ((k1, k2) HASH) INCLUDE (k3)");
      createPartition(stmt, "ltest", YSQLPartitionType.LIST, 1);

      createPartitionedTable(stmt, "rtest", YSQLPartitionType.RANGE);
      stmt.execute("CREATE UNIQUE INDEX ON rtest (k1 ASC, k2 DESC) INCLUDE (k3)");
      createPartition(stmt, "rtest", YSQLPartitionType.RANGE, 1);

      // Perform backup and restore.
      partitionedTestBackupAndRestoreHelper();

      // Verify that duplicate keys cannot be inserted at the target.
      partitionedTestVerifyDuplicateConstraintHelper("htest");
      partitionedTestVerifyDuplicateConstraintHelper("ltest");
      partitionedTestVerifyDuplicateConstraintHelper("rtest");
    }

    try (Connection conn = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = conn.createStatement()) {
      // Verify that the range keys are indeed used in the query plans
      // in the backup target.
      isPartitionedOrderedIndexOnlyScan(stmt,
          "SELECT k1, k2, k3 FROM htest WHERE k1=1 ORDER BY k2",
          "htest_1_k1_k2_k3_key");
      isPartitionedOrderedIndexOnlyScan(stmt,
        "SELECT k1, k2, k3 FROM rtest ORDER BY k1",
         "rtest_1_k1_k2_k3_idx");
      isPartitionedOrderedIndexOnlyScan(stmt,
        "SELECT k1, k2, k3 FROM rtest WHERE k1=1 ORDER BY k2 DESC",
        "rtest_1_k1_k2_k3_idx");
    }
    partitionedTestCleanupHelper();
  }


  @Test
  public void testPartitionedTableWithChildUniqueKey() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Create a partitioned table. Create one child with unique key and one child
      // without a unique key.
      createPartitionedTable(stmt, "htest", YSQLPartitionType.HASH);
      createPartition(stmt, "htest", YSQLPartitionType.HASH, 1,
                      "k1, k2" /* unique_key */, "k3" /* unique_include */);
      createPartition(stmt, "htest", YSQLPartitionType.HASH, 2);

      createPartitionedTable(stmt, "ltest", YSQLPartitionType.LIST);
      createPartition(stmt, "ltest", YSQLPartitionType.LIST, 1);
      stmt.execute("CREATE UNIQUE INDEX ON ltest_1 ((k1, k2) HASH) INCLUDE (k3)");
      createPartition(stmt, "ltest", YSQLPartitionType.LIST, 2);

      createPartitionedTable(stmt, "rtest", YSQLPartitionType.RANGE);
      createPartition(stmt, "rtest", YSQLPartitionType.RANGE, 1);
      stmt.execute("CREATE UNIQUE INDEX ON rtest_1 (k1 ASC, k2 DESC) INCLUDE (k3)");
      createPartition(stmt, "rtest", YSQLPartitionType.RANGE, 2);

      // Perform backup and restore.
      partitionedTestBackupAndRestoreHelper();

      // Fail duplicate key insertion on child partitions with unique key at the target.
      partitionedTestVerifyDuplicateConstraintHelper("htest");
      partitionedTestVerifyDuplicateConstraintHelper("ltest");
      partitionedTestVerifyDuplicateConstraintHelper("rtest");
    }

    try (Connection conn = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = conn.createStatement()) {
      // Insert of duplicate keys succeeds on children without unique key.
      stmt.execute("INSERT INTO htest VALUES (3, 2, 3, 4, 5), (3, 2, 3, 4, 5)");
      partitionedTestVerifyDuplicateInsert("ltest");
      partitionedTestVerifyDuplicateInsert("rtest");

      isOrderedIndexOnlyScan(stmt,
          "SELECT k1, k2, k3 FROM htest_1 WHERE k1=1 ORDER BY k2",
          "htest_1_k1_k2_k3_key");
      isOrderedIndexOnlyScan(stmt,
        "SELECT k1, k2, k3 FROM rtest_1 ORDER BY k1",
         "rtest_1_k1_k2_k3_idx");
      isOrderedIndexOnlyScan(stmt,
        "SELECT k1, k2, k3 FROM rtest_1 WHERE k1=1 ORDER BY k2 DESC",
        "rtest_1_k1_k2_k3_idx");
    }
    partitionedTestCleanupHelper();
  }


  @Test
  public void testPartitionedTableWithParentSecIndex() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      createPartitionedTable(stmt, "htest", YSQLPartitionType.HASH);
      stmt.execute("CREATE INDEX ON htest(k1, k2) INCLUDE (k3, v1)");
      createPartition(stmt, "htest", YSQLPartitionType.HASH, 1);

      createPartitionedTable(stmt, "ltest", YSQLPartitionType.LIST);
      stmt.execute("CREATE INDEX ON ltest ((k1, k2) HASH) INCLUDE (k3, v2)");
      createPartition(stmt, "ltest", YSQLPartitionType.LIST, 1);

      createPartitionedTable(stmt, "rtest", YSQLPartitionType.RANGE);
      stmt.execute("CREATE INDEX ON rtest (k1 ASC, k2 DESC) INCLUDE (v1, v2)");
      createPartition(stmt, "rtest", YSQLPartitionType.RANGE, 1);

      // Perform backup and restore.
      partitionedTestBackupAndRestoreHelper();
    }

    try (Connection conn = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = conn.createStatement()) {
      // Verify that the range keys are indeed used in the query plans.
      isPartitionedOrderedIndexOnlyScan(stmt,
          "SELECT k1, k2, k3, v1 FROM htest WHERE k1=1 ORDER BY k2",
          "htest_1_k1_k2_k3_v1_idx");
      isPartitionedOrderedIndexOnlyScan(stmt,
          "SELECT k1, k2, v1, v2 FROM rtest ORDER BY k1",
          "rtest_1_k1_k2_v1_v2_idx");
      isPartitionedOrderedIndexOnlyScan(stmt,
          "SELECT k1, k2, v1, v2 FROM rtest WHERE k1=1 ORDER BY k2 DESC",
          "rtest_1_k1_k2_v1_v2_idx");
    }
    partitionedTestCleanupHelper();
  }


  @Test
  public void testPartitionedTableWithChildSecIndex() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Create a partitioned table. Create one child with index and one child
      // without an index.
      createPartitionedTable(stmt, "htest", YSQLPartitionType.HASH);
      createPartition(stmt, "htest", YSQLPartitionType.HASH, 1);
      stmt.execute("CREATE INDEX ON htest_1(k1, k2) INCLUDE (k3, v1)");
      createPartition(stmt, "htest", YSQLPartitionType.HASH, 2);

      createPartitionedTable(stmt, "ltest", YSQLPartitionType.LIST);
      createPartition(stmt, "ltest", YSQLPartitionType.LIST, 1);
      stmt.execute("CREATE INDEX ON ltest_1 ((k1, k2) HASH) INCLUDE (k3, v2)");
      createPartition(stmt, "ltest", YSQLPartitionType.LIST, 2);

      createPartitionedTable(stmt, "rtest", YSQLPartitionType.RANGE);
      createPartition(stmt, "rtest", YSQLPartitionType.RANGE, 1);
      stmt.execute("CREATE INDEX ON rtest_1  (k1 ASC, k2 DESC) INCLUDE (v1, v2)");
      createPartition(stmt, "rtest", YSQLPartitionType.RANGE, 2);

      // Perform backup and restore.
      partitionedTestBackupAndRestoreHelper();
    }

    try (Connection conn = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = conn.createStatement()) {
      // Verify that indexes and their ordering are used for child partitions with
      // a secondary index.
      isOrderedIndexOnlyScan(stmt,
          "SELECT k1, k2, k3 FROM htest_1 WHERE k1=1 ORDER BY k2",
          "htest_1_k1_k2_k3_v1_idx");
      isOrderedIndexOnlyScan(stmt,
          "SELECT k1, k2, v1, v2 FROM rtest_1 ORDER BY k1",
          "rtest_1_k1_k2_v1_v2_idx");
      isOrderedIndexOnlyScan(stmt,
          "SELECT k1, k2, v1, v2 FROM rtest_1 WHERE k1=1 ORDER BY k2 DESC",
          "rtest_1_k1_k2_v1_v2_idx");
      isIndexScan(stmt,
          "SELECT k1, k2, k3, v2 FROM ltest_1 WHERE k1=1 AND k2='2'",
          "ltest_1_k1_k2_k3_v2_idx");

      // Verify that indexes are not used for children without secondary index.
      String plan = getQueryPlanString(stmt, "SELECT k1, k2 FROM htest_2 WHERE k1=1 ORDER BY k2");
      assertTrue(plan.contains("Sort"));

      plan = getQueryPlanString(stmt, "SELECT k1 FROM rtest_2 ORDER BY k1");
      assertTrue(plan.contains("Sort"));

      plan = getQueryPlanString(stmt, "SELECT k1, k2 FROM rtest_2 WHERE k1=1 ORDER BY k2 DESC");
      assertTrue(plan.contains("Sort"));
    }
    partitionedTestCleanupHelper();
  }

  @Test
  public void testColocatedPartitionedTable() throws Exception {
    // This test is to test if we correctly preserve tablegroup oid before the
    // creation of the first colocated partitioned table, not before table partitions.
    // Preserving implicit tablegroup oid before the creation of the first table relation
    // is needed to ensure the success of backup && restore of a colocated database.
    String dbName = "colocated_db";
    String backupDir = null;
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATION=true", dbName));
    }
    try (Connection conn = getConnectionBuilder().withDatabase(dbName).connect();
         Statement stmt = conn.createStatement()) {
      // Create a partitioned table and its table partitions.
      createPartitionedTable(stmt, "htest", YSQLPartitionType.HASH);
      createPartition(stmt, "htest", YSQLPartitionType.HASH, 1);
      stmt.execute("CREATE INDEX ON htest_1(k1, k2) INCLUDE (k3, v1)");
      createPartition(stmt, "htest", YSQLPartitionType.HASH, 2);

      stmt.execute("INSERT INTO htest VALUES (1, 2, 3, 4, 5)");

      backupDir = YBBackupUtil.getTempBackupDir();
      String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
          "--keyspace", String.format("ysql.%s", dbName));
      if (!TestUtils.useYbController()) {
        backupDir = new JSONObject(output).getString("snapshot_url");
      }
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql.yb2");

    try (Connection conn = getConnectionBuilder().withDatabase("yb2").connect();
         Statement stmt = conn.createStatement()) {
      partitionedTestVerifyInsertedDataHelper(stmt, "htest");
    }

    partitionedTestCleanupHelper();
  }
}
