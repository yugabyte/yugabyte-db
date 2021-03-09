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

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import org.junit.Test;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import org.apache.commons.lang3.ArrayUtils;

import java.net.InetAddress;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Calendar;
import java.util.GregorianCalendar;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.ThreadLocalRandom;
import java.util.Random;
import java.util.stream.*;
import java.util.TreeSet;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestOrderedColumns extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestOrderedColumns.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return 300;
  }

  private void createTable(String type1, String type2, String orderStmt, boolean invalid)
      throws Exception {
    String createStmt = String.format("CREATE TABLE test_create " +
                                      "(h1 int, r1 %s, r2 %s, v1 int, v2 varchar, " +
                                      "primary key((h1), r1, r2))%s;",
                                      type1, type2, orderStmt);
    LOG.info("createStmt: " + createStmt);
    if (invalid) {
      runInvalidStmt(createStmt);
    } else {
      session.execute(createStmt);
    }
  }

  private void createTable(String type1, String type2, String r1Order, String r2Order,
      boolean invalid) throws Exception {
    String orderStmt = "";
    if (r1Order != "") {
      orderStmt += "r1 " + r1Order;
    }
    if (r2Order != "") {
      if (orderStmt != "") {
        orderStmt += ", ";
      }
      orderStmt += "r2 " + r2Order;
    }
    if (orderStmt != "") {
      orderStmt = " WITH CLUSTERING ORDER BY(" + orderStmt + ")";
    }
    createTable(type1, type2, orderStmt, invalid);
  }

  private void dropTable() throws Exception {
    String drop_stmt = "DROP TABLE test_create;";
    session.execute(drop_stmt);
  }

  private String getColumnInString(Row row, String type, String column) throws Exception {
    switch(type) {
      case "bigint": return String.valueOf(row.getLong(column));
      case "int": return String.valueOf(row.getInt(column));
      case "smallint": return String.valueOf(row.getShort(column));
      case "tinyint": return String.valueOf(row.getByte(column));
      case "float": return String.format("%.2f", row.getFloat(column));
      case "double": return String.format("%.2f", row.getDouble(column));
      case "varchar": return "'" + row.getString(column) + "'";
      case "timestamp": return Long.toString(row.getTimestamp(column).getTime());
      case "inet": return row.getInet(column).toString()
          .replace("/", "'").replace(":0:0:0:", "::") + "'";
      case "uuid": return row.getUUID(column).toString();
      case "timeuuid": return row.getUUID(column).toString();
      default: throw new Exception("Invalid data type " + type);
    }
  }

  private String[] getValuesForType(String type) throws Exception {
    switch(type) {
      case "bigint": return new String[]
        {String.valueOf(Long.MIN_VALUE), "-100", "-1", "0", "1", String.valueOf(Long.MAX_VALUE)};
      case "int": return new String[]{"1", "2", "3", "4", "5", "16"};
      case "smallint": return new String[]
        {String.valueOf(Short.MIN_VALUE), "-100", "-1", "0", "1", String.valueOf(Short.MAX_VALUE)};
      case "tinyint": return new String[]
        {String.valueOf(Byte.MIN_VALUE), "-100", "-1", "0", "1", String.valueOf(Byte.MAX_VALUE)};
      case "float": return new String[]
        {"-101.09", "-5.00", "-0.01", "0.02", "12.10", "101.10"};
      case "double": return new String[]
        {"-101.09", "-5.00", "-0.01", "0.02", "12.10", "101.10"};
      case "varchar": return new String[]{"'s1'", "'s16'", "'s2'", "'s3'", "'s4'", "'s5'"};
      case "timestamp":
        long curTime = System.currentTimeMillis();
        return new String[]{Long.toString(curTime/5), Long.toString(curTime/4),
          Long.toString(curTime/3), Long.toString(curTime/2), Long.toString(curTime),
          Long.toString(curTime * 2)};
      case "inet": return new String[]{"'1.2.3.4'", "'180::2978:9018:b288:3f6c'", "'2.2.3.4'",
          "'2.2.3.5'"};
      case "uuid": return new String[]{
        "157c4b82-ff52-1053-bced-0eba570d969e",
        "157c4b82-ff32-1073-bced-0eba570d969e",
        "157c4b82-ff32-1075-bced-0eba570d969e",
        "a57c4b82-ff52-1173-bced-0eba570d969e",
        "167c4b82-ef22-1174-bced-0eba570d969e",
        "157c4b82-ef22-1274-bced-0eba570d969e",
        "a67c4b82-ef22-2173-bced-0eba570d969e",
        "c57c4b82-ef52-2073-aced-0eba570d969e",
        "167c4b82-ef22-3173-bced-0eba570d969e",
        "f67c4b82-ef22-3173-bced-0eba570d969e",
        "367c4b82-ef22-4173-bced-0eba570d969e",
        "467c4b82-ef22-4173-bced-0eba570d969e"};
      case "timeuuid": return new String[]{
        "040c318e-3422-11e7-a919-92ebcb67fe33",
        "36fa2f10-3422-11e7-b687-92ebcb67fe33",
        "5c077b28-3422-11e7-a919-92ebcb67fe33",
        "692c076a-3422-11e7-b687-92ebcb67fe33",};
      default: throw new Exception("Invalid data type " + type);
    }
  }

  // Create, insert, select, and verify the results for the given combination of arguments.
  // create_order and scan_order can take values "ASC", "DESC", "" (3rd option means unspecified)
  // r1BoundType and r2BoundType can be "EXC", "INC" or "", meaning the scan specifies boundary
  // conditions of the column: exclusive bounds, eg. > <, or inclusive bounds >= <=, or no bounds
  private void testCombination(String type1, String type2,
    String r1CreateOrder, String r2CreateOrder,
    String r1ScanOrder, String r2ScanOrder,
    String r1BoundType, String r2BoundType)
      throws Exception {

    LOG.info("Testing types " + type1 + ", " + type2 + "; Created in order " + r1CreateOrder + ", "
        + r2CreateOrder + "; Scanned in order " + r1ScanOrder + ", " + r2ScanOrder + "; Bounded "
        + r1BoundType + ", " + r2BoundType + " respectively.");

    String[] values1 = getValuesForType(type1);
    String[] values2 = getValuesForType(type2);

    if (r1CreateOrder == "" && r2CreateOrder != "") {
      // According to CQL specs, the order for clustering / range columns must be specified from
      // left to right without gaps.
      createTable(type1, type2, r1CreateOrder, r2CreateOrder, true);
      return;
    }

    createTable(type1, type2, r1CreateOrder, r2CreateOrder, false);

    for (String s : values1) {
      for (String t : values2) {
        String insertStmt = String.format("INSERT INTO test_create (h1, r1, r2, v1, v2) " +
                                          "VALUES (1, %s, %s, 1, 'c');", s, t);
        session.execute(insertStmt);
      }
    }

    int r1size = values1.length;
    int r2size = values2.length;

    String r1LowerBound = values1[1];
    String r1UpperBound = values1[r1size-2];
    String r2LowerBound = values2[1];
    String r2UpperBound = values2[r2size-2];

    String selectStmt = "SELECT h1, r1, r2, v1, v2 FROM test_create WHERE h1 = 1";
    if (r1BoundType == "EXC") {
      selectStmt += " AND r1 > " + r1LowerBound + " AND r1 < " + r1UpperBound;
    } else if(r1BoundType == "INC") {
      selectStmt += " AND r1 >= " + r1LowerBound + " AND r1 <= " + r1UpperBound;
    }
    if (r2BoundType == "EXC") {
      selectStmt += " AND r2 > " + r2LowerBound + " AND r2 < " + r2UpperBound;
    } else if(r2BoundType == "INC") {
      selectStmt += " AND r2 >= " + r2LowerBound + " AND r2 <= " + r2UpperBound;
    }

    String orderByClause = "";
    if (r1ScanOrder != "") {
      orderByClause += "r1 " + r1ScanOrder;
    }
    if (r2ScanOrder != "") {
      if (orderByClause != "") {
        orderByClause += ", ";
      }
      orderByClause +=  "r2 " + r2ScanOrder;
    }

    if (orderByClause != "") {
      orderByClause = " ORDER BY " + orderByClause;
    }

    selectStmt += orderByClause;

    boolean isR1Increasing = r1ScanOrder == "ASC" || (r1ScanOrder == "" && r1CreateOrder != "DESC");
    // According to CQL specs, if r2ScanOrder is unspecified, then it can depend on r1CreateOrder,
    // r1ScanOrder to make sure both rows are forward or both rows are backward.
    boolean isR2Increasing = r2ScanOrder == "ASC" || (r2ScanOrder == "" &&
      ((r2CreateOrder != "DESC") != ((r1CreateOrder != "DESC") != isR1Increasing)));

    if (r1ScanOrder == "" && r2ScanOrder != "") {
      // When r2 is specified but r1 not specified it is an error.
      runInvalidStmt(selectStmt);
      dropTable();
      return;
    }

    if ((isR1Increasing == (r1CreateOrder != "DESC")) !=
      (isR2Increasing == (r2CreateOrder != "DESC"))) {
      // If r1 is forward but r2 is not forward, or vice versa, it is an error.
      runInvalidStmt(selectStmt);
      dropTable();
      return;
    }

    LOG.info("SelectStatement: " + selectStmt);

    ResultSet rs = session.execute(selectStmt);

    String[] r1Vals, r2Vals;

    if (r1BoundType == "EXC") {
      r1Vals = Arrays.copyOfRange(values1, 2, r1size-2);
    } else if(r1BoundType == "INC") {
      r1Vals = Arrays.copyOfRange(values1, 1, r1size-1);
    } else {
      r1Vals = values1;
    }
    if (r2BoundType == "EXC") {
      r2Vals = Arrays.copyOfRange(values2, 2, r2size-2);
    } else if(r2BoundType == "INC") {
      r2Vals = Arrays.copyOfRange(values2, 1, r2size-1);
    } else {
      r2Vals = values2;
    }

    if (!isR1Increasing) {
      ArrayUtils.reverse(r1Vals);
    }

    if (!isR2Increasing) {
      ArrayUtils.reverse(r2Vals);
    }

    assertEquals(r1Vals.length * r2Vals.length, rs.getAvailableWithoutFetching());

    for (int i = 0; i < r1Vals.length; i++) {
      for (int j = 0; j < r2Vals.length; j++) {
        Row row = rs.one();
        assertEquals(1, row.getInt("h1"));
        assertEquals(r1Vals[i], getColumnInString(row, type1, "r1"));
        assertEquals(r2Vals[j], getColumnInString(row, type2, "r2"));
        LOG.info("rowResult: " + row.toString());
      }
    }

    dropTable();

    return;
  }

  // Takes an array options for the inputs
  // type1, type2, r1CreateOrder, r2CreateOrder, r1ScanOrder, r2ScanOrder, r1BoundType, r2BoundType
  // If an input is set to "ANY", a random arg or all possible args are tested, depending on
  // isRandom.
  private void testCombination(String[] options, boolean isRandom)
        throws Exception {
    assertEquals(8, options.length);
    for (int i = 0; i < options.length; i++) {
      if (options[i] == "ANY") {
        String[] choices;
        if (i < 2) {
          choices = new String[]{"bigint", "int", "smallint", "tinyint", "varchar", "float",
              "double", "timestamp", "inet", "uuid", "timeuuid"};
        } else if (i < 6) {
          choices = new String[]{"", "ASC", "DESC"};
        } else {
          choices = new String[]{"", "EXC", "INC"};
        }
        if (!isRandom) {
          for (String choice : choices) {
            String[] copyOptions = options.clone();
            copyOptions[i] = choice;
            testCombination(copyOptions, isRandom);
          }
          return;
        } else {
          int index = ThreadLocalRandom.current().nextInt(0, choices.length);
          options[i] = choices[index];
        }
      }
    }
    testCombination(
      options[0], options[1], options[2], options[3],
      options[4], options[5], options[6], options[7]);
  }

  // Test n random combinations using the function testRandomCombination() defined above
  private void testRandomCombinations(String[] options, int numCombinations)
          throws Exception {
    for(int i = 0; i < numCombinations; i++) {
      String[] copyOptions = options.clone();
      testCombination(copyOptions, true);
    }
  }

  private void testAllCombinations(String[] options) throws Exception {
    testCombination(options, false);
  }

  @Test
  public void testBasic() throws Exception {
    testAllCombinations(new String[]{"int", "varchar", "ASC", "DESC", "DESC", "ASC", "EXC", "INC"});
  }

  @Test
  public void testSingleTypeCreateOrders() throws Exception {
    testAllCombinations(new String[]{"ANY", "int", "ANY", "", "", "", "INC", ""});
  }

  @Test
  public void testSingleTypeScanOrders() throws Exception {
    testAllCombinations(new String[]{"ANY", "int", "DESC", "", "ANY", "", "EXC", ""});
  }

  @Test
  public void testSingleTypeBounds() throws Exception {
    testAllCombinations(new String[]{"ANY", "int", "DESC", "", "ASC", "", "ANY", ""});
  }

  // Following tests are too slow, creates 81 tables each. So we randomly sample some combinations

  @Test
  public void testPairOfTType() throws Exception {
    testRandomCombinations(
      new String[]{"ANY", "ANY", "DESC", "ASC", "ASC", "DESC", "EXC", ""}, 2);
  }

  @Test
  public void testCreateOrderAndBounds() throws Exception {
    testRandomCombinations(
      new String[]{"int", "varchar", "ANY", "ANY", "ASC", "DESC", "ANY", "ANY"}, 2);
  }

  @Test
  public void testScanOrderAndBounds() throws Exception {
    testRandomCombinations(new String[]{
      "int", "varchar", "DESC", "ASC", "ANY", "ANY", "ANY", "ANY"}, 2);
  }

  @Test
  public void testCreateAndScanOrders() throws Exception {
    testRandomCombinations(new String[]{
      "int", "varchar", "ANY", "ANY", "ANY", "ANY", "", "INC"}, 2);
  }

  @Test
  public void testThreeRangeColumns() throws Exception {
    // Invalid create table query due to bad order by clause
    runInvalidStmt("CREATE TABLE test_create (h1 int, r1 int, r2 varchar, r3 int, v1 varchar, " +
        "primary key((h1), r1, r2, r3)) WITH CLUSTERING ORDER BY(r1 ASC, r3 DESC);");
    // Valid create table.
    session.execute("CREATE TABLE test_create (h1 int, r1 int, r2 varchar, r3 int, v1 varchar, " +
        "primary key((h1), r1, r2, r3)) WITH CLUSTERING ORDER BY(r1 DESC, r2 ASC, r3 DESC);");

    // Some inserts in random order
    session.execute("INSERT INTO test_create (h1, r1, r2, r3, v1) VALUES (1, 2, 's12', 1, 'a');");
    session.execute("INSERT INTO test_create (h1, r1, r2, r3, v1) VALUES (1, 2, 's1', 2, 'b');");
    session.execute("INSERT INTO test_create (h1, r1, r2, r3, v1) VALUES (1, 3, 's123', 3, 'c');");
    session.execute("INSERT INTO test_create (h1, r1, r2, r3, v1) VALUES (1, 3, 's123', 2, 'd');");
    // In the DB, the forward order will be 3 4 2 1 of the inserted rows.
    // (1, 3, 's123', 3, 'c'), (1, 3, 's123', 2, 'd'), (1, 2, 's1', 2, 'b'), (1, 2, 's12', 1, 'a')

    String selectTemplate = "SELECT h1, r1, r2, r3, v1 FROM test_create WHERE h1 = 1";
    // Invalid select statements due to bad order by clause
    runInvalidStmt(
      selectTemplate + " ORDER BY r1 ASC, r2 ASC;");
    runInvalidStmt(
      selectTemplate + " ORDER BY r1 ASC, r3 DESC;");
    runInvalidStmt(
      selectTemplate + " ORDER BY r1 DESC, r2 DESC;");
    runInvalidStmt(
      selectTemplate + " ORDER BY r1 DESC, r3 ASC;");

    ResultSet rs;
    Row row;
    // Testing forward scan
    rs = session.execute(selectTemplate + " ORDER BY r1 DESC, r2 ASC;");
    assertEquals(4, rs.getAvailableWithoutFetching());
    row = rs.one(); assertEquals("Row[1, 3, s123, 3, c]", row.toString());
    row = rs.one(); assertEquals("Row[1, 3, s123, 2, d]", row.toString());
    row = rs.one(); assertEquals("Row[1, 2, s1, 2, b]", row.toString());
    row = rs.one(); assertEquals("Row[1, 2, s12, 1, a]", row.toString());

    // Testing reverse scan
    rs = session.execute(selectTemplate + " ORDER BY r1 ASC, r2 DESC, r3 ASC;");
    assertEquals(4, rs.getAvailableWithoutFetching());
    row = rs.one(); assertEquals("Row[1, 2, s12, 1, a]", row.toString());
    row = rs.one(); assertEquals("Row[1, 2, s1, 2, b]", row.toString());
    row = rs.one(); assertEquals("Row[1, 3, s123, 2, d]", row.toString());
    row = rs.one(); assertEquals("Row[1, 3, s123, 3, c]", row.toString());

    dropTable();
  }

  @Test
  public void testOrderingWithStaticColumns() throws Exception {
    session.execute("CREATE TABLE test_static (h int, s int static, r1 int, r2 int, v int, " +
        "primary key((h), r1, r2)) WITH CLUSTERING ORDER BY(r1 DESC, r2 ASC);");

    session.execute("INSERT INTO test_static (h, s, r1, r2, v) VALUES (1, 2, 1, 1, 1);");
    session.execute("INSERT INTO test_static (h, s, r1, r2, v) VALUES (1, 2, 1, 2, 1);");
    session.execute("INSERT INTO test_static (h, s, r1, r2, v) VALUES (1, 2, 2, 1, 1);");
    session.execute("INSERT INTO test_static (h, s, r1, r2, v) VALUES (1, 2, 2, 2, 1);");

    String selectTemplate = "SELECT h, s, r1, r2, v FROM test_static WHERE h = 1";

    assertQueryRowsOrdered(selectTemplate,
        "Row[1, 2, 2, 1, 1]",
        "Row[1, 2, 2, 2, 1]",
        "Row[1, 2, 1, 1, 1]",
        "Row[1, 2, 1, 2, 1]");

    assertQueryRowsOrdered(selectTemplate + " ORDER BY r1 ASC, r2 DESC",
        "Row[1, 2, 1, 2, 1]",
        "Row[1, 2, 1, 1, 1]",
        "Row[1, 2, 2, 2, 1]",
        "Row[1, 2, 2, 1, 1]");
  }

}
