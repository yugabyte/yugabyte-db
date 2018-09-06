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

import com.datastax.driver.core.*;
import org.junit.Test;

import java.util.*;

import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestUserDefinedTypes extends BaseCQLTest {

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
  public void testCreateType() throws Exception {
    createType("test_all_types", "f1 tinyint", "f2 smallint", "f3 int", "f4 bigint",
        "f5 varchar", "f6 timestamp", "f7 inet", "f8 uuid", "f9 timeuuid", "f10 blob",
        "f11 float", "f12 double", "f13 boolean");

    //----------------------------------------------------------------------------------------------
    // Testing Invalid Stmts.
    //----------------------------------------------------------------------------------------------

    // Type requires at least one field.
    runInvalidStmt("CREATE TYPE test ();");

    // Duplicate field names not allowed.
    runInvalidStmt("CREATE TYPE test (a int, a int);");
    runInvalidStmt("CREATE TYPE test (a int, b int, a text);");

    // Cannot create type if another type with that name already exists
    // -- `test_all_types` created above
    runInvalidStmt("CREATE TYPE test_all_types (a int);");

    // Un-frozen collections not allowed as field types
    runInvalidStmt("CREATE TYPE test (a int, b list<int>);");

    // Field types cannot refer to other user defined types
    runInvalidStmt("CREATE TYPE test (a int, b test_all_types);");
    runInvalidStmt("CREATE TYPE test (a int, b frozen<test_all_types>);");
  }

  @Test
  public void testCreateTableWithUDT() throws Exception {
    session.execute("CREATE KEYSPACE udt_ks1");
    session.execute("CREATE KEYSPACE udt_ks2");
    createType("udt_ks1.udt_test", "a int", "b text");

    // User-Defined Types can only be used in the keyspace where they are defined.
    session.execute("CREATE TABLE udt_ks1.test1(h int primary key, v udt_ks1.udt_test)");
    session.execute("CREATE TABLE udt_ks1.test2(h int primary key, v frozen<udt_ks1.udt_test>)");
    session.execute("USE udt_ks1");
    session.execute("CREATE TABLE test3(h int primary key, v udt_ks1.udt_test)");
    session.execute("CREATE TABLE test4(h int primary key, v frozen<udt_ks1.udt_test>)");

    // If omitted, UDT keyspace defaults to table keyspace.
    session.execute("USE udt_ks2");
    session.execute("CREATE TABLE udt_ks1.test5(h int primary key, v udt_test)");
    session.execute("CREATE TABLE udt_ks1.test6(h int primary key, v frozen<udt_test>)");

    session.execute("USE udt_ks1");
    runInvalidStmt("CREATE TABLE udt_ks2.test(h int primary key, v udt_test)");
    runInvalidStmt("CREATE TABLE udt_ks2.test(h int primary key, v frozen<udt_test>)");

    // Otherwise, UDT keyspace will default to current keyspace (like tables).
    session.execute("USE udt_ks1");
    session.execute("CREATE TABLE test7(h int primary key, v udt_test)");
    session.execute("CREATE TABLE test8(h int primary key, v frozen<udt_test>)");

    session.execute("USE udt_ks2");
    runInvalidStmt("CREATE TABLE test(h int primary key, v udt_test)");
    runInvalidStmt("CREATE TABLE test(h int primary key, v frozen<udt_test>)");

    // Create table with non-existent types should fail.
    runInvalidStmt("CREATE TABLE test(h int primary key, v non_existent_udt)");
    runInvalidStmt("CREATE TABLE test(h non_existent_udt primary key)");
    runInvalidStmt("CREATE TABLE test(h non_existent_udt, primary key(h))");
  }

  @Test
  public void testBasicUDTs() throws Exception {
    String tableName = "test_basic_udts";
    String typeName = "test_udt_employee";
    createType(typeName, "first_name text", "last_name text", "ssn bigint");

    String createStmt = String.format("CREATE TABLE %s (h int, r int, " +
        "v test_udt_employee, primary key((h), r));", tableName);
    LOG.info("createStmt: " + createStmt);
    session.execute(createStmt);

    //------------------------------------------------------------------------------------------
    // Testing Insert.
    //------------------------------------------------------------------------------------------

    // Basic UDT literal.
    {
      String insert_template = "INSERT INTO " + tableName + "(h, r, v) VALUES (%d, %d, %s);";
      session.execute(String.format(insert_template, 1, 1,
          "{first_name : 'a', last_name : 'b', ssn : 3}"));

      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("a", val.getString("first_name"));
      assertEquals("b", val.getString("last_name"));
      assertEquals(3L, val.getLong("ssn"));
      assertFalse(rows.hasNext());
    }

    // Fields in different order.
    {
      String insert_template = "INSERT INTO " + tableName + "(h, r, v) VALUES (%d, %d, %s);";
      session.execute(String.format(insert_template, 2, 2,
          "{ssn : 3, last_name : 'b', first_name : 'a'}"));

      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 2, 2));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("a", val.getString("first_name"));
      assertEquals("b", val.getString("last_name"));
      assertEquals(3L, val.getLong("ssn"));
      assertFalse(rows.hasNext());
    }

    // Missing field (allowed in CQL).
    {
      String insert_template = "INSERT INTO " + tableName + "(h, r, v) VALUES (%d, %d, %s);";
      session.execute(String.format(insert_template, 2, 2,
          "{ssn : 3, first_name : 'a'}"));
      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 2, 2));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("a", val.getString("first_name"));
      assertTrue(val.isNull("last_name"));
      assertEquals(3L, val.getLong("ssn"));
      assertFalse(rows.hasNext());
    }

    // Missing all fields not allowed.
    {
      String insert_template = "INSERT INTO " + tableName + "(h, r, v) VALUES (%d, %d, %s);";
      runInvalidStmt(String.format(insert_template, 2, 2, "{}"));
    }

    //------------------------------------------------------------------------------------------
    // Testing Update.
    //------------------------------------------------------------------------------------------
    {
      String update_template = "UPDATE " + tableName + " SET v = %s WHERE h = %d AND r = %d";
      session.execute(String.format(update_template,
          "{first_name : 'x', last_name : 'y', ssn : 999999}", 1, 1));

      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("x", val.getString("first_name"));
      assertEquals("y", val.getString("last_name"));
      assertEquals(999999L, val.getLong("ssn"));
      assertFalse(rows.hasNext());
    }

    //------------------------------------------------------------------------------------------
    // Testing Invalid Stmts.
    //------------------------------------------------------------------------------------------

    // Invalid field name: first field
    runInvalidStmt("INSERT INTO " + tableName + " (h, r, v) VALUES " +
        "(1, 1, {'first_nam' : 'a', last_name : 'b', ssn : 3});");

    // Invalid field type Int instead of String
    runInvalidStmt("INSERT INTO " + tableName + " (h, r, v) VALUES " +
        "(1, 1, {first_name : 'a', last_name : 2, ssn : 3});");

    // Invalid field type: Double instead of Int
    runInvalidStmt("INSERT INTO " + tableName + " (h, r, v) VALUES " +
        "(1, 1, {first_name : 'a', last_name : 'b', ssn : 3.5});");

    // Invalid type: Extra fields.
    runInvalidStmt("INSERT INTO " + tableName + " (h, r, v) VALUES " +
        "(1, 1, {first_name : 'a', last_name : 'b', ssn : 3.5, 'x' : 2});");

    // Invalid syntax: giving field names as strings.
    runInvalidStmt("INSERT INTO " + tableName + " (h, r, v) VALUES " +
        "(1, 1, {'first_name' : 'a', 'last_name' : 'b', 'ssn' : 3});");
  }

  @Test
  public void testUDTsWithBind() throws Exception {
    String tableName = "test_udt_bind";
    String typeName = "test_udt_employee";
    createType(typeName, "first_name text", "last_name text", "ssn bigint");

    String createStmt = String.format("CREATE TABLE %s (h int, r int, " +
        "v test_udt_employee, primary key((h), r));", tableName);
    LOG.info("createStmt: " + createStmt);
    session.execute(createStmt);

    //------------------------------------------------------------------------------------------
    // Testing Insert with Bind.
    //------------------------------------------------------------------------------------------
    {
      String insert_stmt = "INSERT INTO " + tableName + "(h, r, v) VALUES (?, ?, ?);";
      UserType udt = cluster.getMetadata()
          .getKeyspace(DEFAULT_TEST_KEYSPACE)
          .getUserType(typeName);
      UDTValue udtValue = udt.newValue()
          .set("first_name", "John", String.class)
          .set("last_name", "Doe", String.class)
          .set("ssn", 127L, Long.class);

      session.execute(insert_stmt, new Integer(1), new Integer(1), udtValue);
      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("John", val.getString("first_name"));
      assertEquals("Doe", val.getString("last_name"));
      assertEquals(127L, val.getLong("ssn"));
      assertFalse(rows.hasNext());
    }

    //------------------------------------------------------------------------------------------
    // Testing Update with Bind.
    //------------------------------------------------------------------------------------------
    {
      String update_stmt = "UPDATE " + tableName + " SET v = ? WHERE h = ? AND r = ?";
      UserType udt = cluster.getMetadata()
          .getKeyspace(DEFAULT_TEST_KEYSPACE)
          .getUserType(typeName);
      UDTValue udtValue = udt.newValue()
          .set("first_name", "Jane", String.class)
          .set("last_name", "Doe2", String.class)
          .set("ssn", 721L, Long.class);

      session.execute(update_stmt, udtValue, new Integer(1), new Integer(1));
      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("Jane", val.getString("first_name"));
      assertEquals("Doe2", val.getString("last_name"));
      assertEquals(721L, val.getLong("ssn"));
      assertFalse(rows.hasNext());
    }

    //------------------------------------------------------------------------------------------
    // Testing missing fields with Bind.
    //------------------------------------------------------------------------------------------
    {
      String update_stmt = "UPDATE " + tableName + " SET v = ? WHERE h = ? AND r = ?";
      UserType udt = cluster.getMetadata()
          .getKeyspace(DEFAULT_TEST_KEYSPACE)
          .getUserType(typeName);
      UDTValue udtValue = udt.newValue()
          .set("first_name", "Jack", String.class);

      session.execute(update_stmt, udtValue, new Integer(1), new Integer(1));
      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("Jack", val.getString("first_name"));
      assertTrue(val.isNull("last_name"));
      assertTrue(val.isNull("ssn"));
      assertFalse(rows.hasNext());
    }
  }

  @Test
  public void testUDTsWithFrozen() throws Exception {
    String tableName = "test_basic_fields";
    String typeName = "test_udt_employee";
    createType(typeName, "name text", "ssn bigint");

    String createStmt = String.format("CREATE TABLE %s (h %2$s, r %2$s, v %2$s, " +
        "primary key((h), r));", tableName, "frozen<" + typeName + ">");
    LOG.info("createStmt: " + createStmt);

    session.execute(createStmt);
    UserType udt_type = cluster.getMetadata()
        .getKeyspace(DEFAULT_TEST_KEYSPACE)
        .getUserType(typeName);
    UDTValue udt1 = udt_type.newValue()
        .set("name", "John", String.class)
        .set("ssn", 123L, Long.class);
    UDTValue udt2 = udt_type.newValue()
        .set("name", "Jane", String.class)
        .set("ssn", 234L, Long.class);
    UDTValue udt3 = udt_type.newValue()
        .set("name", "Jack", String.class)
        .set("ssn", 321L, Long.class);

    String udt1_lit = "{name : 'John', ssn : 123}";
    String udt2_lit = "{name : 'Jane', ssn : 234}";
    String udt3_lit = "{name : 'Jack', ssn : 321}";

    //------------------------------------------------------------------------------------------
    // Testing Insert
    //------------------------------------------------------------------------------------------

    //---------------------------------- Basic Insert ------------------------------------------
    {
      String insert_template = "INSERT INTO " + tableName + "(h, r, v) VALUES (%s, %s, %s);";
      session.execute(String.format(insert_template, udt1_lit, udt2_lit, udt3_lit));

      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %s";
      Iterator<Row> rows = runSelect(String.format(select_template, udt1_lit));
      Row row = rows.next();
      assertEquals(udt1, row.getUDTValue("h"));
      assertEquals(udt2, row.getUDTValue("r"));
      assertEquals(udt3, row.getUDTValue("v"));
      assertFalse(rows.hasNext());
    }

    //-------------------------------- Insert with Bind ----------------------------------------
    {
      String insert_stmt = "INSERT INTO " + tableName + "(h, r, v) VALUES (?, ?, ?);";
      session.execute(insert_stmt, udt3, udt2, udt1);
      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %s;";
      Iterator<Row> rows = runSelect(String.format(select_template, udt3_lit));
      Row row = rows.next();
      assertEquals(udt3, row.getUDTValue("h"));
      assertEquals(udt2, row.getUDTValue("r"));
      assertEquals(udt1, row.getUDTValue("v"));
      assertFalse(rows.hasNext());
    }

    //------------------------------------------------------------------------------------------
    // Testing Invalid Stmts.
    //------------------------------------------------------------------------------------------
    {
      String insert_template = "INSERT INTO " + tableName + "(h, r, v) VALUES (%s, %s, %s);";

      // Invalid field name
      runInvalidStmt(String.format(insert_template, udt1_lit,
          "{'nam' : 'a', ssn : 3}", udt3_lit));

      // Invalid field type (Int instead of String)
      runInvalidStmt(String.format(insert_template, udt1_lit,
          "{name : 3, ssn : 3}", udt3_lit));

      // Invalid field type (Double instead of Int)
      runInvalidStmt(String.format(insert_template, udt1_lit,
          "{name : 'a', ssn : 3.5}", udt3_lit));

      // Extra fields.
      runInvalidStmt(String.format(insert_template, udt1_lit,
          "{name : 'a', ssn : 3, 'other' : 'x'}", udt3_lit));

      // Cannot update range key.
      runInvalidStmt("UPDATE " + tableName + " SET r = " +
          "{name : 'a', ssn : 3} WHERE h = {name : 'a', ssn : 3};");

      // Cannot update hash key.
      runInvalidStmt("UPDATE " + tableName + " SET h = " +
          "{name : 'a', ssn : 3} WHERE r = {name : 'a', ssn : 3};");
    }
  }
}
