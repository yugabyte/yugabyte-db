package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_HASH;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_HASH_JOIN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_NESTED_LOOP;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.RELATIONSHIP_OUTER_TABLE;
import static org.yb.pgsql.ExplainAnalyzeUtils.RELATIONSHIP_INNER_TABLE;

import java.sql.Statement;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;

@RunWith(value=YBTestRunner.class)
public class TestPgExplainAnalyzeJoins extends BasePgExplainAnalyzeTest {
  private static final String MAIN_TABLE = "foo";
  private static final String SIDE_TABLE = "bar";
  private static final String MAIN_RANGE_MC_INDEX = "foo_v1_v2"; // Multi-column Range Index
  private static final String MAIN_HASH_INDEX = "foo_v3";
  private static final String MAIN_RANGE_INDEX = "foo_v4";
  private static final String SIDE_HASH_INDEX = "bar_v1";
  private static final String SIDE_RANGE_INDEX = "bar_v2";
  private static final int NUM_PAGES = 10;
  private TopLevelCheckerBuilder JOINS_TOP_LEVEL_CHECKER = makeTopLevelBuilder()
      .storageWriteRequests(Checkers.equal(0))
      .storageFlushRequests(Checkers.equal(0));

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE TABLE %s (k INT PRIMARY KEY, v1 INT, "
          + "v2 INT, v3 INT, v4 INT, v5 INT) SPLIT INTO 2 TABLETS", MAIN_TABLE));

      // Multi-column Range Index on (v1, v2)
      stmt.execute(String.format("CREATE INDEX %s ON %s (v1, v2 ASC)",
        MAIN_RANGE_MC_INDEX, MAIN_TABLE));

      // Hash Index on v3
      stmt.execute(String.format("CREATE INDEX %s ON %s (v3 HASH)",
        MAIN_HASH_INDEX, MAIN_TABLE));

      // Range Index on v4
      stmt.execute(String.format("CREATE INDEX %s ON %s (v4 ASC)",
        MAIN_RANGE_INDEX, MAIN_TABLE));

      // Populate the table
      stmt.execute(String.format("INSERT INTO %s (SELECT i, i, i %% 1024, i, i, "
        + "i %% 128 FROM generate_series(0, %d) i)", MAIN_TABLE, (NUM_PAGES * 1024 - 1)));

      // Create table 'bar'
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT, v1 INT, "
          +"v2 INT, v3 INT, PRIMARY KEY (k1 ASC, k2 DESC))", SIDE_TABLE));

      // Hash Index on v1
      stmt.execute(String.format("CREATE INDEX %s ON %s (v1 HASH)",
        SIDE_HASH_INDEX, SIDE_TABLE));

      // Range Index on v2.
      stmt.execute(String.format("CREATE INDEX %s ON %s (v2 ASC)",
        SIDE_RANGE_INDEX, SIDE_TABLE));

      // Populate the table
      stmt.execute(String.format("INSERT INTO %s (SELECT i, i %% 128, i, i %% 1024, i "
        + "FROM generate_series(0, %d) i)", SIDE_TABLE, (NUM_PAGES * 1024 - 1)));
    }
  }

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  @Test
  public void testHashJoin() throws Exception {
    String simpleHashJoin = "/*+ HashJoin(%1$s %2$s) Leading((%1$s %2$s)) */ SELECT * FROM %1$s "
    + "JOIN %2$s ON %1$s.%3$s = %2$s.%4$s %5$s";

    PlanCheckerBuilder hashJoinNodeChecker = makePlanBuilder()
        .nodeType(NODE_HASH_JOIN);

    // 1. Simple Hash Join
    PlanCheckerBuilder outerTableScanChecker = makePlanBuilder()
        .nodeType(NODE_SEQ_SCAN)
        .relationName(MAIN_TABLE)
        .alias(MAIN_TABLE)
        .parentRelationship(RELATIONSHIP_OUTER_TABLE)
        .storageTableReadExecutionTime(Checkers.greater(0.0));

    PlanCheckerBuilder innerTableHashChecker = makePlanBuilder()
        .nodeType(NODE_HASH)
        .parentRelationship(RELATIONSHIP_INNER_TABLE);

    PlanCheckerBuilder innerTableScanChecker = makePlanBuilder()
        .nodeType(NODE_SEQ_SCAN)
        .relationName(SIDE_TABLE)
        .alias(SIDE_TABLE)
        .storageTableReadExecutionTime(Checkers.greater(0.0));

    {
      Checker checker = JOINS_TOP_LEVEL_CHECKER
          .plan(hashJoinNodeChecker
              .plans(
                  outerTableScanChecker
                  .storageTableReadRequests(Checkers.equal(NUM_PAGES + 1))
                  .build(),
                  innerTableHashChecker
                  .plans(innerTableScanChecker
                      .storageTableReadRequests(Checkers.equal(NUM_PAGES))
                      .build())
                  .build())
              .build())
          .build();

      testExplain(String.format(simpleHashJoin, MAIN_TABLE, SIDE_TABLE, "k", "v1", ""), checker);
    }

    // 2. Hash Join with Index Scan
    PlanCheckerBuilder innerTableIndexScanChecker = makePlanBuilder()
        .nodeType(NODE_INDEX_SCAN)
        .relationName(SIDE_TABLE)
        .alias(SIDE_TABLE)
        .storageTableReadRequests(Checkers.equal(1))
        .storageTableReadExecutionTime(Checkers.greater(0.0));

    {
      Checker checker = JOINS_TOP_LEVEL_CHECKER
        .plan(hashJoinNodeChecker
            .plans(
                outerTableScanChecker
                .storageTableReadRequests(Checkers.equal(1))
                .build(),
                innerTableHashChecker
                .plans(innerTableIndexScanChecker.build())
                .build())
            .build())
        .build();

      testExplain(String.format(simpleHashJoin, MAIN_TABLE, SIDE_TABLE, "k", "v1",
        "AND foo.k < 10 AND bar.k1 < 10"), checker);
    }
  }

  @Test
  public void testNestedLoopJoin() throws Exception {
    String simpleNLQuery = "/*+ Set(yb_bnl_batch_size 1) NestLoop(%1$s %2$s) "
      + " Leading((%1$s %2$s)) */ SELECT * FROM %1$s "
      + " JOIN %2$s ON %1$s.%3$s = %2$s.%4$s %5$s";

    // 1. Simple Nested Loop
    PlanCheckerBuilder nestedLoopNodeChecker = makePlanBuilder()
        .nodeType(NODE_NESTED_LOOP);

    PlanCheckerBuilder outerTableSeqScanChecker = makePlanBuilder()
        .nodeType(NODE_SEQ_SCAN)
        .relationName(MAIN_TABLE)
        .alias(MAIN_TABLE)
        .parentRelationship(RELATIONSHIP_OUTER_TABLE)
        .storageTableReadExecutionTime(Checkers.greater(0.0));

    PlanCheckerBuilder innerTableIndexScanChecker = makePlanBuilder()
        .nodeType(NODE_INDEX_SCAN)
        .relationName(SIDE_TABLE)
        .alias(SIDE_TABLE)
        .parentRelationship(RELATIONSHIP_INNER_TABLE)
        .storageTableReadRequests(Checkers.equal(1))
        .storageTableReadExecutionTime(Checkers.greater(0.0))
        .storageIndexReadRequests(Checkers.equal(1))
        .storageIndexReadExecutionTime(Checkers.greater(0.0));

    Checker checker = JOINS_TOP_LEVEL_CHECKER
        .plan(nestedLoopNodeChecker
            .plans(
                outerTableSeqScanChecker
                .storageTableReadRequests(Checkers.equal(1))
                .build(),
                innerTableIndexScanChecker
                .actualLoops(Checkers.equal(10))
                .build())
            .build())
        .storageReadRequests(Checkers.equal((2 * 10) + 1))
        .build();

    testExplain(String.format(simpleNLQuery, MAIN_TABLE, SIDE_TABLE, "k", "v1",
      "AND foo.k < 10"), checker);
  }

  // Test to make sure hash joins charge costs to qpquals. Made to address issue
  // #19021.
  @Test
  public void testHashJoinWithQPQual() throws Exception {
    String baselineHashJoinQuery = "/*+ HashJoin(%1$s %2$s) Leading((%1$s %2$s)) */ " +
        "SELECT * FROM %1$s JOIN %2$s ON %1$s.%3$s = %2$s.%4$s " +
        "AND %1$s.%3$s - 1 = %2$s.%5$s %6$s";
    ExplainAnalyzeUtils.Cost baseCost =
        getExplainTotalCost(String.format(baselineHashJoinQuery, MAIN_TABLE, SIDE_TABLE,
            "k", "k1", "k2", "AND foo.k + bar.k1 = bar.k1 + bar.k2"));

    // Making the qpqual logically equivalent but more computationally
    // expensive should increase the cost.
    ExplainAnalyzeUtils.Cost expensiveQualCost =
        getExplainTotalCost(String.format(baselineHashJoinQuery, MAIN_TABLE, SIDE_TABLE,
            "k", "k1", "k2", "AND sqrt(foo.k + bar.k1) = sqrt(bar.k1 + bar.k2)"));
    assertGreaterThan(expensiveQualCost, baseCost);
  }
}
