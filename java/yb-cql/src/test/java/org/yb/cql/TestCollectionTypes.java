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
import com.google.common.collect.ImmutableList;
import org.junit.Test;
import org.yb.client.TestUtils;

import java.util.*;

import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestCollectionTypes extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestCollectionTypes.class);

  private String createTableStmt(String tableName, String keyType, String elemType)
      throws Exception {
    return String.format("CREATE TABLE %s (h int, r int, " +
        "vm map<%2$s, %3$s>, vs set<%2$s>, vl list<%3$s>," +
        "primary key((h), r));", tableName, keyType, elemType);
  }

  private void createCollectionTable(String tableName, String keyType, String elemType)
      throws Exception {
    String createStmt = createTableStmt(tableName, keyType, elemType);
    LOG.info("createStmt: " + createStmt);
    session.execute(createStmt);
  }

  private void createCollectionTable(String tableName) throws Exception {
    String createStmt = String.format("CREATE TABLE %s (h int, r int, " +
        "vm map<int, varchar>, vs set<varchar>, vl list<double>," +
        "primary key((h), r));", tableName);
    LOG.info("createStmt: " + createStmt);
    session.execute(createStmt);
  }

  @Test
  public void testCreateTable() throws Exception {
    // Types that can valid keys (and therefore also map keys or set elements)
    List<String> validKeyTypes = Arrays.asList("tinyint",
                                               "smallint",
                                               "int",
                                               "bigint",
                                               "varchar",
                                               "timestamp",
                                               "inet",
                                               "uuid",
                                               "timeuuid",
                                               "blob",
                                               "float",
                                               "double",
                                               "date",
                                               "time",
                                               "boolean");

    // Types that cannot be keys but can be valid collection elements (map values or list elems)
    List<String> nonKeyTypes = Arrays.asList();

    //------------------------------------------------------------------------------------------
    // Testing Valid Create Table Statements
    //------------------------------------------------------------------------------------------

    // Create a table testing all valid key/value collection type parameters.
    StringBuilder sb = new StringBuilder();
    String collTemplate = "vm_%2$s map<%1$s, %2$s>, vs_%2$s set<%1$s>, vl_%2$s list<%2$s>, ";

    sb.append("CREATE TABLE test_coll_types_create_valid_table (h int, r int, ");
    for (String tp : validKeyTypes) {
      sb.append(String.format(collTemplate, tp, tp));
    }
    for (String tp : nonKeyTypes) {
      // Non-key types only allowed as values, so we use 'int' for the keys.
      sb.append(String.format(collTemplate, "int", tp));
    }
    sb.append("PRIMARY KEY (h, r))");
    session.execute(sb.toString());

    //------------------------------------------------------------------------------------------
    // Testing Invalid Create Table Statements
    //------------------------------------------------------------------------------------------

    String invTableNameBase = "test_coll_types_create_invalid_table";
    // checking that non-key types are not allowed as set elems or map keys
    for (String tp : nonKeyTypes) {
      runInvalidStmt(createTableStmt(invTableNameBase + tp, tp, "int"));
    }

    // testing collection types separately because we may enable features selectively later

    // checking collection types cannot be primary keys
    String invalidCreateStmt = "CREATE TABLE coll_test_invalid (h map<int, varchar>, r int, " +
        "v int, primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r map<varchar, int>, v int, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h set<int>, r int, v int, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r set<int>, v int, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h list<int>, r int, v int, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r list<int>, v int, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);

    // checking that nested collections are disabled
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, v list<list<int>>, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, v list<set<int>>, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, v list<map<int,int>>, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);

    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, v map<int,list<int>>, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, v map<int, set<int>>, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, " +
        "v map<int, map<int, int>>, primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    // TODO (mihnea) when we enable nested collections the two cases below must remain invalid:
    // set elements and map keys are encoded as docdb keys so collections cannot be allowed
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, v set<list<int>>, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, v set<set<int>>, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, v set<map<int, int>>, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, v map<list<int>,int>, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, v map<set<int>,int>, " +
        "primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
    invalidCreateStmt = "CREATE TABLE coll_test_invalid (h int, r int, " +
        "v map<map<int, int>,int>, primary key((h), r));";
    runInvalidStmt(invalidCreateStmt);
  }

  @Test
  public void testValidQueries() throws Exception {
    String tableName = "test_collection_types";
    createCollectionTable(tableName);

    //------------------------------------------------------------------------------------------
    // Testing Insert -- covers parsing, type analysis and conversions (e.g. int -> double)
    //------------------------------------------------------------------------------------------
    String insert_template = "INSERT INTO " + tableName +
        " (h, r, vm, vs, vl) VALUES (%d, %d, %s, %s, %s);";

    // Testing Basic Insert
    String insert_stmt1 = String.format(insert_template, 1, 1, "{1 : 'a', 2 : 'b', 3 : 'c'}",
        "{'a', 'b', 'c'}", "[1.5, 2, 3.5]");
    session.execute(insert_stmt1);

    // Testing Inserting Null Values -- as Empty Collections
    String insert_stmt2 = String.format(insert_template, 1, 2, "{}", "{ }", "[ ]");
    session.execute(insert_stmt2);

    // Testing Inserting Null Values -- by omission
    String insert_stmt3 = "INSERT INTO " + tableName + " (h, r) VALUES (1, 3)";
    session.execute(insert_stmt3);

    // Testing Inserting Null Values -- by using explicit nulls
    String insert_stmt4 = "INSERT INTO " + tableName + " (h, r, vm, vs, vl) VALUES " +
        "(1, 4, null, null, null)";
    session.execute(insert_stmt4);

    // Testing Inserting Duplicates
    String insert_stmt5 = String.format(insert_template, 2, 1, "{1 : 'a', 2 : 'a', 1 : 'b'}",
        "{'3', '3', '4', '1', '2', '1', '4', '3'}", "[3.0, 3.0, 1.5, 1.5, 2.5, 3.0 ]");
    session.execute(insert_stmt5);

    // Testing Expressions inside collections (for now only '-' is available)
    String insert_stmt6 = String.format(insert_template, 2, 2, "{-1 : 'b', -2 : 'a'}",
        "{}", "[-3.5, -3, -1.5]");
    session.execute(insert_stmt6);


    //------------------------------------------------------------------------------------------
    // Testing Select -- covers mostly docdb storage, serialization & wire_protocol
    //------------------------------------------------------------------------------------------
    String select_template = "SELECT * FROM %s WHERE h = %d and r = %d";

    // -------------------------- Check Selecting Basic Values -------------------------------\\
    String select_stmt1 = String.format(select_template, tableName, 1, 1);
    Row row = runSelect(select_stmt1).next();
    assertEquals(1, row.getInt(0));
    assertEquals(1, row.getInt(1));

    // Checking Map Value
    Map<Integer, String> map_value = row.getMap(2, Integer.class, String.class);
    assertEquals(3, map_value.size());
    assertTrue(map_value.containsKey(1));
    assertEquals("a", map_value.get(1));
    assertTrue(map_value.containsKey(2));
    assertEquals("b", map_value.get(2));
    assertTrue(map_value.containsKey(3));
    assertEquals("c", map_value.get(3));

    // Checking Set Value
    Set<String> set_value = row.getSet(3, String.class);
    assertEquals(3, set_value.size());
    assertTrue(set_value.contains("a"));
    assertTrue(set_value.contains("b"));
    assertTrue(set_value.contains("c"));

    // Checking List Value
    List<Double> list_value = row.getList(4, Double.class);
    assertEquals(3, list_value.size());
    assertEquals(1.5, list_value.get(0), 0.0 /* delta */);
    assertEquals(2.0, list_value.get(1), 0.0 /* delta */);
    assertEquals(3.5, list_value.get(2), 0.0 /* delta */);

    //--------------- Check Selecting Null Values -- as Empty Collections --------------------\\
    String select_stmt2 = String.format(select_template, tableName, 1, 2);
    row = runSelect(select_stmt2).next();
    assertEquals(1, row.getInt(0));
    assertEquals(2, row.getInt(1));

    // Checking Map Value
    map_value = row.getMap(2, Integer.class, String.class);
    assertEquals(0, map_value.size());

    // Checking Set Value
    set_value = row.getSet(3, String.class);
    assertEquals(0, set_value.size());

    // Checking List Value
    list_value = row.getList(4, Double.class);
    assertEquals(0, list_value.size());

    //----------------- Check Selecting Null Values -- as Missing Values ---------------------\\
    String select_stmt3 = String.format(select_template, tableName, 1, 3);
    row = runSelect(select_stmt3).next();
    assertEquals(1, row.getInt(0));
    assertEquals(3, row.getInt(1));

    // Checking Map Value
    map_value = row.getMap(2, Integer.class, String.class);
    assertEquals(0, map_value.size());

    // Checking Set Value
    set_value = row.getSet(3, String.class);
    assertEquals(0, set_value.size());

    // Checking List Value
    list_value = row.getList(4, Double.class);
    assertEquals(0, list_value.size());

    //----------------- Check Selecting Null Values -- as Explicit Nulls ---------------------\\
    String select_stmt4 = String.format(select_template, tableName, 1, 4);
    row = runSelect(select_stmt4).next();
    assertEquals(1, row.getInt(0));
    assertEquals(4, row.getInt(1));

    // Checking Map Value
    map_value = row.getMap(2, Integer.class, String.class);
    assertEquals(0, map_value.size());

    // Checking Set Value
    set_value = row.getSet(3, String.class);
    assertEquals(0, set_value.size());

    // Checking List Value
    list_value = row.getList(4, Double.class);
    assertEquals(0, list_value.size());

    //------------------------ Check Selecting Values with Duplicates ------------------------\\
    // some properties (e.g. sets and map keys removing duplicates) are implicitly enforced by
    // the Java classes used by Datastax -- we test these on the C++ side also to be safe
    String select_stmt5 = String.format(select_template, tableName, 2, 1);
    row = runSelect(select_stmt5).next();
    assertEquals(2, row.getInt(0));
    assertEquals(1, row.getInt(1));

    // Checking Map Value -- should merge duplicate keys, but not values
    map_value = row.getMap(2, Integer.class, String.class);
    assertEquals(2, map_value.size());
    assertTrue(map_value.containsKey(1));
    assertEquals("b", map_value.get(1));
    assertTrue(map_value.containsKey(2));
    assertEquals("a", map_value.get(2));

    // Checking Set Value -- should preserve only distinct values (remove duplicates)
    set_value = row.getSet(3, String.class);
    assertEquals(4, set_value.size());
    assertTrue(set_value.contains("1"));
    assertTrue(set_value.contains("2"));
    assertTrue(set_value.contains("3"));
    assertTrue(set_value.contains("4"));

    // Checking List Value -- should preserve size and order
    list_value = row.getList(4, Double.class);
    assertEquals(6, list_value.size());
    assertEquals(3.0, list_value.get(0), 0.0 /* delta */);
    assertEquals(3.0, list_value.get(1), 0.0 /* delta */);
    assertEquals(1.5, list_value.get(2), 0.0 /* delta */);
    assertEquals(1.5, list_value.get(3), 0.0 /* delta */);
    assertEquals(2.5, list_value.get(4), 0.0 /* delta */);
    assertEquals(3.0, list_value.get(5), 0.0 /* delta */);

    //---------------------- Check Selecting Values Given As Expressions ---------------------\\
    String select_stmt6 = String.format(select_template, tableName, 2, 2);
    row = runSelect(select_stmt6).next();
    assertEquals(2, row.getInt(0));
    assertEquals(2, row.getInt(1));

    // Checking Map Value
    map_value = row.getMap(2, Integer.class, String.class);
    assertEquals(2, map_value.size());
    assertTrue(map_value.containsKey(-2));
    assertEquals("a", map_value.get(-2));
    assertTrue(map_value.containsKey(-1));
    assertEquals("b", map_value.get(-1));

    // Checking Set Value  -- should be empty (no expressions for strings yet)
    set_value = row.getSet(3, String.class);
    assertTrue(set_value.isEmpty());


    // Checking List Value
    list_value = row.getList(4, Double.class);
    assertEquals(3, list_value.size());
    assertEquals(-3.5, list_value.get(0), 0.0 /* delta */);
    assertEquals(-3.0, list_value.get(1), 0.0 /* delta */);
    assertEquals(-1.5, list_value.get(2), 0.0 /* delta */);

    //------------------------------------------------------------------------------------------
    // Testing Update -- covers mostly parsing and type analysis
    //------------------------------------------------------------------------------------------
    String update_template = "UPDATE " + tableName + " SET %s = %s WHERE h = 1 and r = 1";

    //-------------------------------- Testing Map Update ------------------------------------\\
    String update_stmt1 =
        String.format(update_template, "vm", "{11 : 'abcd', 12 : 'efgh', 13: 'xyza', 14: 'pqrs'}");
    session.execute(update_stmt1);

    // checking update result
    assertQuery(select_stmt1,
        "Row[1, 1, {11=abcd, 12=efgh, 13=xyza, 14=pqrs}, [a, b, c], [1.5, 2.0, 3.5]]");

    //-------------------------------- Testing Set Update ------------------------------------\\
    String update_stmt2 = String.format(update_template, "vs", "{'x', 'y'}");
    session.execute(update_stmt2);
    // checking update result
    row = runSelect(select_stmt1).next();
    assertEquals(1, row.getInt(0));
    assertEquals(1, row.getInt(1));
    set_value = row.getSet(3, String.class);
    assertEquals(2, set_value.size());
    assertTrue(set_value.contains("x"));
    assertTrue(set_value.contains("y"));

    //-------------------------------- Testing List Update -----------------------------------\\
    String update_stmt3 = String.format(update_template, "vl", "[2, 2.5, 3.0, 3.5, 4.0, 4.5]");
    session.execute(update_stmt3);
    // checking update result
    row = runSelect(select_stmt1).next();
    assertEquals(1, row.getInt(0));
    assertEquals(1, row.getInt(1));
    list_value = row.getList(4, Double.class);
    assertEquals(6, list_value.size());
    assertEquals(2.0, list_value.get(0), 0.0 /* delta */);
    assertEquals(2.5, list_value.get(1), 0.0 /* delta */);
    assertEquals(3.0, list_value.get(2), 0.0 /* delta */);
    assertEquals(3.5, list_value.get(3), 0.0 /* delta */);
    assertEquals(4.0, list_value.get(4), 0.0 /* delta */);
    assertEquals(4.5, list_value.get(5), 0.0 /* delta */);

    //------------------------------------------------------------------------------------------
    // Testing Delete -- basic functionality
    //------------------------------------------------------------------------------------------

    String delete_template = "DELETE %s FROM " + tableName + " WHERE h = 1 and r = 1";

    //-------------------------------- Testing Map Delete ------------------------------------\\
    String delete_stmt1 = String.format(delete_template, "vm[11], vm[13]");
    session.execute(delete_stmt1);
    assertQuery(String.format("SELECT * from %s where h = 1 and r = 1", tableName),
        "Row[1, 1, {12=efgh, 14=pqrs}, [x, y], [2.0, 2.5, 3.0, 3.5, 4.0, 4.5]]");

    //-------------------------------- Testing List Delete ------------------------------------\\
    String delete_stmt2 = String.format(delete_template, "vl[2], vl[4]");
    session.execute(delete_stmt2);
    assertQuery(String.format("SELECT * from %s where h = 1 and r = 1", tableName),
        "Row[1, 1, {12=efgh, 14=pqrs}, [x, y], [2.0, 2.5, 3.5, 4.5]]");

    // 3.5 and 4.5 should now be at index 2 and 3 respectively after the previous statement.
    String delete_stmt3 = String.format(delete_template, "vl[2], vl[3]");
    session.execute(delete_stmt3);
    assertQuery(String.format("SELECT * from %s where h = 1 and r = 1", tableName),
        "Row[1, 1, {12=efgh, 14=pqrs}, [x, y], [2.0, 2.5]]");

    //-------------------------------- Testing Basic Delete ------------------------------------\\
    String delete_stmt = "DELETE FROM " + tableName + " WHERE h = 1 and r = 2";
    session.execute(delete_stmt);

    // checking row was deleted
    ResultSet rs = session.execute(select_stmt2);
    Iterator<Row> iter = rs.iterator();
    assertFalse(iter.hasNext());

    // checking the previous and next rows are still there
    rs = session.execute(select_stmt1);
    iter = rs.iterator();
    assertTrue(iter.hasNext());
    rs = session.execute(select_stmt3);
    iter = rs.iterator();
    assertTrue(iter.hasNext());

    // Done -- cleaning up
    dropTable(tableName);
  }

  @Test
  public void TestInvalidQueries() throws Exception {

    //--------------------- Setting up for Insert and Update tests -----------------------------
    String tableName = "test_collection_types_invalid_queries";
    createCollectionTable(tableName);
    String insert_template = "INSERT INTO " + tableName +
        " (h, r, vm, vs, vl) VALUES (%d, %d, %s, %s, %s);";
    String insert_stmt0 = String.format(insert_template, 1, 1, "{222 : 'a', 333 : 'bcd'}",
        "{'a', 'b'}", "[1.0, 2.0, 3.0]");
    session.execute(insert_stmt0);

    //------------------------------------------------------------------------------------------
    // Testing Invalid Inserts
    //------------------------------------------------------------------------------------------

    // testing invalid list syntax
    String insert_stmt1 = String.format(insert_template, 1, 2, "{222 : 'a', 333: 'bcd'}",
        "{'a', 'b'}", "[1.0 : 2.0, 2.0 : 1.0]");
    runInvalidStmt(insert_stmt1);

    // testing wrong collection type (given List expected Set)
    String insert_stmt2 = String.format(insert_template, 1, 2, "{222 : 'a', 333: 'bcd'}",
        "['a', 'b']", "[1.0, 2.0, 3.0]");
    runInvalidStmt(insert_stmt2);

    // testing wrong map key type (given Double expected Int -- for one key only)
    String insert_stmt3 = String.format(insert_template, 1, 2, "{222 : 'a', 333.0: 'bcd'}",
        "{'a', 'b'}", "[1.0, 2.0, 3.0]");
    runInvalidStmt(insert_stmt3);

    // testing wrong map values type (given Int expected String -- for all values)
    String insert_stmt4 = String.format(insert_template, 1, 2, "{222 : 2, 333: 3}",
        "{'a', 'b'}", "[1.0, 2.0, 3.0]");
    runInvalidStmt(insert_stmt4);

    // testing wrong set elems type (given Int expected String -- for all elems)
    String insert_stmt5 = String.format(insert_template, 1, 2, "{222 : 'a', 333: 'bcd'}",
        "{1, 2}", "[1.0, 2.0, 3.0]");
    runInvalidStmt(insert_stmt5);

    // testing wrong list elem type (given String expected Int -- for one elem only)
    String insert_stmt6 = String.format(insert_template, 1, 2, "{222 : 'a', 333: 'bcd'}",
        "{'a', 'b'}", "[1.0, 'y', 3.0]");
    runInvalidStmt(insert_stmt6);

    // testing NULL in collections
    for (List<String> values : Arrays.asList(Arrays.asList("{null : 'a'}", "{'a'}", "[1.0]"),
                                             Arrays.asList("{1 : null}", "{'a'}", "[1.0]"),
                                             Arrays.asList("{1 : 'a'}", "{null}", "[1.0]"),
                                             Arrays.asList("{1 : 'a'}", "{'a'}", "[null]"))) {
      runInvalidStmt(
          String.format(insert_template, 1, 2, values.get(0), values.get(1), values.get(2)),
          "null is not supported inside collections");
    }

    //------------------------------------------------------------------------------------------
    // Testing Invalid Updates
    //------------------------------------------------------------------------------------------
    String update_template = "UPDATE " + tableName + " SET %s = %s WHERE h = 1 and r = 1";

    // testing invalid map syntax
    String update_stmt1 = String.format(update_template, "vm", "{121 : 'x', 12}");
    runInvalidStmt(update_stmt1);

    // testing wrong collection type (given Set expected Map)
    String update_stmt2 = String.format(update_template, "vm", "{12, 21, 2}");
    runInvalidStmt(update_stmt2);

    // testing wrong elements type (given String expected Double)
    String update_stmt3 = String.format(update_template, "vl", "['a', 'b', 'c']");
    runInvalidStmt(update_stmt3);

    // testing wrong keys type (given Double expected Int)
    String update_stmt4 = String.format(update_template, "vm", "{1.0 : 'a', 2.0 : 'b'}");
    runInvalidStmt(update_stmt4);

    // testing comparisons in where clause using collection columns
    String update_stmt5 = String.format(update_template, "vl", "[1.0, 2.0, 3.0]") +
        " AND vm = {}";
    runInvalidStmt(update_stmt5);

    // testing comparisons in if clause using collection columns
    String update_stmt6 = String.format(update_template, "vl", "[1.0, 2.0, 3.0]") +
        " IF vs = {'a', 'b'}";
    runInvalidStmt(update_stmt6);

    String update_stmt7 = String.format(update_template, "vs", "{'x', 'y'}") +
        " IF vl < [1.0, 2.0, 4.0]";
    runInvalidStmt(update_stmt7);

    // testing NULL in collections
    update_template = "UPDATE " + tableName + " SET %s WHERE h = 1 and r = 1";
    String update_template2 = "UPDATE " + tableName + " SET vm = {1:'a'} WHERE %s";

    for (String expr : ImmutableList.of(
        "vm = {null : 'a'}", "vm = {1 : null}", "vs = {null}", "vl = [null]")) {
      runInvalidStmt(String.format(update_template, expr),
                     "null is not supported inside collections");

      runInvalidStmt(String.format(update_template2, expr),
                     "null is not supported inside collections");
    }

    runInvalidStmt("UPDATE " + tableName + " SET vm = {1 : 'a'} WHERE h IN (1)",
                   "Operator not supported for write operations");

    runInvalidStmt("UPDATE " + tableName + " SET vs = { } IF vm = {}",
                   "Missing partition key");

    //------------------------------------------------------------------------------------------
    // Testing Invalid Deletes
    //------------------------------------------------------------------------------------------
    String delete_stmt = "DELETE FROM " + tableName + " WHERE %s";

    for (String expr : ImmutableList.of(
        "vm = {null : 'a'}", "vm = {1 : null}", "vs = {null}", "vl = [null]")) {
      runInvalidStmt(String.format(delete_stmt, expr),
                     "null is not supported inside collections");
    }

    runInvalidStmt("DELETE FROM " + tableName + " IF h = 1",
                   "syntax error, unexpected IF_P");

    // Done -- cleaning up
    dropTable(tableName);
  }

  // TODO (mihnea) this test will get more comprehensive once the append/prepend and index-based
  // update operations are supported
  @Test
  public void TestQueriesWithTTL() throws Exception {
    String tableName = "test_collection_types_with_ttl";
    createCollectionTable(tableName);

    String insert_template = "INSERT INTO " + tableName + " (h, r, vm, vs, vl) " +
        "VALUES (%d, %d, %s, %s, %s) USING TTL %d;";

    String insert_stmt1 = String.format(insert_template, 1, 1, "{1 : 'a', 2 : 'b', 3 : 'c'}",
        "{'a', 'b', 'c'}", "[1.0, 2.0, 3.0]", 3);
    session.execute(insert_stmt1);

    String update_template = "UPDATE " + tableName + " USING TTL %d SET %s = %s " +
        "WHERE h = 1 and r = 1 ";
    String update_stmt1 = String.format(update_template, 1, "vs", "{'d', 'e'}");
    session.execute(update_stmt1);
    TestUtils.waitForTTL(1000L);

    String select_stmt = "SELECT * FROM " + tableName + " WHERE h = 1 and r = 1";
    Row row = runSelect(select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals(1, row.getInt(1));
    // check set expired
    assertTrue(row.getSet(3, String.class).isEmpty());

    TestUtils.waitForTTL(2000L);
    // check entire row expired

    ResultSet rs = session.execute(select_stmt);
    Iterator<Row> iter = rs.iterator();
    assertFalse(iter.hasNext());

    // Done -- cleaning up
    dropTable(tableName);
  }

  @Test
  public void TestQueriesWithBind() throws Exception {
    String tableName = "test_collection_types_with_bind";
    createCollectionTable(tableName);

    //------------------------------------------------------------------------------------------
    // Testing Insert with Bind
    //------------------------------------------------------------------------------------------

    // preparing
    Map<Integer, String> map_input = new HashMap<>();
    map_input.put(1, "a");
    map_input.put(2, "b");
    map_input.put(3, "c");
    Set<String> set_input = new HashSet<>();
    set_input.add("a");
    set_input.add("b");
    set_input.add("c");
    List<Double> list_input = new ArrayList<>();
    list_input.add(1.0);
    list_input.add(2.0);
    list_input.add(3.0);

    //---------------------------- Testing Bind By Position ----------------------------------\\
    // Inserting the values
    String insert_stmt = "INSERT INTO " + tableName +
        " (h, r, vm, vs, vl) VALUES (?, ?, ?, ?, ?);";
    session.execute(insert_stmt,
        new Integer(1), new Integer(1),
        map_input, set_input, list_input);

    // Select data from the test table.
    String select_stmt = "SELECT * FROM " + tableName +
        " WHERE h = 1 AND r = 1;";
    Row row = runSelect(select_stmt).next();
    // checking row values
    assertEquals(1, row.getInt(0));
    assertEquals(1, row.getInt(1));
    assertEquals(map_input, row.getMap(2, Integer.class, String.class));
    assertEquals(set_input, row.getSet(3, String.class));
    assertEquals(list_input, row.getList(4, Double.class));

    //------------------------------ Testing Bind By Name ------------------------------------\\
    // Inserting the values
    insert_stmt = "INSERT INTO " + tableName + " (h, r, vm, vs, vl)" +
        " VALUES (:v1, :v2, :v3, :v4, :v5);";
    session.execute(insert_stmt,
        new HashMap<String, Object>() {{
          put("v1", new Integer(1));
          put("v2", new Integer(2));
          put("v3", map_input);
          put("v4", set_input);
          put("v5", list_input);
        }});

    // Select data from the test table.
    select_stmt = "SELECT * FROM " + tableName +
        " WHERE h = 1 AND r = 2;";
    row = runSelect(select_stmt).next();
    // checking row values;
    assertEquals(1, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(map_input, row.getMap(2, Integer.class, String.class));
    assertEquals(set_input, row.getSet(3, String.class));
    assertEquals(list_input, row.getList(4, Double.class));

    //------------------------------------------------------------------------------------------
    // Testing Update with Bind
    //------------------------------------------------------------------------------------------

    // preparing
    map_input.clear();
    map_input.put(4, "x");
    map_input.put(5, "y");
    map_input.put(6, "z");
    map_input.put(7, "a");
    map_input.put(8, "b");
    map_input.put(9, "c");
    set_input.clear();
    set_input.add("f");
    set_input.add("e");
    set_input.add("d");
    list_input.clear();
    list_input.add(2.0);
    list_input.add(3.0);
    list_input.add(4.0);
    list_input.add(5.0);
    list_input.add(6.0);
    list_input.add(7.0);

    //--------------------------- Testing Update By Position ---------------------------------\\
    // updating the values
    String update_stmt = "UPDATE " + tableName + " set vm = ?, vs = ?, vl = ?" +
        " WHERE h = ? AND r = ?;";
    session.execute(update_stmt,
        map_input, set_input, list_input,
        new Integer(1), new Integer(1));

    // Select data from the test table.
    select_stmt = "SELECT * FROM " + tableName +
        " WHERE h = 1 AND r = 1;";
    row = runSelect(select_stmt).next();
    // checking row values
    assertEquals(1, row.getInt(0));
    assertEquals(1, row.getInt(1));
    assertEquals(map_input, row.getMap(2, Integer.class, String.class));
    assertEquals(set_input, row.getSet(3, String.class));
    assertEquals(list_input, row.getList(4, Double.class));

    //----------------------------- Testing Update By Name -----------------------------------\\

    // updating the values
    update_stmt = "UPDATE " + tableName + " set vm = :v3, vs = :v4, vl = :v5" +
        " WHERE h = :v1 AND r = :v2;";
    session.execute(update_stmt,
        new HashMap<String, Object>() {{
          put("v1", new Integer(1));
          put("v2", new Integer(2));
          put("v3", map_input);
          put("v4", set_input);
          put("v5", list_input);
        }});

    // Select data from the test table.
    select_stmt = "SELECT * FROM " + tableName +
        " WHERE h = 1 AND r = 2;";
    row = runSelect(select_stmt).next();
    // checking row values
    assertEquals(1, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(map_input, row.getMap(2, Integer.class, String.class));
    assertEquals(set_input, row.getSet(3, String.class));
    assertEquals(list_input, row.getList(4, Double.class));

    //------------------------------------------------------------------------------------------
    // Testing Delete with Bind
    //------------------------------------------------------------------------------------------

    //--------------------------- Testing Delete By Position ---------------------------------\\
    String delete_stmt = "DELETE vm[?], vl[?] FROM " + tableName + " WHERE h = ? AND r = ?;";
    session.execute(delete_stmt, 4, 1, 1, 1);
    assertQuery(String.format("SELECT * from %s where h = 1 and r = 1", tableName),
        "Row[1, 1, {5=y, 6=z, 7=a, 8=b, 9=c}, [d, e, f], [2.0, 4.0, 5.0, 6.0, 7.0]]");

    //----------------------------- Testing Delete By Name -----------------------------------\\
    session.execute(delete_stmt, new HashMap<String, Object>() {{
        put("key(vm)", 6);
        put("idx(vl)", 1);
        put("h", 1);
        put("r", 1);
      }});
    assertQuery(String.format("SELECT * from %s where h = 1 and r = 1", tableName),
        "Row[1, 1, {5=y, 7=a, 8=b, 9=c}, [d, e, f], [2.0, 5.0, 6.0, 7.0]]");

    //--------------------------- Testing Delete By Named Markers -----------------------------\\
    String del_stmt2 = "DELETE vm[:mk], vl[:li] FROM " + tableName + " WHERE h = :hv AND r = :rv;";
    session.execute(del_stmt2, new HashMap<String, Object>() {{
        put("mk", 8);
        put("li", 2);
        put("hv", 1);
        put("rv", 1);
      }});
    assertQuery(String.format("SELECT * from %s where h = 1 and r = 1", tableName),
        "Row[1, 1, {5=y, 7=a, 9=c}, [d, e, f], [2.0, 5.0, 7.0]]");

    //------------------------------------------------------------------------------------------
    // SELECT statements do not require additional tests for binds because
    // collections cannot be primary keys and do not support comparison operations
    //------------------------------------------------------------------------------------------
  }

  @Test
  public void TestCollectionLiterals() throws Exception {
    String tableNameIntBlob = "test_collection_literals_int_blob";
    String tableNameBlobBlob = "test_collection_literals_blob_blob";

    createCollectionTable(tableNameIntBlob, "int", "blob");
    createCollectionTable(tableNameBlobBlob, "blob", "blob");

    String insertTemplateIntBlob = "INSERT INTO " + tableNameIntBlob +
        " (h, r, vm, vs, vl) VALUES (%d, %d, %s, %s, %s);";
    String insertTemplateBlobBlob = "INSERT INTO " + tableNameBlobBlob +
        " (h, r, vm, vs, vl) VALUES (%d, %d, %s, %s, %s);";

    // Insert with valid literals.
    String insertStmt;
    insertStmt = String.format(insertTemplateIntBlob, 1, 1,
                                "{ 1 : 0x01, 2 : textAsBlob('2'), 3 : intAsBlob(3) }",
                                "{ 1, 2, 3 }",
                                "[ 0x01, textAsBlob('a'), intAsBlob(3) ]");
    execute(insertStmt);

    insertStmt = String.format(insertTemplateBlobBlob, 1, 1,
                                "{ 0x01 : 0x01, 0x02 : textAsBlob('2'), 0x03 : intAsBlob(3) }",
                                "{ 0x01, textAsBlob('a'), intAsBlob(3) }",
                                "[ 0x01, textAsBlob('a'), intAsBlob(3) ]");
    execute(insertStmt);

    // Incorrect use of collection literals in SELECT fields.
    runInvalidQuery(String.format("SELECT { 1 : 1 } FROM %s", tableNameIntBlob));
    runInvalidQuery(String.format("SELECT { 1 : r } FROM %s", tableNameIntBlob));
    runInvalidQuery(String.format("SELECT { r : 1 } FROM %s", tableNameIntBlob));
    runInvalidQuery(String.format("SELECT [ 1 ] FROM %s", tableNameIntBlob));
    runInvalidQuery(String.format("SELECT [ r ] FROM %s", tableNameIntBlob));
    runInvalidQuery(String.format("SELECT { 1 } FROM %s", tableNameIntBlob));
    runInvalidQuery(String.format("SELECT { r } FROM %s", tableNameIntBlob));

    // Incorrect use of collection literals in WHERE clause.
    String selectTemplate = "SELECT * FROM %s WHERE %s IN (%s)";
    runInvalidQuery(String.format(selectTemplate, tableNameIntBlob, "vm",
                                  "{ 1 : 0x01, 2 : textAsBlob('2'), 3 : intAsBlob(3) }"));
    runInvalidQuery(String.format(selectTemplate, tableNameBlobBlob, "vs",
                                  "{ 0x01, textAsBlob('a'), intAsBlob(3) }"));
    runInvalidQuery(String.format(selectTemplate, tableNameIntBlob, "vl",
                                  "[ 0x01, textAsBlob('a'), intAsBlob(3) ]"));

    // Insert with invalid MAP literals.
    insertStmt = String.format(insertTemplateIntBlob, 1, 2,
                               "{ 1 : 1 }", null, null);
    runInvalidStmt(insertStmt);

    insertStmt = String.format(insertTemplateIntBlob, 1, 3,
                               "{ r : 0x01 }", null, null);
    runInvalidStmt(insertStmt);

    insertStmt = String.format(insertTemplateIntBlob, 1, 4,
                               "{ 1 : intAsBlob(r) }", null, null);
    runInvalidStmt(insertStmt);

    insertStmt = String.format(insertTemplateIntBlob, 1, 5,
                               "{ 1 : cast(1 as blob) }", null, null);
    runInvalidStmt(insertStmt);

    // Insert with invalid SET literals.
    insertStmt = String.format(insertTemplateBlobBlob, 1, 6,
                               null, "{ 1 }", null);
    runInvalidStmt(insertStmt);

    insertStmt = String.format(insertTemplateIntBlob, 1, 7,
                               null, "{ r }", null);
    runInvalidStmt(insertStmt);

    insertStmt = String.format(insertTemplateBlobBlob, 1, 8,
                               null, "{ intAsBlob(r) }", null);
    runInvalidStmt(insertStmt);

    insertStmt = String.format(insertTemplateBlobBlob, 1, 9,
                               null, "{ cast(1 as blob) }", null);
    runInvalidStmt(insertStmt);

    // Insert with invalid LIST literals.
    insertStmt = String.format(insertTemplateIntBlob, 1, 10,
                               null, null, "[ 1 ]");
    runInvalidStmt(insertStmt);

    insertStmt = String.format(insertTemplateIntBlob, 1, 11,
                               null, null, "[ r ]");
    runInvalidStmt(insertStmt);

    insertStmt = String.format(insertTemplateIntBlob, 1, 12,
                               null, null, "[ intAsBlob(r) ]");
    runInvalidStmt(insertStmt);

    insertStmt = String.format(insertTemplateIntBlob, 1, 13,
                               null, null, "[ cast(r as blob) ]");
    runInvalidStmt(insertStmt);

    // Insert with mismatch datatype.
    insertStmt = String.format(insertTemplateIntBlob, 2, 1,
                               "{ textAsBlob('2') : 2 }", null, null);
    runInvalidQuery(insertStmt);

    insertStmt = String.format(insertTemplateIntBlob, 2, 2,
                               "{ intAsBlob(3) : 3 }", null, null);
    runInvalidQuery(insertStmt);

    insertStmt = String.format(insertTemplateIntBlob, 2, 3,
                               null, "{ textAsBlob('a') }", null);
    runInvalidQuery(insertStmt);

    insertStmt = String.format(insertTemplateIntBlob, 2, 4,
                               null, "{ intAsBlob(3) }", null);
    runInvalidQuery(insertStmt);
  }
}
