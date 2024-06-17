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


import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.Statement;
import java.util.Map;

import static org.yb.AssertionWrappers.*;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressTabletSplit extends BasePgRegressTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgRegressTabletSplit.class);

  private static final String TURN_OFF_COPY_FROM_BATCH_TRANSACTION =
      "yb_default_copy_from_rows_per_transaction=0";
  private static final String database = "yugabyte";
  private static final int TEST_TIMEOUT = 120000;

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  /*
   * We use the following parameters to enable tablet splitting aggressively.
   */
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("ysql_pg_conf", TURN_OFF_COPY_FROM_BATCH_TRANSACTION);
    flags.put("yb_num_shards_per_tserver", Integer.toString(1));
    flags.put("db_write_buffer_size", Integer.toString(122880));
    flags.put("db_block_size_bytes", Integer.toString(2048));
    flags.put("db_filter_block_size_bytes", Integer.toString(2048));
    flags.put("db_index_block_size_bytes", Integer.toString(2048));
    flags.put("tserver_heartbeat_metrics_interval_ms", Integer.toString(1000));
    flags.put("heartbeat_interval_ms", Integer.toString(1000));
    return flags;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flags = super.getMasterFlags();
    flags.put("yb_num_shards_per_tserver", Integer.toString(1));
    flags.put("tablet_split_low_phase_size_threshold_bytes", Integer.toString(0));
    flags.put("tablet_split_high_phase_size_threshold_bytes", Integer.toString(0));
    flags.put("max_queued_split_candidates", Integer.toString(10));
    flags.put("tablet_split_low_phase_shard_count_per_node", Integer.toString(0));
    flags.put("tablet_split_high_phase_shard_count_per_node", Integer.toString(0));
    flags.put("tablet_force_split_threshold_bytes", Integer.toString(122880));
    flags.put("process_split_tablet_candidates_interval_msec", Integer.toString(1000));
    flags.put("enable_automatic_tablet_splitting", "true");
    return flags;
  }

  private boolean runSelectTillTabletsSplit(String table, int expectedRowCount) throws Exception {
    long startTime = System.currentTimeMillis();
    while (System.currentTimeMillis() - startTime < TEST_TIMEOUT) {
      LOG.info("num tablets for table: " + getTabletsForTable(database, table).size());
      if (getTabletsForTable(database, table).size() > 1) {
        return true;
      }
      try (Statement statement = connection.createStatement()) {
        runQueryWithRowCount(statement,
                             String.format("SELECT * FROM %s", table),
                             expectedRowCount);
      }
    }
    return false;
  }

  @Test
  public void testPgRegressTabletSplit() throws Exception {
    final String table = "airports";
    // 1. Creates a large table (airports)
    // 2. Copies content from an csv file
    // 3. Executes a bunch of select operations
    runPgRegressTest("yb_large_table_tablet_split_schedule");
    // Tablet splits are background operations. Sometimes, it might take longer to complete
    // splitting the tablets. Hence, we issue selects till tablets are split. We also add a timeout
    // after which we return an assertion error.
    int expectedRowCount = 9999;
    if (runSelectTillTabletsSplit(table, expectedRowCount)) {
      return;
    }
    assertTrue("Timeout reached before number of tablets split", false);
  }

  @Test
  public void testTabletSplitWithAutoCommit() throws Exception {
    final String table = "test";
    int expectedRowCount = 10000;
    // Auto commit commits every statements.
    connection.setAutoCommit(true);
    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (id int primary key, val int) " +
                                      "SPLIT INTO 1 TABLETS;", table));
      for (int i = 0; i < expectedRowCount; i++) {
        statement.execute(String.format("INSERT INTO %s VALUES (%d, %d);", table, i, i));
      }
      runQueryWithRowCount(statement,
                           String.format("SELECT * FROM %s", table),
                           expectedRowCount);
    }
    // This test ensures that tablet splitting works with SQL statements executed with JDBC
    // connections. Auto commits should commit inserts immediately. As we are splitting tablets
    // aggressively this should create a few tablets.
    if (runSelectTillTabletsSplit(table, expectedRowCount)) {
      return;
    }
    assertTrue("Timeout reached before number of tablets split", false);
  }

  @Test
  public void testTabletSplitWithAbort() throws Exception {
    final String table = "test";
    try (Statement statement = connection.createStatement()) {
      statement.execute("BEGIN");
      statement.execute(String.format("CREATE TABLE %s (id int primary key, val int) " +
                                      "SPLIT INTO 1 TABLETS;", table));
      int numRowsInserted = 10000;
      int expectedRowCount = 0;
      for (int i = 0; i < numRowsInserted; i++) {
        statement.execute(String.format("INSERT INTO %s VALUES (%d, %d);", table, i, i));
      }
      statement.execute("ROLLBACK");
      runQueryWithRowCount(statement,
                           String.format("SELECT * FROM %s", table),
                           expectedRowCount);
    }
    // Yugabyte does not abort DDLs within transactions. Hence the table will be created and at
    // least one tablet will be created. However since the subsequent inserts are aborted, tablets
    // will not be split further.
    assert getTabletsForTable(database, table).size() == 1;
  }
}
