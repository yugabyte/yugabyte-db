package org.yb.pgsql;

import static org.junit.Assert.assertTrue;

import java.sql.ResultSet;
import java.sql.Statement;
import java.util.List;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.json.Checker;
import org.yb.util.json.JsonUtil;
import org.yb.util.json.ObjectChecker;
import org.yb.util.json.ObjectCheckerBuilder;
import org.yb.util.json.ValueChecker;

import com.google.gson.JsonElement;
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
  public static final String NODE_YB_SEQ_SCAN = "YB Seq Scan";

  public static final String OPERATION_INSERT = "Insert";
  public static final String OPERATION_UPDATE = "Update";

  public static final String RELATIONSHIP_OUTER_TABLE = "Outer";
  public static final String RELATIONSHIP_INNER_TABLE = "Inner";

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
      Statement stmt, String query, Checker checker, boolean timing) throws Exception {
    LOG.info("Query: " + query);
    ResultSet rs = stmt.executeQuery(String.format(
        "EXPLAIN (FORMAT json, ANALYZE true, SUMMARY true, DIST true, TIMING %b) %s",
        timing, query));
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
    testExplain(stmt, query, checker, true);
  }

  public static void testExplainNoTiming(
      Statement stmt, String query, Checker checker) throws Exception {
    testExplain(stmt, query, checker, false);
  }
}
