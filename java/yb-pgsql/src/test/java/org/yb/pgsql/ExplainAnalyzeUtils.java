package org.yb.pgsql;

import static org.junit.Assert.assertTrue;

import java.sql.ResultSet;
import java.sql.Statement;
import java.util.List;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.util.json.ObjectChecker;
import org.yb.util.json.ObjectCheckerBuilder;
import org.yb.util.json.ValueChecker;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

public class ExplainAnalyzeUtils {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgExplainAnalyze.class);
  public static final String NODE_AGGREGATE = "Aggregate";
  public static final String NODE_FUNCTION_SCAN = "Function Scan";
  public static final String NODE_HASH = "Hash";
  public static final String NODE_HASH_JOIN = "Hash Join";
  public static final String NODE_INDEX_ONLY_SCAN = "Index Only Scan";
  public static final String NODE_INDEX_SCAN = "Index Scan";
  public static final String NODE_LIMIT = "Limit";
  public static final String NODE_MERGE_JOIN = "Merge Join";
  public static final String NODE_MODIFY_TABLE = "ModifyTable";
  public static final String NODE_NESTED_LOOP = "Nested Loop";
  public static final String NODE_RESULT = "Result";
  public static final String NODE_SEQ_SCAN = "Seq Scan";
  public static final String NODE_SORT = "Sort";
  public static final String NODE_VALUES_SCAN = "Values Scan";

  public static final String NODE_YB_BATCHED_NESTED_LOOP = "YB Batched Nested Loop";

  public static final String PLAN = "Plan";

  public static final String OPERATION_INSERT = "Insert";
  public static final String OPERATION_UPDATE = "Update";

  public static final String RELATIONSHIP_OUTER_TABLE = "Outer";
  public static final String RELATIONSHIP_INNER_TABLE = "Inner";

  public static final String TOTAL_COST = "Total Cost";

  public interface TopLevelCheckerBuilder extends ObjectCheckerBuilder {
    TopLevelCheckerBuilder plan(ObjectChecker checker);
    TopLevelCheckerBuilder storageReadRequests(ValueChecker<Long> checker);
    TopLevelCheckerBuilder storageReadExecutionTime(ValueChecker<Double> checker);
    TopLevelCheckerBuilder storageWriteRequests(ValueChecker<Long> checker);
    TopLevelCheckerBuilder catalogReadRequests(ValueChecker<Long> checker);
    TopLevelCheckerBuilder catalogReadExecutionTime(ValueChecker<Double> checker);
    TopLevelCheckerBuilder catalogWriteRequests(ValueChecker<Long> checker);
    TopLevelCheckerBuilder storageFlushRequests(ValueChecker<Long> checker);
    TopLevelCheckerBuilder storageFlushExecutionTime(ValueChecker<Double> checker);
    TopLevelCheckerBuilder storageExecutionTime(ValueChecker<Double> checker);
  }

  public interface PlanCheckerBuilder extends ObjectCheckerBuilder {
    PlanCheckerBuilder alias(String value);
    PlanCheckerBuilder indexName(String value);
    PlanCheckerBuilder nodeType(String value);
    PlanCheckerBuilder operation(String value);
    PlanCheckerBuilder planRows(ValueChecker<Long> checker);
    PlanCheckerBuilder plans(Checker... checker);
    PlanCheckerBuilder relationName(String value);
    PlanCheckerBuilder parentRelationship(String value);
    PlanCheckerBuilder actualLoops(ValueChecker<Long> checker);
    PlanCheckerBuilder actualRows(ValueChecker<Long> checker);

    // Table Reads
    // The type of param is Checker and not ValueChecker<> since
    // we also want to be able to verify the absence of these attrs in tests
    // This requires a different type of checker than ValueChecker<>
    PlanCheckerBuilder storageTableReadRequests(Checker checker);
    PlanCheckerBuilder storageTableReadExecutionTime(Checker checker);

    // Table Writes
    PlanCheckerBuilder storageTableWriteRequests(ValueChecker<Long> checker);

    // Index Reads
    PlanCheckerBuilder storageIndexReadRequests(ValueChecker<Long> checker);
    PlanCheckerBuilder storageIndexReadExecutionTime(ValueChecker<Double> checker);

    // Index Writes
    PlanCheckerBuilder storageIndexWriteRequests(ValueChecker<Long> checker);

    // Catalog Reads
    PlanCheckerBuilder storageCatalogReadRequests(ValueChecker<Long> checker);

    // Added to verify that auto_explain.log_analyze works as intended
    PlanCheckerBuilder startupCost(Checker checker);
    PlanCheckerBuilder totalCost(Checker checker);
    PlanCheckerBuilder actualStartupTime(Checker checker);
    PlanCheckerBuilder actualTotalTime(Checker checker);
  }

  private static void testExplain(
      Statement stmt, String query, Checker checker, boolean timing, boolean debug)
      throws Exception {
    LOG.info("Query: " + query);
    ResultSet rs = stmt.executeQuery(String.format(
        "EXPLAIN (FORMAT json, ANALYZE true, SUMMARY true, DIST true, TIMING %b, DEBUG %b) %s",
        timing, debug, query));
    rs.next();
    JsonElement json = JsonParser.parseString(rs.getString(1));
    LOG.info("Response:\n" + JsonUtil.asPrettyString(json));
    if (checker == null) {
      return;
    }
    List<String> conflicts = JsonUtil.findConflicts(json.getAsJsonArray().get(0), checker);
    assertTrue("Json conflicts:\n" + String.join("\n", conflicts),
               conflicts.isEmpty());
  }

  public static void testExplain(
      Statement stmt, String query, Checker checker) throws Exception {
    testExplain(stmt, query, checker, true, false);
  }

  public static void testExplainDebug(
      Statement stmt, String query, Checker checker) throws Exception {
    testExplain(stmt, query, checker, true, true);
  }

  public static void testExplainNoTiming(
      Statement stmt, String query, Checker checker) throws Exception {
    testExplain(stmt, query, checker, false, false);
  }

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  public static void checkReadRequests(
      Statement stmt, String query, String scanType, ValueChecker<Long> readReqChecker,
      int tableRowCount) throws Exception {
    Checker checker = makeTopLevelBuilder()
        .plan(makePlanBuilder()
            .nodeType(scanType)
            .actualRows(Checkers.equal(tableRowCount))
            .build())
        .storageReadRequests(readReqChecker)
        .build();

    testExplain(stmt, query, checker);
  }

    // An opaque holder for costs. The idea of this is for users to only be able
  // to compare costs in tests and not read into potentially finnicky cost
  // values.
  public static class Cost implements Comparable<Cost> {
    public Cost(double value) {
      this.value = value;
    }

    @Override
    public int compareTo(Cost otherCost) {
      return Double.compare(this.value, otherCost.value);
    }

    private double value;
  }

  public static Cost getExplainTotalCost(Statement stmt, String query)
  throws Exception {
    ResultSet rs = stmt.executeQuery(String.format(
      "EXPLAIN (FORMAT json) %s", query));
    rs.next();
    JsonElement json = JsonParser.parseString(rs.getString(1));
    JsonArray rootArray = json.getAsJsonArray();
    if (!rootArray.isEmpty()) {
      JsonObject plan = rootArray.get(0).getAsJsonObject().getAsJsonObject(PLAN);
      double total_cost = plan.get(TOTAL_COST).getAsDouble();
      return new Cost(total_cost);
    }
    throw new IllegalArgumentException("Explain plan for this query returned empty.");
  }
}
