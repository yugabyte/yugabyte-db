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

import com.datastax.driver.core.utils.Bytes;

import com.datastax.driver.core.*;
import org.junit.Ignore;
import org.junit.Test;

import java.math.BigInteger;
import java.util.*;

import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestFrozenType extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestFrozenType.class);

  @Test
  public void testFrozenValues() throws Exception {
    String tableName = "test_frozen";

    String createStmt = String.format("CREATE TABLE %s (h int, r int, " +
        "vm frozen<map<int, text>>, vs frozen<set<text>>, vl frozen<list<double>>," +
        " primary key((h), r));", tableName);
    LOG.info("createStmt: " + createStmt);
    session.execute(createStmt);

    //----------------------------------------------------------------------------------------------
    // Testing Insert and Select.
    //----------------------------------------------------------------------------------------------
    String insertTemplate = "INSERT INTO " + tableName + "(h, r, vm, vs, vl) " +
        " VALUES (%d, %d, %s, %s, %s);";

    //------------------------------------ Test Basic Values ---------------------------------------
    {
      session.execute(String.format(insertTemplate, 1, 1,
          "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]"));

      // Checking Row.
      String selectTemplate = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(selectTemplate, 1, 1));
      Row row = rows.next();
      assertEquals(1, row.getInt("h"));
      assertEquals(1, row.getInt("r"));

      Map<Integer, String> mapValue = row.getMap("vm", Integer.class, String.class);
      assertEquals(2, mapValue.size());
      assertTrue(mapValue.containsKey(1));
      assertEquals("a", mapValue.get(1));
      assertTrue(mapValue.containsKey(2));
      assertEquals("b", mapValue.get(2));

      Set<String> setValue = row.getSet("vs", String.class);
      assertEquals(2, setValue.size());
      assertTrue(setValue.contains("x"));
      assertTrue(setValue.contains("y"));

      List<Double> listValue = row.getList("vl", Double.class);
      assertEquals(3, listValue.size());
      assertEquals(1.5, listValue.get(0), 0.0);
      assertEquals(2.5, listValue.get(1), 0.0);
      assertEquals(3.5, listValue.get(2), 0.0);
      assertFalse(rows.hasNext());
    }

    //------------------------------------ Test Null Values ----------------------------------------
    {
      session.execute(String.format(insertTemplate, 1, 2,
          "{}", "{ }", "[]"));

      // Checking Row.
      String selectTemplate = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(selectTemplate, 1, 2));
      Row row = rows.next();
      assertEquals(1, row.getInt("h"));
      assertEquals(2, row.getInt("r"));

      Map<Integer, String> mapValue = row.getMap("vm", Integer.class, String.class);
      assertEquals(0, mapValue.size());

      Set<String> setValue = row.getSet("vs", String.class);
      assertEquals(0, setValue.size());

      List<Double> listValue = row.getList("vl", Double.class);
      assertEquals(0, listValue.size());

      assertFalse(rows.hasNext());
    }

    //-------------------------------- Test Invalid Insert Stmts -----------------------------------

    // Wrong collection type (Set instead of Map).
    runInvalidStmt(String.format(insertTemplate, 1, 1,
        "{1, 2}", "{'x', 'y'}", "[1.5, 2.5, 3.5]"));

    // Wrong collection type (List instead of Set).
    runInvalidStmt(String.format(insertTemplate, 1, 1,
        "{1 : 'a', 2 : 'b'}", "['x', 'y']", "[1.5, 2.5, 3.5]"));

    // Wrong map key type (String instead of Int).
    runInvalidStmt(String.format(insertTemplate, 1, 1,
        "{'1' : 'a', '2' : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]"));

    // Wrong set element type (Int instead of String)
    runInvalidStmt(String.format(insertTemplate, 1, 1,
        "{1 : 'a', 2 : 'b'}", "{1, 2}", "[1.5, 2.5, 3.5]"));

    // Wrong List element type (String instead of Double)
    runInvalidStmt(String.format(insertTemplate, 1, 1,
        "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "['1', '2', '3']"));

    //----------------------------------------------------------------------------------------------
    // Testing Update and Select.
    //----------------------------------------------------------------------------------------------
    String updateTemplate = "UPDATE " + tableName + " SET vm = %s, vs = %s, vl = %s WHERE " +
        "h = %d AND r = %d;";

    //------------------------------------ Test Basic Values ---------------------------------------
    {
      session.execute(String.format(updateTemplate,
          "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1, 2));

      // Checking Row.
      String selectTemplate = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(selectTemplate, 1, 2));
      Row row = rows.next();
      assertEquals(1, row.getInt("h"));
      assertEquals(2, row.getInt("r"));

      Map<Integer, String> mapValue = row.getMap("vm", Integer.class, String.class);
      assertEquals(2, mapValue.size());
      assertTrue(mapValue.containsKey(1));
      assertEquals("a", mapValue.get(1));
      assertTrue(mapValue.containsKey(2));
      assertEquals("b", mapValue.get(2));

      Set<String> setValue = row.getSet("vs", String.class);
      assertEquals(2, setValue.size());
      assertTrue(setValue.contains("x"));
      assertTrue(setValue.contains("y"));

      List<Double> listValue = row.getList("vl", Double.class);
      assertEquals(3, listValue.size());
      assertEquals(1.5, listValue.get(0), 0.0);
      assertEquals(2.5, listValue.get(1), 0.0);
      assertEquals(3.5, listValue.get(2), 0.0);
      assertFalse(rows.hasNext());
    }

    //------------------------------------- Test Null Values ---------------------------------------
    {
      session.execute(String.format(updateTemplate, "{}", "{ }", "[]", 1, 1));

      // Checking Row.
      String selectTemplate = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(selectTemplate, 1, 1));
      Row row = rows.next();
      assertEquals(1, row.getInt("h"));
      assertEquals(1, row.getInt("r"));

      Map<Integer, String> mapValue = row.getMap("vm", Integer.class, String.class);
      assertEquals(0, mapValue.size());

      Set<String> setValue = row.getSet("vs", String.class);
      assertEquals(0, setValue.size());

      List<Double> listValue = row.getList("vl", Double.class);
      assertEquals(0, listValue.size());

      assertFalse(rows.hasNext());
    }

    //-------------------------------- Test Invalid Update Stmts -----------------------------------

    // Wrong collection type (Set instead of Map).
    runInvalidStmt(String.format(updateTemplate,
        "{1, 2}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1, 2));

    // Wrong collection type (List instead of Set).
    runInvalidStmt(String.format(updateTemplate,
        "{1 : 'a', 2 : 'b'}", "['x', 'y']", "[1.5, 2.5, 3.5]", 1, 2));

    // Wrong map key type (String instead of Int).
    runInvalidStmt(String.format(updateTemplate,
        "{'1' : 'a', '2' : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1, 2));

    // Wrong set element type (Int instead of String)
    runInvalidStmt(String.format(updateTemplate,
        "{1 : 'a', 2 : 'b'}", "{1, 2}", "[1.5, 2.5, 3.5]", 1, 2));

    // Wrong List element type (String instead of Double)
    runInvalidStmt(String.format(updateTemplate,
        "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "['1', '2', '3']", 1, 2));
  }

  @Test
  public void testFrozenCollectionsAsKeys() throws Exception {
    String tableName = "test_frozen";

    String createStmt = String.format("CREATE TABLE %s (h int," +
        "rm frozen<map<int, text>>, rs frozen<set<text>>, rl frozen<list<double>>, v int," +
        " primary key((h), rm, rs, rl));", tableName);
    LOG.info("createStmt: " + createStmt);
    session.execute(createStmt);

    //----------------------------------------------------------------------------------------------
    // Testing Insert and Select.
    //----------------------------------------------------------------------------------------------
    String insertTemplate = "INSERT INTO " + tableName + "(h, rm, rs, rl, v) " +
        " VALUES (%d, %s, %s, %s, %d);";

    //------------------------------------ Test Basic Values ---------------------------------------
    {
      session.execute(String.format(insertTemplate, 1,
          "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1));

      // Checking Row.
      String selectTemplate = "SELECT * FROM " + tableName + " WHERE h = %d";
      Iterator<Row> rows = runSelect(String.format(selectTemplate, 1));
      Row row = rows.next();
      assertEquals(1, row.getInt("h"));
      assertEquals(1, row.getInt("v"));

      Map<Integer, String> mapValue = row.getMap("rm", Integer.class, String.class);
      assertEquals(2, mapValue.size());
      assertTrue(mapValue.containsKey(1));
      assertEquals("a", mapValue.get(1));
      assertTrue(mapValue.containsKey(2));
      assertEquals("b", mapValue.get(2));

      Set<String> setValue = row.getSet("rs", String.class);
      assertEquals(2, setValue.size());
      assertTrue(setValue.contains("x"));
      assertTrue(setValue.contains("y"));

      List<Double> listValue = row.getList("rl", Double.class);
      assertEquals(3, listValue.size());
      assertEquals(1.5, listValue.get(0), 0.0);
      assertEquals(2.5, listValue.get(1), 0.0);
      assertEquals(3.5, listValue.get(2), 0.0);
      assertFalse(rows.hasNext());
    }

    //------------------------------------ Test Empty Values ---------------------------------------
    // Empty values for frozen collections are not considered null in CQL => allowed in keys
    {
      session.execute(String.format(insertTemplate, 2,
          "{}", "{ }", "[]", 1));

      // Checking Row.
      String selectTemplate = "SELECT * FROM " + tableName + " WHERE h = %d";
      Iterator<Row> rows = runSelect(String.format(selectTemplate, 2));
      Row row = rows.next();
      assertEquals(2, row.getInt("h"));
      assertEquals(1, row.getInt("v"));

      Map<Integer, String> mapValue = row.getMap("rm", Integer.class, String.class);
      assertEquals(0, mapValue.size());

      Set<String> setValue = row.getSet("rs", String.class);
      assertEquals(0, setValue.size());

      List<Double> listValue = row.getList("rl", Double.class);
      assertEquals(0, listValue.size());

      assertFalse(rows.hasNext());
    }

    //-------------------------------- Test Invalid Insert Stmts -----------------------------------

    // Wrong collection type (Set instead of Map).
    runInvalidStmt(String.format(insertTemplate, 1,
        "{1, 2}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1));

    // Wrong collection type (List instead of Set).
    runInvalidStmt(String.format(insertTemplate, 1,
        "{1 : 'a', 2 : 'b'}", "['x', 'y']", "[1.5, 2.5, 3.5]", 1));

    // Wrong map key type (String instead of Int).
    runInvalidStmt(String.format(insertTemplate, 1,
        "{'1' : 'a', '2' : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1));

    // Wrong set element type (Int instead of String)
    runInvalidStmt(String.format(insertTemplate, 1,
        "{1 : 'a', 2 : 'b'}", "{1, 2}", "[1.5, 2.5, 3.5]", 1));

    // Wrong List element type (String instead of Double)
    runInvalidStmt(String.format(insertTemplate, 1,
        "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "['1', '2', '3']", 1));

    //----------------------------------------------------------------------------------------------
    // Testing Select.
    // CQL allows equality conditions on entire collection values when frozen
    //----------------------------------------------------------------------------------------------
    session.execute(String.format(insertTemplate, 11,
        "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1));
    session.execute(String.format(insertTemplate, 11,
        "{}", "{}", "[]", 2));
    String selectTemplate = "SELECT * FROM " + tableName + " WHERE h = 11 AND %s";

    //-------------------------------- Testing Map Equality ----------------------------------------

    // Basic values.
    {
      Iterator<Row> rows =
          runSelect(String.format(selectTemplate, "rm = {1 : 'a', 2 : 'b'}"));
      // Checking Row.
      Row row = rows.next();
      assertEquals(11, row.getInt("h"));
      assertEquals(1, row.getInt("v"));

      Map<Integer, String> mapValue = row.getMap("rm", Integer.class, String.class);
      assertEquals(2, mapValue.size());
      assertTrue(mapValue.containsKey(1));
      assertEquals("a", mapValue.get(1));
      assertTrue(mapValue.containsKey(2));
      assertEquals("b", mapValue.get(2));

      Set<String> setValue = row.getSet("rs", String.class);
      assertEquals(2, setValue.size());
      assertTrue(setValue.contains("x"));
      assertTrue(setValue.contains("y"));

      List<Double> listValue = row.getList("rl", Double.class);
      assertEquals(3, listValue.size());
      assertEquals(1.5, listValue.get(0), 0.0);
      assertEquals(2.5, listValue.get(1), 0.0);
      assertEquals(3.5, listValue.get(2), 0.0);

      assertFalse(rows.hasNext());
    }

    // Empty/Null Value.
    {
      Iterator<Row> rows = runSelect(String.format(selectTemplate, "rm = {}"));
      // Checking Row.
      Row row = rows.next();
      assertEquals(11, row.getInt("h"));
      assertEquals(2, row.getInt("v"));

      Map<Integer, String> mapValue = row.getMap("rm", Integer.class, String.class);
      assertEquals(0, mapValue.size());

      Set<String> setValue = row.getSet("rs", String.class);
      assertEquals(0, setValue.size());

      List<Double> listValue = row.getList("rl", Double.class);
      assertEquals(0, listValue.size());

      assertFalse(rows.hasNext());
    }

    // No Results.
    {
      ResultSet rs = session.execute(String.format(selectTemplate, "rm = {1 : 'a'}"));
      // Checking Rows.
      assertFalse(rs.iterator().hasNext());
    }

    //-------------------------------- Testing Set Equality ----------------------------------------

    // Basic values.
    {
      Iterator<Row> rows =
          runSelect(String.format(selectTemplate, "rs = {'x', 'y'}"));
      // Checking Row.
      Row row = rows.next();
      assertEquals(11, row.getInt("h"));
      assertEquals(1, row.getInt("v"));

      Map<Integer, String> mapValue = row.getMap("rm", Integer.class, String.class);
      assertEquals(2, mapValue.size());
      assertTrue(mapValue.containsKey(1));
      assertEquals("a", mapValue.get(1));
      assertTrue(mapValue.containsKey(2));
      assertEquals("b", mapValue.get(2));

      Set<String> setValue = row.getSet("rs", String.class);
      assertEquals(2, setValue.size());
      assertTrue(setValue.contains("x"));
      assertTrue(setValue.contains("y"));

      List<Double> listValue = row.getList("rl", Double.class);
      assertEquals(3, listValue.size());
      assertEquals(1.5, listValue.get(0), 0.0);
      assertEquals(2.5, listValue.get(1), 0.0);
      assertEquals(3.5, listValue.get(2), 0.0);

      assertFalse(rows.hasNext());
    }

    // Empty/Null Value.
    {
      Iterator<Row> rows = runSelect(String.format(selectTemplate, "rs = {}"));
      // Checking Row.
      Row row = rows.next();
      assertEquals(11, row.getInt("h"));
      assertEquals(2, row.getInt("v"));

      Map<Integer, String> mapValue = row.getMap("rm", Integer.class, String.class);
      assertEquals(0, mapValue.size());

      Set<String> setValue = row.getSet("rs", String.class);
      assertEquals(0, setValue.size());

      List<Double> listValue = row.getList("rl", Double.class);
      assertEquals(0, listValue.size());

      assertFalse(rows.hasNext());
    }

    // No Results.
    {
      ResultSet rs = session.execute(String.format(selectTemplate, "rs = {'y'}"));
      // Checking Rows.
      assertFalse(rs.iterator().hasNext());
    }

    //-------------------------------- Testing List Equality ---------------------------------------
    // Basic Values.
    {
      Iterator<Row> rows =
          runSelect(String.format(selectTemplate, "rl = [1.5, 2.5, 3.5]"));
      // Checking Row.
      Row row = rows.next();
      assertEquals(11, row.getInt("h"));
      assertEquals(1, row.getInt("v"));

      Map<Integer, String> mapValue = row.getMap("rm", Integer.class, String.class);
      assertEquals(2, mapValue.size());
      assertTrue(mapValue.containsKey(1));
      assertEquals("a", mapValue.get(1));
      assertTrue(mapValue.containsKey(2));
      assertEquals("b", mapValue.get(2));

      Set<String> setValue = row.getSet("rs", String.class);
      assertEquals(2, setValue.size());
      assertTrue(setValue.contains("x"));
      assertTrue(setValue.contains("y"));

      List<Double> listValue = row.getList("rl", Double.class);
      assertEquals(3, listValue.size());
      assertEquals(1.5, listValue.get(0), 0.0);
      assertEquals(2.5, listValue.get(1), 0.0);
      assertEquals(3.5, listValue.get(2), 0.0);

      assertFalse(rows.hasNext());
    }

    // Null/Empty Value.
    {
      Iterator<Row> rows = runSelect(String.format(selectTemplate, "rl = []"));
      // Checking Row.
      Row row = rows.next();
      assertEquals(11, row.getInt("h"));
      assertEquals(2, row.getInt("v"));

      Map<Integer, String> mapValue = row.getMap("rm", Integer.class, String.class);
      assertEquals(0, mapValue.size());

      Set<String> setValue = row.getSet("rs", String.class);
      assertEquals(0, setValue.size());

      List<Double> listValue = row.getList("rl", Double.class);
      assertEquals(0, listValue.size());

      assertFalse(rows.hasNext());
    }

    // No Results.
    {
      ResultSet rs = session.execute(String.format(selectTemplate, "rl = [2.5]"));
      // Checking Rows.
      assertFalse(rs.iterator().hasNext());
    }
  }

  private void createType(String typeName, String... fields) {
    StringBuilder sb = new StringBuilder();
    sb.append("CREATE TYPE ");
    sb.append(typeName);
    sb.append("(");
    boolean first = true;
    for (String field : fields) {
      if (first)
        first = false;
      else
        sb.append(", ");
      sb.append(field);
    }
    sb.append(");");
    String createStmt = sb.toString();
    LOG.info("createType: " + createStmt);
    session.execute(createStmt);
  }

  @Test
  public void testFrozenUDTsInsideCollections() throws Exception {
    String tableName = "test_frozen_nested_col";
    String typeName = "test_udt_employee";
    createType(typeName, "name text", "ssn bigint", "blb blob");

    String createStmt = "CREATE TABLE " + tableName + " (h int, r int, " +
        "v1 set<frozen<map<int, text>>>, v2 list<frozen<test_udt_employee>>," +
        " primary key((h), r));";
    LOG.info("createStmt: " + createStmt);

    session.execute(createStmt);

    // Setup some maps to add into the set
    TypeCodec<Set<Map<Integer, String>>> setCodec =
        TypeCodec.set(TypeCodec.map(TypeCodec.cint(), TypeCodec.varchar()));

    Map<Integer, String> map1 = new HashMap<>();
    map1.put(1, "a");
    map1.put(2, "b");
    Map<Integer, String> map2 = new HashMap<>();
    map2.put(1, "m");
    map2.put(2, "n");
    Map<Integer, String> map3 = new HashMap<>();
    map3.put(11, "x");
    map3.put(12, "y");

    String map1Lit = "{1 : 'a', 2 : 'b'}";
    String map2Lit = "{1 : 'm', 2 : 'n'}";
    String map3Lit = "{11 : 'x', 12 : 'y'}";

    // Setup some user-defined values to add into list
    UserType udtType = cluster.getMetadata()
        .getKeyspace(DEFAULT_TEST_KEYSPACE)
        .getUserType(typeName);

    UDTValue udt1 = udtType.newValue()
        .set("name", "John", String.class)
        .set("ssn", 123L, Long.class);
    UDTValue udt2 = udtType.newValue()
        .set("name", "Jane", String.class)
        .set("ssn", 234L, Long.class);
    UDTValue udt3 = udtType.newValue()
        .set("name", "Jack", String.class)
        .setBytes("blb", Bytes.fromHexString("0x61"));
    UDTValue udt4 = udtType.newValue()
        .set("name", "Jack", String.class)
        .set("ssn", 321L, Long.class);

    String udt1Lit = "{name : 'John', ssn : 123}";
    String udt2Lit = "{name : 'Jane', ssn : 234}";
    String udt3Lit = "{name : 'Jack', blb : textAsBlob('a')}";
    String udt4Lit = "{name : 'Jack', ssn : 321}";

    //----------------------------------------------------------------------------------------------
    // Testing Insert
    //----------------------------------------------------------------------------------------------
    {
      String insertTemplate =
          "INSERT INTO " + tableName + " (h, r, v1, v2) VALUES (%d, %d, %s, %s);";
      session.execute(String.format(insertTemplate, 1, 1,
          "{" + map1Lit + ", " + map2Lit + "}",
          "[" + udt1Lit + ", " + udt2Lit + ", " + udt3Lit + "]"));
      // Checking Row.
      String selectTemplate = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d";
      Iterator<Row> rows = runSelect(String.format(selectTemplate, 1, 1));
      Row row = rows.next();
      assertEquals(1, row.getInt("h"));
      assertEquals(1, row.getInt("r"));

      Set<Map<Integer, String>> setValue = row.get("v1", setCodec);
      assertEquals(2, setValue.size());
      assertTrue(setValue.contains(map1));
      assertTrue(setValue.contains(map2));

      List<UDTValue> listValue = row.getList("v2", UDTValue.class);
      assertEquals(3, listValue.size());
      assertEquals(udt1, listValue.get(0));
      assertEquals(udt2, listValue.get(1));
      assertEquals(udt3, listValue.get(2));

      assertFalse(rows.hasNext());
    }

    //----------------------------------------------------------------------------------------------
    // Testing Update
    //----------------------------------------------------------------------------------------------
    {
      String updateTemplate =
          "UPDATE " + tableName + " SET v1 = %s WHERE h = %d AND r = %d;";
      session.execute(String.format(updateTemplate,
          "v1 + {" + map3Lit + "}", 1, 1));
      // Checking Row.
      String selectTemplate = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d";
      Iterator<Row> rows = runSelect(String.format(selectTemplate, 1, 1));
      Row row = rows.next();
      assertEquals(1, row.getInt("h"));
      assertEquals(1, row.getInt("r"));

      Set<Map<Integer, String>> setValue = row.get("v1", setCodec);
      assertEquals(3, setValue.size());
      assertTrue(setValue.contains(map1));
      assertTrue(setValue.contains(map2));
      assertTrue(setValue.contains(map3));

      assertFalse(rows.hasNext());
    }
    {
      String updateTemplate =
          "UPDATE " + tableName + " SET v2 = %s WHERE h = %d AND r = %d;";
      session.execute(String.format(updateTemplate,
          "v2 + [" + udt4Lit + "]", 1, 1));
      // Checking Row.
      String selectTemplate = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d";
      Iterator<Row> rows = runSelect(String.format(selectTemplate, 1, 1));
      Row row = rows.next();
      assertEquals(1, row.getInt("h"));
      assertEquals(1, row.getInt("r"));

      List<UDTValue> listValue = row.getList("v2", UDTValue.class);
      assertEquals(4, listValue.size());
      assertTrue(listValue.contains(udt1));
      assertTrue(listValue.contains(udt2));
      assertTrue(listValue.contains(udt3));
      assertTrue(listValue.contains(udt4));

      assertFalse(rows.hasNext());
    }

    //----------------------------------------------------------------------------------------------
    // Testing Bind
    //----------------------------------------------------------------------------------------------
    {
      Set<Map<Integer, String>> setValue = new HashSet<>();
      setValue.add(map1);
      setValue.add(map2);
      setValue.add(map3);

      List<UDTValue> listValue = new LinkedList<>();
      listValue.add(udt1);
      listValue.add(udt2);
      listValue.add(udt3);
      listValue.add(udt4);

      String insertStmt = "INSERT INTO " + tableName + "(h, r, v1, v2) VALUES (?, ?, ?, ?);";
      session.execute(insertStmt, 2, 1, setValue, listValue);

      // Checking Row.
      String selectTemplate = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d";
      Iterator<Row> rows = runSelect(String.format(selectTemplate, 2, 1));
      Row row = rows.next();
      assertEquals(2, row.getInt("h"));
      assertEquals(1, row.getInt("r"));
      assertEquals(setValue, row.get("v1", setCodec));
      assertEquals(listValue, row.getList("v2", UDTValue.class));

      assertFalse(rows.hasNext());
    }
  }

  // Utility function for testFrozenOrdering.
  private void createFrozenTable(String tableName, String rangeColType, String clusteringOrder) {
    String createStmt = "CREATE TABLE " + tableName + " (h int, " +
        "r frozen<" + rangeColType + ">, v int, " +
        "PRIMARY KEY((h), r)) WITH CLUSTERING ORDER BY (r " + clusteringOrder + ");";

    LOG.info("createStmt: " + createStmt);
    session.execute(createStmt);
  }

  // Utility function for testFrozenOrdering.
  private void insertFrozenLiterals(String tableName, List<String> literals, int shuffle_seed) {
    // Shuffle_seed and literals.size() must be co-prime for "randomizer" to generate all indices.
    int gcd = BigInteger.valueOf(shuffle_seed).gcd(BigInteger.valueOf(literals.size())).intValue();
    assertEquals(1, gcd);

    String insertStmt = "INSERT INTO " + tableName + "(h, r, v) VALUES (1, %s, 1);";
    for (int i = 0; i < literals.size(); i++) {
      // "randomize" insert order.
      int index = (shuffle_seed * i) % literals.size();
      session.execute(String.format(insertStmt, literals.get(index)));
    }
  }

  @Test
  public void testFrozenOrdering() throws Exception {
    //----------------------------------------------------------------------------------------------
    // Testing Map ordering
    //----------------------------------------------------------------------------------------------

    List<String> map_literals = Arrays.asList(
        "{ }",
        "{0:'x', 1:'y'}",
        "{1 : 'a'}",
        "{1 : 'abc'}",
        "{1 : 'x', 2 : 'y'}",
        "{2 : 'a', 3 : 'b'}",
        "{2 : 'b'}");

    //------------------------------------- Ascending Order ----------------------------------------
    {
      String tableName = "test_frozen_map_ordering";
      createFrozenTable(tableName, "map<int, text>", "ASC");
      insertFrozenLiterals(tableName, map_literals, 13);

      // Test DocDB order.
      assertQuery("SELECT * FROM " + tableName,
          "Row[1, {}, 1]" +
          "Row[1, {0=x, 1=y}, 1]" +
          "Row[1, {1=a}, 1]" +
          "Row[1, {1=abc}, 1]" +
          "Row[1, {1=x, 2=y}, 1]" +
          "Row[1, {2=a, 3=b}, 1]" +
          "Row[1, {2=b}, 1]");

      // Test Query Layer comparison.
      assertQuery("SELECT * FROM " + tableName + " WHERE r > {1 : 'x', 2 : 'y'}",
          "Row[1, {2=a, 3=b}, 1]" +
          "Row[1, {2=b}, 1]");
    }

    //------------------------------------- Descending Order ---------------------------------------
    {
      String tableName = "test_frozen_map_desc_ordering";
      createFrozenTable(tableName, "map<int, text>", "DESC");
      insertFrozenLiterals(tableName, map_literals, 11);

      // Test DocDB order.
      assertQuery("SELECT * FROM " + tableName,
          "Row[1, {2=b}, 1]" +
          "Row[1, {2=a, 3=b}, 1]" +
          "Row[1, {1=x, 2=y}, 1]" +
          "Row[1, {1=abc}, 1]" +
          "Row[1, {1=a}, 1]" +
          "Row[1, {0=x, 1=y}, 1]" +
          "Row[1, {}, 1]");

      // Test Query Layer comparison.
      assertQuery("SELECT * FROM " + tableName + " WHERE r >= {1 : 'x', 2 : 'y'}",
          "Row[1, {2=b}, 1]" +
          "Row[1, {2=a, 3=b}, 1]" +
          "Row[1, {1=x, 2=y}, 1]");
    }

    //----------------------------------------------------------------------------------------------
    // Testing Set ordering
    //----------------------------------------------------------------------------------------------

    List<String> set_literals = Arrays.asList(
        "{ }",
        "{0, 1}",
        "{1}",
        "{1, 2, 3}",
        "{1, 3}",
        "{2}",
        "{2, 3}");

    //------------------------------------- Ascending Order ----------------------------------------
    {
      String tableName = "test_frozen_set_ordering";
      createFrozenTable(tableName, "set<int>", "ASC");
      insertFrozenLiterals(tableName, set_literals, 17);

      // Test DocDB order.
      assertQuery("SELECT * FROM " + tableName,
          "Row[1, [], 1]" +
          "Row[1, [0, 1], 1]" +
          "Row[1, [1], 1]" +
          "Row[1, [1, 2, 3], 1]" +
          "Row[1, [1, 3], 1]" +
          "Row[1, [2], 1]" +
          "Row[1, [2, 3], 1]");

      // Test Query Layer comparison.
      assertQuery("SELECT * FROM " + tableName + " WHERE r <= {1}",
          "Row[1, [], 1]" +
          "Row[1, [0, 1], 1]" +
          "Row[1, [1], 1]");
    }

    //------------------------------------- Descending Order ---------------------------------------
    {
      String tableName = "test_frozen_set_desc_ordering";
      createFrozenTable(tableName, "set<int>", "DESC");
      insertFrozenLiterals(tableName, set_literals, 11);

      // Test DocDB order.
      assertQuery("SELECT * FROM " + tableName,
          "Row[1, [2, 3], 1]" +
          "Row[1, [2], 1]" +
          "Row[1, [1, 3], 1]" +
          "Row[1, [1, 2, 3], 1]" +
          "Row[1, [1], 1]" +
          "Row[1, [0, 1], 1]" +
          "Row[1, [], 1]");

      // Test Query Layer comparison.
      assertQuery("SELECT * FROM " + tableName + " WHERE r < {1}",
          "Row[1, [0, 1], 1]" +
          "Row[1, [], 1]");
    }

    //----------------------------------------------------------------------------------------------
    // Testing List ordering
    //----------------------------------------------------------------------------------------------

    List<String> list_literals = Arrays.asList(
        "[ ]",
        "[0, 1]",
        "[1]",
        "[1, 0, 1]",
        "[1, 1]",
        "[2]",
        "[2, 1]");

    //------------------------------------- Ascending Order ----------------------------------------
    {
      String tableName = "test_frozen_list_ordering";
      createFrozenTable(tableName, "list<int>", "ASC");
      insertFrozenLiterals(tableName, list_literals, 17);

      // Test DocDB order.
      assertQuery("SELECT * FROM " + tableName,
          "Row[1, [], 1]" +
          "Row[1, [0, 1], 1]" +
          "Row[1, [1], 1]" +
          "Row[1, [1, 0, 1], 1]" +
          "Row[1, [1, 1], 1]" +
          "Row[1, [2], 1]" +
          "Row[1, [2, 1], 1]");

      // Test Query Layer comparison.
      assertQuery("SELECT * FROM " + tableName + " WHERE r > [1, 1]",
          "Row[1, [2], 1]" +
          "Row[1, [2, 1], 1]");
    }

    //------------------------------------- Descending Order ---------------------------------------
    {
      String tableName = "test_frozen_list_desc_ordering";
      createFrozenTable(tableName, "list<int>", "DESC");
      insertFrozenLiterals(tableName, list_literals, 11);

      // Test DocDB order.
      assertQuery("SELECT * FROM " + tableName,
          "Row[1, [2, 1], 1]" +
          "Row[1, [2], 1]" +
          "Row[1, [1, 1], 1]" +
          "Row[1, [1, 0, 1], 1]" +
          "Row[1, [1], 1]" +
          "Row[1, [0, 1], 1]" +
          "Row[1, [], 1]");

      // Test Query Layer comparison.
      assertQuery("SELECT * FROM " + tableName + " WHERE r >= [1, 1]",
          "Row[1, [2, 1], 1]" +
          "Row[1, [2], 1]" +
          "Row[1, [1, 1], 1]");
    }

    //----------------------------------------------------------------------------------------------
    // Testing UDT ordering
    //----------------------------------------------------------------------------------------------
    String typeName = "test_order_udt";
    createType(typeName, "t text", "b bigint", "l frozen<list<int>>");

    List<String> udt_literals = Arrays.asList(
        "{l : [1, 1]}",
        "{l : [1, 2]}",
        "{b : 1}",
        "{b : 2}",
        "{b : 2, l : [1]}",
        "{t : 'a' }",
        "{t : 'ab'}",
        "{t : 'ab', l : [3]}",
        "{t : 'ab', b : 2}",
        "{t : 'ab', b : 2, l : [3]}");

    //------------------------------------- Ascending Order ----------------------------------------
    {
      String tableName = "test_frozen_udt_ordering";
      createFrozenTable(tableName, "test_order_udt", "ASC");
      insertFrozenLiterals(tableName, udt_literals, 13);

      // Test DocDB order.
      assertQuery("SELECT * FROM " + tableName,
          "Row[1, {t:NULL,b:NULL,l:[1,1]}, 1]" +
          "Row[1, {t:NULL,b:NULL,l:[1,2]}, 1]" +
          "Row[1, {t:NULL,b:1,l:[]}, 1]" +
          "Row[1, {t:NULL,b:2,l:[]}, 1]" +
          "Row[1, {t:NULL,b:2,l:[1]}, 1]" +
          "Row[1, {t:'a',b:NULL,l:[]}, 1]" +
          "Row[1, {t:'ab',b:NULL,l:[]}, 1]" +
          "Row[1, {t:'ab',b:NULL,l:[3]}, 1]" +
          "Row[1, {t:'ab',b:2,l:[]}, 1]" +
          "Row[1, {t:'ab',b:2,l:[3]}, 1]");

      // Test Query Layer comparison.
      assertQuery("SELECT * FROM " + tableName + " WHERE r <= {b : 1}",
          "Row[1, {t:NULL,b:NULL,l:[1,1]}, 1]" +
          "Row[1, {t:NULL,b:NULL,l:[1,2]}, 1]" +
          "Row[1, {t:NULL,b:1,l:[]}, 1]");
    }

    //------------------------------------- Descending Order ---------------------------------------
    {
      String tableName = "test_frozen_udt_desc_ordering";
      createFrozenTable(tableName, "test_order_udt", "DESC");
      insertFrozenLiterals(tableName, udt_literals, 17);

      // Test DocDB order.
      assertQuery("SELECT * FROM " + tableName,
          "Row[1, {t:'ab',b:2,l:[3]}, 1]" +
          "Row[1, {t:'ab',b:2,l:[]}, 1]" +
          "Row[1, {t:'ab',b:NULL,l:[3]}, 1]" +
          "Row[1, {t:'ab',b:NULL,l:[]}, 1]" +
          "Row[1, {t:'a',b:NULL,l:[]}, 1]" +
          "Row[1, {t:NULL,b:2,l:[1]}, 1]" +
          "Row[1, {t:NULL,b:2,l:[]}, 1]" +
          "Row[1, {t:NULL,b:1,l:[]}, 1]" +
          "Row[1, {t:NULL,b:NULL,l:[1,2]}, 1]" +
          "Row[1, {t:NULL,b:NULL,l:[1,1]}, 1]");

      // Test Query Layer comparison.
      assertQuery("SELECT * FROM " + tableName + " WHERE r < {b : 1}",
          "Row[1, {t:NULL,b:NULL,l:[1,2]}, 1]" +
          "Row[1, {t:NULL,b:NULL,l:[1,1]}, 1]");
    }
  }

  @Test
  public void testFrozenEquality() throws Exception {
    String typeName = "test_eq_udt";
    createType(typeName, "t text", "b bigint", "l frozen<set<int>>");

    String tableName = "test_frozen_equality";
    String createStmt = "CREATE TABLE " + tableName + " (" +
        "h1 frozen<map<int, text>>, h2 frozen<set<text>>, " +
        "h3 frozen<list<int>>, h4 frozen<test_eq_udt>, " +
        "v int, primary key((h1, h2, h3, h4)));";
    LOG.info("createStmt: " + createStmt);
    session.execute(createStmt);

    String insertStmt =
        "INSERT INTO " + tableName + "(h1, h2, h3, h4, v) VALUES (%s, %s, %s, %s, 1);";


    String mapLit1 = "{1 : 'a', 2 : 'b', 3 : 'c', 4 : 'd'}";
    String mapLit2 = "{2 : 'b0', 1 : 'a0', 4 : 'd', 1 : 'a', 3 : 'c', 2 : 'b'}";
    String mapJavaLit = "{1=a, 2=b, 3=c, 4=d}";

    String setLit1 = "{'w', 'x', 'y', 'z'}";
    String setLit2 = "{'z', 'x', 'y', 'y', 'z', 'x', 'x', 'z', 'y', 'w'}";
    String setJavaLit = "[w, x, y, z]";

    String listLit = "[3, 1, 1, 2]";
    String listJavaLit = "[3, 1, 1, 2]";

    String udtLit1 = "{t : 'foo', b : 1, l : {1, 2}}";
    String udtLit2 = "{l : {2, 1}, b : 1, t : 'foo'}";
    String udtJavaLit = "{t:'foo',b:1,l:{1,2}}";

    session.execute(String.format(insertStmt, mapLit1, setLit1, listLit, udtLit1));
    session.execute(String.format(insertStmt, mapLit2, setLit2, listLit, udtLit2));

    // Check that there is exactly one row in the table.
    Iterator<Row> rows = session.execute("SELECT * FROM " + tableName).iterator();
    assertTrue(rows.hasNext());
    Row row = rows.next();
    assertEquals(mapJavaLit, row.getMap("h1", Integer.class, String.class).toString());
    assertEquals(setJavaLit, row.getSet("h2", String.class).toString());
    assertEquals(listJavaLit, row.getList("h3", Integer.class).toString());
    assertEquals(udtJavaLit, row.getUDTValue("h4").toString());
    assertFalse(rows.hasNext());

    //---------------------------------- Check DocDB equality --------------------------------------

    // Give full hash key in where clause => match exact key in DocDB.
    rows = session.execute("SELECT * FROM " + tableName + " WHERE " +
        "h1 = " + mapLit2 + " AND h2 = " + setLit2 + " AND " +
        "h3 = " + listLit + " AND h4 = " + udtLit2).iterator();
    assertTrue(rows.hasNext());
    row = rows.next();
    assertEquals(mapJavaLit, row.getMap("h1", Integer.class, String.class).toString());
    assertEquals(setJavaLit, row.getSet("h2", String.class).toString());
    assertEquals(listJavaLit, row.getList("h3", Integer.class).toString());
    assertEquals(udtJavaLit, row.getUDTValue("h4").toString());
    assertFalse(rows.hasNext());

    //------------------------------- Check Query Layer equality -----------------------------------

    // Give only first two hash values => scan table and filter matching results in Query Layer.
    rows = session.execute("SELECT * FROM " + tableName + " WHERE " +
        "h1 = " + mapLit2 + " AND h2 = " + setLit2).iterator();
    assertTrue(rows.hasNext());
    row = rows.next();
    assertEquals(mapJavaLit, row.getMap("h1", Integer.class, String.class).toString());
    assertEquals(setJavaLit, row.getSet("h2", String.class).toString());
    assertEquals(listJavaLit, row.getList("h3", Integer.class).toString());
    assertEquals(udtJavaLit, row.getUDTValue("h4").toString());
    assertFalse(rows.hasNext());

    // Give only last two hash values => scan table and filter matching results in Query Layer.
    rows = session.execute("SELECT * FROM " + tableName + " WHERE " +
        "h3 = " + listLit + " AND h4 = " + udtLit2).iterator();
    assertTrue(rows.hasNext());
    row = rows.next();
    assertEquals(mapJavaLit, row.getMap("h1", Integer.class, String.class).toString());
    assertEquals(setJavaLit, row.getSet("h2", String.class).toString());
    assertEquals(listJavaLit, row.getList("h3", Integer.class).toString());
    assertEquals(udtJavaLit, row.getUDTValue("h4").toString());
    assertFalse(rows.hasNext());
  }
}
