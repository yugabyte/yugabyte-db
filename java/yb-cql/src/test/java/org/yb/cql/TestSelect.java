// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.cql;

import java.util.*;
import java.util.stream.Collectors;
import java.math.BigDecimal;
import java.text.SimpleDateFormat;

import com.datastax.driver.core.LocalDate;
import com.datastax.driver.core.SimpleStatement;
import com.datastax.driver.core.Statement;
import com.yugabyte.driver.core.TableSplitMetadata;
import com.yugabyte.driver.core.policies.PartitionAwarePolicy;
import org.junit.Test;

import org.yb.minicluster.IOMetrics;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.RocksDBMetrics;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.InvalidQueryException;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestSelect extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestSelect.class);

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // Generate system.partitions table on query.
    builder.yqlSystemPartitionsVtableRefreshSecs(0);
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();

    flagMap.put("ycql_allow_in_op_with_order_by", "true");
    flagMap.put("ycql_enable_packed_row", "false");
    return flagMap;
  }

  @Test
  public void testSimpleQuery() throws Exception {
    LOG.info("TEST CQL SIMPLE QUERY - Start");

    // Setup test table.
    setupTable("test_select", 10);

    // Select data from the test table.
    String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                         "  WHERE h1 = 7 AND h2 = 'h7' AND r1 = 107;";
    ResultSet rs = session.execute(select_stmt);

    int row_count = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      if (rs.getAvailableWithoutFetching() == 100 && !rs.isFullyFetched()) {
        rs.fetchMoreResults();
      }

      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), 7);
      assertEquals(row.getString(1), "h7");
      assertEquals(row.getInt(2), 107);
      assertEquals(row.getString(3), "r107");
      assertEquals(row.getInt(4), 1007);
      assertEquals(row.getString(5), "v1007");
      row_count++;
    }
    assertEquals(row_count, 1);

    // Insert multiple rows with the same partition key.
    int num_rows = 20;
    int h1_shared = 1111111;
    String h2_shared = "h2_shared_key";
    for (int idx = 0; idx < num_rows; idx++) {
      // INSERT: Valid statement with column list.
      String insert_stmt = String.format(
        "INSERT INTO test_select(h1, h2, r1, r2, v1, v2) VALUES(%d, '%s', %d, 'r%d', %d, 'v%d');",
        h1_shared, h2_shared, idx+100, idx+100, idx+1000, idx+1000);
      session.execute(insert_stmt);
    }

    // Verify multi-row select.
    String multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                                      "  WHERE h1 = %d AND h2 = '%s';",
                                      h1_shared, h2_shared);
    rs = session.execute(multi_stmt);

    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      if (rs.getAvailableWithoutFetching() == 100 && !rs.isFullyFetched()) {
        rs.fetchMoreResults();
      }

      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_rows);

    // Test ALLOW FILTERING clause.
    multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                               "  WHERE h1 = %d AND h2 = '%s' ALLOW FILTERING;",
                               h1_shared, h2_shared);
    rs = session.execute(multi_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      if (rs.getAvailableWithoutFetching() == 100 && !rs.isFullyFetched()) {
        rs.fetchMoreResults();
      }

      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_rows);

    LOG.info("TEST CQL SIMPLE QUERY - End");
  }

  public void testNotEqualsQuery(boolean use_if_clause) throws Exception {
    LOG.info("TEST CQL QUERY WITH NOT EQUALS - Start");

    // Setup test table.
    setupTable("test_select", 0);

    // Populate rows.
    {
      String insert_stmt = "INSERT INTO test_select (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (?, ?, ?, ?, ?, ?)";
      PreparedStatement stmt = session.prepare(insert_stmt);
      for (int i = 1; i <= 2; i++) {
        for (int j = 1; j <= 2; j++) {
          session.execute(stmt.bind(new Integer(i), "h" + i,
                                    new Integer(j), "r" + j,
                                    new Integer(j), "v" + i + j));
        }
      }
      session.execute("INSERT INTO test_select (h1, h2, r1, r2, v1, v2) " +
                      "VALUES (3, 'h3', 1, 'r1', null, 'v')");
    }

    String clause_type = use_if_clause ? "IF" : "WHERE";

    // Tests on primary key columns work only with WHERE clause.
    if (!use_if_clause) {
      // Test with "!=" on hash key (both null and non-null case
      // even though the null case doesn't make sense for primary key columns).
      assertQueryRowsUnorderedWithoutDups(
        String.format("SELECT * FROM test_select %s h1 != NULL", clause_type),
        "Row[1, h1, 1, r1, 1, v11]",
        "Row[1, h1, 2, r2, 2, v12]",
        "Row[2, h2, 1, r1, 1, v21]",
        "Row[2, h2, 2, r2, 2, v22]",
        "Row[3, h3, 1, r1, NULL, v]");

      assertQueryRowsUnorderedWithoutDups(
        String.format("SELECT * FROM test_select %s h1 != 1", clause_type),
        "Row[2, h2, 1, r1, 1, v21]",
        "Row[2, h2, 2, r2, 2, v22]",
        "Row[3, h3, 1, r1, NULL, v]");

      // Test with "!=" on range key (both null and non-null case
      // even though the null case doesn't make sense for primary key columns).
      assertQueryRowsUnorderedWithoutDups(
        String.format("SELECT * FROM test_select %s r1 != NULL", clause_type),
        "Row[1, h1, 1, r1, 1, v11]",
        "Row[1, h1, 2, r2, 2, v12]",
        "Row[2, h2, 1, r1, 1, v21]",
        "Row[2, h2, 2, r2, 2, v22]",
        "Row[3, h3, 1, r1, NULL, v]");

      assertQueryRowsUnorderedWithoutDups(
        String.format("SELECT * FROM test_select %s r1 != 1", clause_type),
        "Row[1, h1, 2, r2, 2, v12]",
        "Row[2, h2, 2, r2, 2, v22]");
    }

    // Test with "!=" on regular column (both null and non-null case).
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM test_select %s v1 != NULL", clause_type),
      "Row[1, h1, 1, r1, 1, v11]",
      "Row[1, h1, 2, r2, 2, v12]",
      "Row[2, h2, 1, r1, 1, v21]",
      "Row[2, h2, 2, r2, 2, v22]");

    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM test_select %s v1 != 1", clause_type),
      "Row[1, h1, 2, r2, 2, v12]",
      "Row[2, h2, 2, r2, 2, v22]",
      "Row[3, h3, 1, r1, NULL, v]");

    // Test with "!=" operator on multiple columns.
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM test_select %s v1 != 1 AND v2 != 'v12'", clause_type),
        "Row[2, h2, 2, r2, 2, v22]",
        "Row[3, h3, 1, r1, NULL, v]");

    // Test with "<>" instead of != operator - both are allowed in YCQL and have same semantics.
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM test_select %s v1 <> 1 AND v2 != 'v12'", clause_type),
        "Row[2, h2, 2, r2, 2, v22]",
        "Row[3, h3, 1, r1, NULL, v]");

    // Test with "!=" operator on json/collection column.
    //   i) JSON should be comparable with NULL and other JSONs.
    //   ii) Collections can only be compared with NULL. See IsComparable() in ql_type.h for ref.
    String alter_stmt = "ALTER TABLE test_select add v_json jsonb, v_set SET<int>, " +
      "v_map MAP<int,int>, v_list LIST<int>";

    session.execute(alter_stmt);
    session.execute("TRUNCATE TABLE test_select"); // To reduce the output space for tests.

    session.execute("INSERT INTO test_select(h1, h2, r1, r2, v1, v2, v_json, v_set, v_map, " +
      "v_list) VALUES (1, 'h1', 1, 'r1', 1, 'v11', '{\"key\": \"val\"}', {1}, {1:1}, [1])");

    // For json.

    // TODO: As of now != null works on collections but not with json.
    // There were two approaches to allow json comparison with null -
    // 1. Change the isConvertible() related conversion matrix for json and null
    // 2. Add a condition in semantic analysis to not check isConvertible() for
    //    =,!=,IN,NOT IN operators in where clause.
    //
    // assertQueryRowsUnorderedWithoutDups(
    //   String.format("SELECT * FROM test_select %s v_json != null", clause_type),
    //   "Row[1, h1, 1, r1, 1, v11, {\"key\": \"val\"}, {1}, {1:1}, [1]]");

    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM test_select %s v_json != '{\"key\": \"val\"}'", clause_type));

    // For set.
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM test_select %s v_set != null", clause_type),
      "Row[1, h1, 1, r1, 1, v11, {\"key\":\"val\"}, [1], {1=1}, [1]]");

    // For map.
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM test_select %s v_map != null", clause_type),
      "Row[1, h1, 1, r1, 1, v11, {\"key\":\"val\"}, [1], {1=1}, [1]]");

    // For list.
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM test_select %s v_list != null", clause_type),
      "Row[1, h1, 1, r1, 1, v11, {\"key\":\"val\"}, [1], {1=1}, [1]]");

    // Negative tests below.
    String illogicalConditionErrString = "Illogical condition for where clause";

    if (!use_if_clause) {
      // Test with more than one "!=" operator on one column.
      runInvalidStmt(String.format("SELECT * FROM test_select %s v1 != 1 AND v1 != 2", clause_type),
        illogicalConditionErrString);
      runInvalidStmt(String.format("SELECT * FROM test_select %s v1 <> 1 AND v1 != 2", clause_type),
        illogicalConditionErrString);

      // Test with "!=" operator with other operators - <, >, <=, >=, =, IN, NOT IN.
      runInvalidStmt(String.format("SELECT * FROM test_select %s v1 < 1 AND v1 != 2", clause_type),
        illogicalConditionErrString);
      runInvalidStmt(String.format("SELECT * FROM test_select %s v1 > 1 AND v1 != 2", clause_type),
        illogicalConditionErrString);
      runInvalidStmt(String.format("SELECT * FROM test_select %s v1 <= 1 AND v1 != 2", clause_type),
        illogicalConditionErrString);
      runInvalidStmt(String.format("SELECT * FROM test_select %s v1 >= 1 AND v1 != 2", clause_type),
        illogicalConditionErrString);
      runInvalidStmt(String.format("SELECT * FROM test_select %s v1 = 1 AND v1 != 2", clause_type),
        illogicalConditionErrString);
      runInvalidStmt(
        String.format("SELECT * FROM test_select %s v1 IN (1) AND v1 != 2", clause_type),
        illogicalConditionErrString);
      runInvalidStmt(
        String.format("SELECT * FROM test_select %s v1 NOT IN (1) AND v1 != 2", clause_type),
        illogicalConditionErrString);

      // Test with "!=" operator on json/collection subscripted column.
      runInvalidStmt(
        String.format("SELECT * FROM test_select %s v_json->>'key' != ''", clause_type),
        "Invalid CQL Statement. Operator not supported for subscripted column");
      runInvalidStmt(String.format("SELECT * FROM test_select %s v_map[1] != null", clause_type),
        "Invalid CQL Statement. Operator not supported for subscripted column");
      runInvalidStmt(String.format("SELECT * FROM test_select %s v_list[1] != null", clause_type),
        "Invalid CQL Statement. Operator not supported for subscripted column");
    }

    // Test with incomparable data type.
    runInvalidStmt(String.format("SELECT * FROM test_select %s v1 != ''", clause_type),
      "Datatype Mismatch");
  }

  @Test
  public void testNotEqualsQueryWithWhere() throws Exception {
    testNotEqualsQuery(false /* use_if_clause */);
  }

  @Test
  public void testNotEqualsQueryWithIf() throws Exception {
    testNotEqualsQuery(true /* use_if_clause */);
  }

  @Test
  public void testRangeQuery() throws Exception {
    LOG.info("TEST CQL RANGE QUERY - Start");

    // Setup test table.
    setupTable("test_select", 0);

    // Populate rows.
    {
      String insert_stmt = "INSERT INTO test_select (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (?, ?, ?, ?, ?, ?);";
      PreparedStatement stmt = session.prepare(insert_stmt);
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 3; j++) {
          session.execute(stmt.bind(new Integer(i), "h" + i,
                                    new Integer(j), "r" + j,
                                    new Integer(j), "v" + i + j));
        }
      }
    }

    // Test with ">" and "<".
    assertQuery("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 > 1 AND r1 < 3;",
                "Row[1, h1, 2, r2, 2, v12]");

    // Test with mixing ">" and "<=".
    assertQuery("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 > 1 AND r1 <= 3;",
                "Row[1, h1, 2, r2, 2, v12]" +
                "Row[1, h1, 3, r3, 3, v13]");

    // Test with ">=" and "<=".
    assertQuery("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 >= 1 AND r1 <= 3;",
                "Row[1, h1, 1, r1, 1, v11]" +
                "Row[1, h1, 2, r2, 2, v12]" +
                "Row[1, h1, 3, r3, 3, v13]");

    // Test with ">=" and "<=" on r1 and ">" and "<" on r2.
    assertQuery("SELECT * FROM test_select " +
                "WHERE h1 = 1 AND h2 = 'h1' AND r1 >= 1 AND r1 <= 3 AND r2 > 'r1' AND r2 < 'r3';",
                "Row[1, h1, 2, r2, 2, v12]");

    // Test with "=>" and "<=" with partial hash key.
    assertQuery("SELECT * FROM test_select WHERE h1 = 1 AND r1 >= 1 AND r1 <= 3;",
                "Row[1, h1, 1, r1, 1, v11]" +
                "Row[1, h1, 2, r2, 2, v12]" +
                "Row[1, h1, 3, r3, 3, v13]");

    // Test with ">" and "<" with no hash key.
    assertQuery("SELECT * FROM test_select WHERE r1 > 1 AND r1 < 3;",
                "Row[1, h1, 2, r2, 2, v12]" +
                "Row[3, h3, 2, r2, 2, v32]" +
                "Row[2, h2, 2, r2, 2, v22]");

    // Invalid range: equal and bound conditions cannot be used together.
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 >= 1 AND r1 = 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 > 1 AND r1 = 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 = 1 AND r1 < 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 = 1 AND r1 <= 3;");

    // Invalid range: two lower or two upper bound conditions cannot be used together.
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 >= 1 AND r1 >= 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 >= 1 AND r1 > 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 < 1 AND r1 <= 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 <= 1 AND r1 <= 3;");

    LOG.info("TEST CQL RANGE QUERY - End");
  }

  @Test
  public void testCqlContainsKey() throws Exception {
    LOG.info("TEST CQL CONTAINS KEY QUERY - Start");

    // Test CONTAINS KEY on map with INT, BOOLEAN, TEXT, FROZEN collections as keys.
    session.execute("CREATE TABLE t1_int_int (a int, b map<int, int>, primary key (a));");
    session.execute("INSERT INTO t1_int_int (a, b) VALUES (1, {1:1, 2:2, 4:4});");
    session.execute("INSERT INTO t1_int_int (a, b) VALUES (2, {10:1, 92:2, 14:4});");
    session.execute("INSERT INTO t1_int_int (a, b) VALUES (20, {120:1, 92:2, 142:4});");
    session.execute("INSERT INTO t1_int_int (a, b) VALUES (12, {101:1, 912:2, 124:4});");
    assertQuery("SELECT * FROM t1_int_int WHERE b CONTAINS KEY 92;",
        "Row[2, {10=1, 14=4, 92=2}]Row[20, {92=2, 120=1, 142=4}]");

    session.execute("CREATE TABLE t1_bool_int (a int, b map<boolean, int>, primary key (a));");
    session.execute("INSERT INTO t1_bool_int (a, b) VALUES (1, {true:1});");
    session.execute("INSERT INTO t1_bool_int (a, b) VALUES (2, {false:0});");
    session.execute("INSERT INTO t1_bool_int (a, b) VALUES (20, {true:1, false:0});");
    session.execute("INSERT INTO t1_bool_int (a, b) VALUES (12, {true:0});");
    assertQuery("SELECT * FROM t1_bool_int WHERE b CONTAINS KEY false;",
        "Row[2, {false=0}]Row[20, {false=0, true=1}]");
    assertQuery("SELECT * FROM t1_bool_int WHERE b CONTAINS KEY false AND b[true] = 1;",
        "Row[20, {false=0, true=1}]");

    session.execute("CREATE TABLE t1_text_int (a int, b map<text, int>, primary key (a));");
    session.execute("INSERT INTO t1_text_int (a, b) VALUES (1,  {'1':1, '2':2, '4':4});");
    session.execute("INSERT INTO t1_text_int (a, b) VALUES (2,  {'10':1, '92':2, '14':4});");
    session.execute("INSERT INTO t1_text_int (a, b) VALUES (20, {'120':1, '92':2, '142':4});");
    session.execute("INSERT INTO t1_text_int (a, b) VALUES (12, {'101':1, '912':2, '124':4});");
    assertQuery("SELECT * FROM t1_text_int WHERE b CONTAINS KEY '92';",
        "Row[2, {10=1, 14=4, 92=2}]Row[20, {120=1, 142=4, 92=2}]");
    assertQuery("SELECT * FROM t1_text_int WHERE b CONTAINS KEY '92' AND b CONTAINS KEY '142';",
        "Row[20, {120=1, 142=4, 92=2}]");

    session.execute("CREATE TABLE t1_set_int (a int, b map<frozen<set<int>>, int>, "
        + "primary key (a));");
    session.execute("INSERT INTO t1_set_int (a, b) VALUES (1,  {{1}:1, {1,2}:2, {1,2,3}:4});");
    session.execute("INSERT INTO t1_set_int (a, b) VALUES (2,  {{1,2}:1, {1,2,3}:2, "
        + "{1,2,3,4}:4});");
    session.execute("INSERT INTO t1_set_int (a, b) VALUES (20, {{1,2,3}:1, {1,2,3,4}:2, "
        + "{1,2,3,4,5}:4});");
    session.execute("INSERT INTO t1_set_int (a, b) VALUES (12, {{1,2,3}:1, {1,3}:2, {1,2}:4});");
    assertQuery("SELECT * FROM t1_set_int WHERE b CONTAINS KEY {1,2,3,4};",
        "Row[2, {[1, 2]=1, [1, 2, 3]=2, [1, 2, 3, 4]=4}]"
            + "Row[20, {[1, 2, 3]=1, [1, 2, 3, 4]=2, [1, 2, 3, 4, 5]=4}]");

    session.execute("CREATE TABLE t1_list_int (a int, b map<frozen<list<int>>, int>, "
        + "primary key (a));");
    session.execute("INSERT INTO t1_list_int (a, b) VALUES (1,  {[1]:1, [1,2]:2, [1,2,3]:4});");
    session.execute("INSERT INTO t1_list_int (a, b) VALUES (2,  {[1,2]:1, "
        + "[1,2,3]:2, [1,2,3,4]:4});");
    session.execute("INSERT INTO t1_list_int (a, b) VALUES (20, {[1,2,3]:1, "
        + "[1,2,3,4]:2, [1,2,3,4,5]:4});");
    session.execute("INSERT INTO t1_list_int (a, b) VALUES (12, {[1,2,3]:1, [1,3]:2, [1,2]:4});");
    assertQuery("SELECT * FROM t1_list_int WHERE b CONTAINS KEY [1,2,3,4];",
        "Row[2, {[1, 2]=1, [1, 2, 3]=2, [1, 2, 3, 4]=4}]"
            + "Row[20, {[1, 2, 3]=1, [1, 2, 3, 4]=2, [1, 2, 3, 4, 5]=4}]");

    session.execute("CREATE TABLE t1_map_int (a int, b map<frozen<map<int,int>>, int>, "
        + "primary key (a));");
    session.execute("INSERT INTO t1_map_int (a, b) VALUES (1,  {{1:1}:1, {1:1,2:2}:2, "
        + "{1:1,2:2,3:3}:4});");
    session.execute("INSERT INTO t1_map_int (a, b) VALUES (2,  {{1:1,2:2}:1, "
        + "{1:1,2:2,3:3}:2, {1:1,2:2,3:3,4:4}:4});");
    session.execute("INSERT INTO t1_map_int (a, b) VALUES (20, {{1:1,2:2,3:3}:1, "
        + "{1:1,2:2,3:3,4:4}:2, {1:1,2:2,3:3,4:4,5:5}:4});");
    session.execute("INSERT INTO t1_map_int (a, b) VALUES (12, {{1:1,2:2,3:3}:1, "
        + "{1:1,3:3}:2, {1:1,2:2}:4});");
    assertQuery("SELECT * FROM t1_map_int WHERE b CONTAINS KEY {1:1,2:2,3:3,4:4};",
        "Row[2, {{1=1, 2=2}=1, {1=1, 2=2, 3=3}=2, {1=1, 2=2, 3=3, 4=4}=4}]"
            + "Row[20, {{1=1, 2=2, 3=3}=1, {1=1, 2=2, 3=3, 4=4}=2, {1=1, 2=2, 3=3, 4=4, 5=5}=4}]");

    // TODO: Add tests to test CONTAINS KEY on a set / map / list of frozen tuples
    // Currently TServer crashes when trying to create a table with a collection containing frozen
    // tuple.
    // GHI - https://github.com/yugabyte/yugabyte-db/issues/3588,
    //       https://github.com/yugabyte/yugabyte-db/issues/936
    // Example Test: CREATE TABLE table (id int, deps map<frozen<tuple<int, int>>, int>,
    //                                   primary key (id))
    // INSERT INTO table (id, deps) VALUES (1, {(1,1):1, (2,2):2});
    // INSERT INTO table (id, deps) VALUES (2, {(2,2):3, (3,3):4});
    // SELECT * FROM table WHERE deps CONTAINS KEY (1,1)
    // Expected Result: Row[1, [(1,1)=1, (2,2)=2]]
    runInvalidStmt("CREATE TABLE tuple_not_implemented (id int primary key, deps tuple<int,int>)",
        "Feature Not Supported");

    // Test failure for invalid types.
    final String unsupportedTypeForContainsKey = "Datatype Mismatch. CONTAINS KEY is only "
        + "supported for Maps that are not frozen.";

    session.execute("CREATE TABLE t2_frozen_map (a int, b frozen<map<int,int>>,"
        + " primary key(a))");
    runInvalidStmt(
        "SELECT * FROM t2_frozen_map WHERE b CONTAINS KEY 5;", unsupportedTypeForContainsKey);

    session.execute("CREATE TABLE t2_map_frozen_map (a int, b map<int, frozen<map<int,int>>>, "
        + "primary key (a));");
    runInvalidStmt(
        "SELECT * FROM t2_map_frozen_map WHERE b[0] CONTAINS KEY 5", unsupportedTypeForContainsKey);

    session.execute("CREATE TABLE t2_set (a int, b set<int>,"
        + " primary key(a))");
    runInvalidStmt("SELECT * FROM t2_set WHERE b CONTAINS KEY 5;", unsupportedTypeForContainsKey);

    session.execute("CREATE TABLE t2_list (a int, b list<int>,"
        + " primary key(a))");
    runInvalidStmt("SELECT * FROM t2_list WHERE b CONTAINS KEY 5;", unsupportedTypeForContainsKey);

    // Test failure on NULL as RHS.
    final String nullNotSupported = "CONTAINS KEY does not support NULL";

    session.execute("CREATE TABLE t3 (a int, b map<int, int>, primary key(a))");
    runInvalidStmt("SELECT * FROM t3 WHERE b CONTAINS KEY NULL", nullNotSupported);

    // Test failure with incomparable types
    final String incomparableTypeString = "Datatype Mismatch";
    runInvalidStmt("SELECT * FROM t3 WHERE b CONTAINS KEY 'text'", incomparableTypeString);
    runInvalidStmt("SELECT * FROM t3 WHERE b CONTAINS KEY 1.2", incomparableTypeString);

    // Tests with bind variables.
    session.execute("CREATE TABLE t4 (a int, b map<int,int>, primary key(a))");
    PreparedStatement query = session.prepare("SELECT * FROM t4 WHERE b CONTAINS KEY ?");

    try {
      session.execute(query.bind().setToNull(0));
      fail("Query '" + query + "' did not fail with null");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
      String errorMessage = "Invalid Arguments. CONTAINS KEY does not support NULL";
      LOG.info("Expected exception", e);
      assertTrue("Error message '" + e.getMessage() + "' should contain '" + errorMessage + "'",
          e.getMessage().contains(errorMessage));
    }

    try {
      session.execute(query.bind().setString(0, "text"));
      fail("Query '" + query + "' did not fail with text");
    } catch (com.datastax.driver.core.exceptions.CodecNotFoundException e) {
      String errorMessage = "Codec not found for requested operation: [int <-> java.lang.String]";
      LOG.info("Expected exception", e);
      assertTrue("Error message '" + e.getMessage() + "' should contain '" + errorMessage + "'",
          e.getMessage().contains(errorMessage));
    }

    try {
      session.execute(query.bind().setDecimal(0, new BigDecimal(1.2324)));
      fail("Query '" + query + "' did not fail with 1.2324");
    } catch (com.datastax.driver.core.exceptions.CodecNotFoundException e) {
      String errorMessage =
          "Codec not found for requested operation: [int <-> java.math.BigDecimal]";
      LOG.info("Expected exception", e);
      assertTrue("Error message '" + e.getMessage() + "' should contain '" + errorMessage + "'",
          e.getMessage().contains(errorMessage));
    }
  }

  @Test
  public void testCqlContains() throws Exception {
    LOG.info("TEST CQL CONTAINS QUERY - Start");

    // Test CONTAINS on sets of text, varint, boolean, decimal.
    session.execute("CREATE TABLE t1_text (id int, deps set<text>, primary key(id))");
    session.execute("INSERT INTO t1_text(id, deps) VALUES (1, {'1', '2', '3'})");
    session.execute("INSERT INTO t1_text(id, deps) VALUES (2, {'1', '2', '3'})");
    session.execute("INSERT INTO t1_text(id, deps) VALUES (3, {'2', '3'})");
    session.execute("INSERT INTO t1_text(id, deps) VALUES (4, {'3'})");
    assertQuery(
        "SELECT * FROM t1_text WHERE deps CONTAINS '1'", "Row[1, [1, 2, 3]]Row[2, [1, 2, 3]]");
    assertQuery("SELECT * FROM t1_text WHERE deps CONTAINS '1' AND id = 2", "Row[2, [1, 2, 3]]");

    session.execute("CREATE TABLE t1_varint (id int, deps set<varint>, primary key(id))");
    session.execute("INSERT INTO t1_varint(id, deps) VALUES (1, {1, 2, 3, 5})");
    session.execute("INSERT INTO t1_varint(id, deps) VALUES (2, {1, 2, 3})");
    session.execute("INSERT INTO t1_varint(id, deps) VALUES (3, {1})");
    session.execute("INSERT INTO t1_varint(id, deps) VALUES (4, {})");
    assertQuery("SELECT * FROM t1_varint WHERE deps CONTAINS 1",
        "Row[1, [1, 2, 3, 5]]Row[2, [1, 2, 3]]Row[3, [1]]");

    session.execute("CREATE TABLE t1_bool (id int, deps set<boolean>, primary key(id))");
    session.execute("INSERT INTO t1_bool(id, deps) VALUES (1, {true})");
    session.execute("INSERT INTO t1_bool(id, deps) VALUES (2, {false})");
    session.execute("INSERT INTO t1_bool(id, deps) VALUES (3, {true, false})");
    session.execute("INSERT INTO t1_bool(id, deps) VALUES (4, {})");
    assertQuery(
        "SELECT * FROM t1_bool WHERE deps CONTAINS true", "Row[1, [true]]Row[3, [false, true]]");
    assertQuery("SELECT * FROM t1_bool WHERE deps CONTAINS true AND deps CONTAINS false",
        "Row[3, [false, true]]");

    session.execute("CREATE TABLE t1_decimal (id int, deps set<decimal>, primary key(id))");
    session.execute("INSERT INTO t1_decimal(id, deps) VALUES (1, {1.0, 3.45, 1.34, 3.14})");
    session.execute("INSERT INTO t1_decimal(id, deps) VALUES (2, {2, 4, 3})");
    session.execute("INSERT INTO t1_decimal(id, deps) VALUES (3, {3.14, 5.0, 2.8})");
    session.execute("INSERT INTO t1_decimal(id, deps) VALUES (4, {1.0})");
    assertQuery("SELECT * FROM t1_decimal WHERE deps CONTAINS 1.0",
        "Row[1, [1, 1.34, 3.14, 3.45]]Row[4, [1]]");

    // Test CONTAINS on a set of frozen sets.
    session.execute("CREATE TABLE t1_frozen_set (id int, "
        + "deps set<frozen<set<varint>>>, primary key(id))");
    session.execute("INSERT INTO t1_frozen_set (id, deps) VALUES (1, {{1, 2}, {3, 4}})");
    session.execute("INSERT INTO t1_frozen_set (id, deps) VALUES (2, {{1, 3}, {1, 2}})");
    session.execute("INSERT INTO t1_frozen_set (id, deps) VALUES (3, {{5, 6}, {7, 8}})");
    session.execute("INSERT INTO t1_frozen_set (id, deps) VALUES (4, {{1, 2}, {9, 10}})");
    assertQuery("SELECT * FROM t1_frozen_set  WHERE deps CONTAINS {1,2}",
        "Row[1, [[1, 2], [3, 4]]]Row[4, [[1, 2], [9, 10]]]Row[2, [[1, 2], [1, 3]]]");

    // Test CONTAINS on a set of frozen lists.
    session.execute("CREATE TABLE t1_frozen_list(id int, "
        + "deps set<frozen<list<varint>>>, primary key(id))");
    session.execute("INSERT INTO t1_frozen_list (id, deps) VALUES (1, {[1, 2], [3, 4]})");
    session.execute("INSERT INTO t1_frozen_list (id, deps) VALUES (2, {[1, 3], [1, 2]})");
    session.execute("INSERT INTO t1_frozen_list (id, deps) VALUES (3, {[5, 6], [7, 8]})");
    session.execute("INSERT INTO t1_frozen_list (id, deps) VALUES (4, {[1, 2], [9, 1]})");
    assertQuery("SELECT * FROM t1_frozen_list WHERE deps CONTAINS [1,2]",
        "Row[1, [[1, 2], [3, 4]]]Row[4, [[1, 2], [9, 1]]]Row[2, [[1, 2], [1, 3]]]");

    // Test CONTAINS on a set of frozen maps.
    session.execute("CREATE TABLE t1_frozen_map (id int, "
        + "deps set<frozen<map<varint,varint>>>, primary key(id))");
    session.execute("INSERT INTO t1_frozen_map (id, deps) VALUES (1, {{1:2}, {3:4}})");
    session.execute("INSERT INTO t1_frozen_map (id, deps) VALUES (2, {{1:3}, {1:2}})");
    session.execute("INSERT INTO t1_frozen_map (id, deps) VALUES (3, {{5:6}, {7:8}})");
    session.execute("INSERT INTO t1_frozen_map (id, deps) VALUES (4, {{1:2}, {9:1}})");
    assertQuery("SELECT * FROM t1_frozen_map WHERE deps CONTAINS {1:2}",
        "Row[1, [{1=2}, {3=4}]]Row[4, [{1=2}, {9=1}]]Row[2, [{1=2}, {1=3}]]");

    // TODO: Add tests to test CONTAINS on a set/list/map of frozen tuples.
    // Currently TServer crashes when trying to create a table with a collection containing frozen
    // tuple.
    // GHI - https://github.com/yugabyte/yugabyte-db/issues/3588,
    //       https://github.com/yugabyte/yugabyte-db/issues/936
    // Example Test: CREATE TABLE table (id int, deps set<frozen<tuple<int, int>>, primary key (id))
    // INSERT INTO table (id, deps) VALUES (1, {(1,1), (2,2)});
    // INSERT INTO table (id, deps) VALUES (2, {(2,2), (3,3)});
    // SELECT * FROM table WHERE deps CONTAINS (1,1)
    // Expected Result: Row[1, [(1,1), (2,2)]]
    runInvalidStmt("CREATE TABLE tuple_not_implemented (id int primary key, deps tuple<int,int>)",
        "Feature Not Supported");

    // Test CONTAINS on LIST of TEXT, VARINT, BOOLEAN, DECIMAL.
    session.execute("CREATE TABLE t2_text (id int, deps list<text>, primary key(id))");
    session.execute("INSERT INTO t2_text(id, deps) VALUES (1, ['1', '2', '3'])");
    session.execute("INSERT INTO t2_text(id, deps) VALUES (2, ['1', '2', '3'])");
    session.execute("INSERT INTO t2_text(id, deps) VALUES (3, ['2', '3'])");
    session.execute("INSERT INTO t2_text(id, deps) VALUES (4, ['3'])");
    assertQuery(
        "SELECT * FROM t2_text WHERE deps CONTAINS '1'", "Row[1, [1, 2, 3]]Row[2, [1, 2, 3]]");

    session.execute("CREATE TABLE t2_varint (id int, deps list<varint>, primary key(id))");
    session.execute("INSERT INTO t2_varint(id, deps) VALUES (1, [1, 2, 3])");
    session.execute("INSERT INTO t2_varint(id, deps) VALUES (2, [1, 2, 3])");
    session.execute("INSERT INTO t2_varint(id, deps) VALUES (3, [1])");
    session.execute("INSERT INTO t2_varint(id, deps) VALUES (4, [])");
    assertQuery("SELECT * FROM t2_varint WHERE deps CONTAINS 1 AND deps[2] = 3",
        "Row[1, [1, 2, 3]]Row[2, [1, 2, 3]]");

    session.execute("CREATE TABLE t2_bool (id int, deps list<boolean>, primary key(id))");
    session.execute("INSERT INTO t2_bool(id, deps) VALUES (1, [true])");
    session.execute("INSERT INTO t2_bool(id, deps) VALUES (2, [false])");
    session.execute("INSERT INTO t2_bool(id, deps) VALUES (3, [true, false])");
    session.execute("INSERT INTO t2_bool(id, deps) VALUES (4, [])");
    assertQuery(
        "SELECT * FROM t2_bool WHERE deps CONTAINS true", "Row[1, [true]]Row[3, [true, false]]");

    session.execute("CREATE TABLE t2_decimal (id int, deps list<decimal>, primary key(id))");
    session.execute("INSERT INTO t2_decimal(id, deps) VALUES (1, [1.0, 3.45, 1.34, 3.14])");
    session.execute("INSERT INTO t2_decimal(id, deps) VALUES (2, [2, 4, 3])");
    session.execute("INSERT INTO t2_decimal(id, deps) VALUES (3, [3.14, 5.0, 2.8])");
    session.execute("INSERT INTO t2_decimal(id, deps) VALUES (4, [1.0])");
    assertQuery("SELECT * FROM t2_decimal WHERE deps CONTAINS 1.0",
        "Row[1, [1, 3.45, 1.34, 3.14]]Row[4, [1]]");

    // Test CONTAINS on a list of frozen sets.
    session.execute("CREATE TABLE t2_frozen_set (id int, "
        + "deps list<frozen<set<varint>>>, primary key(id))");
    session.execute("INSERT INTO t2_frozen_set (id, deps) VALUES (1, [{1, 2}, {3, 4}])");
    session.execute("INSERT INTO t2_frozen_set (id, deps) VALUES (2, [{1, 3}, {1, 2}])");
    session.execute("INSERT INTO t2_frozen_set (id, deps) VALUES (3, [{5, 6}, {7, 8}])");
    session.execute("INSERT INTO t2_frozen_set (id, deps) VALUES (4, [{1, 2}, {9, 10}])");
    assertQuery("SELECT * FROM t2_frozen_set  WHERE deps CONTAINS {1,2}",
        "Row[1, [[1, 2], [3, 4]]]Row[4, [[1, 2], [9, 10]]]Row[2, [[1, 3], [1, 2]]]");

    // Test CONTAINS on a list of frozen lists.
    session.execute("CREATE TABLE t2_frozen_list (id int, "
        + "deps list<frozen<list<varint>>>, primary key(id))");
    session.execute("INSERT INTO t2_frozen_list (id, deps) VALUES (1, [[1, 2], [3, 4]])");
    session.execute("INSERT INTO t2_frozen_list (id, deps) VALUES (2, [[1, 3], [1, 2]])");
    session.execute("INSERT INTO t2_frozen_list (id, deps) VALUES (3, [[5, 6], [7, 8]])");
    session.execute("INSERT INTO t2_frozen_list (id, deps) VALUES (4, [[1, 2], [9, 10]])");
    assertQuery("SELECT * FROM t2_frozen_list  WHERE deps CONTAINS [1,2]",
        "Row[1, [[1, 2], [3, 4]]]Row[4, [[1, 2], [9, 10]]]Row[2, [[1, 3], [1, 2]]]");

    // Test CONTAINS on a list of frozen map.
    session.execute("CREATE TABLE t2_frozen_map (id int, "
        + "deps list<frozen<map<varint,varint>>>, primary key(id))");
    session.execute("INSERT INTO t2_frozen_map (id, deps) VALUES (1, [{1:2}, {3:4}])");
    session.execute("INSERT INTO t2_frozen_map (id, deps) VALUES (2, [{1:3}, {1:2}])");
    session.execute("INSERT INTO t2_frozen_map (id, deps) VALUES (3, [{5:6}, {7:8}])");
    session.execute("INSERT INTO t2_frozen_map (id, deps) VALUES (4, [{1:2}, {9:1}])");
    assertQuery("SELECT * FROM t2_frozen_map WHERE deps CONTAINS {1:2}",
        "Row[1, [{1=2}, {3=4}]]Row[4, [{1=2}, {9=1}]]Row[2, [{1=3}, {1=2}]]");

    // Test CONTAINS on MAP (INT, INT) (INT, TEXT) (TEXT, BOOLEAN).
    session.execute("CREATE TABLE t3_int_int (id int, deps map<int, int>, primary key(id))");
    session.execute("INSERT INTO t3_int_int(id, deps) VALUES (1, {1:1, 2:2, 3:3, 4:4})");
    session.execute("INSERT INTO t3_int_int(id, deps) VALUES (2, {4:1, 2:5, 3:3, 6:4})");
    session.execute("INSERT INTO t3_int_int(id, deps) VALUES (3, {8:4, 4:2, 3:5, 2:4})");
    session.execute("INSERT INTO t3_int_int(id, deps) VALUES (4, {0:2, 2:43, 3:7, 5:4})");
    assertQuery("SELECT * FROM t3_int_int WHERE deps CONTAINS 1",
        "Row[1, {1=1, 2=2, 3=3, 4=4}]Row[2, {2=5, 3=3, 4=1, 6=4}]");

    session.execute("CREATE TABLE t3_int_text (id int, deps map<int, text>, primary key(id))");
    session.execute("INSERT INTO t3_int_text(id, deps) VALUES (1, {1:'1', 2:'2', 3:'3', 4:'4'})");
    session.execute("INSERT INTO t3_int_text(id, deps) VALUES (2, {4:'1', 2:'5', 3:'3', 6:'4'})");
    session.execute("INSERT INTO t3_int_text(id, deps) VALUES (3, {8:'4', 4:'2', 3:'5', 2:'4'})");
    session.execute("INSERT INTO t3_int_text(id, deps) VALUES (4, {0:'2', 2:'4', 3:'7', 5:'4'})");
    assertQuery("SELECT * FROM t3_int_text WHERE deps CONTAINS '1'",
        "Row[1, {1=1, 2=2, 3=3, 4=4}]Row[2, {2=5, 3=3, 4=1, 6=4}]");

    session.execute("CREATE TABLE t3_text_boolean (id int, deps map<text, boolean>, "
        + "primary key(id))");
    session.execute("INSERT INTO t3_text_boolean(id, deps) VALUES (1, "
        + "{'1':false, '2':false, '3':false, '4':false})");
    session.execute("INSERT INTO t3_text_boolean(id, deps) VALUES (2, "
        + "{'1':true, '5':true, '3':false, '4':true})");
    session.execute("INSERT INTO t3_text_boolean(id, deps) VALUES (3, "
        + "{'4':true, '2':false, '5':false, '40':true})");
    session.execute("INSERT INTO t3_text_boolean(id, deps) VALUES (4, "
        + "{'2':false, '4':false, '7':false, '4':false})");
    assertQuery("SELECT * FROM t3_text_boolean WHERE deps CONTAINS true",
        "Row[2, {1=true, 3=false, 4=true, 5=true}]Row[3, {"
            + "2=false, 4=true, 40=true, 5=false}]");

    // Test CONTAINS on a map of frozen list.
    session.execute("CREATE TABLE t4_map_frozen_list (id int, deps map<text, "
        + "frozen<list<boolean>>>, primary key(id))");
    session.execute("INSERT INTO t4_map_frozen_list(id, deps) VALUES (1, "
        + "{'1':[true, true, true], '2':[false, false, false], '3':[false, false, false], "
        + "'4':[false, false, false]})");
    session.execute("INSERT INTO t4_map_frozen_list(id, deps) VALUES (2, "
        + "{'1':[true, true, false], '5':[true, true, false], '3':[false, false, false],"
        + " '4':[true, true, false]})");
    session.execute("INSERT INTO t4_map_frozen_list(id, deps) VALUES (3, "
        + "{'4':[false, false, false], '2':[false, false, false], '5':[true, true, false], "
        + "'40':[false, false, false]})");
    session.execute("INSERT INTO t4_map_frozen_list(id, deps) VALUES (4, "
        + "{'2':[true, true, false], '4':[true, true, false], '7':[false, false, false], "
        + "'40':[true, true, false]})");
    assertQuery("SELECT * FROM t4_map_frozen_list WHERE deps CONTAINS [true, true, true]",
        "Row[1, {1=[true, true, true], 2=[false, false, false], "
            + "3=[false, false, false], 4=[false, false, false]}]");

    // Test CONTAINS on a map of frozen set.
    session.execute("CREATE TABLE t4_map_frozen_set (id int, deps map<text, "
        + "frozen<set<boolean>>>, primary key(id))");
    session.execute("INSERT INTO t4_map_frozen_set(id, deps) VALUES (1, "
        + "{'1':{true}, '2':{false}, '3':{true}, "
        + "'4':{false}})");
    session.execute("INSERT INTO t4_map_frozen_set(id, deps) VALUES (2, "
        + "{'1':{true}, '5':{false, true}})");
    assertQuery("SELECT * FROM t4_map_frozen_set WHERE deps CONTAINS {true, false}",
        "Row[2, {1=[true], 5=[false, true]}]");

    // Test CONTAINS on a map of frozen map.
    session.execute("CREATE TABLE t4_map_frozen_map (id int, deps map<text, "
        + "frozen<map<boolean, boolean>>>, primary key(id))");
    session.execute("INSERT INTO t4_map_frozen_map(id, deps) VALUES (1, "
        + "{'1':{true:true}, '2':{false:false}, '3':{true:true}, "
        + "'4':{false:false}})");
    session.execute("INSERT INTO t4_map_frozen_map(id, deps) VALUES (2, "
        + "{'1':{true:true}, '5':{false:false, true:true}})");
    assertQuery("SELECT * FROM t4_map_frozen_map WHERE deps CONTAINS {false:false, true:true}",
        "Row[2, {1={true=true}, 5={false=false, true=true}}]");

    // Test failure for frozen collections.
    final String unsupportedTypeForContains = "Datatype Mismatch. CONTAINS is only supported for "
        + "Collections that are not frozen.";

    session.execute("CREATE TABLE t5_frozen_list (id int, deps frozen<list<int>>,"
        + " primary key(id))");
    runInvalidStmt(
        "SELECT * FROM t5_frozen_list WHERE deps CONTAINS 5", unsupportedTypeForContains);

    session.execute("CREATE TABLE t5_frozen_set (id int, deps frozen<set<int>>,"
        + " primary key(id))");
    runInvalidStmt("SELECT * FROM t5_frozen_set WHERE deps CONTAINS 5", unsupportedTypeForContains);

    session.execute("CREATE TABLE t5_frozen_map (id int, deps frozen<map<int, int>>,"
        + " primary key(id))");
    runInvalidStmt("SELECT * FROM t5_frozen_map WHERE deps CONTAINS 5", unsupportedTypeForContains);

    session.execute("CREATE TABLE t5_list_of_frozen_map (id int, "
        + "deps list<frozen<map<int, int>>>,"
        + " primary key(id))");
    runInvalidStmt(
        "SELECT * FROM t5_list_of_frozen_map WHERE deps[0] CONTAINS 5", unsupportedTypeForContains);

    // Test failure with NULL as RHS.
    final String nullNotSupported = "CONTAINS does not support NULL";

    session.execute("CREATE TABLE t6 (id int, deps set<int>, primary key(id))");
    runInvalidStmt("SELECT * FROM t6 WHERE deps CONTAINS NULL", nullNotSupported);

    // Test failure incomparable types
    final String incomparableTypes = "Datatype Mismatch";
    runInvalidStmt("SELECT * FROM t6 WHERE deps CONTAINS 'text'", incomparableTypes);
    runInvalidStmt("SELECT * FROM t6 WHERE deps CONTAINS 1.2", incomparableTypes);

    // Test with bind variables.
    session.execute("CREATE TABLE t7 (id int, deps map<int, int>, primary key(id))");
    PreparedStatement query = session.prepare("SELECT * FROM t7 WHERE deps CONTAINS ?");

    try {
      session.execute(query.bind().setToNull(0));
      fail("CONTAINS query with NULL did not fail with Bind Variables");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
      String errorMessage = "Invalid Arguments. CONTAINS does not support NULL";
      LOG.info("Expected exception", e);
      assertTrue("Error message '" + e.getMessage() + "' should contain '" + errorMessage + "'",
          e.getMessage().contains(errorMessage));
    }

    try {
      session.execute(query.bind().setString(0, "text"));
      fail("CONTAINS query with mismatched datatype did not fail.");
    } catch (com.datastax.driver.core.exceptions.CodecNotFoundException e) {
      String errorMessage = "Codec not found for requested operation: [int <-> java.lang.String]";
      LOG.info("Expected exception", e);
      assertTrue("Error message '" + e.getMessage() + "' should contain '" + errorMessage + "'",
          e.getMessage().contains(errorMessage));
    }

    try {
      session.execute(query.bind().setDecimal(0, new BigDecimal(1.23445)));
      fail("CONTAINS query with mismatched datatype did not fail.");
    } catch (com.datastax.driver.core.exceptions.CodecNotFoundException e) {
      String errorMessage =
          "Codec not found for requested operation: [int <-> java.math.BigDecimal]";
      LOG.info("Expected exception", e);
      assertTrue("Error message '" + e.getMessage() + "' should contain '" + errorMessage + "'",
          e.getMessage().contains(errorMessage));
    }
  }

  @Test
  public void testSelectWithLimit() throws Exception {
    LOG.info("TEST CQL LIMIT QUERY - Start");

    // Setup test table.
    setupTable("test_select", 0);

    // Insert multiple rows with the same partition key.
    int num_rows = 20;
    int h1_shared = 1111111;
    int num_limit_rows = 10;
    String h2_shared = "h2_shared_key";
    for (int idx = 0; idx < num_rows; idx++) {
      // INSERT: Valid statement with column list.
      String insert_stmt = String.format(
          "INSERT INTO test_select(h1, h2, r1, r2, v1, v2) VALUES(%d, '%s', %d, 'r%d', %d, 'v%d');",
          h1_shared, h2_shared, idx + 100, idx + 100, idx + 1000, idx + 1000);
      session.execute(insert_stmt);
    }

    // Verify multi-row select.
    String multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
            "  WHERE h1 = %d AND h2 = '%s' LIMIT %d;",
        h1_shared, h2_shared, num_limit_rows);
    ResultSet rs = session.execute(multi_stmt);

    int row_count = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
          row.getInt(0),
          row.getString(1),
          row.getInt(2),
          row.getString(3),
          row.getInt(4),
          row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_limit_rows);

    // Test allow filtering.
    multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
            "  WHERE h1 = %d AND h2 = '%s' LIMIT %d ALLOW FILTERING;",
        h1_shared, h2_shared, num_limit_rows);
    rs = session.execute(multi_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
          row.getInt(0),
          row.getString(1),
          row.getInt(2),
          row.getString(3),
          row.getInt(4),
          row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_limit_rows);

    LOG.info("TEST CQL LIMIT QUERY - End");
  }

  private void assertQueryWithPageSize(String query, String expected, int pageSize) {
    SimpleStatement stmt = new SimpleStatement(query);
    stmt.setFetchSize(pageSize);
    assertQuery(stmt, expected);
  }

  private void testMultiShardScansWithOffset(int pageSize) {
    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 9 OFFSET 0",
        "Row[5, 5, 5]" +
        "Row[1, 1, 1]" +
        "Row[6, 6, 6]" +
        "Row[7, 7, 7]" +
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 5 OFFSET 3",
        "Row[7, 7, 7]" +
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset OFFSET 3 LIMIT 5",
        "Row[7, 7, 7]" +
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 10 OFFSET 3",
        "Row[7, 7, 7]" +
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 5 OFFSET 4",
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 9 OFFSET 8",
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 9 OFFSET 6",
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 3 OFFSET 3",
        "Row[7, 7, 7]" +
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 9 OFFSET 9",
        "", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 >= 5 LIMIT 9 OFFSET 0",
        "Row[5, 5, 5]" +
        "Row[6, 6, 6]" +
        "Row[7, 7, 7]" +
        "Row[8, 8, 8]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 >= 5 LIMIT 2 OFFSET 1",
        "Row[6, 6, 6]" +
        "Row[7, 7, 7]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 < 4 LIMIT 9 OFFSET 0",
        "Row[1, 1, 1]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 < 4 LIMIT 2 OFFSET 1",
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 < 4 LIMIT 4 OFFSET 1",
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset OFFSET 4",
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]" +
        "Row[3, 3, 3]", pageSize);
  }

  private void testSingleShardScansWithOffset() {
    assertQueryWithPageSize(
        "SELECT * FROM test_offset WHERE h1 = 1 ORDER BY r1 DESC LIMIT 2 OFFSET 3",
        "Row[1, 2, 2]" +
        "Row[1, 1, 1]", Integer.MAX_VALUE);
    assertQueryWithPageSize(
        "SELECT * FROM test_offset WHERE h1 = 1 ORDER BY r1 DESC LIMIT 2 OFFSET 4",
        "Row[1, 1, 1]", Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset WHERE h1 = 1 ORDER BY r1 DESC OFFSET 2",
                            "Row[1, 3, 3]" +
                            "Row[1, 2, 2]" +
                            "Row[1, 1, 1]", Integer.MAX_VALUE);

    // Offset applies only to matching rows.
    assertQueryWithPageSize(
        "SELECT * FROM test_offset WHERE c1 <= 4 AND h1 = 1 ORDER BY r1 DESC LIMIT 2 OFFSET 2",
        "Row[1, 2, 2]" +
        "Row[1, 1, 1]", Integer.MAX_VALUE);
    assertQueryWithPageSize(
        "SELECT * FROM test_offset WHERE c1 IN (1, 3, 5) AND h1 = 1 ORDER BY r1 DESC LIMIT 2 " +
        "OFFSET 1",
        "Row[1, 3, 3]" +
        "Row[1, 1, 1]", Integer.MAX_VALUE);
    assertQueryWithPageSize(
        "SELECT * FROM test_offset WHERE c1 IN (1, 3, 5) AND h1 = 1 ORDER BY r1 DESC LIMIT 2 " +
         "OFFSET 2",
        "Row[1, 1, 1]", Integer.MAX_VALUE);
  }

  @Test
  public void testSelectWithOffset() throws Exception {
    session.execute("CREATE TABLE test_offset (h1 int, r1 int, c1 int, PRIMARY KEY(h1, r1))");

    // Test single shard offset and limits.
    session.execute("INSERT INTO test_offset (h1, r1, c1) VALUES (1, 1, 1)");
    session.execute("INSERT INTO test_offset (h1, r1, c1) VALUES (1, 2, 2)");
    session.execute("INSERT INTO test_offset (h1, r1, c1) VALUES (1, 3, 3)");
    session.execute("INSERT INTO test_offset (h1, r1, c1) VALUES (1, 4, 4)");
    session.execute("INSERT INTO test_offset (h1, r1, c1) VALUES (1, 5, 5)");

    // Test full scan but with single-shard data.
    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 2 OFFSET 3",
        "Row[1, 4, 4]" +
        "Row[1, 5, 5]", Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset OFFSET 3 LIMIT 2",
        "Row[1, 4, 4]" +
        "Row[1, 5, 5]", Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 2 OFFSET 4", "Row[1, 5, 5]",
        Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset OFFSET 2",
        "Row[1, 3, 3]" +
        "Row[1, 4, 4]" +
        "Row[1, 5, 5]", Integer.MAX_VALUE);

    // Offset applies only to matching rows.
    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 >= 2 LIMIT 2 OFFSET 2",
        "Row[1, 4, 4]" +
        "Row[1, 5, 5]", Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 IN (1, 3, 5) LIMIT 2 OFFSET 1",
        "Row[1, 3, 3]" +
        "Row[1, 5, 5]", Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 IN (1, 3, 5) LIMIT 2 OFFSET 2",
        "Row[1, 5, 5]", Integer.MAX_VALUE);

    // Test single-shard scan.
    testSingleShardScansWithOffset();

    // Test single-shard scan with dense data (other rows in the database).
    // Insert a bunch of other hashes to ensure there are rows before/after the target range on that
    // tablet. This ensure the start/end detection works correctly.
    for (Integer h1 = 0; h1 < 100; h1++) {
      session.execute("INSERT INTO test_offset (h1, r1, c1) VALUES (?, 1, 1)", h1);
    }
    testSingleShardScansWithOffset();

    // Test multi-shard offset and limits.
    // Delete and re-create the table first.
    session.execute("DROP TABLE test_offset");
    session.execute("CREATE TABLE test_offset (h1 int, r1 int, c1 int, PRIMARY KEY(h1, r1))");

    int totalShards = miniCluster.getClusterParameters().numShardsPerTServer *
        miniCluster.getClusterParameters().numTservers;
    for (int i = 0; i < totalShards; i++) {
      // 1 row per tablet (roughly).
      session.execute(String.format("INSERT INTO test_offset (h1, r1, c1) VALUES (%d, %d, %d)",
          i, i, i));
    }

    testMultiShardScansWithOffset(Integer.MAX_VALUE);
    for (int i = 0; i <= totalShards; i++) {
      testMultiShardScansWithOffset(i);
    }

    // Test select with offset and limit. Fetch the exact number of rows. Verify that the query
    // ends explicitly with an empty paging state.
    for (int i = 0; i < 10; i++) {
      session.execute(String.format("INSERT INTO test_offset (h1, r1, c1) VALUES (%d, %d, %d)",
          100, i, i));
    }
    ResultSet rs = session.execute("SELECT * FROM test_offset WHERE h1 = 100 OFFSET 3 LIMIT 4");
    for (int i = 3; i < 3 + 4; i++) {
      assertEquals(String.format("Row[100, %d, %d]", i, i), rs.one().toString());
    }
    assertNull(rs.getExecutionInfo().getPagingState());

    // Test Invalid offsets.
    runInvalidStmt("SELECT * FROM test_offset OFFSET -1");
    runInvalidStmt(String.format("SELECT * FROM test_offset OFFSET %d",
        (long)Integer.MAX_VALUE + 1));
  }

  @Test
  public void testLocalTServerCalls() throws Exception {
    // Create test table.
    session.execute("CREATE TABLE test_local (k int PRIMARY KEY, v int);");

    // Get the base metrics of each tserver.
    Map<MiniYBDaemon, IOMetrics> baseMetrics = new HashMap<>();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      IOMetrics metrics = createIOMetrics(ts);
      baseMetrics.put(ts, metrics);
    }

    // Insert rows and select them back.
    final int NUM_KEYS = 100;
    PreparedStatement stmt = session.prepare("INSERT INTO test_local (k, v) VALUES (?, ?);");
    for (int i = 1; i <= NUM_KEYS; i++) {
      session.execute(stmt.bind(Integer.valueOf(i), Integer.valueOf(i + 1)));
    }
    stmt = session.prepare("SELECT v FROM test_local WHERE k = ?");
    for (int i = 1; i <= NUM_KEYS; i++) {
      assertEquals(i + 1, session.execute(stmt.bind(Integer.valueOf(i))).one().getInt("v"));
    }

    // Check the metrics again.
    IOMetrics totalMetrics = new IOMetrics();
    int tsCount = miniCluster.getTabletServers().values().size();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      IOMetrics metrics = createIOMetrics(ts).subtract(baseMetrics.get(ts));
      LOG.info("Metrics of " + ts.toString() + ": " + metrics.toString());
      totalMetrics.add(metrics);
    }

    // Verify there are some local read and write calls.
    assertTrue(totalMetrics.localReadCount > 0);
    assertTrue(totalMetrics.localWriteCount > 0);

    // Verify total number of read / write calls. It is possible to have more calls than the
    // number of keys because some calls may reach step-down leaders and need retries.
    assertTrue(totalMetrics.localReadCount + totalMetrics.remoteReadCount >= NUM_KEYS);
    assertTrue(totalMetrics.localWriteCount + totalMetrics.remoteWriteCount >= NUM_KEYS);
  }

  @Test
  public void testTtlInWhereClauseOfSelect() throws Exception {
    LOG.info("TEST SELECT TTL queries - Start");

    // Setup test table.
    int[] ttls = {
      100,
      100,
      100,
      100,
      100,
      100,
      100,
      100,
      100,
      200
    };
    setupTable("test_ttl", ttls);

    // Select data from the test table.
    String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_ttl WHERE ttl(v1) > 150";
    ResultSet rs = session.execute(select_stmt);

    List<Row> rows = rs.all();
    assertEquals(1, rows.size());
    Row row = rows.get(0);
    assertEquals(9, row.getInt(0));
    assertEquals("h9", row.getString(1));
    assertEquals(109, row.getInt(2));
    assertEquals("r109", row.getString(3));
    assertEquals(1009, row.getInt(4));
    assertEquals(1009, row.getInt(5));

    String update_stmt = "UPDATE test_ttl USING ttl 300 SET v1 = 1009" +
                         "  WHERE h1 = 9 and h2 = 'h9' and r1 = 109 and r2 = 'r109' ";
    session.execute(update_stmt);
    select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_ttl WHERE ttl(v1) > 250";

    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(9, row.getInt(0));
    assertEquals("h9", row.getString(1));
    assertEquals(109, row.getInt(2));
    assertEquals("r109", row.getString(3));
    assertEquals(1009, row.getInt(4));
    assertEquals(1009, row.getInt(5));

    select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_ttl WHERE ttl(v2) > 250";
    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(0, rows.size());
  }

  @Test(expected=InvalidQueryException.class)
  public void testTtlOfCollectionsThrowsError() throws Exception {
    int []ttls = {100};
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl(h int, v1 list<int>, v2 int, primary key (h));";
    session.execute(create_stmt);

    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, [1], 1) using ttl 100;";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v1) < 150";
    session.execute(select_stmt);
  }

  @Test(expected=InvalidQueryException.class)
  public void testTtlOfPrimaryThrowsError() throws Exception {
    int []ttls = {100};
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl(h int, v1 list<int>, v2 int, primary key (h));";
    session.execute(create_stmt);
    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, [1], 1) using ttl 100;";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(h) < 150";
    session.execute(select_stmt);
  }

  @Test(expected=InvalidQueryException.class)
  public void testTtlWrongParametersThrowsError() throws Exception {
    int []ttls = {100};
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl(h int, v1 int, v2 int, primary key (h));";
    session.execute(create_stmt);

    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, 1, 1) using ttl 100;";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl() < 150";
    session.execute(select_stmt);
  }

  @Test
  public void testTtlOfDefault() throws Exception {
    LOG.info("CREATE TABLE test_ttl");

    String create_stmt = "CREATE TABLE test_ttl (h int, v1 list<int>, v2 int, primary key (h)) " +
                         "with default_time_to_live = 100;";
    session.execute(create_stmt);
    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, [1], 1);";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v2) <= 100";
    ResultSet rs = session.execute(select_stmt);
    List<Row> rows = rs.all();
    assertEquals(1, rows.size());

    select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v2) >= 90";
    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(1, rows.size());

    String insert_stmt_2 = "INSERT INTO test_ttl (h, v1, v2) VALUES(2, [2], 2) using ttl 150;";
    session.execute(insert_stmt_2);
    select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v2) >= 140";
    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(1, rows.size());
  }

  @Test
  public void testTtlWhenNoneSpecified() throws Exception {
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl " +
                         " (h int, v1 list<int>, v2 int, primary key (h));";
    session.execute(create_stmt);
    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, [1], 1);";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v2) > 100;";
    ResultSet rs = session.execute(select_stmt);
    List<Row> rows = rs.all();
    //The number of rows when we query ttl on v2 should be 0, since ttl(v2) isn't defined.
    assertEquals(0, rows.size());

    select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v2) <= 100";
    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(0, rows.size());
  }

  protected static enum UseIndex { ON, OFF }

  private void runPartitionHashTest(String func_name, UseIndex use_index) throws Exception {
    LOG.info(String.format("TEST %s - Start with using-index=%s", func_name, use_index));
    final String tbl_name = func_name + "_test_tbl";

    if (use_index == UseIndex.ON) {
      session.execute(
          "CREATE TABLE " + tbl_name + " (h1 int, h2 text, r1 int, r2 text," +
          "    v1 int, v2 text, PRIMARY KEY ((h1, h2), r1, r2))" +
          "    WITH CLUSTERING ORDER BY (r1 ASC, r2 DESC)" +
          "    AND transactions = {'enabled':'true'}");
      session.execute("CREATE INDEX idx ON " + tbl_name + " ((h1, r2), h2, r1)" +
                      "    WITH CLUSTERING ORDER BY (h2 DESC, r1 ASC)");
      waitForReadPermsOnAllIndexes(tbl_name);

      session.execute("INSERT INTO " + tbl_name + " (h1, h2, r1, r2, v1, v2)" +
                      "    VALUES (2, 'h2', 102, 'r102', 1002, 'v1002')");
    } else {
      setupTable(tbl_name, 10);
    }

    // Testing only basic token call as sanity check here.
    // Main token tests are in YbSqlQuery (C++) and TestBindVariable (Java) tests.
    assertQuery("SELECT * FROM " + tbl_name + " WHERE " +
                func_name + "(h1,h2) = " + func_name + "(2, 'h2')",
                "Row[2, h2, 102, r102, 1002, v1002]");
    // Use index-based scan-path.
    assertQuery("SELECT count(*) FROM " + tbl_name + " WHERE " +
                func_name + "(h1, h2) >= 0 AND " + func_name + "(h1, h2) <= 65535 " +
                "AND h1 = 0 AND r2 = 'text'", "Row[0]");

    // Try to run the function with the Primary Key from the index: (h1, r2).
    assertQueryError("SELECT count(*) FROM " + tbl_name + " WHERE " +
                     func_name + "(h1, r2)>=0 AND " + func_name + "(h1, r2)<=65535 " +
                     "AND h1=0 AND r2='text'",
                     "Invalid " + func_name + " call, found reference to unexpected column");

    LOG.info(String.format("TEST %s - End with using-index=%s", func_name, use_index));
  }

  @Test
  public void testToken() throws Exception {
    runPartitionHashTest("token", UseIndex.OFF);
  }

  @Test
  public void testPartitionHash() throws Exception {
    runPartitionHashTest("partition_hash", UseIndex.OFF);
  }

  @Test
  public void testTokenWithIndex() throws Exception {
    runPartitionHashTest("token", UseIndex.ON);
  }

  @Test
  public void testPartitionHashWithIndex() throws Exception {
    runPartitionHashTest("partition_hash", UseIndex.ON);
  }

  @Test
  public void testInKeyword() throws Exception {
    LOG.info("TEST IN KEYWORD - Start");
    setupTable("in_test", 10);

    // Test basic IN condition on hash column.
    {
      Iterator<Row> rows = session.execute("SELECT * FROM in_test WHERE " +
              "h1 IN (3, -1, 1, 7, 1) AND h2 in ('h7', 'h3', 'h1', 'h2')").iterator();

      // Check rows: expecting no duplicates and ascending order.
      assertTrue(rows.hasNext());
      assertEquals(1, rows.next().getInt("h1"));
      assertTrue(rows.hasNext());
      assertEquals(3, rows.next().getInt("h1"));
      assertTrue(rows.hasNext());
      assertEquals(7, rows.next().getInt("h1"));
      assertFalse(rows.hasNext());
    }

    // Test basic IN condition on range column.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE " +
                                     "r2 IN ('foo', 'r101','r103','r107')");
      Set<String> expected_values = new HashSet<>();
      expected_values.add("r101");
      expected_values.add("r103");
      expected_values.add("r107");
      // Check rows
      for (Row row : rs) {
        String r2 = row.getString("r2");
        assertTrue(expected_values.contains(r2));
        expected_values.remove(r2);
      }
      assertTrue(expected_values.isEmpty());
    }

    // Test basic IN condition on both hash and range columns.
    {
      String stmt = "SELECT h1 FROM in_test WHERE " +
          "h1 IN (3, -1, 4, 7, 1) AND h2 in ('h7', 'h3', 'h1', 'h4') AND " +
          "r1 IN (107, -100, 101, 104) and r2 IN ('r101', 'foo', 'r107', 'r104')";

      // Check rows: expecting no duplicates and ascending order.
      assertQueryRowsOrdered(stmt, "Row[1]", "Row[4]", "Row[7]");
    }

    // Test basic IN condition on regular column.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE v1 IN (1006, 1002, -1)");
      Set<Integer> expected_values = new HashSet<>();
      expected_values.add(1002);
      expected_values.add(1006);
      // Check rows
      for (Row row : rs) {
        Integer v1 = row.getInt("v1");
        assertTrue(expected_values.contains(v1));
        expected_values.remove(v1);
      }
      assertTrue(expected_values.isEmpty());
    }

    // Test multiple IN conditions.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE " +
              "h2 IN ('h1', 'h2', 'h7', 'h8') AND v2 in ('v1001', 'v1004', 'v1007')");
      // Since all values are unique we identify rows by the first hash column.
      Set<Integer> expected_values = new HashSet<>();
      expected_values.add(1);
      expected_values.add(7);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expected_values.contains(h1));
        expected_values.remove(h1);
      }
      assertTrue(expected_values.isEmpty());
    }

    // Test IN condition with single entry.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE h1 IN (4)");

      Set<Integer> expected_values = new HashSet<>();
      expected_values.add(4);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expected_values.contains(h1));
        expected_values.remove(h1);
      }
      assertTrue(expected_values.isEmpty());
    }

    // Test empty IN condition.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE h1 IN ()");
      assertFalse(rs.iterator().hasNext());

      rs = session.execute("SELECT * FROM in_test WHERE r2 IN ()");
      assertFalse(rs.iterator().hasNext());

      rs = session.execute("SELECT * FROM in_test WHERE v1 IN ()");
      assertFalse(rs.iterator().hasNext());
    }

    // Test NOT IN condition.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE " +
              "h1 NOT IN (0, 1, 3, -1, 4, 5, 7, -2)");
      Set<Integer> expected_values = new HashSet<>();
      expected_values.add(2);
      expected_values.add(6);
      expected_values.add(8);
      expected_values.add(9);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expected_values.contains(h1));
        expected_values.remove(h1);
      }
      assertTrue(expected_values.isEmpty());
    }

    // Test Invalid Statements.

    // Column cannot be restricted by more than one relation if it includes an IN
    runInvalidStmt("SELECT * FROM in_test WHERE h1 IN (1,2) AND h1 = 2");
    runInvalidStmt("SELECT * FROM in_test WHERE r1 IN (1,2) AND r1 < 2");
    runInvalidStmt("SELECT * FROM in_test WHERE v1 >= 2 AND v1 NOT IN (1,2)");
    runInvalidStmt("SELECT * FROM in_test WHERE v1 IN (1,2) AND v1 NOT IN (2,3)");

    // IN tuple elements must be convertible to column type.
    runInvalidStmt("SELECT * FROM in_test WHERE h1 IN (1.2,2.2)");
    runInvalidStmt("SELECT * FROM in_test WHERE h2 NOT IN ('a', 1)");

    LOG.info("TEST IN KEYWORD - End");
  }

  @Test
  public void testFilteringUsingIN() throws Exception {
    session.execute("CREATE TABLE test(h int, r int, v int, PRIMARY KEY (h, r))");
    session.execute("INSERT INTO test (h,r,v) values (1,1,1)");
    session.execute("INSERT INTO test (h,r,v) values (1,2,2)");
    session.execute("INSERT INTO test (h,r,v) values (1,3,3)");
    session.execute("INSERT INTO test (h,r,v) values (2,1,2)");
    session.execute("INSERT INTO test (h,r,v) values (2,2,3)");
    session.execute("INSERT INTO test (h,r,v) values (2,3,1)");
    session.execute("INSERT INTO test (h,r,v) values (3,1,3)");
    session.execute("INSERT INTO test (h,r,v) values (3,2,1)");
    session.execute("INSERT INTO test (h,r,v) values (3,3,2)");

    // Filter by hash & range columns.
    assertQuery("SELECT * FROM test WHERE h IN (1,2) AND r IN (1,2)",
        "Row[1, 1, 1]" +
        "Row[1, 2, 2]" +
        "Row[2, 1, 2]" +
        "Row[2, 2, 3]");
    // Filter by hash & non-key columns.
    assertQuery("SELECT * FROM test WHERE h IN (1,2) AND v IN (1,2)",
        "Row[1, 1, 1]" +
        "Row[1, 2, 2]" +
        "Row[2, 1, 2]" +
        "Row[2, 3, 1]");
    assertQuery("SELECT * FROM test WHERE h IN (1,2) AND v = 2",
        "Row[1, 2, 2]" +
        "Row[2, 1, 2]");
    // Filter by range & non-key columns.
    assertQuery("SELECT * FROM test WHERE r IN (1,2) AND v IN (1,2)",
        "Row[1, 1, 1]" +
        "Row[1, 2, 2]" +
        "Row[2, 1, 2]" +
        "Row[3, 2, 1]");
    assertQuery("SELECT * FROM test WHERE r IN (1,2) AND v = 2",
        "Row[1, 2, 2]" +
        "Row[2, 1, 2]");
    // Filter by hash & range & non-key columns.
    assertQuery("SELECT * FROM test WHERE h IN (1,2) AND r IN (1,2) AND v IN (1,2)",
        "Row[1, 1, 1]" +
        "Row[1, 2, 2]" +
        "Row[2, 1, 2]");
    assertQuery("SELECT * FROM test WHERE h IN (1,2) AND r IN (1,2) AND v = 2",
        "Row[1, 2, 2]" +
        "Row[2, 1, 2]");
    // Filter by hash & range columns + IF by non-key columns.
    assertQuery("SELECT * FROM test WHERE h IN (1,2) AND r IN (1,2) IF v IN (1,2)",
        "Row[1, 1, 1]" +
        "Row[1, 2, 2]" +
        "Row[2, 1, 2]");
    assertQuery("SELECT * FROM test WHERE h IN (1,2) AND r IN (1,2) IF v = 2",
        "Row[1, 2, 2]" +
        "Row[2, 1, 2]");
  }

  private void assertSelectWithFlushes(String stmt,
                                       List<String> expectedRows,
                                       int expectedFlushesCount) throws Exception {
    assertSelectWithFlushes(new SimpleStatement(stmt), expectedRows, expectedFlushesCount);
  }

  private void assertSelectWithFlushes(Statement stmt,
                                       List<String> expectedRows,
                                       int expectedFlushesCount) throws Exception {

    // Get the initial metrics.
    Map<MiniYBDaemon, Metrics> beforeMetrics = getAllMetrics();

    List<Row> rows = session.execute(stmt).all();

    // Get the after metrics.
    Map<MiniYBDaemon, Metrics> afterMetrics = getAllMetrics();

    // Check the result.
    assertEquals(expectedRows, rows.stream().map(Row::toString).collect(Collectors.toList()));

    // Check the metrics.
    int numFlushes = 0;
    int numSelects = 0;
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      numFlushes += afterMetrics.get(ts).getHistogram(TSERVER_FLUSHES_METRIC).totalSum -
          beforeMetrics.get(ts).getHistogram(TSERVER_FLUSHES_METRIC).totalSum;
      numSelects += afterMetrics.get(ts).getHistogram(TSERVER_SELECT_METRIC).totalCount -
          beforeMetrics.get(ts).getHistogram(TSERVER_SELECT_METRIC).totalCount;
    }

    // We could have a node-refresh even in the middle of this select, triggering selects to the
    // system tables -- but each of those should do exactly one flush. Therefore, we subtract
    // the extra selects (if any) from numFlushes.
    numFlushes -= numSelects - 1;
    assertEquals(expectedFlushesCount, numFlushes);
  }

  @Test
  public void testLargeParallelIn() throws Exception {
    LOG.info("TEST IN KEYWORD - Start");

    // Setup the table.
    session.execute("CREATE TABLE parallel_in_test(h1 int, h2 int, r1 int, v1 int," +
                        "PRIMARY KEY ((h1, h2), r1))");

    PreparedStatement insert = session.prepare(
        "INSERT INTO parallel_in_test(h1, h2, r1, v1) VALUES (?, ?, 1, 1)");

    for (Integer h1 = 0; h1 < 20; h1++) {
      for (Integer h2 = 0; h2 < 20; h2++) {
        session.execute(insert.bind(h1, h2));
      }
    }

    // SELECT statement: 200 (10 * 10 * 2), 100 with actual results (i.e. for r1 = 1).
    // We expect 100 internal queries, one for each hash key option (the 2 range key options are
    // handled in one multi-point query).
    String select = "SELECT * FROM parallel_in_test WHERE " +
        "h1 IN (0,2,4,6,8,10,12,14,16,18) AND h2 IN (1,3,5,7,9,11,13,15,17,19) AND " +
        "r1 IN (0,1)";

    // Compute expected rows.
    List<String> expectedRows = new ArrayList<>();
    String rowTemplate = "Row[%d, %d, 1, 1]";
    for (int h1 = 0; h1 < 20; h1 += 2) {
      for (int h2 = 1; h2 < 20; h2 += 2) {
        expectedRows.add(String.format(rowTemplate, h1, h2));
      }
    }

    // Test normal query: expect parallel execution.
    assertSelectWithFlushes(select, expectedRows, /* expectedFlushesCount = */1);

    // Test limit greater or equal than max results (200): expect parallel execution.
    assertSelectWithFlushes(select + " LIMIT 200", expectedRows, /* expectedFlushesCount = */1);

    // Test limit smaller than max results (200): expect serial execution.
    assertSelectWithFlushes(select + " LIMIT 199", expectedRows, /* expectedFlushesCount = */100);

    // Test page size equal to max results: expect parallel execution.
    SimpleStatement stmt = new SimpleStatement(select);
    stmt.setFetchSize(200);
    assertSelectWithFlushes(stmt, expectedRows, /* expectedFlushesCount = */1);

    // Test offset clause: always use serial execution with offset.
    assertSelectWithFlushes(select + " OFFSET 1", expectedRows.subList(1, expectedRows.size()),
        /* expectedFlushesCount = */100);
  }

  @Test
  public void testClusteringInSparseData() throws Exception {
    String createStmt = "CREATE TABLE in_sparse_cols_test(" +
        "h int, r1 int, r2 int, r3 int, r4 int, r5 int, v int, " +
        "PRIMARY KEY (h, r1, r2, r3, r4, r5)) " +
        "WITH CLUSTERING ORDER BY (r1 DESC, r2 DESC, r3 DESC, r4 DESC, r5 DESC);";
    session.execute(createStmt);

    // Testing sparse table data with dense in queries, load only 5 rows.
    String insertTemplate = "INSERT INTO in_sparse_cols_test(h, r1, r2, r3, r4, r5, v) " +
        "VALUES (1, %d, %d, %d, %d, %d, 100)";
    for (int i = 0; i < 50; i += 10) {
      session.execute(String.format(insertTemplate, i % 50, (i + 10) % 50, (i + 20) % 50,
                                    (i + 30) % 50, (i + 40) % 50));
    }

    // Up to 16 opts per column so around 1 mil total options.
    String allOpts = "(-1, -3, 0, 4, 8, 10, 12, 20, 21, 22, 30, 32, 37, 40, 41, 43)";
    String partialOpts = "(8, 10, 12, 20, 21, 22, 30, 32, 33, 37)";
    String selectTemplate = "SELECT * FROM in_sparse_cols_test WHERE h = 1 AND " +
        "r1 IN %s AND r2 IN %s AND r3 IN %s AND r4 IN %s AND r5 IN %s";


    // Test basic select.
    assertQueryRowsOrdered(
        String.format(selectTemplate, allOpts, allOpts, allOpts, allOpts, allOpts),
        "Row[1, 40, 0, 10, 20, 30, 100]",
        "Row[1, 30, 40, 0, 10, 20, 100]",
        "Row[1, 20, 30, 40, 0, 10, 100]",
        "Row[1, 10, 20, 30, 40, 0, 100]",
        "Row[1, 0, 10, 20, 30, 40, 100]");

    // Test reverse order.
    assertQueryRowsOrdered(String.format(selectTemplate + " ORDER BY r1 ASC",
                                         allOpts, allOpts, allOpts, allOpts, allOpts),
                           "Row[1, 0, 10, 20, 30, 40, 100]",
                           "Row[1, 10, 20, 30, 40, 0, 100]",
                           "Row[1, 20, 30, 40, 0, 10, 100]",
                           "Row[1, 30, 40, 0, 10, 20, 100]",
                           "Row[1, 40, 0, 10, 20, 30, 100]");

    // Test partial options (missing 0 and 40 for r1)
    assertQueryRowsOrdered(
        String.format(selectTemplate, partialOpts, allOpts, allOpts, allOpts, allOpts),
        "Row[1, 30, 40, 0, 10, 20, 100]",
        "Row[1, 20, 30, 40, 0, 10, 100]",
        "Row[1, 10, 20, 30, 40, 0, 100]");

    // Test partial options (missing 0 and 40 for r5) plus reverse order.
    assertQueryRowsOrdered(String.format(selectTemplate + " ORDER BY r1 ASC",
                                         allOpts, allOpts, allOpts, allOpts, partialOpts),
        "Row[1, 20, 30, 40, 0, 10, 100]",
        "Row[1, 30, 40, 0, 10, 20, 100]",
        "Row[1, 40, 0, 10, 20, 30, 100]");

    // Test partial options (missing 0 and 40 for r3 and r4)
    assertQueryRowsOrdered(
        String.format(selectTemplate, allOpts, allOpts, partialOpts, partialOpts, allOpts),
        "Row[1, 40, 0, 10, 20, 30, 100]",
        "Row[1, 0, 10, 20, 30, 40, 100]");
  }

  @Test
  public void testClusteringInSeeks() throws Exception {
    String createTable = "CREATE TABLE in_range_test(h int, r1 int, r2 text, v int," +
            " PRIMARY KEY((h), r1, r2)) WITH CLUSTERING ORDER BY (r1 DESC, r2 ASC)";
    session.execute(createTable);

    String insertTemplate = "INSERT INTO in_range_test(h, r1, r2, v) VALUES (%d, %d, '%d', %d)";

    for (int h = 0; h < 10; h++) {
      for (int r1 = 0; r1 < 10; r1++) {
        for (int r2 = 0; r2 < 10; r2++) {
          int v = h * 100 + r1 * 10 + r2;
          // Multiplying range keys by 10 so we can test sparser data with dense keys later.
          // (i.e. several key options given by IN condition, in between two actual rows).
          session.execute(String.format(insertTemplate, h, r1 * 10, r2 * 10, v));
        }
      }
    }

    // Test basic IN results and ordering.
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND r1 IN (60, 80, 10) AND " +
              "r2 IN ('70', '30')";

      String[] rows = {"Row[1, 80, 30, 183]",
              "Row[1, 80, 70, 187]",
              "Row[1, 60, 30, 163]",
              "Row[1, 60, 70, 167]",
              "Row[1, 10, 30, 113]",
              "Row[1, 10, 70, 117]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // 3 * 2 = 6 options.
      assertEquals(6, metrics.seekCount);
    }

    // Test IN results and ordering with non-existing keys.
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND r1 IN (70, -10, 20) AND " +
              "r2 IN ('40', '10', '-10')";

      String[] rows = {"Row[1, 70, 10, 171]",
              "Row[1, 70, 40, 174]",
              "Row[1, 20, 10, 121]",
              "Row[1, 20, 40, 124]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // 9 options, but some seeks should jump over options due to SeekPossiblyUsingNext
      // optimisation.
      assertEquals(5, metrics.seekCount);
    }

    // Test combining IN and equality conditions.
    {
      String query =
              "SELECT * FROM in_range_test WHERE h = 1 AND r1 IN (80, -10, 0, 30) AND r2 = '50'";

      String[] rows = {"Row[1, 80, 50, 185]",
              "Row[1, 30, 50, 135]",
              "Row[1, 0, 50, 105]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // 1 * 4 = 4 options.
      assertEquals(4, metrics.seekCount);
    }

    // Test using a partial specification of range key
    {
        String query =
            "SELECT * FROM in_range_test WHERE h = 1 AND r1 IN (80, 30)";

        String[] rows = {"Row[1, 80, 0, 180]",
                        "Row[1, 80, 10, 181]",
                        "Row[1, 80, 20, 182]",
                        "Row[1, 80, 30, 183]",
                        "Row[1, 80, 40, 184]",
                        "Row[1, 80, 50, 185]",
                        "Row[1, 80, 60, 186]",
                        "Row[1, 80, 70, 187]",
                        "Row[1, 80, 80, 188]",
                        "Row[1, 80, 90, 189]",
                        "Row[1, 30, 0, 130]",
                        "Row[1, 30, 10, 131]",
                        "Row[1, 30, 20, 132]",
                        "Row[1, 30, 30, 133]",
                        "Row[1, 30, 40, 134]",
                        "Row[1, 30, 50, 135]",
                        "Row[1, 30, 60, 136]",
                        "Row[1, 30, 70, 137]",
                        "Row[1, 30, 80, 138]",
                        "Row[1, 30, 90, 139]"};
        RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query,
        rows);
        // seeking to 2 places
        // Seeking to DocKey(0x0a73, [1], [80, kLowest])
        // Seeking to DocKey(0x0a73, [1], [30, kLowest])
        assertEquals(2, metrics.seekCount);
    }

    // Test ORDER BY clause with IN (reverse scan).
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND " +
              "r1 IN (70, 20) AND r2 IN ('40', '10') ORDER BY r1 ASC, r2 DESC";

      String[] rows = {"Row[1, 20, 40, 124]",
              "Row[1, 20, 10, 121]",
              "Row[1, 70, 40, 174]",
              "Row[1, 70, 10, 171]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // 4 options, but reverse scans do 2 seeks for each option since PrevDocKey calls Seek twice
      // internally.
      assertEquals(8, metrics.seekCount);
    }

    // Test single IN option (equivalent to just using equality constraint).
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND r1 IN (90) AND r2 IN ('40')";

      String[] rows = {"Row[1, 90, 40, 194]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      assertEquals(1, metrics.seekCount);
    }

    // Test dense IN target keys (with sparse table rows).
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND " +
              "r1 IN (57, 59, 60, 61, 63, 65, 67, 73, 75, 80, 82, 83) AND " +
              "r2 IN ('18', '19', '20', '23', '27', '31', '36', '40', '42', '43')";

      String[] rows = {"Row[1, 80, 20, 182]",
              "Row[1, 80, 40, 184]",
              "Row[1, 60, 20, 162]",
              "Row[1, 60, 40, 164]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // There are 12 * 10 = 120 total target keys, but we should skip most of them as one seek in
      // the DB will invalidate (jump over) several target keys:
      // 1. Initialize start seek target as smallest target key.
      // 2. Seek for current target key (will find the first DB key equal or bigger than target).
      // 3. If that matches the current (or a bigger) target key we add to the result.
      // 4. We continue seeking from the next target key.
      // Note that r1 is sorted DESC, and r2 is sorted ASC, so e.g. [83, "18"] is the smallest key
      // Seek No.   Seek For       Find       Matches
      //    1      [83, "18"]    [80, "0"]       N
      //    2      [80, "18"]    [80, "20"]      Y (Result row 1)
      //    3      [80, "23"]    [80, "30"]      N
      //    4      [80, "31"]    [80, "40"]      Y (Result row 2)
      //    5      [80, "42"]    [80, "50"]      N
      //    6      [75, "18"]    [70, "0"]       N
      //    7      [67, "18"]    [60, "0"]       N
      //    8      [60, "18"]    [60, "20"]      Y (Result row 3)
      //    9      [60, "23"]    [60, "30"]      N
      //   10      [60, "31"]    [60, "40"]      Y (Result row 4)
      //   11      [60, "42"]    [60, "50"]      N
      //   12      [59, "18"]    [50, "0"]       N (Bigger than largest target key so we are done)
      // Actual number of seeks could be lower because there is Next instead of Seek optimisation in
      // DocDB (see SeekPossiblyUsingNext).
      assertLessThanOrEqualTo(metrics.seekCount, 12);
    }
  }

  @Test
  public void testSeekWithRangeFilter() throws Exception {
    String createTable = "CREATE TABLE in_range_test(h int, r1 int, r2 text, v int," +
        " PRIMARY KEY((h), r1, r2)) WITH CLUSTERING ORDER BY (r1 DESC, r2 ASC)";
    session.execute(createTable);

    String insertTemplate = "INSERT INTO in_range_test(h, r1, r2, v) VALUES (%d, %d, '%d', %d)";

    for (int h = 0; h < 10; h++) {
      for (int r1 = 0; r1 < 10; r1++) {
        for (int r2 = 0; r2 < 10; r2++) {
          int v = h * 100 + r1 * 10 + r2;
          // Multiplying range keys by 10 so we can test sparser data with dense keys later.
          // (i.e. several key options given by IN condition, in between two actual rows).
          session.execute(String.format(insertTemplate, h, r1 * 10, r2 * 10, v));
        }
      }
    }

    // Test basic seek optimisation with fwd scans.
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND r1 > 50 AND r1 < 90 AND " +
              "r2  > '20' AND r2 < '50'";

      String[] rows = {
              "Row[1, 80, 30, 183]",
              "Row[1, 80, 40, 184]",
              "Row[1, 70, 30, 173]",
              "Row[1, 70, 40, 174]",
              "Row[1, 60, 30, 163]",
              "Row[1, 60, 40, 164]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // There are n = 3 values of r1 to look at (80, 70, 60).
      // For each r1 we have m = 2 values to look for in the range (30, 40). But, we only
      // seek to the very first one.
      // Then do Next(s) until we get out of range for r2.
      // If there are more r1's to look at, we'd seek to r2=Max.
      // We will be performing (n - 1) seeks to the Max value for finding the next r1
      // We also do one seek per r1 to get the initial r2 value in range.
      // We also do an initial seek to find the first valid value for r1
      // Thus, this scan will have to Seek to n + n + 1 = 3 + 3 + 1 = 7
      // locations.
      // For example,
      //   Seeking to DocKey(0x0a73, [1], [90, +Inf])
      //   Seeking to DocKey(0x0a73, [1], [80, "20", +Inf])
      //   Seeking to DocKey(0x0a73, [1], [80, +Inf])
      //   Seeking to DocKey(0x0a73, [1], [70, "20", +inf])
      //   Seeking to DocKey(0x0a73, [1], [70, +Inf])
      //   Seeking to DocKey(0x0a73, [1], [60, "20", +inf])
      //   Seeking to DocKey(0x0a73, [1], [60, +Inf])
      assertEquals(7, metrics.seekCount);
    }

    // Test basic seek optimisation with fwd scans.
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND r1 >= 60 AND r1 <= 80 AND " +
              "r2  >= '30' AND r2 <= '40'";

      String[] rows = {
              "Row[1, 80, 30, 183]",
              "Row[1, 80, 40, 184]",
              "Row[1, 70, 30, 173]",
              "Row[1, 70, 40, 174]",
              "Row[1, 60, 30, 163]",
              "Row[1, 60, 40, 164]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // There are n = 3 values of r1 to look at (80, 70, 60).
      // For each r1 we have m = 2 values to look for in the range (30, 40). But, we only
      // seek to the very first one. Then do Next(s) until we get out of range for r2.
      // If there are more r1's to look at, we'd seek to r2=Max.
      // We will be performing (n - 1) seeks to the Max value for finding the next r1
      // Thus, this scan will have to Seek to n * 1 + (n - 1) = 3 * 1 + (3 - 1) = 5 locations.
      assertEquals(5, metrics.seekCount);
    }

    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND r1 > 60 AND r1 < 80 AND " +
                     "r2  > '30' AND r2 < '40'";

      String[] rows = {};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // We first will seek to (60, Inf) to find a plausible value for r1
      // Then we seek to (70, 30, Inf) to get a plausible r2 value
      // Then we seek to (70, Inf) in hopes of getting another plausible r1
      // value
      assertEquals(3, metrics.seekCount);
    }

    // Test basic seek optimisation with fwd scans. No hash componenet specified.
    {
      String query = "SELECT * FROM in_range_test WHERE r1 = 80 AND r2 = '90'";

      String[] rows = {
              "Row[0, 80, 90, 89]",
              "Row[1, 80, 90, 189]",
              "Row[2, 80, 90, 289]",
              "Row[3, 80, 90, 389]",
              "Row[4, 80, 90, 489]",
              "Row[5, 80, 90, 589]",
              "Row[6, 80, 90, 689]",
              "Row[7, 80, 90, 789]",
              "Row[8, 80, 90, 889]",
              "Row[9, 80, 90, 989]"
      };

      // use unordered because the hash keys could go in random order.
      RocksDBMetrics metrics = assertUnorderedPartialRangeSpec("in_range_test", query, rows);
      // For each Hash key in 0 .. 9 we'll have 2 of these seeks.
      // Seeking to DocKey(0x0a73, [h], [80, "90"])
      // Seeking to DocKey(0x0a73, [h], [+Inf])
      // Additionally, one
      //   Seeking to DocKey([], []) per tablet.
      // Overall, 2 * 10 + 9
      assertEquals(29, metrics.seekCount);
    }

    {
      String query =
              "SELECT * FROM in_range_test WHERE r1 > 50 AND r1 < 90 AND r2  > '20' AND r2 < '50'";

      String[] rows = {
              "Row[0, 80, 30, 83]",
              "Row[0, 80, 40, 84]",
              "Row[0, 70, 30, 73]",
              "Row[0, 70, 40, 74]",
              "Row[0, 60, 30, 63]",
              "Row[0, 60, 40, 64]",
              "Row[1, 80, 30, 183]",
              "Row[1, 80, 40, 184]",
              "Row[1, 70, 30, 173]",
              "Row[1, 70, 40, 174]",
              "Row[1, 60, 30, 163]",
              "Row[1, 60, 40, 164]",
              "Row[2, 80, 30, 283]",
              "Row[2, 80, 40, 284]",
              "Row[2, 70, 30, 273]",
              "Row[2, 70, 40, 274]",
              "Row[2, 60, 30, 263]",
              "Row[2, 60, 40, 264]",
              "Row[3, 80, 30, 383]",
              "Row[3, 80, 40, 384]",
              "Row[3, 70, 30, 373]",
              "Row[3, 70, 40, 374]",
              "Row[3, 60, 30, 363]",
              "Row[3, 60, 40, 364]",
              "Row[4, 80, 30, 483]",
              "Row[4, 80, 40, 484]",
              "Row[4, 70, 30, 473]",
              "Row[4, 70, 40, 474]",
              "Row[4, 60, 30, 463]",
              "Row[4, 60, 40, 464]",
              "Row[5, 80, 30, 583]",
              "Row[5, 80, 40, 584]",
              "Row[5, 70, 30, 573]",
              "Row[5, 70, 40, 574]",
              "Row[5, 60, 30, 563]",
              "Row[5, 60, 40, 564]",
              "Row[6, 80, 30, 683]",
              "Row[6, 80, 40, 684]",
              "Row[6, 70, 30, 673]",
              "Row[6, 70, 40, 674]",
              "Row[6, 60, 30, 663]",
              "Row[6, 60, 40, 664]",
              "Row[7, 80, 30, 783]",
              "Row[7, 80, 40, 784]",
              "Row[7, 70, 30, 773]",
              "Row[7, 70, 40, 774]",
              "Row[7, 60, 30, 763]",
              "Row[7, 60, 40, 764]",
              "Row[8, 80, 30, 883]",
              "Row[8, 80, 40, 884]",
              "Row[8, 70, 30, 873]",
              "Row[8, 70, 40, 874]",
              "Row[8, 60, 30, 863]",
              "Row[8, 60, 40, 864]",
              "Row[9, 80, 30, 983]",
              "Row[9, 80, 40, 984]",
              "Row[9, 70, 30, 973]",
              "Row[9, 70, 40, 974]",
              "Row[9, 60, 30, 963]",
              "Row[9, 60, 40, 964]"
      };

      // use unordered because the hash keys could go in random order.
      RocksDBMetrics metrics = assertUnorderedPartialRangeSpec("in_range_test", query, rows);
      // For each Hash key in 0 .. 9 we'll have 8 of these seeks.
      // Seeking to DocKey(0x0a73, [h], [90, +Inf])
      // Seeking to DocKey(0x0a73, [h], [80, "20", +Inf])
      // Seeking to DocKey(0x0a73, [h], [80, +Inf])
      // Seeking to DocKey(0x0a73, [h], [70, "20", +Inf])
      // Seeking to DocKey(0x0a73, [h], [70, +Inf])
      // Seeking to DocKey(0x0a73, [h], [60, "20", +Inf])
      // Seeking to DocKey(0x0a73, [h], [60, +Inf])
      // Seeking to DocKey(0x0a73, [h], [+Inf])
      // Additionally, one
      //   Seeking to DocKey([], []) per tablet.
      // Overall, 8 * 10 + 9
      assertEquals(89, metrics.seekCount);
    }

    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND " +
                     "r1 >= 20 AND r1 < 40 AND r2  > '20' AND r2 < '50'";

      String[] rows = {"Row[1, 30, 30, 133]",
                       "Row[1, 30, 40, 134]",
                       "Row[1, 20, 30, 123]",
                       "Row[1, 20, 40, 124]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // Similar to above.
      // There are n = 2 values of r1 to look at. For each r1 we have m = 2
      // values to look for in the range. During the scan, for each r1
      // we look at m + 1 = 3 values before deciding to seek out of r1 by
      // going to r2=Max. We will be performing n seeks to the Max value for
      // finding the next r1.
      // Thus, this scan will have to Seek to
      // n * (m + 1) + (n - 1) = 2 * 3 + 2 = 8 locations.
      // But reverse scans do 2 seeks for each option
      // since PrevDocKey calls Seek twice internally.
      // So, the expected number of seeks = 8 * 2 = 16
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20,
      //                        kString : "50"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20,
      //                        kString : "40"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20,
      //                        kString : "30"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20,
      //                        kString : "20"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20,
      //                        kString : "10"]), []))
      // Trying to seek out of r1 = 20. [1, 20, _]
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30,
      //                        kString : "90"]), []))
      // Try to get into the range for r2.
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30,
      //                        kString : "50"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30,
      //                        kString : "40"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30,
      //                        kString : "30"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30,
      //                        kString : "20"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30,
      //                        kString : "10"]), []))
      // Trying to seek out of r1 = 30. [1, 30, _]
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 40,
      //                        kString : "90"]), []))
      // Try to get into the range for r2.
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 40,
      //                        kString : "50"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 40,
      //                        kString : "40"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 40,
      //                        kString : "30"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 40,
      //                        kString : "20"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 40,
      //                        kString : "10"]), []))
      assertEquals(4, metrics.seekCount);
    }

    // Test ORDER BY clause (reverse scan).
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND " +
              "r1 >= 20 AND r1 <= 30 AND r2  >= '30' AND r2 <= '40' ORDER BY r1 ASC, r2 DESC";

      String[] rows = {
              "Row[1, 20, 40, 124]",
              "Row[1, 20, 30, 123]",
              "Row[1, 30, 40, 134]",
              "Row[1, 30, 30, 133]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // There are n = 2 values of r1 to look at. For each r1 we have m = 2 values to look for
      // in the range. During the scan, for each r1 we look at m + 1 = 3 values before deciding
      // to seek out of r1 by going to r2=Max. We will be performing (n - 1) seeks to the Max
      // value for finding the next r1.
      // Thus, this scan will have to Seek to n * (m + 1) + (n - 1) = 7 locations.
      // But reverse scans do 2 seeks for each option since PrevDocKey calls Seek twice internally.
      // So the total number of seeks will be 7 * 2 = 14
      //Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20, kString : "40"]), []))
      //Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20, kString : "30"]), []))
      //Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20, kString : "20"]), []))
      // Trying to seek out of r1 = 20. [1, 20, _]
      //Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30, kString : "90"]), []))
      // Try to get into the range for r2.
      //Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30, kString : "40"]), []))
      //Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30, kString : "30"]), []))
      //Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30, kString : "20"]), []))
      assertEquals(10, metrics.seekCount);
    }

    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND " +
                     "r1 >= 20 AND r1 < 30 AND r2  > '30' AND r2 <= '40' ORDER BY r1 ASC, r2 DESC";

      String[] rows = {"Row[1, 20, 40, 124]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // There are n = 1 values of r1 to look at. For each r1 we have m = 1 values to look for
      // in the range. During the scan, for each r1 we look at m + 1 = 3 values before deciding
      // to seek out of r1 by going to r2=Max. We will be performing n seeks to the Max
      // value for finding the next r1.
      // Thus, this scan will have to Seek to n * (m + 1) + n = 3 locations.
      // But reverse scans do 2 seeks for each option since PrevDocKey
      // calls Seek twice internally.
      // So the total number of seeks will be 3 * 2 = 6
      // Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20,
      //                       kString : "40"]), []))
      // Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20,
      //                       kLowest"]), []))
      // Trying to seek out of r1 = 20. [1, 20, _]
      // Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30,
      //                       kString : "90"]), []))

      assertEquals(6, metrics.seekCount);
    }

    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND " +
                     "r1 >= 20 AND r1 < 40 AND r2  > '20' AND r2 < '50' ORDER BY r1 ASC, r2 DESC";

      String[] rows = {
              "Row[1, 20, 40, 124]",
              "Row[1, 20, 30, 123]",
              "Row[1, 30, 40, 134]",
              "Row[1, 30, 30, 133]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // Similar to above.
      // There are n = 2 values of r1 to look at. For each r1 we have m = 2 values to look for in
      // the range. During the scan, for each r1 we look at m + 1 = 3 values before deciding to
      // seek out of r1 by going to r2=Max
      // We will be performing n seeks to the Max value for finding the next r1
      // Thus, this scan will have to Seek to n * (m + 1) + n = 2 * 3 + 2 = 8 locations.
      // But reverse scans do 2 seeks for each option since PrevDocKey calls Seek twice internally.
      // So, the expected number of seeks = 8 * 2 = 16
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20,
      //                        kString : "50"], kLowest), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20,
      //                        kString : "40"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20,
      //                        kString : "30"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 20,
      //                        kLowest]), []))
      // Trying to seek out of r1 = 20. [1, 20, _]
      // Try to get into the range for r2.
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30,
      //                        kString : "50"], kLowest), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30,
      //                        kString : "40"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30,
      //                        kString : "30"]), []))
      //  Seek(SubDocKey(DocKey(0x1210, [kInt32 : 1], [kInt32Descending : 30,
      //                        kLowest), []))
      assertEquals(16, metrics.seekCount);
    }
  }

  @Test
  public void testStatementList() throws Exception {
    // Verify handling of empty statements.
    assertEquals(0, session.execute("").all().size());
    assertEquals(0, session.execute(";").all().size());
    assertEquals(0, session.execute("  ;  ;  ").all().size());

    // Verify handling of multi-statement (not supported yet).
    setupTable("test_select", 0);
    runInvalidStmt("SELECT * FROM test_select; SELECT * FROM test_select;");
  }

  private RocksDBMetrics assertUnorderedPartialRangeSpec(String tableName,
                                                         String query,
                                                         String... rows)
      throws Exception {
    return assertPartialRangeSpecOrderedOrUnorderd(/* ordered */ false,
                                                   tableName, query, rows);
  }

  private RocksDBMetrics assertPartialRangeSpec(String tableName, String query,
                                                String... rows)
      throws Exception {
    return assertPartialRangeSpecOrderedOrUnorderd(/* ordered */ true,
                                                   tableName, query, rows);
  }

  private RocksDBMetrics
  assertPartialRangeSpecOrderedOrUnorderd(boolean ordered, String tableName,
                                          String query, String... rows)
      throws Exception {
    RocksDBMetrics beforeMetrics = getRocksDBMetric(tableName);
    LOG.info(tableName + " metric before: " + beforeMetrics);
    if (ordered) {
      assertQueryRowsOrdered(query, rows);
    } else {
      assertQueryRowsUnordered(query, rows);
    }
    RocksDBMetrics afterMetrics = getRocksDBMetric(tableName);
    LOG.info(tableName + " metric after: " + afterMetrics);
    return afterMetrics.subtract(beforeMetrics);
  }

  @Test
  public void testPartialRangeSpec() throws Exception {
    {
      // Create test table and populate data.
      session.execute("CREATE TABLE test_range (h INT, r1 TEXT, r2 INT, c INT, " +
                      "PRIMARY KEY ((h), r1, r2));");
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 5; j++) {
          for (int k = 1; k <= 3; k++) {
            session.execute("INSERT INTO test_range (h, r1, r2, c) VALUES (?, ?, ?, ?);",
                            Integer.valueOf(i), "r" + j, Integer.valueOf(k), Integer.valueOf(k));
          }
        }
      }

      // Specify only r1 range in SELECT. Verify result.
      String query = "SELECT * FROM test_range WHERE h = 2 AND r1 >= 'r2' AND r1 <= 'r3';";
      String[] rows = {"Row[2, r2, 1, 1]",
                       "Row[2, r2, 2, 2]",
                       "Row[2, r2, 3, 3]",
                       "Row[2, r3, 1, 1]",
                       "Row[2, r3, 2, 2]",
                       "Row[2, r3, 3, 3]"};
      RocksDBMetrics metrics1 = assertPartialRangeSpec("test_range", query, rows);

      // Insert some more rows
      for (int i = 1; i <= 3; i++) {
        for (int j = 6; j <= 10; j++) {
          for (int k = 1; k <= 3; k++) {
            session.execute("INSERT INTO test_range (h, r1, r2, c) VALUES (?, ?, ?, ?);",
                            Integer.valueOf(i), "r" + j, Integer.valueOf(k), Integer.valueOf(k));
          }
        }
      }

      // Specify only r1 range in SELECT again. Verify result.
      RocksDBMetrics metrics2 = assertPartialRangeSpec("test_range", query, rows);

      // Verify that the seek/next metrics is the same despite more rows in the range.
      assertEquals(metrics1, metrics2);

      session.execute("DROP TABLE test_range;");
    }

    {
      // Create test table and populate data.
      session.execute("CREATE TABLE test_range (h INT, r1 INT, r2 TEXT, r3 INT, c INT, " +
                      "PRIMARY KEY ((h), r1, r2, r3));");
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 5; j++) {
          for (int k = 1; k <= 3; k++) {
            for (int l = 1; l <= 5; l++) {
              session.execute("INSERT INTO test_range (h, r1, r2, r3, c) VALUES (?, ?, ?, ?, ?);",
                              Integer.valueOf(i),
                              Integer.valueOf(j),
                              "r" + k,
                              Integer.valueOf(l),
                              Integer.valueOf(l));
            }
          }
        }
      }

      // Specify only r1 and r3 ranges in SELECT. Verify result.
      String query = "SELECT * FROM test_range WHERE " +
                     "h = 2 AND r1 >= 2 AND r1 <= 3 AND r3 >= 4 and r3 <= 5;";
      String[] rows = {"Row[2, 2, r1, 4, 4]",
                       "Row[2, 2, r1, 5, 5]",
                       "Row[2, 2, r2, 4, 4]",
                       "Row[2, 2, r2, 5, 5]",
                       "Row[2, 2, r3, 4, 4]",
                       "Row[2, 2, r3, 5, 5]",
                       "Row[2, 3, r1, 4, 4]",
                       "Row[2, 3, r1, 5, 5]",
                       "Row[2, 3, r2, 4, 4]",
                       "Row[2, 3, r2, 5, 5]",
                       "Row[2, 3, r3, 4, 4]",
                       "Row[2, 3, r3, 5, 5]"};
      RocksDBMetrics metrics1 = assertPartialRangeSpec("test_range", query, rows);

      // Insert some more rows
      for (int i = 1; i <= 3; i++) {
        for (int j = 6; j <= 10; j++) {
          for (int k = 1; k <= 3; k++) {
            for (int l = 1; l <= 5; l++) {
              session.execute("INSERT INTO test_range (h, r1, r2, r3, c) VALUES (?, ?, ?, ?, ?);",
                              Integer.valueOf(i),
                              Integer.valueOf(j),
                              "r" + k,
                              Integer.valueOf(l),
                              Integer.valueOf(l));
            }
          }
        }
      }

      // Specify only r1 range in SELECT again. Verify result.
      RocksDBMetrics metrics2 = assertPartialRangeSpec("test_range", query, rows);

      // Verify that the seek/next metrics is the same despite more rows in the range.
      assertEquals(metrics1, metrics2);

      session.execute("DROP TABLE test_range;");
    }
  }

  // This test is to check that SELECT expression is supported. Currently, only TTL and WRITETIME
  // are available.  We use TTL() function here.
  public void testSelectTtl() throws Exception {
    LOG.info("TEST SELECT TTL - Start");

    // Setup test table.
    int[] ttls = {
      100,
      200,
      300,
      400,
      500,
      600,
      700,
      800,
      900,
    };
    setupTable("test_ttl", ttls);
    Thread.sleep(1000);

    // Select data from the test table.
    String select_stmt = "SELECT ttl(v1) FROM test_ttl;";
    ResultSet rs = session.execute(select_stmt);
    List<Row> rows = rs.all();
    assertEquals(ttls.length, rows.size());

    for (int i = 0; i < rows.size(); i++) {
      Row row = rows.get(i);
      LOG.info("Selected TTL value is " + row.getLong(0));

      // Because ORDER BY is not yet supported, we cannot assert row by row.
      assertTrue(999 >= row.getLong(0));
    }
  }

  @Test
  public void testInvalidSelectQuery() throws Exception {
    session.execute("CREATE TABLE t (a int, primary key (a));");
    runInvalidStmt("SELECT FROM t;");
    runInvalidStmt("SELECT t;");
  }

  @Test
  public void testQualifiedColumnReference() throws Exception {

    setupTable("test_select", 0);

    // Verify qualified name for column reference is disallowed.
    runInvalidStmt("SELECT t.h1 FROM test_select;");
    runInvalidStmt("INSERT INTO test_select (t.h1) VALUES (1);");
    runInvalidStmt("UPDATE test_select SET t.h1 = 1;");
    runInvalidStmt("DELETE t.h1 FROM test_select;");
  }

  @Test
  public void testScanLimitsWithToken() throws Exception {
    // The main test for 'token' scans is ql-query-test.cc/TestScanWithBounds which checks that the
    // correct results are returned.
    // Therefore, it ensures that we are hitting all the right tablets.
    // However, hitting extra tablets (outside the scan range) never yields any new results, so that
    // test would not catch redundant tablet reads.
    //
    // This test only checks that the expected number of partitions (tablets) are hit when doing
    // table scans with upper/lower bounds via 'token'.
    // Since we know all the needed ones are hit (based on ql-query-test.cc/TestScanWithBounds),
    // this ensures we are not doing redundant reads.
    // TODO (Mihnea) Find a way to integrate these two tests into one.

    session.execute("CREATE TABLE test_token_limits(h int primary key, v int);");

    // Wait to ensure the partitions metadata was updated.
    // Schema change should trigger a refresh but playing it safe in case debouncing will delay it.
    Thread.sleep(2 * MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);

    // Get the number of partitions of the source table.
    TableSplitMetadata tableSplitMetadata =
        cluster.getMetadata().getTableSplitMetadata(DEFAULT_TEST_KEYSPACE, "test_token_limits");
    Integer[] keys = tableSplitMetadata.getPartitionMap().navigableKeySet().toArray(new Integer[0]);
    // Need at least 3 partitions for this test to make sense -- current default for tests is 9.
    assertTrue(keys.length >= 3);

    PreparedStatement select =
        session.prepare("SELECT * FROM test_token_limits where token(h) >= ? and token(h) < ?");

    // Scan [first, last) partitions interval -- should hit all partitions except last.
    {
      // Get the initial metrics.
      Map<MiniYBDaemon, IOMetrics> initialMetrics = getTSMetrics();
      session.execute(select.bind(PartitionAwarePolicy.YBToCqlHashCode(keys[0]),
                                  PartitionAwarePolicy.YBToCqlHashCode(keys[keys.length - 1])));
      // Check the metrics again.
      IOMetrics totalMetrics = getCombinedMetrics(initialMetrics);
      // Check all but one partitions were hit.
      assertEquals(keys.length - 1, totalMetrics.readCount());
    }

    // Scan [first, second) partitions interval -- should hit just first partition.
    {
      // Get the initial metrics.
      Map<MiniYBDaemon, IOMetrics> initialMetrics = getTSMetrics();

      // Execute query.
      session.execute(select.bind(PartitionAwarePolicy.YBToCqlHashCode(keys[0]),
                                  PartitionAwarePolicy.YBToCqlHashCode(keys[1])));
      // Check the metrics again.
      IOMetrics totalMetrics = getCombinedMetrics(initialMetrics);
      // Check only one partition was hit.
      assertEquals(1, totalMetrics.readCount());
    }

    // Scan [second-to-last, last) partitions interval -- should hit just second-to-last partition.
    {
      // Get the initial metrics.
      Map<MiniYBDaemon, IOMetrics> initialMetrics = getTSMetrics();
      // Execute query.
      session.execute(select.bind(PartitionAwarePolicy.YBToCqlHashCode(keys[keys.length - 2]),
                                  PartitionAwarePolicy.YBToCqlHashCode(keys[keys.length - 1])));
      // Get the metrics again.
      IOMetrics totalMetrics = getCombinedMetrics(initialMetrics);
      // Check only one partition was hit.
      assertEquals(1, totalMetrics.readCount());
    }
  }

  private void selectAndVerify(String query, String result)  {
    assertEquals(result, session.execute(query).one().getString(0));
  }

  private void selectAndVerify(String query, int result)  {
    assertEquals(result, session.execute(query).one().getInt(0));
  }

  private void selectAndVerify(String query, short result)  {
    assertEquals(result, session.execute(query).one().getShort(0));
  }

  private void selectAndVerify(String query, long result)  {
    assertEquals(result, session.execute(query).one().getLong(0));
  }

  private void selectAndVerify(String query, float result)  {
    assertEquals(result, session.execute(query).one().getFloat(0), 1e-13);
  }

  private void selectAndVerify(String query, double result)  {
    assertEquals(result, session.execute(query).one().getDouble(0), 1e-13);
  }

  private void selectAndVerify(String query, Date result)  {
    assertEquals(result, session.execute(query).one().getTimestamp(0));
  }

  private void selectAndVerify(String query, LocalDate result)  {
    assertEquals(result, session.execute(query).one().getDate(0));
  }

  @Test
  public void testIntegerBounds() throws Exception {
    session.execute("CREATE TABLE test_int_bounds(h int primary key, " +
        "t tinyint, s smallint, i int, b bigint)");

    String insertStmt = "INSERT INTO test_int_bounds(h, %s) VALUES (1, %s)";

    // Test upper bounds.
    session.execute(String.format(insertStmt, "t", "127"));
    session.execute(String.format(insertStmt, "s", "32767"));
    session.execute(String.format(insertStmt, "i", "2147483647"));
    session.execute(String.format(insertStmt, "b", "9223372036854775807"));
    assertQuery("SELECT t, s, i, b FROM test_int_bounds WHERE h = 1",
        "Row[127, 32767, 2147483647, 9223372036854775807]");

    runInvalidStmt(String.format(insertStmt, "t", "128"));
    runInvalidStmt(String.format(insertStmt, "s", "32768"));
    runInvalidStmt(String.format(insertStmt, "i", "2147483648"));
    runInvalidStmt(String.format(insertStmt, "b", "9223372036854775808"));

    // Test lower bounds.
    session.execute(String.format(insertStmt, "t", "-128"));
    session.execute(String.format(insertStmt, "s", "-32768"));
    session.execute(String.format(insertStmt, "i", "-2147483648"));
    session.execute(String.format(insertStmt, "b", "-9223372036854775808"));
    assertQuery("SELECT t, s, i, b FROM test_int_bounds WHERE h = 1",
        "Row[-128, -32768, -2147483648, -9223372036854775808]");

    runInvalidStmt(String.format(insertStmt, "t", "-129"));
    runInvalidStmt(String.format(insertStmt, "s", "-32769"));
    runInvalidStmt(String.format(insertStmt, "i", "-2147483649"));
    runInvalidStmt(String.format(insertStmt, "b", "-9223372036854775809"));
  }

  @Test
  public void testCasts() throws Exception {
    // Create test table.
    session.execute("CREATE TABLE test_local (c1 int PRIMARY KEY, c2 float, c3 double, c4 " +
        "smallint, c5 bigint, c6 text, c7 date, c8 time, c9 timestamp);");
    session.execute("INSERT INTO test_local (c1, c2, c3, c4, c5, c6, c7, c8, c9) values " +
        "(1, 2.5, 3.3, 4, 5, '100', '2018-2-14', '1:2:3.123456789', " +
        "'2018-2-14 13:24:56.987+01:00')");
    selectAndVerify("SELECT CAST(c1 as integer) FROM test_local", 1);
    selectAndVerify("SELECT CAST(c1 as int) FROM test_local", 1);
    selectAndVerify("SELECT CAST(c1 as smallint) FROM test_local", (short)1);
    selectAndVerify("SELECT CAST(c1 as bigint) FROM test_local", 1L);
    selectAndVerify("SELECT CAST(c1 as float) FROM test_local", 1.0f);
    selectAndVerify("SELECT CAST(c1 as double) FROM test_local", 1.0d);
    selectAndVerify("SELECT CAST(c1 as text) FROM test_local", "1");

    selectAndVerify("SELECT CAST(c2 as integer) FROM test_local", 2);
    selectAndVerify("SELECT CAST(c2 as smallint) FROM test_local", (short)2);
    selectAndVerify("SELECT CAST(c2 as bigint) FROM test_local", 2L);
    selectAndVerify("SELECT CAST(c2 as double) FROM test_local", 2.5d);
    selectAndVerify("SELECT CAST(c2 as text) FROM test_local", "2.500000");

    selectAndVerify("SELECT CAST(c3 as float) FROM test_local", 3.3f);
    selectAndVerify("SELECT CAST(c3 as integer) FROM test_local", 3);
    selectAndVerify("SELECT CAST(c3 as bigint) FROM test_local", 3L);
    selectAndVerify("SELECT CAST(c3 as smallint) FROM test_local", (short)3);
    selectAndVerify("SELECT CAST(c3 as text) FROM test_local", "3.300000");

    selectAndVerify("SELECT CAST(c4 as float) FROM test_local", 4f);
    selectAndVerify("SELECT CAST(c4 as integer) FROM test_local", 4);
    selectAndVerify("SELECT CAST(c4 as bigint) FROM test_local", 4L);
    selectAndVerify("SELECT CAST(c4 as smallint) FROM test_local", (short)4);
    selectAndVerify("SELECT CAST(c4 as double) FROM test_local", 4d);
    selectAndVerify("SELECT CAST(c4 as text) FROM test_local", "4");

    selectAndVerify("SELECT CAST(c5 as float) FROM test_local", 5f);
    selectAndVerify("SELECT CAST(c5 as integer) FROM test_local", 5);
    selectAndVerify("SELECT CAST(c5 as bigint) FROM test_local", 5L);
    selectAndVerify("SELECT CAST(c5 as smallint) FROM test_local", (short)5);
    selectAndVerify("SELECT CAST(c5 as double) FROM test_local", 5d);
    selectAndVerify("SELECT CAST(c5 as text) FROM test_local", "5");

    selectAndVerify("SELECT CAST(c6 as float) FROM test_local", 100f);
    selectAndVerify("SELECT CAST(c6 as integer) FROM test_local", 100);
    selectAndVerify("SELECT CAST(c6 as bigint) FROM test_local", 100L);
    selectAndVerify("SELECT CAST(c6 as smallint) FROM test_local", (short)100);
    selectAndVerify("SELECT CAST(c6 as double) FROM test_local", 100d);
    selectAndVerify("SELECT CAST(c6 as text) FROM test_local", "100");

    selectAndVerify("SELECT CAST(c7 as timestamp) FROM test_local",
        new SimpleDateFormat("yyyy-MM-dd Z").parse("2018-02-14 +0000"));
    selectAndVerify("SELECT CAST(c7 as text) FROM test_local", "2018-02-14");

    selectAndVerify("SELECT CAST(c8 as text) FROM test_local", "01:02:03.123456789");

    selectAndVerify("SELECT CAST(c9 as date) FROM test_local",
        LocalDate.fromYearMonthDay(2018, 2, 14));
    selectAndVerify("SELECT CAST(c9 as text) FROM test_local",
        "2018-02-14T12:24:56.987000+0000");

    // Test aliases and related functions of CAST.
    selectAndVerify("SELECT TODATE(c9) FROM test_local",
        LocalDate.fromYearMonthDay(2018, 2, 14));
    selectAndVerify("SELECT TOTIMESTAMP(c7) FROM test_local",
        new SimpleDateFormat("yyyy-MM-dd Z").parse("2018-02-14 +0000"));
    selectAndVerify("SELECT TOUNIXTIMESTAMP(c7) FROM test_local", 1518566400000L);
    selectAndVerify("SELECT TOUNIXTIMESTAMP(c9) FROM test_local", 1518611096987L);

    // Try edge cases.
    session.execute("INSERT INTO test_local (c1, c2, c3, c4, c5, c6) values (2147483647, 2.5, " +
        "3.3, 32767, 9223372036854775807, '2147483647')");
    selectAndVerify("SELECT CAST(c1 as int) FROM test_local WHERE c1 = 2147483647", 2147483647);
    selectAndVerify("SELECT CAST(c1 as bigint) FROM test_local WHERE c1 = 2147483647", 2147483647L);
    selectAndVerify("SELECT CAST(c1 as smallint) FROM test_local WHERE c1 = 2147483647",
        (short)2147483647);
    selectAndVerify("SELECT CAST(c5 as int) FROM test_local WHERE c1 = 2147483647",
        (int)9223372036854775807L);
    selectAndVerify("SELECT CAST(c5 as smallint) FROM test_local WHERE c1 = 2147483647",
        (short)9223372036854775807L);
    selectAndVerify("SELECT CAST(c6 as smallint) FROM test_local WHERE c1 = 2147483647",
        (short)2147483647);
    selectAndVerify("SELECT CAST(c6 as int) FROM test_local WHERE c1 = 2147483647",
        2147483647);
    selectAndVerify("SELECT CAST(c6 as bigint) FROM test_local WHERE c1 = 2147483647",
        2147483647L);
    selectAndVerify("SELECT CAST(c6 as text) FROM test_local WHERE c1 = 2147483647",
        "2147483647");
    selectAndVerify("SELECT CAST(c5 as text) FROM test_local WHERE c1 = 2147483647",
        "9223372036854775807");

    // Verify invalid CAST target type.
    runInvalidQuery("SELECT CAST(c1 as unixtimestamp) FROM test_local");
  }

  @Test
  public void testCurrentTimeFunctions() throws Exception {
    // Create test table and insert with current date/time/timestamp functions.
    session.execute("create table test_current (k int primary key, d date, t time, ts timestamp);");
    session.execute("insert into test_current (k, d, t, ts) values " +
                    "(1, currentdate(), currenttime(), currenttimestamp());");

    // Verify date, time and timestamp to be with range.
    LocalDate d = session.execute("select d from test_current").one().getDate("d");
    long date_diff = java.time.temporal.ChronoUnit.DAYS.between(
        java.time.LocalDate.ofEpochDay(d.getDaysSinceEpoch()),
        java.time.LocalDateTime.now(java.time.ZoneOffset.UTC).toLocalDate());
    assertTrue("Current date is " + d, date_diff >= 0 && date_diff <= 1);

    long t = session.execute("select t from test_current").one().getTime("t");
    long nowTime = java.time.LocalTime.now(java.time.ZoneOffset.UTC).toNanoOfDay();
    if (nowTime < t) { // Handle day wrap.
      nowTime += 86400000000000L;
    }
    long time_diff_sec = (nowTime - t) / 1000000000;
    assertTrue("Current time is " + t, time_diff_sec >= 0 && time_diff_sec <= 60);

    Date ts = session.execute("select ts from test_current").one().getTimestamp("ts");
    long timestamp_diff_sec = (System.currentTimeMillis() - ts.getTime()) / 1000;
    assertTrue("Current timestamp is " + ts, timestamp_diff_sec >= 0 && timestamp_diff_sec <= 60);
  }

  @Test
  public void testDistinct() throws Exception {
    // Create test table with hash/range and static/non-static columns.
    session.execute("create table test_distinct (h int, r int, s int static, c int," +
                    " primary key ((h), r));");

    // Verify that the WHERE clause of a SELECT DISTINCT allows reference to hash/static column
    // but not range or non-static columns.
    assertQuery("select distinct h, s from test_distinct where h = 0;", "");
    assertQuery("select distinct h, s from test_distinct where s > 0;", "");
    runInvalidQuery("select distinct h, s from test_distinct where r > 0;");
    runInvalidQuery("select distinct h, s from test_distinct where c < 0;");
  }

  @Test
  public void testDistinctPushdown() throws Exception {
    session.execute("create table t(h int, c int, primary key(h, c))");
    session.execute("insert into t(h, c) values (0, 0)");
    session.execute("insert into t(h, c) values (0, 1)");
    session.execute("insert into t(h, c) values (0, 2)");
    session.execute("insert into t(h, c) values (1, 0)");
    session.execute("insert into t(h, c) values (1, 1)");
    session.execute("insert into t(h, c) values (1, 2)");

    // For both queries, the scan should jump directly to the relevant primary key,
    // so the number of seeks is equal to the items to be retrived.
    {
      String query = "select distinct h from t where h = 0";
      String[] rows = {"Row[0]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("t", query, rows);
      assertEquals(1, metrics.seekCount);
    }

    {
      String query = "select distinct h from t where h in (0, 1)";
      String[] rows = {"Row[0]", "Row[1]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("t", query, rows);
      assertEquals(2, metrics.seekCount);
    }
  }

  @Test
  public void testDistinctPushdownSecondColumn() throws Exception {
    session.execute("create table t(r1 int, r2 int, r3 int, primary key(r2, r3))");
    session.execute("insert into t(r1, r2, r3) values (0, 0, 0)");
    session.execute("insert into t(r1, r2, r3) values (0, 0, 1)");
    session.execute("insert into t(r1, r2, r3) values (0, 0, 2)");
    session.execute("insert into t(r1, r2, r3) values (1, 1, 0)");
    session.execute("insert into t(r1, r2, r3) values (1, 1, 1)");
    session.execute("insert into t(r1, r2, r3) values (1, 1, 2)");

    // For both queries, the scan should jump directly to the relevant primary key,
    // so the number of seeks is equal to the items to be retrived.
    {
      String query = "select distinct r2 from t where r2 = 0";
      String[] rows = {"Row[0]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("t", query, rows);
      assertEquals(1, metrics.seekCount);
    }

    {
      String query = "select distinct r2 from t where r2 in (0, 1)";
      String[] rows = {"Row[0]", "Row[1]"};

      RocksDBMetrics metrics = assertPartialRangeSpec("t", query, rows);
      assertEquals(2, metrics.seekCount);
    }
  }

  @Test
  public void testToJson() throws Exception {
    // Create test table.
    session.execute("CREATE TABLE test_tojson (c1 int PRIMARY KEY, c2 float, c3 double, c4 " +
        "smallint, c5 bigint, c6 text, c7 date, c8 time, c9 timestamp, c10 blob, " +
        "c11 tinyint, c12 inet, c13 varint, c14 decimal, c15 boolean, c16 uuid);");
    session.execute("INSERT INTO test_tojson " +
        "(c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14, c15, c16) values " +
        "(1, 2.5, 3.25, 4, 5, 'value', '2018-2-14', '1:2:3.123456789', " +
        "'2018-2-14 13:24:56.987+01:00', 0xDEADBEAF, -128, '1.2.3.4', -123456, " +
        "-123456.125, true, 87654321-DEAD-BEAF-0000-deadbeaf0000)");

    selectAndVerify("SELECT tojson(c1) FROM test_tojson", "1");
    selectAndVerify("SELECT toJson(c2) FROM test_tojson", "2.5");
    selectAndVerify("SELECT Tojson(c3) FROM test_tojson", "3.25");
    selectAndVerify("SELECT ToJson(c4) FROM test_tojson", "4");
    selectAndVerify("SELECT TOjson(c5) FROM test_tojson", "5");
    selectAndVerify("SELECT toJSON(c6) FROM test_tojson", "\"value\"");
    selectAndVerify("SELECT TOJSON(c7) FROM test_tojson", "\"2018-02-14\"");
    selectAndVerify("SELECT ToJsOn(c8) FROM test_tojson", "\"01:02:03.123456789\"");
    selectAndVerify("SELECT tOjSoN(c9) FROM test_tojson", "\"2018-02-14T12:24:56.987000+0000\"");
    selectAndVerify("SELECT TojsoN(c10) FROM test_tojson", "\"0xdeadbeaf\"");
    selectAndVerify("SELECT tOJSOn(c11) FROM test_tojson", "-128");
    selectAndVerify("SELECT tOjson(c12) FROM test_tojson", "\"1.2.3.4\"");
    selectAndVerify("SELECT toJson(c13) FROM test_tojson", "-123456");
    selectAndVerify("SELECT tojSon(c14) FROM test_tojson", "-123456.125");
    selectAndVerify("SELECT tojsOn(c15) FROM test_tojson", "true");
    selectAndVerify("SELECT tojsoN(c16) FROM test_tojson",
                    "\"87654321-dead-beaf-0000-deadbeaf0000\"");

    // Test NaN/Infinity.
    session.execute("INSERT INTO test_tojson (c1, c2, c3) values (2, NaN, nan)");
    session.execute("INSERT INTO test_tojson (c1, c2, c3) values (3, +NaN, +nan)");
    session.execute("INSERT INTO test_tojson (c1, c2, c3) values (4, -NaN, -nan)");
    session.execute("INSERT INTO test_tojson (c1, c2, c3) values (5, Infinity, infinity)");
    session.execute("INSERT INTO test_tojson (c1, c2, c3) values (6, +Infinity, +infinity)");
    session.execute("INSERT INTO test_tojson (c1, c2, c3) values (7, -Infinity, -infinity)");

    selectAndVerify("SELECT tojson(c2) FROM test_tojson where c1=2;", "null");
    selectAndVerify("SELECT tojson(c3) FROM test_tojson where c1=2;", "null");
    selectAndVerify("SELECT tojson(c2) FROM test_tojson where c1=3;", "null");
    selectAndVerify("SELECT tojson(c3) FROM test_tojson where c1=3;", "null");
    selectAndVerify("SELECT tojson(c2) FROM test_tojson where c1=4;", "null");
    selectAndVerify("SELECT tojson(c3) FROM test_tojson where c1=4;", "null");
    selectAndVerify("SELECT tojson(c2) FROM test_tojson where c1=5;", "null");
    selectAndVerify("SELECT tojson(c3) FROM test_tojson where c1=5;", "null");
    selectAndVerify("SELECT tojson(c2) FROM test_tojson where c1=6;", "null");
    selectAndVerify("SELECT tojson(c3) FROM test_tojson where c1=6;", "null");
    selectAndVerify("SELECT tojson(c2) FROM test_tojson where c1=7;", "null");
    selectAndVerify("SELECT tojson(c3) FROM test_tojson where c1=7;", "null");

    // No keyword 'Inf' (there is 'Infinity').
    runInvalidQuery("INSERT INTO test_tojson (c1, c2, c3) values (8, Inf, Inf)");

    // === TEST COLLECTIONS. ===
    session.execute("CREATE TABLE test_coll (h int PRIMARY KEY, s SET<int>, " +
                    "l list<int>, m map<int, int>)");
    session.execute("INSERT INTO test_coll (h, s, l, m) values (1, " +
                    "{11,22}, [33,44], {55:66,77:88})");
    selectAndVerify("SELECT tojson(h) FROM test_coll;", "1");
    selectAndVerify("SELECT tojson(s) FROM test_coll;", "[11,22]");
    selectAndVerify("SELECT tojson(l) FROM test_coll;", "[33,44]");
    selectAndVerify("SELECT tojson(m) FROM test_coll;", "{\"55\":66,\"77\":88}");

    // === TEST FROZEN. ===
    // Test SET<FROZEN<SET>.
    session.execute("CREATE TABLE test_frozen1 (h int PRIMARY KEY, " +
                    "f FROZEN<set<int>>, sf SET<FROZEN<set<int>>>)");
    session.execute("INSERT INTO test_frozen1 (h, f, sf) values (1, {33,44}, {{55,66}})");
    selectAndVerify("SELECT tojson(h) FROM test_frozen1;", "1");
    selectAndVerify("SELECT tojson(f) FROM test_frozen1", "[33,44]");
    selectAndVerify("SELECT tojson(sf) FROM test_frozen1", "[[55,66]]");

    // Test MAP<FROZEN<SET>:FROZEN<LIST>>.
    session.execute("CREATE TABLE test_frozen2 (h int PRIMARY KEY, " +
        "f map<frozen<set<text>>, frozen<list<int>>>)");
    session.execute("INSERT INTO test_frozen2 (h, f) values (1, " +
        "{{'a','b'}:[66,77,88]})");
    selectAndVerify("SELECT tojson(f) FROM test_frozen2",
        "{\"[\\\"a\\\",\\\"b\\\"]\":[66,77,88]}");

    // === TEST USER DEFINED TYPE. ===
    // Test UDT.
    session.execute("CREATE TYPE udt(v1 int, v2 int)");
    session.execute("CREATE TABLE test_udt (h int PRIMARY KEY, u udt, su SET<FROZEN<udt>>)");
    session.execute("INSERT INTO test_udt (h, u, su) values (1, {v1:11,v2:22}, {{v1:33,v2:44}})");
    selectAndVerify("SELECT tojson(h) FROM test_udt", "1");
    selectAndVerify("SELECT tojson(u) FROM test_udt", "{\"v1\":11,\"v2\":22}");
    selectAndVerify("SELECT tojson(su) FROM test_udt", "[{\"v1\":33,\"v2\":44}]");

    // Test FROZEN<UDT>.
    session.execute("CREATE TABLE test_udt2 (h int PRIMARY KEY, u frozen<udt>)");
    session.execute("INSERT INTO test_udt2 (h, u) values (1, {v1:33,v2:44})");
    selectAndVerify("SELECT tojson(u) FROM test_udt2", "{\"v1\":33,\"v2\":44}");

    // Test LIST<FROZEN<UDT>>.
    session.execute("CREATE TABLE test_udt3 (h int PRIMARY KEY, u list<frozen<udt>>)");
    session.execute("INSERT INTO test_udt3 (h, u) values (1, [{v1:44,v2:55}, {v1:66,v2:77}])");
    selectAndVerify("SELECT tojson(u) FROM test_udt3",
        "[{\"v1\":44,\"v2\":55},{\"v1\":66,\"v2\":77}]");

    // Test MAP<FROZEN<UDT>:FROZEN<UDT>>.
    session.execute("CREATE TABLE test_udt4 (h int PRIMARY KEY, " +
        "u map<frozen<udt>, frozen<udt>>)");
    session.execute("INSERT INTO test_udt4 (h, u) values (1, " +
        "{{v1:44,v2:55}:{v1:66,v2:77}, {v1:88,v2:99}:{v1:11,v2:22}})");
    selectAndVerify("SELECT tojson(u) FROM test_udt4",
        "{\"{\\\"v1\\\":44,\\\"v2\\\":55}\":{\"v1\":66,\"v2\":77}," +
        "\"{\\\"v1\\\":88,\\\"v2\\\":99}\":{\"v1\":11,\"v2\":22}}");

    // Test MAP<FROZEN<LIST<FROZEN<UDT>>>:FROZEN<SET<FROZEN<UDT>>>>.
    session.execute("CREATE TABLE test_udt5 (h int PRIMARY KEY, " +
        "u map<frozen<list<frozen<udt>>>, frozen<set<frozen<udt>>>>)");
    session.execute("INSERT INTO test_udt5 (h, u) values (1, " +
        "{[{v1:44,v2:55}, {v1:66,v2:77}]:{{v1:88,v2:99},{v1:11,v2:22}}})");
    selectAndVerify("SELECT tojson(u) FROM test_udt5",
        "{\"[{\\\"v1\\\":44,\\\"v2\\\":55},{\\\"v1\\\":66,\\\"v2\\\":77}]\":" +
        "[{\"v1\":11,\"v2\":22},{\"v1\":88,\"v2\":99}]}");

    // Test MAP<FROZEN<MAP<FROZEN<UDT>:TEXT>>:FROZEN<SET<FROZEN<UDT>>>>.
    session.execute("CREATE TABLE test_udt6 (h int PRIMARY KEY, " +
        "u map<frozen<map<frozen<udt>, text>>, frozen<set<frozen<udt>>>>)");
    session.execute("INSERT INTO test_udt6 (h, u) values (1, " +
        "{{{v1:11,v2:22}:'text'}:{{v1:55,v2:66},{v1:77,v2:88}}})");
    selectAndVerify("SELECT tojson(u) FROM test_udt6",
        "{\"{\\\"{\\\\\\\"v1\\\\\\\":11,\\\\\\\"v2\\\\\\\":22}\\\":\\\"text\\\"}\":" +
        "[{\"v1\":55,\"v2\":66},{\"v1\":77,\"v2\":88}]}");

    // Test UDT with case-sensitive field names and names with spaces.
    session.execute("CREATE TYPE udt7(v1 int, \"V2\" int, \"v  3\" int, \"V  4\" int)");
    session.execute("CREATE TABLE test_udt7 (h int PRIMARY KEY, u udt7)");
    session.execute("INSERT INTO test_udt7 (h, u) values (1, " +
        "{v1:11,\"V2\":22,\"v  3\":33,\"V  4\":44})");
    selectAndVerify("SELECT tojson(h) FROM test_udt7", "1");
    // Verify that the column names in upper case are double quoted (see the case in Cassandra).
    selectAndVerify("SELECT tojson(u) FROM test_udt7",
        "{\"\\\"V  4\\\"\":44,\"\\\"V2\\\"\":22,\"v  3\":33,\"v1\":11}");

    // Test UDT field which refers to another user-defined type.
    session.execute("CREATE TYPE udt8(i1 int, u1 frozen<udt>)");
    session.execute("CREATE TABLE test_udt_in_udt (h int PRIMARY KEY, u udt8)");
    session.execute("INSERT INTO test_udt_in_udt (h, u) values (1, {i1:33,u1:{v1:44,v2:55}})");
    // Apply ToJson() to the UDT< FROZEN<UDT> > column.
    selectAndVerify("SELECT tojson(u) FROM test_udt_in_udt",
        "{\"i1\":33,\"u1\":{\"v1\":44,\"v2\":55}}");

    session.execute("DROP TABLE test_udt_in_udt");
    session.execute("DROP TYPE udt8"); // Unblock type 'udt' dropping.

    // Test UDT field which refers to a FROZEN<LIST>.
    session.execute("CREATE TYPE udt9(i1 int, l1 frozen<list<int>>)");
    session.execute("CREATE TABLE test_list_in_udt (h int PRIMARY KEY, u udt9)");
    session.execute("INSERT INTO test_list_in_udt (h, u) values (1, {i1:77,l1:[4,5,6]})");
    // Apply ToJson() to the UDT< FROZEN<LIST> > column.
    selectAndVerify("SELECT tojson(u) FROM test_list_in_udt",
        "{\"i1\":77,\"l1\":[4,5,6]}");

    // Test UDT field which refers to a FROZEN<SET>.
    session.execute("CREATE TYPE udt10(i1 int, s1 frozen<set<int>>)");
    session.execute("CREATE TABLE test_set_in_udt (h int PRIMARY KEY, u udt10)");
    session.execute("INSERT INTO test_set_in_udt (h, u) values (1, {i1:66,s1:{3,2,1}})");
    // Apply ToJson() to the UDT< FROZEN<SET> > column.
    selectAndVerify("SELECT tojson(u) FROM test_set_in_udt",
        "{\"i1\":66,\"s1\":[1,2,3]}");

    // Test UDT field which refers to a FROZEN<MAP>.
    session.execute("CREATE TYPE udt11(i1 int, m1 frozen<map<int, text>>)");
    session.execute("CREATE TABLE test_map_in_udt (h int PRIMARY KEY, u udt11)");
    session.execute("INSERT INTO test_map_in_udt (h, u) values (1, {i1:88,m1:{99:'t1',11:'t2'}})");
    // Apply ToJson() to the UDT< FROZEN<MAP> > column.
    selectAndVerify("SELECT tojson(u) FROM test_map_in_udt",
        "{\"i1\":88,\"m1\":{\"11\":\"t2\",\"99\":\"t1\"}}");

    // Test TUPLE.
    // Feature Not Supported
    // https://github.com/YugaByte/yugabyte-db/issues/936
    runInvalidQuery("CREATE TABLE test_tuple (h int PRIMARY KEY, t tuple<int>)");
    // Uncomment the following block if we support TUPLE.
    //    session.execute("CREATE TABLE test_tuple (h int PRIMARY KEY, t tuple<int, text>)");
    //    session.execute("INSERT INTO test_tuple (h, t) values (1, (77, 'string'))");
    //    selectAndVerify("SELECT tojson(u) FROM test_tuple", "[77,\"string\"]");

    // Test SELECT JSON *.
    // Feature Not Supported: Invalid SQL Statement. Syntax error.
    runInvalidQuery("SELECT JSON * FROM test_tojson");

    // Invalid test: FROZEN<int>.
    // Error: Invalid Table Definition. Can only freeze collections or user defined types.
    runInvalidQuery("CREATE TABLE invalid_frozen (h int PRIMARY KEY, u frozen<int>)");

    // Select from system tables.
    selectAndVerify("SELECT tojson(replication) from system_schema.keyspaces " +
        "where keyspace_name='system_schema'",
        "{\"class\":\"org.apache.cassandra.locator.SimpleStrategy\"," +
        "\"replication_factor\":\"3\"}");

    selectAndVerify("SELECT toJson(replication) as replication FROM system_schema.keyspaces " +
        "where keyspace_name='system'",
        "{\"class\":\"org.apache.cassandra.locator.SimpleStrategy\"," +
        "\"replication_factor\":\"3\"}");
  }

  @Test
  public void testInClauseWithTokenRange() throws Exception {
    // Test 1: #9032 Ensure all partitions honour token conditions.
    session.execute("create table test (h1 int primary key, v1 int);");
    session.execute("insert into test (h1, v1) values (1, 2)");
    session.execute("insert into test (h1, v1) values (2, 3)");
    session.execute("insert into test (h1, v1) values (5, 6)");

    Set<Long> partition_keys = new HashSet<Long>();
    for (Row row : session.execute("select h1, token(h1) from test;")) {
      partition_keys.add(row.getLong(1));
    }
    // We know for sure that the tokens for 1, 2, 5 are -7921831744544702464,
    // 4666855113862676480 and -8470426474153771008. We want to pick anything other than that.
    assertFalse(partition_keys.contains(new Long(1)));

    // Note that we test with 2 hash column values 2 and 5 after 1 in the IN clause. This is to
    // confirm that both the lower_bound and the upper_bound of token() range are passed on to later
    // requests for IN clause partitions. If we were to test with only in (1, 2), tests would still
    // pass if hypothetically the db code missed to pass the lower_bound to later requests.
    ResultSet res = session.execute("select * from test where token(h1) >= 1 and token(h1) <= 1 " +
      "and h1 in (1, 2, 5);");

    // The below assertion confirms that all point get queries (i.e., for h1 = 1, 2), the token()
    // conditions are honoured.
    assertTrue("No rows should be returned", res.all().isEmpty());

    // Test 2: Ensure that all partitions use the same token conditions
    // i.e., an op for one partition key might change the token range's lower and upper bound to be
    // the same as the partition key of that op (as an optimization). But ops for subsequent
    // partitions should use the original token ranges.
    //
    // For example, if the above statement didn't hold true, the op for h1=2 would use
    // lower and upper bound as -7921831744544702464 (which is the hash code for h1=1). And in
    // that case it would return just a single row i.e., for the first partition key h1=1.
    //
    // -7921831744544702464 is the hash code for 1
    // -4666855113862676480 is the hash code for 2
    assertQueryRowsUnordered("select * from test where token(h1) >= -7921831744544702464 " +
      "and token(h1) <= 4666855113862676480 and h1 in (1, 2);",
      "Row[1, 2]", "Row[2, 3]");
  }

  @Test
  public void testMultiColumnInSeeks() throws Exception {
    String createTable = "CREATE TABLE in_range_test(h int, r1 int, r2 text, v int," +
        " PRIMARY KEY((h), r1, r2)) WITH CLUSTERING ORDER BY (r1 DESC, r2 ASC)";
    session.execute(createTable);

    String insertTemplate = "INSERT INTO in_range_test(h, r1, r2, v) VALUES (%d, %d, '%d', %d)";

    for (int h = 0; h < 10; h++) {
      for (int r1 = 0; r1 < 10; r1++) {
        for (int r2 = 0; r2 < 10; r2++) {
          int v = h * 100 + r1 * 10 + r2;
          // Multiplying range keys by 10 so we can test sparser data with dense keys later.
          // (i.e. several key options given by IN condition, in between two actual rows).
          session.execute(String.format(insertTemplate, h, r1 * 10, r2 * 10, v));
        }
      }
    }

    // Test basic IN results and ordering.
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND (r1, r2) IN ((60, '70'), "
          + "(80, '70'), (10, '70'), (60, '30'), (80, '30'), (10, '30'))";

      String[] rows = { "Row[1, 80, 30, 183]",
          "Row[1, 80, 70, 187]",
          "Row[1, 60, 30, 163]",
          "Row[1, 60, 70, 167]",
          "Row[1, 10, 30, 113]",
          "Row[1, 10, 70, 117]" };

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      assertEquals(6, metrics.seekCount);
    }

    // Test IN results and ordering with non-existing keys.
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND (r1, r2) IN " +
          "((70,'40'), (-10,'40'), (20,'40'), (70,'10'), (-10,'10'), (20,'10'), (70,'-10'), " +
          "(-10,'-10'), (20,'-10'))";

      String[] rows = { "Row[1, 70, 10, 171]",
          "Row[1, 70, 40, 174]",
          "Row[1, 20, 10, 121]",
          "Row[1, 20, 40, 124]" };

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // 9 options, but the first seek should jump over 3 options (with r1 = -10).
      assertEquals(5, metrics.seekCount);
    }

    // Test ORDER BY clause with IN (reverse scan).
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND " +
          "(r1, r2) IN ((70,'40'), (70,'10'), (20,'40'),(20,'10')) ORDER BY r1 ASC, r2 DESC";

      String[] rows = { "Row[1, 20, 40, 124]",
          "Row[1, 20, 10, 121]",
          "Row[1, 70, 40, 174]",
          "Row[1, 70, 10, 171]" };

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // 4 options, but reverse scans do 2 seeks for each option since PrevDocKey
      // calls Seek twice
      // internally.
      assertEquals(8, metrics.seekCount);
    }

    // Test single IN option (equivalent to just using equality constraint).
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND (r1, r2) IN ((90,'40'))";

      String[] rows = { "Row[1, 90, 40, 194]" };

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      assertEquals(1, metrics.seekCount);
    }

    // Test dense IN target keys (with sparse table rows).
    {
      int r1Values[] = { 75, 80, 60, 83, 73, 65, 67, 63, 57, 59, 82, 61 };
      int r2Values[] = { 36, 19, 43, 23, 40, 31, 18, 27, 42, 20 };

      String values = "";
      for (int r1 : r1Values) {
        for (int r2 : r2Values) {
          values += String.format("(%d, '%d'), ", r1, r2);
        }
      }

      String query = "SELECT * FROM in_range_test WHERE h = 1 AND " +
          "(r1, r2) IN (" + values.substring(0, values.length() - 2) + ")";

      String[] rows = { "Row[1, 80, 20, 182]",
          "Row[1, 80, 40, 184]",
          "Row[1, 60, 20, 162]",
          "Row[1, 60, 40, 164]" };

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // There are 12 * 10 = 120 total target keys, but we should skip most of them as
      // one seek in
      // the DB will invalidate (jump over) several target keys:
      // 1. Initialize start seek target as smallest target key.
      // 2. Seek for current target key (will find the first DB key equal or bigger
      // than target).
      // 3. If that matches the current (or a bigger) target key we add to the result.
      // 4. We continue seeking from the next target key.
      // Note that r1 is sorted DESC, and r2 is sorted ASC, so e.g. [83, "18"] is the
      // smallest key
      // Seek No. Seek For Find Matches
      // 1 [83, "18"] [80, "0"] N
      // 2 [80, "18"] [80, "20"] Y (Result row 1)
      // 3 [80, "23"] [80, "30"] N
      // 4 [80, "31"] [80, "40"] Y (Result row 2)
      // 5 [80, "42"] [80, "50"] N
      // 6 [75, "18"] [70, "0"] N
      // 7 [67, "18"] [60, "0"] N
      // 8 [60, "18"] [60, "20"] Y (Result row 3)
      // 9 [60, "23"] [60, "30"] N
      // 10 [60, "31"] [60, "40"] Y (Result row 4)
      // 11 [60, "42"] [60, "50"] N
      // 12 [59, "18"] [50, "0"] N (Bigger than largest target key so we are done)
      // Actual number of seeks could be lower because there is Next instead of Seek optimisation in
      // DocDB (see SeekPossiblyUsingNext).
      assertLessThanOrEqualTo(metrics.seekCount, 12);
    }
  }


  @Test
  public void testMultiColumnInSeeks2() throws Exception {
    String createTable = "CREATE TABLE in_range_test(h int, r1 int, r2 text, r3 int," +
        " PRIMARY KEY((h), r1, r2, r3)) WITH CLUSTERING ORDER BY (r1 DESC, r2 ASC, r3 ASC)";
    session.execute(createTable);

    String insertTemplate = "INSERT INTO in_range_test(h, r1, r2, r3) VALUES (%d, %d, '%d', %d)";

    for (int h = 0; h < 10; h++) {
      for (int r1 = 0; r1 < 10; r1++) {
        for (int r2 = 0; r2 < 10; r2++) {
          for (int r3 = 0; r3 < 10; r3++) {
            session.execute(String.format(insertTemplate, h, r1 * 10, r2 * 10, r3 * 10));
          }
        }
      }
    }

    // Test combining IN and equality conditions.
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND " +
          "(r1, r2) IN ((80, '50'), (-10, '50'), (0, '50'), (30, '50')) AND r3 = 20";

      String[] rows = { "Row[1, 80, 50, 20]",
          "Row[1, 30, 50, 20]",
          "Row[1, 0, 50, 20]" };

      RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query, rows);
      // 1 * 4 = 4 options.
      assertEquals(4, metrics.seekCount);
    }

    // Test using a partial specification of range key
    {
      String query = "SELECT * FROM in_range_test WHERE h = 1 AND (r1, r2) IN ((80, '10'), " +
          "(30, '40'))";

      String[] rows = { "Row[1, 80, 10, 0]",
          "Row[1, 80, 10, 10]",
          "Row[1, 80, 10, 20]",
          "Row[1, 80, 10, 30]",
          "Row[1, 80, 10, 40]",
          "Row[1, 80, 10, 50]",
          "Row[1, 80, 10, 60]",
          "Row[1, 80, 10, 70]",
          "Row[1, 80, 10, 80]",
          "Row[1, 80, 10, 90]",
          "Row[1, 30, 40, 0]",
          "Row[1, 30, 40, 10]",
          "Row[1, 30, 40, 20]",
          "Row[1, 30, 40, 30]",
          "Row[1, 30, 40, 40]",
          "Row[1, 30, 40, 50]",
          "Row[1, 30, 40, 60]",
          "Row[1, 30, 40, 70]",
          "Row[1, 30, 40, 80]",
          "Row[1, 30, 40, 90]" };
        RocksDBMetrics metrics = assertPartialRangeSpec("in_range_test", query,
            rows);
        // seeking to 2 places
        // Seeking to DocKey(0x0a73, [1], [80, 10, kLowest])
        // Seeking to DocKey(0x0a73, [1], [30, 40, kLowest])
        assertEquals(2, metrics.seekCount);
    }
  }

  @Test
  public void testGroupBy() throws Exception
  {
    // Expect error in SELECT with GROUP BY.
    session.execute("CREATE TABLE test_tbl (id int primary key, v int);");
    runInvalidStmt("SELECT * FROM test_tbl GROUP BY v;");

    // Restart with GROUP BY queries with error suppressed.
    Map<String, String> flags = new HashMap<>();
    flags.put("ycql_suppress_group_by_error", "true");
    restartClusterWithTSFlags(flags);

    session.execute("CREATE TABLE test_tbl (id int primary key, v int);");
    session.execute("SELECT * FROM test_tbl GROUP BY v;");
  }
}
