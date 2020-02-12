package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.function.Consumer;

import static org.yb.AssertionWrappers.assertEquals;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestOperationBatching extends BasePgSQLTest {
  protected static final Logger LOG = LoggerFactory.getLogger(TestOperationBatching.class);

  // Start server in RF=1 to avoid leader re-election and other actions which can affect metrics.
  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  private static final String TABLE_NAME = "test";

  private int getWriteSelfMetric() throws Exception {
    return getTableCounterMetric(TABLE_NAME, "rocksdb_write_self");
  }

  private void createTable(Statement statement) throws SQLException {
    // Use ASC primary key to make single table.
    // This is necessary to use 'rocksdb_write_self' metrics as RPC counter
    statement.execute(
        String.format("CREATE TABLE %s(k int, v int, PRIMARY KEY(k ASC))", TABLE_NAME));
  }

  private void insertValues(Statement statement, String values) throws SQLException {
    statement.execute(String.format("INSERT INTO %s VALUES %s", TABLE_NAME, values));
  }

  private void assertTableEmpty(Statement statement) throws SQLException {
    assertNoRows(statement, String.format("SELECT * FROM %s", TABLE_NAME));
  }

  @Test
  public void testBuffering() throws Exception {
    // Due to operation buffering all statements in test should provide same amount of RPCs.
    // Number of RPC calls can be estimated by 'rocksdb_write_self' metric
    // as table has a single tablet.
    try (Statement stmt = connection.createStatement()) {
      createTable(stmt);
      ArrayList<Integer> writeSelf = new ArrayList<>();
      Consumer<Integer> writeSelfChecker = (Integer value) -> {
        // Compare current metric difference with first one
        assertEquals(writeSelf.get(1) - writeSelf.get(0),
            value - writeSelf.get(writeSelf.size() - 1));
        writeSelf.add(value);
      };
      writeSelf.add(getWriteSelfMetric());
      insertValues(stmt,"(1, 1), (2, 2)");
      writeSelf.add(getWriteSelfMetric());
      insertValues(stmt, "(3, 3), (4, 4), (5, 5), (6, 6), (7, 7)");
      // write_self should not depends on number of inserted elements
      writeSelfChecker.accept(getWriteSelfMetric());
      stmt.execute(String.format(
          "DO $$ BEGIN INSERT INTO %s VALUES(8, 8); INSERT INTO %s VALUES(9, 9); END$$",
          TABLE_NAME,
          TABLE_NAME));
      writeSelfChecker.accept(getWriteSelfMetric());
    }
  }

  @Test
  public void testPrimaryKeyConstraint() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      createTable(stmt);
      runInvalidQuery(
          stmt,
          String.format("INSERT INTO %s VALUES (1, 1), (2, 2), (2, 3)", TABLE_NAME),
          "duplicate key value violates unique constraint \"test_pkey\"");
      assertTableEmpty(stmt);
    }
  }

  @Test
  public void testUniqueIndexConstraint() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      createTable(stmt);
      stmt.execute(String.format("CREATE UNIQUE INDEX ON %s(v)", TABLE_NAME));
      runInvalidQuery(
          stmt,
          String.format("INSERT INTO %s VALUES (1, 1), (2, 2), (3, 2)", TABLE_NAME),
          "duplicate key value violates unique constraint \"test_v_idx\"");
      assertTableEmpty(stmt);
    }
  }
}
