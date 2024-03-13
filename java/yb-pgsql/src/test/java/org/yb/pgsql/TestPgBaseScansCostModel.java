package org.yb.pgsql;

import static org.yb.pgsql.ExplainAnalyzeUtils.testExplain;

import java.sql.Statement;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;

@RunWith(value=YBTestRunner.class)
public class TestPgBaseScansCostModel extends BasePgSQLTest {

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
}
