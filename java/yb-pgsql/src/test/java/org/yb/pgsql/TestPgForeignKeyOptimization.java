package org.yb.pgsql;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;
import org.yb.minicluster.Metrics;

import java.sql.Statement;

import static org.yb.AssertionWrappers.assertEquals;

@RunWith(YBTestRunnerNonTsanOnly.class)
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
    testHelper(
        "CREATE PROCEDURE test() AS $$\n" +
        "BEGIN\n" +
        "  INSERT INTO tbl VALUES(1);\n" +
        "  INSERT INTO fk_tbl VALUES(1, 1);\n" +
        "END; $$ LANGUAGE 'plpgsql';",
      expectedReadCount, null);
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
    // 5 reads are expected, they are:
    // - PERFORM makes one read from tbl table
    // - DELETE makes 3 reads:
    //   - first read from 'tbl' to get row's ybctid as it is not single row update (due to FK)
    //   - second read from 'tbl' but with ROW_MARK_KEYSHARE
    //   - third read from 'fk_tbl' with ROW_MARK_KEYSHARE
    // - INSERT into 'fl_tbl' read from 'tbl' for FK check as FK cache was invalidated by DELETE
    int expectedReadCount = 5;
    testHelper(
        "INSERT INTO tbl VALUES(1);\n" +
        "CREATE PROCEDURE test() AS $$\n" +
        "BEGIN\n" +
        "  PERFORM k FROM tbl WHERE k = 1 FOR KEY SHARE;\n" +
        "  DELETE FROM tbl WHERE k = 1;\n" +
        "  INSERT INTO fk_tbl VALUES(1, 1);\n" +
        "END; $$ LANGUAGE 'plpgsql';", expectedReadCount, "violates foreign key constraint");
  }

  private void testHelper(String testPreparationQuery,
                          int expectedReadCount,
                          String expectedErrorSustring) throws Exception {
    try (Statement stmt = connection.createStatement()) {
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
    }
  }

  private void testSelect(String rowMarker, int expectedReadCount) throws Exception {
    testHelper(String.format(
        "INSERT INTO tbl VALUES(1), (2);\n" +
        "CREATE PROCEDURE test() AS $$\n" +
        "BEGIN\n" +
        "  PERFORM k FROM tbl WHERE k = 1 %s;\n" +
        "  INSERT INTO fk_tbl VALUES(1, 1);\n" +
        "END; $$ LANGUAGE 'plpgsql';", rowMarker),
      expectedReadCount, null);
  }

  private void testSelectWithOptimization(String rowMarker) throws Exception {
    testSelect(rowMarker, 1);
  }

  private void testSelectNoOptimization(String rowMarker) throws Exception {
    testSelect(rowMarker, 2);
  }

  private long getReadOpsCount() throws Exception {
    JsonArray[] metrics = getRawTSMetric();
    assertEquals(1, metrics.length);
    for (JsonElement el : metrics[0]) {
      JsonObject obj = el.getAsJsonObject();
      String metricType = obj.get("type").getAsString();
      if (metricType.equals("server") && obj.get("id").getAsString().equals("yb.tabletserver")) {
        return new Metrics(obj).getHistogram(
            "handler_latency_yb_tserver_TabletServerService_Read").totalCount;
      }
    }
    throw new Exception("handler_latency_yb_tserver_TabletServerService_Read metrict not found");
  }
}
