package org.yb.pgsql;

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_BITMAP_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_BITMAP_OR;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_YB_BITMAP_TABLE_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.testExplain;

import java.sql.Statement;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.util.json.ObjectChecker;



@RunWith(value=YBTestRunner.class)
public class TestPgBaseScansCostModel extends BasePgSQLTest {
  final double DISABLE_COST = Math.pow(10, 9);
  final double BITMAP_SCAN_EXCEEDED_MEMORY_COST = 5 * Math.pow(10, 8);

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  @Test
  public void testYBCostModelUsesPgSelectivity() throws Exception {
    /* #21368: Use PG selectivity estimation when new YB cost model is being used. */
    /* When the YB base scans cost model is enabled using `yb_enable_base_scans_cost_model` flag,
     * then the `yb_enable_optimizer_statistics` flag is ignored and PG selectivity estimation
     * methods are used.
     */
    try (Statement stmt = connection.createStatement()) {
        stmt.execute("CREATE TABLE test_21368 (k1 INT, v1 INT, PRIMARY KEY (k1 ASC))");
        stmt.execute("INSERT INTO test_21368 (SELECT s, (45678 - s) FROM "
            + "generate_series(0, 45678) s)");


        /* Before this change, the selectivity estimate for the following test query would be
         * different when yb_enable_optimizer_statisitcs is disabled vs. enabled. The same test
         * is repeated after running ANALYZE on the table.
         */
        stmt.execute("SET yb_enable_base_scans_cost_model = ON");
        stmt.execute("SET yb_enable_optimizer_statistics = OFF");
        testExplain(
          stmt, "SELECT * FROM test_21368 WHERE k1 > 12345",
          makeTopLevelBuilder()
          .plan(makePlanBuilder()
            .planRows(Checkers.equal(333))
            .build())
          .build());

        stmt.execute("SET yb_enable_base_scans_cost_model = ON");
        stmt.execute("SET yb_enable_optimizer_statistics = ON");
        testExplain(
          stmt, "SELECT * FROM test_21368 WHERE k1 > 12345",
          makeTopLevelBuilder()
          .plan(makePlanBuilder()
            .planRows(Checkers.equal(333))
            .build())
          .build());

        stmt.execute("ANALYZE test_21368");

        stmt.execute("SET yb_enable_base_scans_cost_model = ON");
        stmt.execute("SET yb_enable_optimizer_statistics = OFF");
        testExplain(
          stmt, "SELECT * FROM test_21368 WHERE k1 > 12345",
          makeTopLevelBuilder()
          .plan(makePlanBuilder()
            .planRows(Checkers.greater(32000))
            .planRows(Checkers.less(35000))
            .build())
          .build());

        stmt.execute("SET yb_enable_base_scans_cost_model = ON");
        stmt.execute("SET yb_enable_optimizer_statistics = ON");
        testExplain(
          stmt, "SELECT * FROM test_21368 WHERE k1 > 12345",
          makeTopLevelBuilder()
          .plan(makePlanBuilder()
            .planRows(Checkers.greater(32000))
            .planRows(Checkers.less(35000))
            .build())
          .build());

        stmt.execute("DROP TABLE test_21368");
    }
  }

