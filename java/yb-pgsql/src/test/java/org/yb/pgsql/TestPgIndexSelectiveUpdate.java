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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;

import java.sql.Statement;
import java.util.Map;
import java.util.HashMap;

import static org.yb.AssertionWrappers.assertEquals;

@RunWith(value = YBTestRunner.class)
public class TestPgIndexSelectiveUpdate extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgIndexSelectiveUpdate.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("export_intentdb_metrics", "true");
    return flagMap;
  }

  // Start server in RF=1 mode to make test non-flaky
  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  private static final String TABLE_NAME = "test";
  private static final String METRIC_NAME = "intentsdb_rocksdb_write_self";
  private static final String[] index_list = {"idx_col3", "idx_col5", "idx_col6",
      "idx_col4_idx_col5_idx_col6", "idx_col8"};
  private static final long WAIT_TIMEOUT_MS = 60000;
  private static final int POLL_INTERVAL_MS = 250;

  // Expected value of METRIC_NAME for each index, maintained by initWrites and checkWrites.
  private Map<String, Long> expectedWrites;

  private void prepareTest(Statement statement) throws Exception {
    statement.execute(String.format(
        "CREATE TABLE %s(pk int, col2 int, col3 int, col4 int, col5 int, col6 int, col7 int, "+
        "col8 int, col9 int, PRIMARY KEY (pk ASC), CHECK (col4 != 0))", TABLE_NAME));
    statement.execute(String.format(
        "CREATE INDEX NONCONCURRENTLY %s on %s(col3 ASC) INCLUDE (col4,col5,col6)",
            index_list[0], TABLE_NAME));
    statement.execute(String.format(
        "CREATE INDEX NONCONCURRENTLY %s on %s(col5 ASC) INCLUDE (col6,col7)",
            index_list[1], TABLE_NAME));
    statement.execute(String.format(
        "CREATE INDEX NONCONCURRENTLY %s on %s(col6 ASC) INCLUDE (col9)",
            index_list[2], TABLE_NAME));
    statement.execute(String.format(
        "CREATE INDEX NONCONCURRENTLY %s on %s(col4 ASC, col5 ASC, col6 ASC)",
            index_list[3], TABLE_NAME));
    statement.execute(String.format(
        "CREATE INDEX NONCONCURRENTLY %s on %s(col8 ASC)", index_list[4], TABLE_NAME));

    // Inserting values into the table
    statement.execute(String.format("INSERT INTO %s VALUES (1,1,1,1,1,1,1,1,1)", TABLE_NAME));
    statement.execute(String.format("INSERT INTO %s VALUES (2,2,2,2,2,2,2,2,2)", TABLE_NAME));
    statement.execute(String.format("INSERT INTO %s VALUES (3,3,3,3,3,3,3,3,3)", TABLE_NAME));
    statement.execute(String.format("INSERT INTO %s VALUES (4,4,4,4,4,4,4,4,4)", TABLE_NAME));
    statement.execute(String.format("INSERT INTO %s VALUES (5,5,5,5,5,5,5,5,5)", TABLE_NAME));
    statement.execute(String.format("INSERT INTO %s VALUES (6,6,6,6,6,6,6,6,6)", TABLE_NAME));
  }

  private Map<String, Long> readWrites() throws Exception {
    Map<String, Long> result = new HashMap<>();
    for (String table : index_list) {
      result.put(table, getTserverMetricCountForTable(METRIC_NAME, table));
    }
    return result;
  }

  // Records the current metric values as the baseline for checkWrites. The inserts done by
  // prepareTest are still being applied, so wait until the values stop changing.
  private void initWrites() throws Exception {
    final Map<String, Long> baseline = new HashMap<>();
    TestUtils.waitFor(() -> {
      Map<String, Long> current = readWrites();
      boolean stable = current.equals(baseline);
      baseline.putAll(current);
      return stable;
    }, WAIT_TIMEOUT_MS, POLL_INTERVAL_MS);
    expectedWrites = baseline;
  }

  // Adds the expected per index deltas to the recorded values and waits until the metrics reach
  // them. Each index update produces two intentsdb writes: the intents themselves and their removal
  // when the transaction is applied. The latter happens asynchronously, after the commit is
  // acknowledged to the client, so the metrics may lag behind the statement.
  private void checkWrites(int col3, int col5, int col6, int col4col5col6, int col8)
      throws Exception {
    int[] deltas = {col3, col5, col6, col4col5col6, col8};
    for (int i = 0; i < index_list.length; ++i) {
      expectedWrites.merge(index_list[i], (long) deltas[i], Long::sum);
    }
    final Map<String, Long> actual = new HashMap<>();
    try {
      TestUtils.waitFor(() -> {
        actual.putAll(readWrites());
        return actual.equals(expectedWrites);
      }, WAIT_TIMEOUT_MS, POLL_INTERVAL_MS);
    } catch (Exception e) {
      // Report the mismatching values rather than a bare timeout.
      assertEquals(expectedWrites, actual);
      throw e;
    }
  }

  @Test
  public void testUpdateTableIndexWrites() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      prepareTest(stmt);
      // Add the value of metrics before updating the table test
      initWrites();

      // column 4 is changed. this changes idx_col3, idx_col4_idx_col5_idx_col6.
      stmt.execute(String.format("update %s set col4=11 where pk=1", TABLE_NAME));
      checkWrites(2, 0, 0, 2, 0);

      // column 6 is changed. this changes idx_col3, idx_col5, idx_col6, idx_col4_idx_col5_idx_col6.
      stmt.execute(String.format("update %s set col6=12 where pk=1", TABLE_NAME));
      checkWrites(2, 2, 2, 2, 0);

      // column 5 is changed. this changes idx_col3, idx_col5, idx_col4_idx_col5_idx_col6.
      stmt.execute(String.format("update %s set col5=13 where pk=1", TABLE_NAME));
      checkWrites(2, 2, 0, 2, 0);

      // column 9 is changed. this changes idx_col6.
      stmt.execute(String.format("update %s set col9=14 where pk=1", TABLE_NAME));
      checkWrites(0, 0, 2, 0, 0);

      // column 2 is changed. this does not affect any index.
      stmt.execute(String.format("update %s set col2=15 where pk=1", TABLE_NAME));
      checkWrites(0, 0, 0, 0, 0);

      // column 9 is changed for multiple rows.
      stmt.execute(String.format("update %s set col9=21 where pk>1", TABLE_NAME));
      checkWrites(0, 0, 2, 0, 0);

      // column 8 is changed. No include columns hence just the table and index are updated.
      stmt.execute(String.format("update %s set col8=35 where pk=1", TABLE_NAME));
      checkWrites(0, 0, 0, 0, 2);
    }
  }

  /*
   * Test index updates with pushdown disabled and non-constant SET clause expressions.
   * Not pushable expressions should not prevent index analysis.
   *
   * @throws Exception
   */
  @Test
  public void testUpdateTableIndexWritesNoPushdown() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      prepareTest(stmt);

      // Disable expression pushdown
      stmt.execute("SET yb_enable_expression_pushdown to false");

      // Add the value of metrics before updating the table test
      initWrites();

      // column 4 is changed. this changes idx_col3, idx_col4_idx_col5_idx_col6.
      stmt.execute(String.format("update %s set col4=col4+1 where pk=1", TABLE_NAME));
      checkWrites(2, 0, 0, 2, 0);

      // column 6 is changed. this changes idx_col3, idx_col5, idx_col6, idx_col4_idx_col5_idx_col6.
      stmt.execute(String.format("update %s set col6=col6+1 where pk=1", TABLE_NAME));
      checkWrites(2, 2, 2, 2, 0);

      // column 5 is changed. this changes idx_col3, idx_col5, idx_col4_idx_col5_idx_col6.
      stmt.execute(String.format("update %s set col5=col5+1 where pk=1", TABLE_NAME));
      checkWrites(2, 2, 0, 2, 0);

      // column 9 is changed. this changes idx_col6.
      stmt.execute(String.format("update %s set col9=col9+1 where pk=1", TABLE_NAME));
      checkWrites(0, 0, 2, 0, 0);

      // column 2 is changed. this does not affect any index.
      stmt.execute(String.format("update %s set col2=col2+1 where pk=1", TABLE_NAME));
      checkWrites(0, 0, 0, 0, 0);

      // column 9 is changed for multiple rows.
      stmt.execute(String.format("update %s set col9=col9+1 where pk>1", TABLE_NAME));
      checkWrites(0, 0, 2, 0, 0);

      // column 8 is changed. No include columns hence just the table and index are updated.
      stmt.execute(String.format("update %s set col8=col8+1 where pk=1", TABLE_NAME));
      checkWrites(0, 0, 0, 0, 2);
    }
  }

  @Test
  public void testUpdateTableAvoidTransactionSingleRowModify() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE orders ( k int PRIMARY KEY, v1 text, v2 int)");
      stmt.execute("CREATE INDEX v1_idx ON orders(v1)");
      stmt.execute("INSERT INTO orders(k, v1, v2) values (1, 'hello', 2)");
      long oldTxnValue = getMetricCounter(SINGLE_SHARD_TRANSACTIONS_METRIC);
      // Since its a single row update, we avoid creating a distributed transaction.
      // Hence the number of transactions remains unchanged.
      stmt.execute("UPDATE orders SET v2 = 4 where k = 1;");
      long newTxnValue = getMetricCounter(SINGLE_SHARD_TRANSACTIONS_METRIC);
      assertEquals(oldTxnValue+1, newTxnValue);
    }
  }
}
