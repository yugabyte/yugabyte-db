package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

import java.sql.Connection;
import java.sql.Statement;

import static org.yb.AssertionWrappers.assertEquals;

@RunWith(YBTestRunner.class)
public class TestPgForeignKeyOptimization extends BasePgSQLTest {
  // Start server in RF=1 mode to simplify metrics analysis.
  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Test
  public void testInsert() throws Exception {
    // No reads are expected as referenced item is inserted in context of same transaction.
    int expectedReadCount = 0;
    String queryStr = "CREATE PROCEDURE test() AS $$\n" +
      "BEGIN\n" +
      "  INSERT INTO tbl VALUES(1);\n" +
      "  INSERT INTO fk_tbl VALUES(1, 1);\n" +
      "END; $$ LANGUAGE 'plpgsql';";
    testHelper(queryStr, expectedReadCount, null, IsolationLevel.SERIALIZABLE);
    testHelper(queryStr, expectedReadCount, null, IsolationLevel.REPEATABLE_READ);
  }

  @Test
  public void testSelectForKeyShare() throws Exception {
    testSelectWithOptimization("FOR KEY SHARE");
  }

  @Test
  public void testSelectForNoKeyUpdate() throws Exception {
    testSelectWithOptimization("FOR NO KEY UPDATE");
  }

  @Test
  public void testSelectForUpdate() throws Exception {
    testSelectWithOptimization("FOR UPDATE");
  }

  @Test
  public void testSelectNoRowMarker() throws Exception {
    testSelectNoOptimization("");
  }

  @Test
  public void testDelete() throws Exception {
    // 5 or 6 reads are expected, they are:
    // - PERFORM makes one read from tbl table for SERIALIZABLE level. Two in case of REPEATABLE
    //   READ even though we specify pk because a separate RPC is used to lock tuples.
    //   TODO(foucher): Optimize to ensure we use only 1 rpc for locking if pk is specified in
    //   REPEATABLE READ level.
    // - DELETE makes 3 reads:
    //   - first read from 'tbl' to get row's ybctid as it is not single row update (due to FK)
    //   - second read from 'tbl' but with ROW_MARK_KEYSHARE
    //   - third read from 'fk_tbl' with ROW_MARK_KEYSHARE
    // - INSERT into 'fl_tbl' read from 'tbl' for FK check as FK cache was invalidated by DELETE
    String queryStr = "INSERT INTO tbl VALUES(1);\n" +
      "CREATE PROCEDURE test() AS $$\n" +
      "BEGIN\n" +
      "  PERFORM k FROM tbl WHERE k = 1 FOR KEY SHARE;\n" +
      "  DELETE FROM tbl WHERE k = 1;\n" +
      "  INSERT INTO fk_tbl VALUES(1, 1);\n" +
      "END; $$ LANGUAGE 'plpgsql';";
    testHelper(queryStr, 5 /* expectedReadCount */, "violates foreign key constraint",
               IsolationLevel.SERIALIZABLE);
    testHelper(queryStr, 6 /* expectedReadCount */, "violates foreign key constraint",
               IsolationLevel.REPEATABLE_READ);
  }

  private void testHelper(String testPreparationQuery,
                          int expectedReadCount,
                          String expectedErrorSustring,
                          IsolationLevel isolationLevel) throws Exception {
    try (Connection conn = getConnectionBuilder().withIsolationLevel(isolationLevel).connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE TABLE tbl(k INT PRIMARY KEY) SPLIT INTO 1 TABLETS");
      stmt.execute(
          "CREATE TABLE fk_tbl(k INT PRIMARY KEY, pk INT REFERENCES tbl(k)) SPLIT INTO 1 TABLETS");

      stmt.execute(testPreparationQuery);

      long startReadCount = getReadOpsCount();
      String queryToRun = "CALL test()";
      if (expectedErrorSustring != null) {
        runInvalidQuery(stmt, queryToRun, expectedErrorSustring);
      } else {
        stmt.execute(queryToRun);
      }
      assertEquals(expectedReadCount, getReadOpsCount() - startReadCount);
      stmt.execute("DROP procedure test");
      stmt.execute("DROP TABLE fk_tbl");
      stmt.execute("DROP TABLE tbl");
    }
  }

  private void testSelect(String rowMarker, int expectedReadCount,
                          IsolationLevel isolationLevel) throws Exception {
    testHelper(String.format(
        "INSERT INTO tbl VALUES(1), (2);\n" +
        "CREATE PROCEDURE test() AS $$\n" +
        "BEGIN\n" +
        "  PERFORM k FROM tbl WHERE k = 1 %s;\n" +
        "  INSERT INTO fk_tbl VALUES(1, 1);\n" +
        "END; $$ LANGUAGE 'plpgsql';", rowMarker),
      expectedReadCount, null, isolationLevel);
  }

  private void testSelectWithOptimization(String rowMarker) throws Exception {
    // For REPEATABLE READ, one read to fetch the tuple and the second read rpc to lock only that
    // specific row. SERIALIZABLE requires just 1 rpc to lock row since it doesn't lock specific
    // rows that might possibly filtered by YSQL (it instead locks the longest prefix of pk
    // specified in read request - since it has to lock the whole predicate and block new rows
    // matching predicate from being added in a parallel txn).
    testSelect(rowMarker, 2, IsolationLevel.REPEATABLE_READ);
    testSelect(rowMarker, 1, IsolationLevel.SERIALIZABLE);
  }

  private void testSelectNoOptimization(String rowMarker) throws Exception {
    testSelect(rowMarker, 2, IsolationLevel.SERIALIZABLE);
    testSelect(rowMarker, 2, IsolationLevel.REPEATABLE_READ);
  }

  private long getReadOpsCount() throws Exception {
    return getReadRPCMetric(getTSMetricSources()).count;
  }
}
