package org.yb.pgsql;

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_AGGREGATE;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_ONLY_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_MERGE_JOIN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_NESTED_LOOP;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_HASH_JOIN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SORT;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_YB_BATCHED_NESTED_LOOP;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_HASH;

import java.sql.Statement;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.yb.YBTestRunner;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;

@RunWith(value=YBTestRunner.class)
public class TestPgExplainAnalyzeColocated extends BasePgExplainAnalyzeTest {
  private static final String COLOCATED_DB = "codb";
  private static final String MAIN_TABLE = "foo";
  private static final String SIDE_TABLE = "bar";
  private static final String MAIN_TABLE_PKEY = "foo_pkey";
  private static final String MAIN_RANGE_MC_INDEX = "foo_v1_v2"; // Multi-column Range Index
  private static final String MAIN_RANGE_INDEX = "foo_v4";
  private static final String SIDE_TABLE_PKEY = "bar_pkey";
  private static final String SIDE_RANGE_INDEX = "bar_v2";
  private static final int NUM_PAGES = 10;

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate(String.format("CREATE DATABASE %s WITH colocation = true", COLOCATED_DB));
    }

    connection = getConnectionBuilder().withDatabase(COLOCATED_DB).connect();
    try (Statement stmt = connection.createStatement()) {

      stmt.execute(String.format("CREATE TABLE %s (k INT PRIMARY KEY, v1 INT, "
          + "v2 INT, v3 INT, v4 INT, v5 INT)", MAIN_TABLE));

      // Multi-column Range Index on (v1, v2)
      stmt.execute(String.format("CREATE INDEX %s ON %s (v1, v2 ASC)",
        MAIN_RANGE_MC_INDEX, MAIN_TABLE));

      // Range Index on v4
      stmt.execute(String.format("CREATE INDEX %s ON %s (v4 ASC)",
        MAIN_RANGE_INDEX, MAIN_TABLE));

      // Populate the table
      stmt.execute(String.format("INSERT INTO %s (SELECT i, i, i %% 1024, i, i, "
        + "i %% 128 FROM generate_series(0, %d) i)", MAIN_TABLE, (NUM_PAGES * 1024 - 1)));

      // Create table 'bar'
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT, v1 INT, "
          +"v2 INT, v3 INT, PRIMARY KEY (k1 ASC, k2 DESC))", SIDE_TABLE));

      // Range Index on v2.
      stmt.execute(String.format("CREATE INDEX %s ON %s (v2 ASC)",
        SIDE_RANGE_INDEX, SIDE_TABLE));

      // Populate the table
      stmt.execute(String.format("INSERT INTO %s (SELECT i, i %% 128, i, i %% 1024, i "
        + "FROM generate_series(0, %d) i)", SIDE_TABLE, (NUM_PAGES * 1024 - 1)));
    }
  }

  @After
  public void cleanup() throws Exception {
    connection.close();
  }

  private TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false /* nullify */);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false /* nullify */);
  }

  @Test
  public void testSeqScan() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      Checker checker = makeTopLevelBuilder()
          .storageReadRequests(Checkers.equal(NUM_PAGES))
          .storageReadExecutionTime(Checkers.greater(0.0))
          .storageRowsScanned(Checkers.equal(NUM_PAGES * 1024))
          .storageWriteRequests(Checkers.equal(0))
          .storageFlushRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greater(0.0))
          .plan(makePlanBuilder()
              .nodeType(NODE_SEQ_SCAN)
              .relationName(MAIN_TABLE)
              .alias(MAIN_TABLE)
              .actualRows(Checkers.equal(NUM_PAGES * 1024))
              .storageTableReadRequests(Checkers.equal(NUM_PAGES))
              .storageTableReadExecutionTime(Checkers.greater(0.0))
              .storageTableRowsScanned(Checkers.equal(NUM_PAGES * 1024))
              .build())
          .build();

      // Seq Scan (ybc_fdw ForeignScan)
      testExplain(String.format("SELECT * FROM %s", MAIN_TABLE), checker);

      // real Seq Scan
      testExplain(String.format("/*+ SeqScan(texpl) */SELECT * FROM %s", MAIN_TABLE),
                  checker);
    }
  }

  @Test
  public void testIndexScan() throws Exception {
    final String simpleIndexQuery = "SELECT * FROM %1$s WHERE %2$s = 128";
    final String indexQueryWithPredicate = "/*+ IndexScan(%1$s %3$s) */ SELECT * FROM %1$s WHERE "
      + "%2$s < 128";

    TopLevelCheckerBuilder indexScanTopLevelChecker = makeTopLevelBuilder()
        .storageReadRequests(Checkers.equal(1))
        .storageReadExecutionTime(Checkers.greater(0.0))
        .storageWriteRequests(Checkers.equal(0))
        .storageFlushRequests(Checkers.equal(0))
        .catalogWriteRequests(Checkers.greaterOrEqual(0))
        .storageExecutionTime(Checkers.greater(0.0));

    PlanCheckerBuilder indexScanPlanPrimaryChecker = makePlanBuilder()
        .nodeType(NODE_INDEX_SCAN)
        .relationName(MAIN_TABLE)
        .alias(MAIN_TABLE)
        .indexName(MAIN_TABLE_PKEY)
        .storageTableReadRequests(Checkers.equal(1))
        .storageTableReadExecutionTime(Checkers.greater(0.0));

    PlanCheckerBuilder indexScanPlanSecondaryChecker = makePlanBuilder()
        .nodeType(NODE_INDEX_SCAN)
        .relationName(MAIN_TABLE)
        .alias(MAIN_TABLE)
        .indexName(MAIN_RANGE_MC_INDEX)
        .storageIndexReadRequests(Checkers.equal(1))
        .storageIndexReadExecutionTime(Checkers.greater(0.0));

    // Scan on Primary Index
    {
      Checker checker = indexScanTopLevelChecker
          .storageRowsScanned(Checkers.equal(1))
          .plan(indexScanPlanPrimaryChecker
              .actualRows(Checkers.equal(1))
              .storageTableRowsScanned(Checkers.equal(1))
              .build())
          .build();

      testExplain(String.format(simpleIndexQuery, MAIN_TABLE, "k"), checker);
    }

    {
      Checker checker = indexScanTopLevelChecker
          .storageRowsScanned(Checkers.equal(128))
          .plan(indexScanPlanPrimaryChecker
              .actualRows(Checkers.equal(128))
              .storageTableRowsScanned(Checkers.equal(128))
              .build())
          .build();
      testExplain(String.format(indexQueryWithPredicate, MAIN_TABLE, "k", MAIN_TABLE_PKEY),
                  checker);
    }

    // Scan on Secondary index
    {
      Checker checker = indexScanTopLevelChecker
          .storageRowsScanned(Checkers.equal(2))
          .plan(indexScanPlanSecondaryChecker
              .actualRows(Checkers.equal(1))
              .storageTableRowsScanned(Checkers.equal(1))
              .storageIndexRowsScanned(Checkers.equal(1))
              .build())
          .build();

      testExplain(String.format(simpleIndexQuery, MAIN_TABLE, "v1"), checker);
    }

    {
      Checker checker = indexScanTopLevelChecker
          .storageRowsScanned(Checkers.equal(256))
          .plan(indexScanPlanSecondaryChecker
              .actualRows(Checkers.equal(128))
              .storageTableRowsScanned(Checkers.equal(128))
              .storageIndexRowsScanned(Checkers.equal(128))
              .build())
          .build();

      testExplain(String.format(indexQueryWithPredicate, MAIN_TABLE, "v1", MAIN_RANGE_MC_INDEX),
                  checker);
    }
  }

  @Test
  public void testIndexOnlyScan() throws Exception {
    final String simpleIndexOnlyQuery = "SELECT %2$s FROM %1$s WHERE %2$s = 128";
    final String indexOnlyQueryWithPredicate = "/*+ IndexOnlyScan(%1$s %3$s) */ "
      + "SELECT %2$s FROM %1$s WHERE %2$s < 128";

    TopLevelCheckerBuilder indexOnlyScanTopLevelChecker = makeTopLevelBuilder()
        .storageReadRequests(Checkers.equal(1))
        .storageReadExecutionTime(Checkers.greater(0.0))
        .storageWriteRequests(Checkers.equal(0))
        .storageFlushRequests(Checkers.equal(0))
        .catalogWriteRequests(Checkers.greaterOrEqual(0))
        .storageExecutionTime(Checkers.greater(0.0));

    PlanCheckerBuilder indexOnlyScanPlanChecker = makePlanBuilder()
        .nodeType(NODE_INDEX_ONLY_SCAN)
        .relationName(MAIN_TABLE)
        .alias(MAIN_TABLE)
        .indexName(MAIN_RANGE_MC_INDEX)
        .storageIndexReadRequests(Checkers.equal(1))
        .storageIndexReadExecutionTime(Checkers.greater(0.0));

    // Index-only Scan on leading column in Secondary Index
    {
      Checker checker = indexOnlyScanTopLevelChecker
          .storageRowsScanned(Checkers.equal(1))
          .plan(indexOnlyScanPlanChecker
              .actualRows(Checkers.equal(1))
              .storageIndexRowsScanned(Checkers.equal(1))
              .build())
          .build();

      testExplain(String.format(simpleIndexOnlyQuery, MAIN_TABLE, "v1"), checker);
    }

    // Index-only Scan on leading column in Secondary Index with predicate
    {
      Checker checker = indexOnlyScanTopLevelChecker
          .storageRowsScanned(Checkers.equal(128))
          .plan(indexOnlyScanPlanChecker
              .actualRows(Checkers.equal(128))
              .storageIndexRowsScanned(Checkers.equal(128))
              .build())
          .build();

      testExplain(String.format(indexOnlyQueryWithPredicate, MAIN_TABLE,
          "v1", MAIN_RANGE_MC_INDEX), checker);
    }

    // Index-only Scan on non-leading column in Secondary Index
    {
      Checker checker = indexOnlyScanTopLevelChecker
          .storageRowsScanned(Checkers.equal(10))
          .plan(indexOnlyScanPlanChecker
              .actualRows(Checkers.equal(10))
              .storageIndexRowsScanned(Checkers.equal(10))
              .build())
          .build();

      testExplain(String.format(simpleIndexOnlyQuery, MAIN_TABLE, "v2"), checker);
    }

    // Index-only Scan on leading column in Secondary Index with predicate
    {
      Checker checker = indexOnlyScanTopLevelChecker
          .storageReadRequests(Checkers.equal(2))
          .storageRowsScanned(Checkers.equal(1280))
          .plan(indexOnlyScanPlanChecker
              .actualRows(Checkers.equal(1280))
              .storageIndexReadRequests(Checkers.equal(2))
              .storageIndexRowsScanned(Checkers.equal(1280))
              .build())
          .build();

      testExplain(String.format(indexOnlyQueryWithPredicate, MAIN_TABLE,
          "v2", MAIN_RANGE_MC_INDEX), checker);
    }
  }

  @Test
  public void testAggregates() throws Exception {
    TopLevelCheckerBuilder topLevelChecker = makeTopLevelBuilder()
        .storageReadRequests(Checkers.equal(1))
        .storageReadExecutionTime(Checkers.greater(0.0))
        .storageWriteRequests(Checkers.equal(0))
        .storageFlushRequests(Checkers.equal(0))
        .catalogWriteRequests(Checkers.greaterOrEqual(0))
        .storageExecutionTime(Checkers.greater(0.0));

    PlanCheckerBuilder aggregateChecker = makePlanBuilder()
        .nodeType(NODE_AGGREGATE);

    PlanCheckerBuilder tablePlanChecker = makePlanBuilder()
        .nodeType(NODE_SEQ_SCAN)
        .relationName(MAIN_TABLE)
        .alias(MAIN_TABLE)
        .storageTableReadRequests(Checkers.equal(1))
        .storageTableReadExecutionTime(Checkers.greater(0.0));

    {
      Checker checker = topLevelChecker
          .storageRowsScanned(Checkers.equal(NUM_PAGES * 1024))
          .plan(aggregateChecker
              .actualRows(Checkers.equal(1))
              .plans(tablePlanChecker
                .actualRows(Checkers.equal(1))
                .storageTableRowsScanned(Checkers.equal(NUM_PAGES * 1024))
                .build())
              .build())
          .build();

      testExplain(String.format("SELECT COUNT(%2$s) FROM %1$s", MAIN_TABLE,
          "v1"), checker);
    }
  }

  @Test
  public void testJoins() throws Exception {
    final String query = "%1$s SELECT * FROM foo JOIN bar ON foo.k = bar.k1 WHERE foo.v1 < 1000";

    TopLevelCheckerBuilder topLevelChecker = makeTopLevelBuilder()
        .storageReadExecutionTime(Checkers.greater(0.0))
        .storageWriteRequests(Checkers.equal(0))
        .storageFlushRequests(Checkers.equal(0))
        .storageExecutionTime(Checkers.greater(0.0));

    // Merge Join
    {
      Checker checker = topLevelChecker
          .storageReadRequests(Checkers.equal(2))
          .storageRowsScanned(Checkers.equal((1000 * 2) + 1024))
          .plan(makePlanBuilder()
              .nodeType(NODE_MERGE_JOIN)
              .plans(
                  makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .relationName(SIDE_TABLE)
                      .indexName(SIDE_TABLE_PKEY)
                      .actualRows(Checkers.equal(1000 + 1))
                      .storageTableReadRequests(Checkers.equal(1))
                      .storageTableReadExecutionTime(Checkers.greater(0.0))
                      .storageTableRowsScanned(Checkers.equal(1024))
                      .build(),
                  makePlanBuilder()
                      .nodeType(NODE_SORT)
                      .plans(makePlanBuilder()
                          .relationName(MAIN_TABLE)
                          .indexName(MAIN_RANGE_MC_INDEX)
                          .actualRows(Checkers.equal(1000))
                          .storageTableRowsScanned(Checkers.equal(1000))
                          .storageIndexReadRequests(Checkers.equal(1))
                          .storageIndexReadExecutionTime(Checkers.greater(0.0))
                          .storageIndexRowsScanned(Checkers.equal(1000))
                          .build())
                      .build())
              .build())
          .build();

      testExplain(String.format(query, "/*+ MergeJoin(foo bar) Leading((bar foo)) */"), checker);
    }

    // Hash Join
    {
      Checker checker = topLevelChecker
          .storageReadRequests(Checkers.equal(11))
          .storageRowsScanned(Checkers.equal((1000 * 2) + 10240))
          .plan(makePlanBuilder()
              .nodeType(NODE_HASH_JOIN)
              .plans(
                  makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .indexName(MAIN_RANGE_MC_INDEX)
                      .actualRows(Checkers.equal(1000))
                      .storageTableRowsScanned(Checkers.equal(1000))
                      .storageIndexReadRequests(Checkers.equal(1))
                      .storageIndexReadExecutionTime(Checkers.greater(0.0))
                      .storageIndexRowsScanned(Checkers.equal(1000))
                      .build(),
                  makePlanBuilder()
                      .nodeType(NODE_HASH)
                      .plans(makePlanBuilder()
                          .nodeType(NODE_SEQ_SCAN)
                          .relationName(SIDE_TABLE)
                          .actualRows(Checkers.equal(NUM_PAGES * 1024))
                          .storageTableReadRequests(Checkers.equal(NUM_PAGES))
                          .storageTableReadExecutionTime(Checkers.greater(0.0))
                          .storageTableRowsScanned(Checkers.equal(NUM_PAGES * 1024))
                          .build())
                      .build())
              .build())
          .build();

      testExplain(String.format(query, "/*+ HashJoin(foo bar) Leading((foo bar)) */"), checker);
    }

    // Batch Nested Loop
    {
      Checker checker = topLevelChecker
          .storageReadRequests(Checkers.equal(2))
          .storageRowsScanned(Checkers.equal((1000 * 2) + 1000))
          .plan(makePlanBuilder()
              .nodeType(NODE_YB_BATCHED_NESTED_LOOP)
              .plans(
                  makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .indexName(MAIN_RANGE_MC_INDEX)
                      .actualRows(Checkers.equal(1000))
                      .storageTableRowsScanned(Checkers.equal(1000))
                      .storageIndexReadRequests(Checkers.equal(1))
                      .storageIndexReadExecutionTime(Checkers.greater(0.0))
                      .storageIndexRowsScanned(Checkers.equal(1000))
                      .build(),
                  makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .indexName(SIDE_TABLE_PKEY)
                      .actualLoops(Checkers.equal(1))
                      .actualRows(Checkers.equal(1000))
                      .storageTableRowsScanned(Checkers.equal(1000))
                      .storageTableReadRequests(Checkers.equal(1))
                      .storageTableReadExecutionTime(Checkers.greater(0.0))
                      .build())
              .build())
          .build();

      testExplain(String.format(query, "/*+ YbBatchedNL(foo bar) Leading((foo bar)) */"), checker);
    }

    // Nested Loop
    {
      Checker checker = topLevelChecker
          .storageReadRequests(Checkers.equal(1 + 1000))
          .storageRowsScanned(Checkers.equal((1000 * 2) + 1000))
          .plan(makePlanBuilder()
              .nodeType(NODE_NESTED_LOOP)
              .plans(
                  makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .indexName(MAIN_RANGE_MC_INDEX)
                      .actualRows(Checkers.equal(1000))
                      .storageTableRowsScanned(Checkers.equal(1000))
                      .storageIndexReadRequests(Checkers.equal(1))
                      .storageIndexReadExecutionTime(Checkers.greater(0.0))
                      .storageIndexRowsScanned(Checkers.equal(1000))
                      .build(),
                  makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .indexName(SIDE_TABLE_PKEY)
                      .actualLoops(Checkers.equal(1000))
                      .actualRows(Checkers.equal(1))
                      .storageTableRowsScanned(Checkers.equal(1))
                      .storageTableReadRequests(Checkers.equal(1))
                      .storageTableReadExecutionTime(Checkers.greater(0.0))
                      .build())
              .build())
          .build();

      testExplain(String.format(query, "/*+ NestLoop(foo bar) Leading((foo bar)) */"), checker);
    }

  }

}