  @Test
  public void testBitmapScansExceedingWorkMem() throws Exception {
    final String TABLE_NAME = "test";
    final String INDEX_A_NAME = "test_a_idx";
    final String INDEX_B_NAME = "test_b_idx";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET yb_enable_base_scans_cost_model = ON");
      stmt.execute("SET yb_enable_optimizer_statistics = ON");
      stmt.execute("SET enable_bitmapscan = TRUE");
      stmt.execute(String.format("CREATE TABLE %s (pk INT, a INT, b INT, " +
                                 "c INT, PRIMARY KEY (pk ASC))", TABLE_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT i, i * 2, i / 2, NULLIF(i %% 10, 0) FROM" +
                                 " generate_series(1, 10000) i", TABLE_NAME));
      stmt.execute(String.format("CREATE INDEX %s ON %s(a ASC)", INDEX_A_NAME, TABLE_NAME));
      stmt.execute(String.format("CREATE INDEX %s ON %s(b ASC)", INDEX_B_NAME, TABLE_NAME));
      stmt.execute(String.format("ANALYZE %s", TABLE_NAME));

      final ObjectChecker bitmap_index_a = makePlanBuilder()
        .nodeType(NODE_BITMAP_INDEX_SCAN)
        .indexName("test_a_idx")
        .totalCost(Checkers.less(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
        .build();
      final ObjectChecker bitmap_index_a_exceeded_work_mem = makePlanBuilder()
        .nodeType(NODE_BITMAP_INDEX_SCAN)
        .indexName("test_a_idx")
        .totalCost(Checkers.greater(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
        .build();

      final ObjectChecker bitmap_index_b = makePlanBuilder()
        .nodeType(NODE_BITMAP_INDEX_SCAN)
        .indexName("test_b_idx")
        .totalCost(Checkers.less(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
        .build();

      final ObjectChecker bitmap_index_b_exceeded_work_mem = makePlanBuilder()
        .nodeType(NODE_BITMAP_INDEX_SCAN)
        .indexName("test_b_idx")
        .totalCost(Checkers.greater(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
        .build();

      final String query_prefix = "/*+ BitmapScan(t) */ SELECT * FROM %s AS t WHERE %s";

      testExplain(
          stmt, String.format(query_prefix, TABLE_NAME, "a < 3000"),
          makeTopLevelBuilder()
          .plan(makePlanBuilder()
            .nodeType(NODE_YB_BITMAP_TABLE_SCAN)
            .totalCost(Checkers.less(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
            .plans(bitmap_index_a)
            .build())
          .build());

      testExplain(
          stmt, String.format(query_prefix, TABLE_NAME, "a < 1000 OR b < 3000"),
          makeTopLevelBuilder()
          .plan(makePlanBuilder()
            .nodeType(NODE_YB_BITMAP_TABLE_SCAN)
            .totalCost(Checkers.less(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
            .plans(makePlanBuilder().nodeType(NODE_BITMAP_OR)
              .plans(bitmap_index_a, bitmap_index_b)
              .totalCost(Checkers.less(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
              .build())
            .build())
          .build());

      testExplain(
          stmt, String.format(query_prefix, TABLE_NAME, "a < 1000 OR (b < 3000 AND c IS NULL)"),
          makeTopLevelBuilder()
          .plan(makePlanBuilder()
            .nodeType(NODE_YB_BITMAP_TABLE_SCAN)
            .totalCost(Checkers.less(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
            .plans(makePlanBuilder().nodeType(NODE_BITMAP_OR)
              .plans(bitmap_index_a, bitmap_index_b)
              .totalCost(Checkers.less(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
              .build())
            .build())
          .build());

      /*
       * Validate that when a node is expected to exceed work_mem, that node
       * has a large cost added to it.
       */
      stmt.execute("SET work_mem TO '64kB'");
      testExplain(
          stmt, String.format(query_prefix, TABLE_NAME, "a < 3000"),
          makeTopLevelBuilder()
          .plan(makePlanBuilder()
            .nodeType(NODE_YB_BITMAP_TABLE_SCAN)
            .totalCost(Checkers.greater(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
            .plans(bitmap_index_a_exceeded_work_mem)
            .build())
          .build());

      testExplain(
          stmt, String.format(query_prefix, TABLE_NAME, "a < 1000 OR b < 3000"),
          makeTopLevelBuilder()
          .plan(makePlanBuilder()
            .nodeType(NODE_YB_BITMAP_TABLE_SCAN)
            .totalCost(Checkers.greater(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
            .plans(makePlanBuilder().nodeType(NODE_BITMAP_OR)
              .plans(bitmap_index_a, bitmap_index_b_exceeded_work_mem)
              .totalCost(Checkers.greater(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
              .build())
            .build())
          .build());

      /*
       * A normal index scan for (b < 3000 AND c IS NULL) would use b < 3000 as
       * the index condition, and (c IS NULL) as the Storage Filter. Since
       * bitmap index scans can only use Storage Index Filters, we need to
       * make sure that the row estimate is correct.
      */
      testExplain(
          stmt, String.format(query_prefix, TABLE_NAME, "a < 1000 OR (b < 3000 AND c IS NULL)"),
          makeTopLevelBuilder()
          .plan(makePlanBuilder()
            .nodeType(NODE_YB_BITMAP_TABLE_SCAN)
            .totalCost(Checkers.greater(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
            .plans(makePlanBuilder().nodeType(NODE_BITMAP_OR)
              .plans(bitmap_index_a, bitmap_index_b_exceeded_work_mem)
              .totalCost(Checkers.greater(BITMAP_SCAN_EXCEEDED_MEMORY_COST))
              .build())
            .build())
          .build());
    }
  }
}
