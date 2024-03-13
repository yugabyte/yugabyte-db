package org.yb.pgsql;

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_AGGREGATE;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_ONLY_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.testExplainDebug;

import java.sql.Connection;
import java.sql.Statement;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;

@RunWith(value=YBTestRunner.class)
public class TestPgEstimatedDocdbResultWidth extends BasePgSQLTest {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestPgEstimatedDocdbResultWidth.class);

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  private void testDocdbResultWidhEstimationHelper(
      Statement stmt, String query,
      String table_name, String node_type,
      Integer expected_docdb_result_width) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(node_type)
                  .relationName(table_name)
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
                  .build())
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query);
      LOG.info(e.toString());
      throw e;
    }
  }

  private void testAggregateFunctionsDocdbResultWidhEstimationHelper(
      Statement stmt, String query,
      String table_name,
      String node_type,
      Integer expected_docdb_result_width) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_AGGREGATE)
                  .plans(makePlanBuilder()
                    .nodeType(NODE_SEQ_SCAN)
                    .relationName(table_name)
                    .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
                    .build())
                  .build())
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query);
      LOG.info(e.toString());
      throw e;
    }
  }

  private void helperTestsForFixedTypeSizes(Statement stmt, String table_name,
                                                        String type_name, Integer type_size,
                                                        String value) throws Exception {
    /* The result contains 1 byte for null_indicator for each value. */
    Integer value_size = type_size + 1;
    /* The ybctid for keys with fixed size consists of 1 byte for null indicator, followed by 8
     * bytes for size and 1 byte for group termination. Additionally, each key value is prefixed by
     * 1 byte value type indicator.
     */
    Integer ybctid_size = 10 + 2 * (type_size + 1);

    stmt.execute(String.format("CREATE TABLE %1$s (k1 %2$s, k2 %2$s, v1 %2$s, v2 %2$s, " +
                               "PRIMARY KEY (k1 ASC, k2 ASC));", table_name, type_name));
    stmt.execute(String.format("CREATE INDEX %1$s_index ON %1$s (v1 ASC, v2 ASC)",
                               table_name, type_name));
    stmt.execute(String.format("INSERT INTO %1$s values(%2$s, %2$s, %2$s, %2$s);",
                               table_name, value));
    stmt.execute(String.format("ANALYZE %1$s;", table_name));
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT k1 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT v1 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT k1, k2 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, 2 * value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT k1, v1 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, 2 * value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT v1, v2 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, 2 * value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT k1, k2, v1, v2 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, 4 * value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT * FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, 4 * value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT 0 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, ybctid_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT 0 FROM %1$s WHERE k1 > %2$s", table_name, value),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, ybctid_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT 0 FROM %1$s WHERE k1 > %2$s and k2 > %2$s", table_name, value),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, ybctid_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT 0 FROM %1$s WHERE k1 > %2$s and k2 > %2$s and v1 > %2$s",
                      table_name, value),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, ybctid_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT 0 FROM %1$s WHERE k1 > %2$s and k2 > %2$s and " +
                      "v1 > %2$s and v2 > %2$s", table_name, value),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, ybctid_size);

    /* In case of Index Only Scan when no column is projected and no index conditions are present,
     * then the ybctid is returned. If no columns are projected, but index condition exists, then
     * the columns used in the index condition are returned.
     */
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("/*+ IndexOnlyScan(%1$s %1$s_index) */ SELECT 0 FROM %1$s ",
                      table_name, value),
        String.format("%1$s", table_name), NODE_INDEX_ONLY_SCAN, 2 * ybctid_size - 2);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("/*+ IndexOnlyScan(%1$s %1$s_index) */ SELECT 0 FROM %1$s " +
                      "WHERE v1 > %2$s", table_name, value),
        String.format("%1$s", table_name), NODE_INDEX_ONLY_SCAN, value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("/*+ IndexOnlyScan(%1$s %1$s_index) */ SELECT 0 FROM %1$s " +
                      "WHERE v1 > %2$s and v2 > %2$s", table_name, value),
        String.format("%1$s", table_name), NODE_INDEX_ONLY_SCAN, 2 * value_size);
  }

  private void helperTestsForStringTypes(Statement stmt, String table_name,
                                         String type_name, Integer type_size,
                                         String value) throws Exception {
    /* The result contains 1 byte for null indicator, 8 bytes for the size, and 1 byte for null
     * termination for each value. */
    Integer value_size = 10 + type_size;

    /* The size of the ybctid is computed as follows. The ybctid consists of 1 byte for null
     * indicator, 8 bytes for size and 1 byte for group termination. Additionally, each key value
     * is prefixed by 1 Byte value type indicator, and has 2 bytes for double null termination.
     */
    Integer ybctid_size = 10 + 2 * (type_size + 3);

    LOG.info(String.format("GAURAV : value_size = %d", value_size));

    stmt.execute(String.format("CREATE TABLE %1$s (k1 %2$s, k2 %2$s, v1 %2$s, v2 %2$s, " +
                               "PRIMARY KEY (k1 ASC, k2 ASC));", table_name, type_name));
    stmt.execute(String.format("CREATE INDEX %1$s_index ON %1$s (v1 ASC, v2 ASC)",
                               table_name, type_name));
    stmt.execute(String.format("INSERT INTO %1$s values(%2$s, %2$s, %2$s, %2$s);",
                               table_name, value));
    stmt.execute(String.format("ANALYZE %1$s;", table_name));
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT k1 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT v1 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT k1, k2 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, 2 * value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT k1, v1 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, 2 * value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT v1, v2 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, 2 * value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT k1, k2, v1, v2 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, 4 * value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT * FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, 4 * value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT 0 FROM %1$s", table_name),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, ybctid_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT 0 FROM %1$s WHERE k1 > %2$s", table_name, value),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, ybctid_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT 0 FROM %1$s WHERE k1 > %2$s and k2 > %2$s", table_name, value),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, ybctid_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT 0 FROM %1$s WHERE k1 > %2$s and k2 > %2$s and v1 > %2$s",
                      table_name, value),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, ybctid_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("SELECT 0 FROM %1$s WHERE k1 > %2$s and k2 > %2$s and " +
                      "v1 > %2$s and v2 > %2$s", table_name, value),
        String.format("%1$s", table_name), NODE_SEQ_SCAN, ybctid_size);

    /* In case of Index Only Scan when no column is projected and no index conditions are present,
     * then the ybctid is returned. If no columns are projected, but index condition exists, then
     * the columns used in the index condition are returned.
     */
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("/*+ IndexOnlyScan(%1$s %1$s_index) */ SELECT 0 FROM %1$s ",
                      table_name, value),
        String.format("%1$s", table_name), NODE_INDEX_ONLY_SCAN, 2 * ybctid_size - 2);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("/*+ IndexOnlyScan(%1$s %1$s_index) */ SELECT 0 FROM %1$s " +
                      "WHERE v1 > %2$s", table_name, value),
        String.format("%1$s", table_name), NODE_INDEX_ONLY_SCAN, value_size);
    testDocdbResultWidhEstimationHelper(stmt,
        String.format("/*+ IndexOnlyScan(%1$s %1$s_index) */ SELECT 0 FROM %1$s " +
                      "WHERE v1 > %2$s and v2 > %2$s", table_name, value),
        String.format("%1$s", table_name), NODE_INDEX_ONLY_SCAN, 2 * value_size);
  }

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET yb_enable_optimizer_statistics = true");
      stmt.execute("SET yb_enable_base_scans_cost_model = true");
      stmt.execute("SET yb_bnl_batch_size = 1024");
      stmt.execute("SET enable_bitmapscan = false"); // TODO(#20573): update bitmap scan cost model
    }
  }

  @Test
  public void testDocdbResultWidthEstimationFixedSizeTypes() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      helperTestsForFixedTypeSizes(stmt, "t_int", "int", 4, "1");
      helperTestsForFixedTypeSizes(stmt, "t_double", "double precision", 8, "1.1");
      helperTestsForFixedTypeSizes(stmt, "t_timestamp", "timestamp", 8, "'2024-01-01 01:01:01-01'");
    }
  }

  @Test
  public void testDocdbResultWidthEstimationStringTypes() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      helperTestsForStringTypes(stmt, "t_char_4", "char(4)", 4, "'abcd'");
      helperTestsForStringTypes(stmt, "t_char_16", "char(16)", 16,
                                         "'abcdefghijklmnop'");
      helperTestsForStringTypes(stmt, "t_char_64", "char(64)", 64,
          "'abcdefghijklmnopabcdefghijklmnopabcdefghijklmnopabcdefghijklmnop'");

      helperTestsForStringTypes(stmt, "t_varchar_4", "varchar(4)", 4, "'abcd'");
      helperTestsForStringTypes(stmt, "t_varchar_16", "varchar(16)", 16,
                                     "'abcdefghijklmnop'");
      helperTestsForStringTypes(stmt, "t_varchar_64", "varchar(64)", 64,
        "'abcdefghijklmnopabcdefghijklmnopabcdefghijklmnopabcdefghijklmnop'");

      helperTestsForStringTypes(stmt, "t_numeric_8_4", "numeric(8, 4)",
                                6, "1234.1234");
      helperTestsForStringTypes(stmt, "t_numeric_16_8", "numeric(16, 8)",
                                10, "12345678.12345678");
    }
  }

  @Test
  public void testDocdbResultWidthEstimationCompositeIndices() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t_int_hash_range_key (h1 INT, h2 INT, r1 INT, r2 INT, " +
                   "v1 INT, v2 INT, PRIMARY KEY ((h1, h2) HASH, r1 ASC, r2 ASC));");
      stmt.execute("INSERT INTO t_int_hash_range_key values (1, 1, 1, 1, 1, 1);");
      stmt.execute("ANALYZE t_int_hash_range_key;");
      testDocdbResultWidhEstimationHelper(stmt, "SELECT h1 FROM t_int_hash_range_key",
          "t_int_hash_range_key", NODE_SEQ_SCAN, 5);
      testDocdbResultWidhEstimationHelper(stmt, "SELECT r1 FROM t_int_hash_range_key",
          "t_int_hash_range_key", NODE_SEQ_SCAN, 5);
      testDocdbResultWidhEstimationHelper(stmt, "SELECT h1, r1 FROM t_int_hash_range_key",
          "t_int_hash_range_key", NODE_SEQ_SCAN, 10);
      testDocdbResultWidhEstimationHelper(stmt, "SELECT h1, r1, v1 FROM t_int_hash_range_key",
          "t_int_hash_range_key", NODE_SEQ_SCAN, 15);
      testDocdbResultWidhEstimationHelper(stmt, "SELECT v1 FROM t_int_hash_range_key",
          "t_int_hash_range_key", NODE_SEQ_SCAN, 5);
      testDocdbResultWidhEstimationHelper(stmt, "SELECT v1, v2 FROM t_int_hash_range_key",
          "t_int_hash_range_key", NODE_SEQ_SCAN, 10);
      testDocdbResultWidhEstimationHelper(stmt, "SELECT * FROM t_int_hash_range_key",
          "t_int_hash_range_key", NODE_SEQ_SCAN, 30);
      testDocdbResultWidhEstimationHelper(stmt, "SELECT h1, h2, r1, r2, v1, v2 FROM " +
          "t_int_hash_range_key", "t_int_hash_range_key", NODE_SEQ_SCAN, 30);
      testDocdbResultWidhEstimationHelper(stmt, "SELECT 0 FROM t_int_hash_range_key",
          "t_int_hash_range_key", NODE_SEQ_SCAN, 34);

      stmt.execute("CREATE TABLE t_int_numeric_hash_range_key (hi1 INT, hn2 NUMERIC(16, 8), " +
          "ri1 INT, rn2 NUMERIC(16, 8), vi1 INT, vn2 NUMERIC(16, 8), " +
          "PRIMARY KEY ((hi1, hn2) HASH, ri1 ASC, rn2 ASC));");
      stmt.execute("INSERT INTO t_int_numeric_hash_range_key values (1, 1, 1, 1, 1, 1);");
      stmt.execute("ANALYZE t_int_numeric_hash_range_key;");
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT hi1 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 5);
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT ri1 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 5);
      /* Actual result width is 17. PG over-estimates size of numeric  */
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT hi1, hn2 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 19);
      /* Actual result width is 17. PG over-estimates size of numeric */
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT ri1, rn2 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 19);
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT hi1, ri1 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 10);
      /* Actual result width is 24. PG over-estimates size of numeric */
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT hn2, rn2 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 28);
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT hi1, ri1, vi1 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 15);
      /* Actual result width is 36. PG over-estimates size of numeric */
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT hn2, rn2, vn2 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 42);
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT vi1 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 5);
      /* Actual result width is 17. PG over-estimates size of numeric */
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT vi1, vn2 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 19);
      /* Actual result width is 51. PG over-estimates size of numeric */
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT * FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 57);
      /* Actual result width is 51. PG over-estimates size of numeric */
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT hi1, hn2, ri1, rn2, vi1, vn2 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 57);
      /* Actual width is 30, but PG overestimates the size of numeric.  */
      testDocdbResultWidhEstimationHelper(stmt, "SELECT 0 FROM t_int_numeric_hash_range_key",
          "t_int_numeric_hash_range_key", NODE_SEQ_SCAN, 38);
    }
  }

  @Test
  public void testDocdbResultWidthEstimationArraysAndUserDefinedTypes() throws Exception {
    /* TODO(#20955) : Some aggregate functions can be pushed down to DocDB,
     * but it is not modeled properly in the cost model.
     */
    try (Statement stmt = connection.createStatement()) {

      stmt.execute("CREATE TYPE two_ints AS (i1 INTEGER, i2 INTEGER);");
      stmt.execute("CREATE TABLE test_ints (v two_ints);");
      stmt.execute("INSERT INTO test_ints values (ROW(1, 1));");
      stmt.execute("ANALYZE test_ints;");

      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT * FROM test_ints", "test_ints", NODE_SEQ_SCAN, 38);
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT (v).i1 FROM test_ints", "test_ints", NODE_SEQ_SCAN, 38);

      stmt.execute("CREATE TABLE test_int_array (v int[4]);");
      stmt.execute("INSERT INTO test_int_array values ('{1, 2, 3, 4}');");
      stmt.execute("ANALYZE test_int_array;");

      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT * FROM test_int_array", "test_int_array", NODE_SEQ_SCAN, 46);
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT v[1] FROM test_int_array", "test_int_array", NODE_SEQ_SCAN, 46);

      stmt.execute("CREATE TYPE two_numerics AS (n1 NUMERIC(8, 4), n2 NUMERIC(8, 4));");
      stmt.execute("CREATE TABLE test_numerics (v two_numerics);");
      stmt.execute("INSERT INTO test_numerics values (ROW(1234.1234, 1234.1234));");
      stmt.execute("ANALYZE test_numerics;");

      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT * FROM test_numerics", "test_numerics", NODE_SEQ_SCAN, 44);
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT (v).n1 FROM test_numerics", "test_numerics", NODE_SEQ_SCAN, 44);

      stmt.execute("CREATE TABLE test_numeric_array (v numeric(8, 4)[4]);");
      stmt.execute("INSERT INTO test_numeric_array values " +
          "('{1234.1234, 2345.2345, 3456.3456, 4567.4567}');");
      stmt.execute("ANALYZE test_numeric_array;");

      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT * FROM test_numeric_array", "test_numeric_array", NODE_SEQ_SCAN, 78);
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT v[1] FROM test_numeric_array", "test_numeric_array", NODE_SEQ_SCAN, 78);

      stmt.execute("CREATE TYPE two_varchars AS (v1 VARCHAR(8), v2 VARCHAR(8));");
      stmt.execute("CREATE TABLE test_varchars (v two_varchars);");
      stmt.execute("INSERT INTO test_varchars values (ROW('abcdefgh', 'abcdefgh'));");
      stmt.execute("ANALYZE test_varchars;");

      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT * FROM test_varchars", "test_varchars", NODE_SEQ_SCAN, 48);
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT (v).v1 FROM test_varchars", "test_varchars", NODE_SEQ_SCAN, 48);

      stmt.execute("CREATE TABLE test_varchar_array (v varchar(4)[4]);");
      stmt.execute("INSERT INTO test_varchar_array values " +
          "('{\"abcd\", \"abcd\", \"abcd\", \"abcd\"}');");
      stmt.execute("ANALYZE test_varchar_array;");

      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT * FROM test_varchar_array", "test_varchar_array", NODE_SEQ_SCAN, 62);
      testDocdbResultWidhEstimationHelper(stmt,
          "SELECT v[1] FROM test_varchar_array", "test_varchar_array", NODE_SEQ_SCAN, 62);
    }
  }

  @Test
  public void testDocdbResultWidthEstimationAggregateFunctions() throws Exception {
    /* TODO(#20955) : Some aggregate functions can be pushed down to DocDB,
     * but it is not modeled properly in the cost model.
     */
    try (Statement stmt = connection.createStatement()) {

      stmt.execute("CREATE TABLE t_20955 (i1 INT, i2 INT, i3 INT);");
      stmt.execute("INSERT INTO t_20955 values (1, 1, 1);");
      stmt.execute("ANALYZE t_20955;");

      /* Actual result 9 (1 byte for null indicator and 8 byte for int64 result */
      testAggregateFunctionsDocdbResultWidhEstimationHelper(stmt,
          "SELECT count(i1) FROM t_20955", "t_20955", NODE_SEQ_SCAN, 5);
      /* Actual result 9 (1 byte for null indicator and 8 byte for int64 result */
      testAggregateFunctionsDocdbResultWidhEstimationHelper(stmt,
          "SELECT sum(i1) FROM t_20955", "t_20955", NODE_SEQ_SCAN, 5);
      /* Actual result 18 (1 byte for null indicator and 17 byte for numeric result */
      testAggregateFunctionsDocdbResultWidhEstimationHelper(stmt,
          "SELECT sum(i1) FROM t_20955", "t_20955", NODE_SEQ_SCAN, 5);

      /* Result min and max have same type as input columns */
      testAggregateFunctionsDocdbResultWidhEstimationHelper(stmt,
          "SELECT min(i1) FROM t_20955", "t_20955", NODE_SEQ_SCAN, 5);
      testAggregateFunctionsDocdbResultWidhEstimationHelper(stmt,
          "SELECT max(i1) FROM t_20955", "t_20955", NODE_SEQ_SCAN, 5);

      testAggregateFunctionsDocdbResultWidhEstimationHelper(stmt,
          "SELECT count(*) FROM t_20955", "t_20955", NODE_SEQ_SCAN, 9);
    }
  }

  @Test
  public void testDocdbResultWidthEstimationRecheckColumns() throws Exception {
    /* TODO(#20956): In some cases columns with pushed down filters are returned
     * to PG for result rechecking. This is not correctly modeled in the cost
     * model.
     */
    try (Statement stmt = connection.createStatement()) {

      stmt.execute("CREATE TABLE t_20956 (i1 INT, i2 INT, PRIMARY KEY (i1 ASC));");
      stmt.execute("INSERT INTO t_20956 values (1, 1);");
      stmt.execute("ANALYZE t_20956;");

      /* Both filters on i1 are marked eligible to be pushed down as index
       * filters so we should only receive the value of i2 for the matching rows
       * from DocDB. Instead, only one filter on i1 is actually used as an
       * index condition and the value of i1 is also returned to PG. The second
       * filter is applied on PG side.
       *
       * Actual size of docdb result width is 10, but we estimate it to be 5.
       */
      testDocdbResultWidhEstimationHelper(stmt, "/*+IndexScan(t_20956)*/ SELECT i2 FROM t_20956 " +
          "WHERE i1 = 1 and i1 in (2, 3, 4)",
          "t_20956", NODE_INDEX_SCAN, 5);
    }
  }
}
