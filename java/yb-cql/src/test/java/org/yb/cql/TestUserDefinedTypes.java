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

import java.math.BigDecimal;
import java.math.BigInteger;
import java.text.DecimalFormat;
import java.util.*;

import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestUserDefinedTypes extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestUserDefinedTypes.class);

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
    createType(typeName, "first_name text", "last_name text", "ssn bigint", "blb blob");

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
          "{first_name : 'a', last_name : 'b', ssn : 3, blb : 0x01 }"));

      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("a", val.getString("first_name"));
      assertEquals("b", val.getString("last_name"));
      assertEquals(3L, val.getLong("ssn"));
      assertEquals("0x01", makeBlobString(val.getBytes("blb")));
      assertFalse(rows.hasNext());
    }

    // UDT literal that has function call.
    {
      String insert_template = "INSERT INTO " + tableName + "(h, r, v) VALUES (%d, %d, %s);";
      session.execute(String.format(insert_template, 1, 1,
          "{first_name : 'a', last_name : 'b', ssn : 3, blb : textAsBlob('a') }"));

      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("a", val.getString("first_name"));
      assertEquals("b", val.getString("last_name"));
      assertEquals(3L, val.getLong("ssn"));
      assertEquals("0x61", makeBlobString(val.getBytes("blb")));
      assertFalse(rows.hasNext());
    }

    // Fields in different order.
    {
      String insert_template = "INSERT INTO " + tableName + "(h, r, v) VALUES (%d, %d, %s);";
      session.execute(String.format(insert_template, 2, 2,
          "{ssn : 3, last_name : 'b', blb : 0x01, first_name : 'a'}"));

      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 2, 2));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("a", val.getString("first_name"));
      assertEquals("b", val.getString("last_name"));
      assertEquals(3L, val.getLong("ssn"));
      assertEquals("0x01", makeBlobString(val.getBytes("blb")));
      assertFalse(rows.hasNext());
    }

    // Fields in different order and UDT literal that has function call.
    {
      String insert_template = "INSERT INTO " + tableName + "(h, r, v) VALUES (%d, %d, %s);";
      session.execute(String.format(insert_template, 2, 2,
          "{ssn : 3, last_name : 'b', blb : textAsBlob('a'), first_name : 'a'}"));

      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 2, 2));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("a", val.getString("first_name"));
      assertEquals("b", val.getString("last_name"));
      assertEquals(3L, val.getLong("ssn"));
      assertEquals("0x61", makeBlobString(val.getBytes("blb")));
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
      assertTrue(val.isNull("blb"));
      assertFalse(rows.hasNext());
    }

    // Missing field (allowed in CQL) and UDT literal that has function call.
    {
      String insert_template = "INSERT INTO " + tableName + "(h, r, v) VALUES (%d, %d, %s);";
      session.execute(String.format(insert_template, 2, 2,
          "{blb : textAsBlob('a'), first_name : 'a'}"));
      // Checking Row.
      String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
      Iterator<Row> rows = runSelect(String.format(select_template, 2, 2));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("v");
      assertEquals("a", val.getString("first_name"));
      assertTrue(val.isNull("last_name"));
      assertTrue(val.isNull("ssn"));
      assertEquals("0x61", makeBlobString(val.getBytes("blb")));
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

  private void verifyDecimalValues(int h) {
    // Checking Row.
    Iterator<Row> rows = runSelect("SELECT * FROM test_decimal WHERE h = " + h);
    Row row = rows.next();
    UDTValue val = row.getUDTValue("v");
    assertEquals(10000000, val.getDecimal("d0").intValue());
    assertEquals("1E+7", val.getDecimal("d0").toString());
    assertEquals("10000000", val.getDecimal("d0").toPlainString());
    assertEquals("1.23E+54", val.getDecimal("d1").toString());
    assertEquals("1230000000000000000000000000000000000000000000000000000",
        val.getDecimal("d1").toPlainString());
    assertEquals("1.23E+54", val.getDecimal("d2").toString());
    assertEquals("1.23E+54", val.getDecimal("d3").toString());
    DecimalFormat df = new DecimalFormat("#.###########");
    assertEquals("-2.061584302E-1080701410", val.getDecimal("d4").toString());
    assertEquals("-0",  df.format(val.getDecimal("d4").doubleValue()));
    assertEquals("4.656612595521636421835864894092082977294921875E-10",
        val.getDecimal("d5").toString());
    assertEquals("0.0000000004656612595521636421835864894092082977294921875",
        val.getDecimal("d5").toPlainString());
    assertEquals(4.656612595521636421835864894092082977294921875E-10,
        val.getDecimal("d5").doubleValue());
    assertEquals("0.00000000047",  df.format(val.getDecimal("d5").doubleValue()));
    assertTrue(val.isNull("d6"));
    assertFalse(rows.hasNext());
  }

  @Test
  public void testDecimal() throws Exception {
    createType("test_udt", "d0 decimal", "d1 decimal", "d2 decimal", "d3 decimal",
                           "d4 decimal", "d5 decimal", "d6 decimal");

    String createStmt = "CREATE TABLE test_decimal (h int primary key, v test_udt)";
    LOG.info("createStmt: " + createStmt);
    session.execute(createStmt);

    session.execute("INSERT INTO test_decimal (h, v) VALUES (1, " +
        "{ d0 : 1E+7, d1 : 1.23E54, d2 : 123E52," +
        "  d3 : 1230000000000000000000000000000000000000000000000000000," +
        "  d4 : -2.061584302E-1080701410," +
        "  d5 : 0.4656612595521636421835864894092082977294921875E-9 })");
    verifyDecimalValues(1);

    // Test Prepared Statement.
    UserType udt = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getUserType("test_udt");
    double d = 4.656612595521636421835864894092082977294921875E-10; // Float Hex: 0x2fffffff
    UDTValue v_new = udt.newValue()
        .setDecimal("d0", new BigDecimal(10000000))
        .setDecimal("d1", new BigDecimal("1.23E54"))
        .setDecimal("d2", new BigDecimal("123E52"))
        .setDecimal("d3", new BigDecimal(
            new BigInteger("1230000000000000000000000000000000000000000000000000000")))
        .setDecimal("d4", new BigDecimal("-2.061584302E-1080701410"))
        .setDecimal("d5", new BigDecimal(d));

    PreparedStatement pstmt = session.prepare("UPDATE test_decimal SET v = :v WHERE h = :h");
    session.execute(pstmt.bind().setInt("h", 2).setUDTValue("v", v_new));
    verifyDecimalValues(2);
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

  private void verifyUDTResult(ResultSet rs, int k, Object u1, Object u2, Object u3, Object u4) {
    List<Row> rows = rs.all();
    assertEquals(1, rows.size());
    Row row = rows.get(0);
    assertEquals(k, row.getInt("k"));
    assertEquals(u1, row.getUDTValue("u1"));
    assertEquals(u2, row.getUDTValue("u2"));
    assertEquals(u3, row.getUDTValue("u3"));
    assertEquals(u4, row.getUDTValue("u4"));
  }

  @Test
  public void testNestedUDTs() throws Exception {
    {
      //--------------------------------------------------------------------------------------------
      // Test Creating Types.

      createType("udt1", "i int", "t text");
      createType("udt2", "i int", "u1 frozen<udt1>");
      createType("udt3", "i int", "u2 frozen<udt2>");
      createType("udt4", "i int", "u3 frozen<udt3>");

      // Non-frozen UDTs cannot be nested.
      runInvalidStmt("CREATE TYPE invalid_udt (a udt1)");
      runInvalidStmt("CREATE TYPE invalid_udt (a int, b udt1)");

      //--------------------------------------------------------------------------------------------
      // Setup prerequisites (table and sample type values/literals).

      // Create table.
      String tableName = "test_nested_udts";
      String createStmt = String.format("CREATE TABLE %s (k int PRIMARY KEY, " +
                                                "u1 udt1, u2 udt2, u3 udt3, u4 udt4)",
                                        tableName);
      session.execute(createStmt);

      // Get the type descriptions.
      UserType udt1 = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getUserType("udt1");
      UserType udt2 = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getUserType("udt2");
      UserType udt3 = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getUserType("udt3");
      UserType udt4 = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getUserType("udt4");

      // Set up sample type values and literals.
      UDTValue u1 = udt1.newValue().set("i", 1, Integer.class).set("t", "a", String.class);
      String u1_lit = "{ i : 1, t : 'a' }";
      UDTValue u2 = udt2.newValue().set("i", 2, Integer.class).setUDTValue("u1", u1);
      String u2_lit = String.format("{ i : 2, u1 : %s }", u1_lit);
      UDTValue u3 = udt3.newValue().set("i", 3, Integer.class).setUDTValue("u2", u2);
      String u3_lit = String.format("{ i : 3, u2 : %s }", u2_lit);
      UDTValue u4 = udt4.newValue().set("i", 4, Integer.class).setUDTValue("u3", u3);
      String u4_lit = String.format("{ i : 4, u3 : %s }", u3_lit);
      UDTValue u4_new = udt4.newValue().set("i", 5, Integer.class).setUDTValue("u3", u3);
      String u4_new_lit = String.format("{ i : 5, u3 : %s }", u3_lit);

      //--------------------------------------------------------------------------------------------
      // Test Simple DMLs.

      // Insert and Select statements.
      String insert_stmt =
              "INSERT INTO " + tableName + " (k, u1, u2, u3, u4) VALUES (%d, %s, %s, %s, %s)";
      session.execute(String.format(insert_stmt, 1, u1_lit, u2_lit, u3_lit, u4_lit));
      ResultSet rs = session.execute("SELECT * FROM " + tableName + " WHERE k = 1");
      verifyUDTResult(rs, 1, u1, u2, u3, u4);

      // Update and Select statements.
      String update_stmt = "UPDATE %s SET u4 = %s where k = 1";
      session.execute(String.format(update_stmt, tableName, u4_new_lit));
      rs = session.execute("SELECT * FROM " + tableName + " WHERE k = 1");
      verifyUDTResult(rs, 1, u1, u2, u3, u4_new);

      //--------------------------------------------------------------------------------------------
      // Test DMLs with Bind Variables.

      // Insert statement.
      insert_stmt = "INSERT INTO " + tableName + "(k, u1, u2, u3, u4) VALUES (?, ?, ?, ?, ?);";
      session.execute(insert_stmt, new Integer(2), u1, u2, u3, u4);
      rs = session.execute("SELECT * FROM " + tableName + " WHERE k = 2");
      verifyUDTResult(rs, 2, u1, u2, u3, u4);

      // Update statement.
      PreparedStatement pstmt =
              session.prepare("UPDATE " + tableName + " SET u4 = :u WHERE k = :k");
      session.execute(pstmt.bind().setInt("k", 2).setUDTValue("u", u4_new));
      rs = session.execute("SELECT * FROM " + tableName + " WHERE k = 2");
      verifyUDTResult(rs, 2, u1, u2, u3, u4_new);

      // Test Invalid Stmts.
      runInvalidStmt("SELECT * FROM " + tableName + " WHERE u1 = { i : 1, t : 'a' }",
                     "Incomparable Datatypes");
      runInvalidStmt("SELECT * FROM " + tableName + " IF u1 = { i : 1, t : 'a' }",
                     "Incomparable Datatypes");

      //--------------------------------------------------------------------------------------------
      // Test primary keys (uniqueness/equality).

      String tableName2 = tableName + "_pk";
      session.execute("CREATE TABLE " + tableName2 + " (h int, r frozen<udt4>, v int, " +
                              "PRIMARY KEY (h, r)) WITH transactions = { 'enabled' : true }");

      session.execute("CREATE INDEX " + tableName2 + "_idx ON " + tableName2 + "(r,v)");

      waitForReadPermsOnAllIndexes(tableName2);

      insert_stmt = "INSERT INTO %s (h, r, v) values (1, %s, %d)";

      // Format for r-values (udt4), order/equality will be defined by the values of the primitive
      // types in order of importance: i, u3.i, u2.i, u1.i, u1.t
      String rval = "{i:%d,u3:{i:%d,u2:{i:%d,u1:{i:%d,t:'%s'}}}}";
      List<String> rvals = new ArrayList<>();
      rvals.add(String.format(rval, 1, 1, 1, 1, 'a')); // r0
      rvals.add(String.format(rval, 1, 1, 1, 1, 'b')); // r1
      rvals.add(String.format(rval, 1, 1, 1, 2, 'a')); // r2
      rvals.add(String.format(rval, 1, 3, 0, 1, 'a')); // r3
      rvals.add(String.format(rval, 2, 0, 0, 0, 'b')); // r4
      rvals.add(String.format(rval, 2, 0, 1, 0, 'a')); // r5

      // Insert some values.
      List<String> expectedRows = new ArrayList<>();
      String rowVal = "Row[1, %s, %d]";
      for (int i = 0; i < rvals.size(); i++) {
        session.execute(String.format(insert_stmt, tableName2, rvals.get(i), i));
        expectedRows.add(String.format(rowVal, rvals.get(i), i));
      }

      assertQueryRowsOrdered("SELECT * FROM " + tableName2,
                             expectedRows.toArray(new String[0]));

      // Test alternative literals -- should update the existing rows.
      // r0 but with spaces in the literal.
      session.execute(String.format(insert_stmt,
                                    tableName2,
                                    "{i : 1 , u3 : {i:1,u2 : {i : 1,u1 : {i:1, t:'a'}}}}",
                                    10));
      expectedRows.set(0, String.format(rowVal, rvals.get(0), 10));

      // r2 but with u2's fields reordered.
      session.execute(String.format(insert_stmt,
                                    tableName2,
                                    "{i:1,u3:{i:1,u2:{u1:{i:2,t:'a'}, i:1}}}",
                                    12));
      expectedRows.set(2, String.format(rowVal, rvals.get(2), 12));

      // r3 but all fields reordered and spaces.
      session.execute(String.format(insert_stmt,
                                    tableName2,
                                    "{u3 : {u2:{u1 : {i:1 , t:'a'}, i:0}, i : 3}, i:1}",
                                    13));
      expectedRows.set(3, String.format(rowVal, rvals.get(3), 13));

      assertQueryRowsOrdered("SELECT * FROM " + tableName2,
                             expectedRows.toArray(new String[0]));

      // Test inequality queries.
      assertQueryRowsOrdered("SELECT * FROM " + tableName2 + " WHERE h=1 AND r<" + rvals.get(3),
                             expectedRows.subList(0, 3).toArray(new String[0]));
      assertQueryRowsOrdered("SELECT * FROM " + tableName2 + " WHERE h=1 AND r>=" + rvals.get(2),
                             expectedRows.subList(2, expectedRows.size()).toArray(new String[0]));

      // Test index query.
      String select_stmt = "%s SELECT * FROM %s WHERE r = %s";
      assertQuery(String.format(select_stmt, "", tableName2, rvals.get(1)), expectedRows.get(1));
      rs = session.execute(String.format(select_stmt, "EXPLAIN", tableName2,
                                                  rvals.get(1)));
      assertTrue("Should use index scan", rs.all().toString().contains("Index Only Scan"));

      session.execute("DROP TABLE " + tableName2);

      //--------------------------------------------------------------------------------------------
      // Test Dropping types.

      // Types cannot be dropped while they are used in a table.
      runInvalidStmt("DROP TYPE udt4", "is used in column u4 of table " + tableName);

      // Drop the table to remove dependencies to it.
      session.execute("DROP TABLE " + tableName);

      // Types cannot be dropped while they are used in another type.
      runInvalidStmt("DROP TYPE udt1", "is used in field u1 of type 'udt2'");
      runInvalidStmt("DROP TYPE udt3", "is used in field u3 of type 'udt4'");

      session.execute("DROP TYPE udt4");
      session.execute("DROP TYPE udt3");
      session.execute("DROP TYPE udt2");
      session.execute("DROP TYPE udt1");
    }
  }

  @Test
  public void testNestedUDTsWithCollections() throws Exception {
    {

      //--------------------------------------------------------------------------------------------
      // Test Creating Types.

      createType("udt0", "i int", "t text");
      createType("udt1", "i int", "t text");
      createType("udt2", "u frozen<set<frozen<udt1>>>");
      createType("udt3", "u frozen<map<frozen<udt1>, frozen<udt0>>>");
      createType("udt4", "u frozen<list<frozen<udt2>>>");

      // Top-level collections must be frozen.
      runInvalidStmt("CREATE TYPE invalid_udt (u list<udt1>)");
      runInvalidStmt("CREATE TYPE invalid_udt (u list<frozen<udt1>>)");
      runInvalidStmt("CREATE TYPE invalid_udt (u map<frozen<udt2>, frozen<udt3>>)");

      // UDTs inside collections must also be frozen (same as top-level ones).
      runInvalidStmt("CREATE TYPE invalid_udt (u frozen<set<udt1>>)");
      runInvalidStmt("CREATE TYPE invalid_udt (u frozen<map<frozen<udt2>, udt3>>)");

      //--------------------------------------------------------------------------------------------
      // Setup prerequisites (table and sample type values/literals).

      // Create table.
      String tableName = "test_coll_with_nested_udts";
      session.execute("CREATE TABLE " + tableName +
                              " (k int primary key, u1 udt1, u2 udt2, u3 udt3, u4 udt4)");

      // Get type metadata.
      UserType udt0 = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getUserType("udt0");
      UserType udt1 = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getUserType("udt1");
      UserType udt2 = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getUserType("udt2");
      UserType udt3 = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getUserType("udt3");
      UserType udt4 = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getUserType("udt4");

      // Set up sample values/literals.
      UDTValue u1_1 = udt1.newValue().set("i", 1, Integer.class).set("t", "a", String.class);
      String u1_lit1 = "{ i : 1, t : 'a' }";
      UDTValue u1_2 = udt1.newValue().set("i", 2, Integer.class).set("t", "b", String.class);
      String u1_lit2 = "{ i : 2, t : 'b' }";
      UDTValue u0_1 = udt0.newValue().set("i", 3, Integer.class).set("t", "c", String.class);
      String u0_lit1 = "{ i : 3, t : 'c' }";
      UDTValue u0_2 = udt0.newValue().set("i", 4, Integer.class).set("t", "d", String.class);
      String u0_lit2 = "{ i : 4, t : 'd' }";

      Set<UDTValue> u2_set = new HashSet<>();
      u2_set.add(u1_1);
      u2_set.add(u1_2);
      UDTValue u2 = udt2.newValue().setSet("u", u2_set);
      String u2_lit = String.format("{ u : { %s, %s } }", u1_lit1, u1_lit2);

      Map<UDTValue, UDTValue> u3_map = new HashMap<>();
      u3_map.put(u1_1, u0_1);
      u3_map.put(u1_2, u0_2);
      UDTValue u3 = udt3.newValue().setMap("u", u3_map);
      String u3_lit = String.format("{u:{%s : %s, %s : %s}}", u1_lit1, u0_lit1, u1_lit2, u0_lit2);

      List<UDTValue> u4_list = new ArrayList<>();
      u4_list.add(u2);
      u4_list.add(u2);
      UDTValue u4 = udt4.newValue().setList("u", u4_list);
      String u4_lit = String.format("{ u : [ %s, %s ] }", u2_lit, u2_lit);

      List<UDTValue> u4_new_list = new ArrayList<>(); // used for update.
      u4_new_list.add(u2);
      UDTValue u4_new = udt4.newValue().setList("u", u4_new_list);
      String u4_new_lit = String.format("{ u : [ %s ] }", u2_lit);

      //--------------------------------------------------------------------------------------------
      // Test Simple DMLs.

      // Insert stmt.
      String insert_stmt = "INSERT INTO %s (k, u1, u2, u3, u4) VALUES (%d, %s, %s, %s, %s)";
      session.execute(String.format(insert_stmt, tableName, 1, u1_lit1, u2_lit, u3_lit, u4_lit));
      ResultSet rs = session.execute("SELECT * FROM " + tableName + " WHERE k = 1");
      verifyUDTResult(rs, 1, u1_1, u2, u3, u4);

      // Update stmt.
      PreparedStatement pstmt =
              session.prepare("UPDATE " + tableName + " SET u4 = :u WHERE k = " + ":k");
      session.execute(pstmt.bind().setInt("k", 1).setUDTValue("u", u4_new));
      rs = session.execute("SELECT * FROM " + tableName + " WHERE k = 1");
      verifyUDTResult(rs, 1, u1_1, u2, u3, u4_new);


      //--------------------------------------------------------------------------------------------
      // Test primary keys (uniqueness/equality).

      String tableName2 = tableName + "_pk";
      session.execute("CREATE TABLE " + tableName2 + " (u4 frozen<udt4> PRIMARY KEY, v int)");
      insert_stmt = "INSERT INTO %s (u4, v) values (%s, %d)";
      session.execute(String.format(insert_stmt, tableName2, u4_lit, 1));
      session.execute(String.format(insert_stmt, tableName2, u4_new_lit, 2));

      // Test an alternative literal for u4_new -- should update existing row.
      String u4_new_lit_alt = String.format("{ u : [ { u : { %s, %s, %s, %s} } ] }",
                                             u1_lit2,
                                             u1_lit1,
                                             u1_lit1,
                                             u1_lit2);

      session.execute(String.format(insert_stmt, tableName2, u4_new_lit_alt, 3));

      assertQueryRowsUnordered("SELECT * FROM " + tableName2,
                               "Row[{u:[{u:{{i:1,t:'a'},{i:2,t:'b'}}}," +
                                       "{u:{{i:1,t:'a'},{i:2,t:'b'}}}]}, 1]",
                               "Row[{u:[{u:{{i:1,t:'a'},{i:2,t:'b'}}}]}, 3]");

      //--------------------------------------------------------------------------------------------
      // Test UDTs inside collections in table.

      String tableName3 = tableName + "_coll";
      session.execute("CREATE TABLE " + tableName3 +
                              " (k frozen<set<frozen<udt4>>> PRIMARY KEY, v list<frozen<udt2>>)");

      // Set up sample values.
      Set<UDTValue> k = new HashSet<>();
      k.add(u4);
      k.add(u4_new);
      String k_lit = "{" + u4_lit + ", " + u4_new_lit + "}";

      List<UDTValue> v1 = new ArrayList<>();
      v1.add(u2);
      v1.add(u2);
      String v1_lit =  "[" + u2_lit + ", " + u2_lit + "]";

      List<UDTValue> v2 = new ArrayList<>();
      v2.add(u2);
      v2.add(u2);
      v2.add(u2);

      // Test literals.
      session.execute(String.format("INSERT INTO " + tableName3 + "(k, v) VALUES (%s, %s)",
                                    k_lit, v1_lit));
      List<Row> rows = session.execute("SELECT * FROM " + tableName3).all();
      assertEquals(1, rows.size());
      assertEquals(k, rows.get(0).getSet("k", UDTValue.class));
      assertEquals(v1, rows.get(0).getList("v", UDTValue.class));

      // Test Bind Variables.
      pstmt = session.prepare("UPDATE " + tableName3 + " SET v = :v where k = :k");
      session.execute(pstmt.bind().setSet("k", k).setList("v", v2));
      rows = session.execute("SELECT * FROM " + tableName3).all();
      assertEquals(1, rows.size());
      assertEquals(k, rows.get(0).getSet("k", UDTValue.class));
      assertEquals(v2, rows.get(0).getList("v", UDTValue.class));

      //--------------------------------------------------------------------------------------------
      // Test dropping types.

      // udt4 is referenced in tableName & tableName2 & tableName3. Possible errors:
      // tableName : It is used in column u4 of table test_coll_with_nested_udts
      // tableName2: It is used in column u4 of table test_coll_with_nested_udts_pk
      // tableName3: It is used in column k of table test_coll_with_nested_udts_coll
      runInvalidStmt("DROP TYPE udt4", "is used in column");
      // udt2 is referenced in tableName & tableName2 & tableName3. Possible errors:
      // tableName : It is used in column u2 of table test_coll_with_nested_udts
      // tableName2: It is used in column u4 of table test_coll_with_nested_udts_pk
      // tableName3: It is used in column k of table test_coll_with_nested_udts_coll
      runInvalidStmt("DROP TYPE udt2", "is used in column");
      session.execute("DROP TABLE " + tableName3);

      // udt4 is referenced in tableName & tableName2. Possible errors:
      // tableName : It is used in column u4 of table test_coll_with_nested_udts
      // tableName2: It is used in column u4 of table test_coll_with_nested_udts_pk
      runInvalidStmt("DROP TYPE udt4", "is used in column u4 of table " + tableName);
      session.execute("DROP TABLE " + tableName2);

      // Types cannot be dropped while they are used in a table.
      runInvalidStmt("DROP TYPE udt4", "is used in column u4 of table " + tableName);

      // Drop the table to remove dependencies to it.
      session.execute("DROP TABLE " + tableName);

      // Types cannot be dropped while they are used in another type.
      runInvalidStmt("DROP TYPE udt0", "is used in field u of type 'udt3'");
      runInvalidStmt("DROP TYPE udt2", "is used in field u of type 'udt4'");

      // Clean up.
      session.execute("DROP TYPE udt4");
      session.execute("DROP TYPE udt3");
      session.execute("DROP TYPE udt2");
      session.execute("DROP TYPE udt1");
      session.execute("DROP TYPE udt0");
    }
  }

  @Test
  public void testNull() throws Exception {
    session.execute("CREATE TYPE test_type (a int)");
    //----------------------------------------------------------------------------------------------
    // Testing UDT.
    //----------------------------------------------------------------------------------------------
    String tableName = "test_udt";
    session.execute("CREATE TABLE " + tableName + "(h int, r int, " +
        "vt test_type, primary key((h), r))");
    String insertTemplate = "INSERT INTO %s (h, r, vt) VALUES (%d, %d, %s)";
    String selectTemplate = "SELECT * FROM %s WHERE h = %d AND r = %d";

    { // INSERT: ROW(1, 1, {a:1})
      session.execute(String.format(insertTemplate, tableName, 1, 1, "{a:1}"));

      Iterator<Row> rows = runSelect(String.format(selectTemplate, tableName, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("vt");
      assertEquals(1, val.getInt("a"));
      assertFalse(rows.hasNext());
    }

    { // INSERT: ROW(1, 1, null)
      session.execute(String.format(insertTemplate, tableName, 1, 1, "null"));

      Iterator<Row> rows = runSelect(String.format(selectTemplate, tableName, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("vt");
      assertEquals(null, val);
      assertFalse(rows.hasNext());
    }

    { // INSERT: ROW(1, 1, {a:1})
      session.execute(String.format(insertTemplate, tableName, 1, 1, "{a:1}"));

      Iterator<Row> rows = runSelect(String.format(selectTemplate, tableName, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("vt");
      assertEquals(1, val.getInt("a"));
      assertFalse(rows.hasNext());
    }

    { // INSERT: ROW(1, 1, {a:null})
      session.execute(String.format(insertTemplate, tableName, 1, 1, "{a:null}"));

      Iterator<Row> rows = runSelect(String.format(selectTemplate, tableName, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("vt");
      assertEquals(null, val);
      assertFalse(rows.hasNext());
    }

    runInvalidStmt(String.format(insertTemplate, tableName, 1, 1, "{null:1}"),
                   "Invalid Arguments. Field names for user-defined types must be field reference");

    //----------------------------------------------------------------------------------------------
    // Testing FROZEN<UDT>.
    //----------------------------------------------------------------------------------------------
    tableName = "test_frozen_udt";
    session.execute("CREATE TABLE " + tableName + "(h int, r int, " +
        "vt frozen<test_type>, primary key((h), r))");

    { // INSERT: ROW(1, 1, {a:1})
      session.execute(String.format(insertTemplate, tableName, 1, 1, "{a:1}"));

      Iterator<Row> rows = runSelect(String.format(selectTemplate, tableName, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("vt");
      assertEquals(1, val.getInt("a"));
      assertFalse(rows.hasNext());
    }

    { // INSERT: ROW(1, 1, null)
      session.execute(String.format(insertTemplate, tableName, 1, 1, "null"));

      Iterator<Row> rows = runSelect(String.format(selectTemplate, tableName, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("vt");
      assertEquals(null, val);
      assertFalse(rows.hasNext());
    }

    { // INSERT: ROW(1, 1, {a:1})
      session.execute(String.format(insertTemplate, tableName, 1, 1, "{a:1}"));

      Iterator<Row> rows = runSelect(String.format(selectTemplate, tableName, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("vt");
      assertEquals(1, val.getInt("a"));
      assertFalse(rows.hasNext());
    }

    { // INSERT: ROW(1, 1, {a:null})
      session.execute(String.format(insertTemplate, tableName, 1, 1, "{a:null}"));

      Iterator<Row> rows = runSelect(String.format(selectTemplate, tableName, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("vt");
      assertEquals(0, val.getInt("a"));
      assertFalse(rows.hasNext());
    }

    runInvalidStmt(String.format(insertTemplate, tableName, 1, 1, "{null:1}"),
                   "Invalid Arguments. Field names for user-defined types must be field reference");
  }

  @Test
  public void testNullInPrepared() throws Exception {
    session.execute("CREATE TYPE test_type (a int)");
    UserType udt =
        cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getUserType("test_type");

    String tableName = "test_udt";
    session.execute("CREATE TABLE " + tableName + "(h int, r int, " +
        "vt test_type, primary key((h), r))");
    String selectTemplate = "SELECT * FROM %s WHERE h = %d AND r = %d";

    String insertPrepared = "INSERT INTO %s (h, r, vt) VALUES (?, ?, ?)";
    PreparedStatement stmt = session.prepare(String.format(insertPrepared, tableName));

    { // INSERT: ROW(1, 1, null)
      session.execute(stmt.bind(new Integer(1), new Integer(1), null));

      Iterator<Row> rows = runSelect(String.format(selectTemplate, tableName, 1, 1));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("vt");
      assertEquals(null, val);
      assertFalse(rows.hasNext());
    }

    { // INSERT: ROW(2, 2, {a:null})
      UDTValue u = udt.newValue().set("a", null, Integer.class);
      session.execute(stmt.bind(new Integer(2), new Integer(2), u));

      Iterator<Row> rows = runSelect(String.format(selectTemplate, tableName, 2, 2));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("vt");
      assertEquals(null, val);
      assertFalse(rows.hasNext());
    }

    // UDT: {null:1}
    try {
      UDTValue u = udt.newValue().set(null, 1, Integer.class);
      fail("UDT initialization did not fail with Null as field name");
    } catch (java.lang.IllegalArgumentException e) {
      LOG.info("Expected exception", e);
      assertTrue(e.getMessage().contains("null is not a field defined in this UDT"));
    }

    { // INSERT: ROW(3, 3, {a:1})
      UDTValue u = udt.newValue().set("a", 1, Integer.class);
      session.execute(stmt.bind(new Integer(3), new Integer(3), u));

      Iterator<Row> rows = runSelect(String.format(selectTemplate, tableName, 3, 3));
      Row row = rows.next();
      UDTValue val = row.getUDTValue("vt");
      assertEquals(1, val.getInt("a"));
      assertFalse(rows.hasNext());
    }
  }
}
