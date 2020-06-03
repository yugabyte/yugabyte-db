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

import org.apache.commons.lang3.RandomStringUtils;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;

import com.datastax.driver.core.BoundStatement;
import com.google.gson.Gson;

// TODO: Rework to encompass both INSERT and SELECT JSON after #890
@RunWith(value = YBTestRunner.class)
public class TestInsertJson extends BaseCQLTest {

  @Override
  public int getTestMethodTimeoutSec() {
    // No need to adjust for TSAN vs. non-TSAN here, it will be done automatically.
    return 240;
  }

  /*
   * TODO: Port the rest of cases from org.apache.cassandra.cql3.validation.entities.JsonTest.java
   * in #890
   */

  //
  // Tests adopted from Apache Cassandra
  //

  @Test
  public void testInsertJsonSyntax() throws Throwable {
    String tableName = generateTableName();
    String selectFromTable = fmt("SELECT * FROM %s;", tableName);
    String insertIntoTable = fmt("INSERT INTO %s", tableName) + " JSON '%s'";
    session.execute(fmt("CREATE TABLE %s (k int primary key, v int);", tableName));

    // Plain statement
    session.execute(fmt("INSERT INTO %s JSON '%s'", tableName,
        "{\"k\": 0, \"v\": 0}"));
    assertQuery(selectFromTable,
        "Row[0, 0]");

    // Omitted value
    session.execute(fmt(insertIntoTable, "{\"k\": 0}"));
    assertQuery(selectFromTable, "Row[0, NULL]");

    // Simple bound statement
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"v\": null}");
    assertQuery(selectFromTable,
        "Row[0, NULL]");

    // Prepared statement
    session.execute(session.prepare(fmt("INSERT INTO %s JSON ?", tableName))
        .bind("{\"v\": 1, \"k\": 0}"));
    assertQuery(selectFromTable,
        "Row[0, 1]");

    // Prepared statement - named bind variable
    session.execute(session.prepare(fmt("INSERT INTO %s JSON :myvar_name", tableName))
        .bind()
        .setString("myvar_name", "{\"v\": 123, \"k\": 0}"));
    assertQuery(selectFromTable,
        "Row[0, 123]");

    String decodeErrorMsg = "Could not decode JSON string as a map";
    assertQueryError(
        fmt(insertIntoTable, "null"),
        decodeErrorMsg);
    assertQueryError(
        fmt(insertIntoTable, "\"notamap\""),
        decodeErrorMsg);
    assertQueryError(
        fmt(insertIntoTable, "12.34"),
        decodeErrorMsg);
    assertQueryError(
        fmt(insertIntoTable, "{\"k\": 0, \"v\": 0, \"zzz\": 0}"),
        "column");
    assertQueryError(
        fmt(insertIntoTable, "{\"k\": 0, \"v\": \"notanint\"}"),
        "unable to make int from 'notanint'");
  }

  @Test
  public void testInsertJsonSyntaxDefaultNullUnset() throws Throwable {
    String tableName = generateTableName();
    session.execute(fmt("CREATE TABLE %s (k int primary key, v1 int, v2 int);", tableName));

    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"v1\": 0, \"v2\": 0}");
    // Leave v1 unset
    session.execute(fmt("INSERT INTO %s JSON ? DEFAULT UNSET", tableName),
        "{\"k\": 0, \"v2\": 2}");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, 0, 2]");

    // Explicit specification DEFAULT NULL
    session.execute(fmt("INSERT INTO %s JSON ? DEFAULT NULL", tableName),
        "{\"k\": 0, \"v2\": 2}");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, NULL, 2]");

    // Implicitly setting v2 to null
    session.execute(fmt("INSERT INTO %s JSON ? DEFAULT NULL", tableName),
        "{\"k\": 0}");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, NULL, NULL]");

    // Mix setting null explicitly with default unset:
    // Set values for all fields
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 1, \"v1\": 1, \"v2\": 1}");
    // Explicitly set v1 to null while leaving v2 unset which retains its value
    session.execute(fmt("INSERT INTO %s JSON ? DEFAULT UNSET", tableName),
        "{\"k\": 1, \"v1\": null}");
    assertQuery(fmt("SELECT * FROM %s WHERE k=1", tableName), ""
        + "Row[1, NULL, 1]");

    // Test string literal instead of bind marker
    session.execute(fmt("INSERT INTO %s JSON ", tableName)
        + "'{\"k\": 2, \"v1\": 2, \"v2\": 2}'");
    // Explicitly set v1 to null while leaving v2 unset which retains its value
    session.execute(fmt("INSERT INTO %s JSON ", tableName)
        + "'{\"k\": 2, \"v1\": null}' "
        + "DEFAULT UNSET");
    assertQuery(fmt("SELECT * FROM %s WHERE k=2", tableName),
        "Row[2, NULL, 2]");
    session.execute(fmt("INSERT INTO %s JSON ", tableName)
        + "'{\"k\": 2}' "
        + "DEFAULT NULL");
    assertQuery(fmt("SELECT * FROM %s WHERE k=2", tableName),
        "Row[2, NULL, NULL]");
  }

  @Test
  public void testCaseSensitivity() throws Throwable {
    String tableName = generateTableName();
    session.execute(fmt("CREATE TABLE %s (k int primary key, \"Foo\" int)", tableName));

    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"\\\"Foo\\\"\": 0}");
    assertQuery(fmt("SELECT * FROM %s", tableName), "Row[0, 0]");

    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"K\": 0, \"\\\"Foo\\\"\": 1}");
    assertQuery(fmt("SELECT * FROM %s", tableName), "Row[0, 1]");

    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"\\\"k\\\"\": 0, \"\\\"Foo\\\"\": 2}");
    assertQuery(fmt("SELECT * FROM %s", tableName), "Row[0, 2]");

    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    /*
    // results should preserve and quote case-sensitive identifiers
    assertRows(execute("SELECT JSON * FROM %s"),
        row("{\"k\": 0, \"\\\"Foo\\\"\": 2}"));
    assertRows(execute("SELECT JSON k, \"Foo\" as foo FROM %s"),
        row("{\"k\": 0, \"foo\": 2}"));
    assertRows(execute("SELECT JSON k, \"Foo\" as \"Bar\" FROM %s"),
        row("{\"k\": 0, \"\\\"Bar\\\"\": 2}"));
    */

    assertQueryError(
        fmt("INSERT INTO %s JSON %s", tableName, "{\"k\": 0, \"foo\": 0}"),
        "foo");
    assertQueryError(
        fmt("INSERT INTO %s JSON %s", tableName, "{\"k\": 0, \"\\\"foo\\\"\": 0}"),
        "foo");
  }

  @Test
  public void testCaseSensitivityInUDTs() throws Throwable {
    String tableName = generateTableName();
    String typeName = generateTypeName();
    session.execute(fmt("CREATE TYPE %s (a int, \"Foo\" int)", typeName));
    session.execute(fmt("CREATE TABLE %s (", tableName)
        + "k int primary key, "
        + "v frozen<" + typeName + ">)");

    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"v\": {\"a\": 0, \"\\\"Foo\\\"\": 0}}");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, {a:0,\"Foo\":0}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    //    assertRows(execute("SELECT JSON k, v FROM %s"),
    //        row("{\"k\": 0, \"v\": {\"a\": 0, \"\\\"Foo\\\"\": 0}}"));

    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"v\": {\"A\": 1, \"\\\"Foo\\\"\": 1}}");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, {a:1,\"Foo\":1}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    //    assertRows(execute("SELECT JSON k, v FROM %s"),
    //        row("{\"k\": 0, \"v\": {\"a\": 1, \"\\\"Foo\\\"\": 1}}"));

    assertQueryError(
        fmt("INSERT INTO %s JSON %s", tableName,
            "{\"k\": 0, \"v\": {\"a\": 0, \"Foo\": 0}}"),
        "foo");
    assertQueryError(
        fmt("INSERT INTO %s JSON %s", tableName,
            "{\"k\": 0, \"v\": {\"a\": 0, \"\\\"foo\\\"\": 0}}"),
        "foo");
  }

  @Test
  public void testInsertJsonSyntaxWithCollections() throws Throwable {
    String tableName = generateTableName();
    session.execute(fmt("CREATE TABLE %s ("
        + "k int PRIMARY KEY, "
        + "m map<text, boolean>, "
        + "mf frozen<map<text, boolean>>, "
        + "s set<int>, "
        + "sf frozen<set<int>>, "
        + "l list<int>, "
        + "lf frozen<list<int>>)", tableName));

    // map
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"m\": {\"a\": true, \"b\": false}}");
    assertQuery(fmt("SELECT k, m FROM %s", tableName), "Row[0, {a=true, b=false}]");

    // frozen map
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"mf\": {\"a\": true, \"b\": false}}");
    assertQuery(fmt("SELECT k, mf FROM %s", tableName), "Row[0, {a=true, b=false}]");

    // set
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"s\": [1, 3, 2, 3]}");
    assertQuery(fmt("SELECT k, s FROM %s", tableName), "Row[0, [1, 2, 3]]");

    // frozen set
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"sf\": [1, 3, 2, 3]}");
    assertQuery(fmt("SELECT k, sf FROM %s", tableName), "Row[0, [1, 2, 3]]");

    // list
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"l\": [1, 3, 2, 3]}");
    assertQuery(fmt("SELECT k, l FROM %s", tableName), "Row[0, [1, 3, 2, 3]]");

    // frozen list
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"lf\": [1, 3, 2, 3]}");
    assertQuery(fmt("SELECT k, lf FROM %s", tableName), "Row[0, [1, 3, 2, 3]]");
  }

  @Test
  public void testInsertJsonSyntaxWithNonNativeMapKeys() throws Throwable {
    // JSON doesn't allow non-string keys, so we accept string representations of any type
    // as map keys and return maps with string keys when necessary.
    String tableName = generateTableName();
    String typeName = generateTypeName();
    session.execute(fmt("CREATE TYPE %s (a int)", typeName));
    session.execute(fmt("CREATE TABLE %s (", tableName)
        + "k int PRIMARY KEY, "
        + "intmap map<int, boolean>, "
        + "bigintmap map<bigint, boolean>, "
        + "varintmap map<varint, boolean>, "
        + "smallintmap map<smallint, boolean>, "
        + "tinyintmap map<tinyint, boolean>, "
        + "booleanmap map<boolean, boolean>, "
        + "floatmap map<float, boolean>, "
        + "doublemap map<double, boolean>, "
        + "decimalmap map<decimal, boolean>, "
        // TODO: Enable after #936
        // + "tuplemap map<frozen<tuple<int, text>>, boolean>, "
        + "udtmap map<frozen<" + typeName + ">, boolean>, "
        + "setmap map<frozen<set<int>>, boolean>, "
        + "listmap map<frozen<list<int>>, boolean>, "
        + "textsetmap map<frozen<set<text>>, boolean>, "
        + "nestedsetmap map<frozen<map<frozen<set<text>>, text>>, boolean>, "
        + "frozensetmap frozen<map<frozen<set<int>>, boolean>>)");

    // int keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"intmap\": {\"0\": true, \"1\": false}}");
    assertQuery(fmt("SELECT k, intmap FROM %s", tableName),
        "Row[0, {0=true, 1=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, intmap FROM %s"),
    // row("{\"k\": 0, \"intmap\": {\"0\": true, \"1\": false}}"));

    // bigint keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"bigintmap\": {\"0\": true, \"1\": false}}");
    assertQuery(fmt("SELECT k, bigintmap FROM %s", tableName),
        "Row[0, {0=true, 1=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, bigintmap FROM %s"),
    // row("{\"k\": 0, \"bigintmap\": {\"0\": true, \"1\": false}}"));

    // varint keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"varintmap\": {\"0\": true, \"1\": false}}");
    assertQuery(fmt("SELECT k, varintmap FROM %s", tableName),
        "Row[0, {0=true, 1=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, varintmap FROM %s"),
    // row("{\"k\": 0, \"varintmap\": {\"0\": true, \"1\": false}}"));

    // smallint keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"smallintmap\": {\"0\": true, \"1\": false}}");
    assertQuery(fmt("SELECT k, smallintmap FROM %s", tableName),
        "Row[0, {0=true, 1=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, smallintmap FROM %s"),
    // row("{\"k\": 0, \"smallintmap\": {\"0\": true, \"1\": false}}"));

    // tinyint keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"tinyintmap\": {\"0\": true, \"1\": false}}");
    assertQuery(fmt("SELECT k, tinyintmap FROM %s", tableName),
        "Row[0, {0=true, 1=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, tinyintmap FROM %s"),
    // row("{\"k\": 0, \"tinyintmap\": {\"0\": true, \"1\": false}}"));

    // boolean keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"booleanmap\": {\"true\": true, \"false\": false}}");
    assertQuery(fmt("SELECT k, booleanmap FROM %s", tableName),
        "Row[0, {false=false, true=true}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, booleanmap FROM %s"),
    // row("{\"k\": 0, \"booleanmap\": {\"false\": false, \"true\": true}}"));

    // float keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"floatmap\": {\"1.23\": true, \"4.56\": false}}");
    assertQuery(fmt("SELECT k, floatmap FROM %s", tableName),
        "Row[0, {1.23=true, 4.56=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, floatmap FROM %s"),
    // row("{\"k\": 0, \"floatmap\": {\"1.23\": true, \"4.56\": false}}"));

    // double keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"doublemap\": {\"1.23\": true, \"4.56\": false}}");
    assertQuery(fmt("SELECT k, doublemap FROM %s", tableName),
        "Row[0, {1.23=true, 4.56=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, doublemap FROM %s"),
    // row("{\"k\": 0, \"doublemap\": {\"1.23\": true, \"4.56\": false}}"));

    // decimal keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"decimalmap\": {\"1.23\": true, \"4.56\": false}}");
    assertQuery(fmt("SELECT k, decimalmap FROM %s", tableName),
        "Row[0, {1.23=true, 4.56=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, decimalmap FROM %s"),
    // row("{\"k\": 0, \"decimalmap\": {\"1.23\": true, \"4.56\": false}}"));

    // TODO: Enable after #936
    // // tuple<int, text> keys
    // execute("INSERT INTO %s JSON ?",
    // "{\"k\": 0, \"tuplemap\": {\"[0, \\\"a\\\"]\": true, \"[1, \\\"b\\\"]\": false}}");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, tuplemap FROM %s"),
    // row("{\"k\": 0, \"tuplemap\": {\"[0, \\\"a\\\"]\": true, \"[1, \\\"b\\\"]\": false}}"));

    // UDT keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"udtmap\": {\"{\\\"a\\\": 0}\": true, \"{\\\"a\\\": 1}\": false}}");
    assertQuery(fmt("SELECT k, udtmap FROM %s", tableName),
        "Row[0, {{a:0}=true, {a:1}=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, udtmap FROM %s"),
    // row("{\"k\": 0, \"udtmap\": {\"{\\\"a\\\": 0}\": true, \"{\\\"a\\\": 1}\": false}}"));

    // set<int> keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"setmap\": {\"[0, 1, 2]\": true, \"[3, 4, 5]\": false}}");
    assertQuery(fmt("SELECT k, setmap FROM %s", tableName),
        "Row[0, {[0, 1, 2]=true, [3, 4, 5]=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, setmap FROM %s"),
    // row("{\"k\": 0, \"setmap\": {\"[0, 1, 2]\": true, \"[3, 4, 5]\": false}}"));

    // list<int> keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"listmap\": {\"[0, 1, 2]\": true, \"[3, 4, 5]\": false}}");
    assertQuery(fmt("SELECT k, listmap FROM %s", tableName),
        "Row[0, {[0, 1, 2]=true, [3, 4, 5]=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, listmap FROM %s"),
    // row("{\"k\": 0, \"listmap\": {\"[0, 1, 2]\": true, \"[3, 4, 5]\": false}}"));
    //
    // set<text> keys
    session.execute(fmt("INSERT INTO %s JSON ?", tableName), "{"
        + "\"k\": 0, "
        + "\"textsetmap\": {\"[\\\"0\\\", \\\"1\\\"]\": true, \"[\\\"3\\\", \\\"4\\\"]\": false}"
        + "}");
    assertQuery(fmt("SELECT k, textsetmap FROM %s", tableName),
        "Row[0, {[0, 1]=true, [3, 4]=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, textsetmap FROM %s"), row(
    // "{\"k\": 0, \"textsetmap\": {\"[\\\"0\\\", \\\"1\\\"]\": true, \"[\\\"3\\\", \\\"4\\\"]\":
    // false}}"));

    // map<set<text>, text> keys
    Gson gson = new Gson(); // Note that output GSON-serialization of strings is already quoted
    String key111 = "[\"0\", \"1\"]";
    String key11 = fmt("{%s: \"a\"}", gson.toJson(key111));
    String key1 = gson.toJson(key11);
    String key211 = "[\"3\", \"4\"]";
    String key21 = fmt("{%s: \"b\"}", gson.toJson(key211));
    String key2 = gson.toJson(key21);
    session.execute(fmt("INSERT INTO %s JSON ?", tableName), "{"
        + "\"k\": 0, "
        + "\"nestedsetmap\": {" + key1 + ": true, " + key2 + ": false}"
        + "}");
    assertQuery(fmt("SELECT k, nestedsetmap FROM %s", tableName),
        "Row[0, {{[0, 1]=a}=true, {[3, 4]=b}=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, nestedsetmap FROM %s"),
    // row("{\"k\": 0, \"nestedsetmap\": {\"" + stringKey1 + "\": true, \"" + stringKey2
    // + "\": false}}"));

    // set<int> keys in a frozen map
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"frozensetmap\": {\"[0, 1, 2]\": true, \"[3, 4, 5]\": false}}");
    assertQuery(fmt("SELECT k, frozensetmap FROM %s", tableName),
        "Row[0, {[0, 1, 2]=true, [3, 4, 5]=false}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON k, frozensetmap FROM %s"),
    // row("{\"k\": 0, \"frozensetmap\": {\"[0, 1, 2]\": true, \"[3, 4, 5]\": false}}"));
  }

  @Ignore // TODO: Enable after #936
  public void testInsertJsonSyntaxWithTuplesAndUDTs() throws Throwable {
    String tableName = generateTableName();
    String typeName = generateTypeName();
    session.execute(fmt("CREATE TYPE %s (a int, b frozen<set<int>>, c tuple<int, int>)", typeName));
    session.execute(fmt("CREATE TABLE %s (", tableName)
        + "k int PRIMARY KEY, "
        + "a frozen<" + typeName + ">, "
        + "b tuple<int, boolean>)");

    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"a\": {\"a\": 0, \"b\": [1, 2, 3], \"c\": [0, 1]}, \"b\": [0, true]}");
    assertQuery(fmt("SELECT k, a.a, a.b, a.c, b FROM %s", tableName),
        "Row[0, 0, [1, 2, 3], [0, 1], [0, true]]");

    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"a\": {\"a\": 0, \"b\": [1, 2, 3], \"c\": null}, \"b\": null}");
    assertQuery(fmt("SELECT k, a.a, a.b, a.c, b FROM %s", tableName),
        "Row[0, 0, [1, 2, 3], NULL, NULL]");
  }

  @Test
  public void emptyStringJsonSerializationTest() throws Throwable {
    String tableName = generateTableName();
    session.execute(fmt("CREATE TABLE %s (id INT, name TEXT, PRIMARY KEY(id))", tableName));
    session.execute(fmt("INSERT INTO %s (id, name) VALUES (0, 'Foo');", tableName));
    session.execute(fmt("INSERT INTO %s (id, name) VALUES (2, '');", tableName));
    session.execute(fmt("INSERT INTO %s (id, name) VALUES (3, null);", tableName));

    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, Foo]" + "Row[2, ]" + "Row[3, NULL]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    //    assertRows(execute("SELECT JSON * FROM %s"),
    //        row("{\"id\": 0, \"name\": \"Foo\"}"),
    //        row("{\"id\": 2, \"name\": \"\"}"),
    //        row("{\"id\": 3, \"name\": null}"));
  }

  @Ignore // TODO: Enable after SELECT JSON in #890 is implemented
  public void testJsonOrdering() throws Throwable {
    String tableName = generateTableName();
    session.execute(fmt("CREATE TABLE %s (a INT, b INT, PRIMARY KEY (a, b))", tableName));
    session.execute(fmt("INSERT INTO %s (a, b) VALUES (20, 30)", tableName));
    session.execute(fmt("INSERT INTO %s (a, b) VALUES (100, 200)", tableName));

    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON a, b FROM %s WHERE a IN (20, 100) ORDER BY b"),
    //     row("{\"a\": 20, \"b\": 30}"),
    //     row("{\"a\": 100, \"b\": 200}"));
    //
    // assertRows(execute("SELECT JSON a, b FROM %s WHERE a IN (20, 100) ORDER BY b DESC"),
    //     row("{\"a\": 100, \"b\": 200}"),
    //     row("{\"a\": 20, \"b\": 30}"));
    //
    // assertRows(execute("SELECT JSON a FROM %s WHERE a IN (20, 100) ORDER BY b DESC"),
    //     row("{\"a\": 100}"),
    //     row("{\"a\": 20}"));
    //
    // // Check ordering with alias
    // assertRows(execute("SELECT JSON a, b as c FROM %s WHERE a IN (20, 100) ORDER BY b"),
    //     row("{\"a\": 20, \"c\": 30}"),
    //     row("{\"a\": 100, \"c\": 200}"));
    //
    // assertRows(execute("SELECT JSON a, b as c FROM %s WHERE a IN (20, 100) ORDER BY b DESC"),
    //     row("{\"a\": 100, \"c\": 200}"),
    //     row("{\"a\": 20, \"c\": 30}"));
    //
    // // Check ordering with CAST
    // assertRows(
    //     execute("SELECT JSON a, CAST(b AS FLOAT) FROM %s WHERE a IN (20, 100) ORDER BY b"),
    //     row("{\"a\": 20, \"cast(b as float)\": 30.0}"),
    //     row("{\"a\": 100, \"cast(b as float)\": 200.0}"));
    //
    // assertRows(
    //     execute("SELECT JSON a, CAST(b AS FLOAT) FROM %s WHERE a IN (20, 100) ORDER BY b DESC"),
    //     row("{\"a\": 100, \"cast(b as float)\": 200.0}"),
    //     row("{\"a\": 20, \"cast(b as float)\": 30.0}"));
  }

  @Ignore // TODO: Enable after SELECT JSON in #890 is implemented
  public void testJsonWithNaNAndInfinity() throws Throwable {
    String tableName = generateTableName();
    session.execute(fmt(
        "CREATE TABLE %s (", tableName)
        + "pk int PRIMARY KEY,"
        + "f1 float,"
        + "f2 float,"
        + "f3 float,"
        + "d1 double,"
        + "d2 double,"
        + "d3 double)");
    session.execute(
        fmt("INSERT INTO %s (pk, f1, f2, f3, d1, d2, d3) VALUES (?, ?, ?, ?, ?, ?, ?)", tableName),
        1, Float.NaN, Float.POSITIVE_INFINITY, Float.NEGATIVE_INFINITY, Double.NaN,
        Double.POSITIVE_INFINITY, Double.NEGATIVE_INFINITY);

    // JSON does not support NaN, Infinity and -Infinity values.
    // Most of the parser convert them into null.
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    // assertRows(execute("SELECT JSON * FROM %s"), row(
    //     "{\"pk\": 1, \"d1\": null, \"d2\": null, \"d3\": null, "
    //     + "\"f1\": null, \"f2\": null, \"f3\": null}"));
  }

  //
  // Custom tests
  //

  @Test
  public void nullForPrimaryKeySimple() throws Throwable {
    String tableName = generateTableName();
    session.execute(fmt(
        "CREATE TABLE %s (", tableName)
        + "pk int PRIMARY KEY, "
        + "i int)");

    // Invalid
    assertQueryError(
        fmt("INSERT INTO %s JSON '{}'", tableName),
        "null argument for primary key");
    assertQueryError(
        fmt("INSERT INTO %s JSON '{\"i\": 0}'", tableName),
        "null argument for primary key");

    // Valid
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 1, \"i\": 2}");
    assertQuery(fmt("SELECT pk, i FROM %s", tableName),
        "Row[1, 2]");

  }

  @Test
  public void nullForPrimaryKeyComplex() throws Throwable {
    String tableName = generateTableName();
    session.execute(fmt(
        "CREATE TABLE %s (", tableName)
        + "pk1 int, "
        + "pk2 int, "
        + "i int, "
        + "PRIMARY KEY(pk1, pk2))");

    // Invalid
    assertQueryError(
        fmt("INSERT INTO %s JSON '{}'", tableName),
        "null argument for primary key");
    assertQueryError(
        fmt("INSERT INTO %s JSON '{\"pk1\": 0, \"i\": 0}'", tableName),
        "null argument for primary key");
    assertQueryError(
        fmt("INSERT INTO %s JSON '{\"pk2\": 0}'", tableName),
        "null argument for primary key");

    // Valid
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk1\": 1, \"pk2\": 2, \"i\": 3}");
    assertQuery(fmt("SELECT pk1, pk2, i FROM %s", tableName),
        "Row[1, 2, 3]");
  }

  @Test
  public void longNumericLiterals() throws Throwable {
    String tableName = generateTableName();
    session.execute(fmt(
        "CREATE TABLE %s (", tableName)
        + "pk int PRIMARY KEY, "
        // Integers
        + "i int, "
        + "vi varint, "
        // Fractions
        + "f float, "
        + "d double, "
        + "dc decimal)");

    String longInt = "999888777666555444333222111";
    String longFrac = "999888777666555444333222111.111222333444555666777888999";

    // Integers
    assertQueryError(
        fmt("INSERT INTO %s JSON '{\"pk\": 0, \"i\": %s }'", tableName, longInt),
        "invalid integer");
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"vi\": " + longInt + "  }");
    assertQuery(fmt("SELECT vi FROM %s", tableName),
        "Row[" + longInt + "]");
    // TODO: Add SELECT JSON test after #890

    // Fractions
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"f\": " + longFrac + " }");
    session.execute(fmt("INSERT INTO %s JSON ? DEFAULT UNSET", tableName),
        "{\"pk\": 0, \"d\": " + longFrac + " }");
    session.execute(fmt("INSERT INTO %s JSON ? DEFAULT UNSET", tableName),
        "{\"pk\": 0, \"dc\": " + longFrac + " }");
    assertQuery(fmt("SELECT f, d, dc FROM %s", tableName),
        "Row[9.998888E26, 9.998887776665555E26, " + longFrac + "]");
    // TODO: Add SELECT JSON test after #890
  }

  @Test
  public void variousDecimalFormats() throws Throwable {
    // TODO: Would be cool to CAST the results as text, but issue #963 prevents that
    String tableName = generateTableName();
    session.execute(fmt(
        "CREATE TABLE %s (", tableName)
        + "pk int PRIMARY KEY, "
        + "dc decimal)");

    // Simple scientific notation
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"dc\": 1e2 }");
    assertQuery(fmt("SELECT dc FROM %s", tableName),
        "Row[1E+2]");
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"dc\": 1e+2 }");
    assertQuery(fmt("SELECT dc FROM %s", tableName),
        "Row[1E+2]");
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"dc\": 1e-2 }");
    assertQuery(fmt("SELECT dc FROM %s", tableName),
        "Row[0.01]");

    // Scientific notation with decimal separator
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"dc\": \"1.e2\" }");
    assertQuery(fmt("SELECT dc FROM %s", tableName),
        "Row[1E+2]");
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"dc\": \"1.e-2\" }");
    assertQuery(fmt("SELECT dc FROM %s", tableName),
        "Row[0.01]");
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"dc\": \".1e3\" }");
    assertQuery(fmt("SELECT dc FROM %s", tableName),
        "Row[1E+2]");
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"dc\": \".1e-1\" }");
    assertQuery(fmt("SELECT dc FROM %s", tableName),
        "Row[0.01]");

    // Scientific notation with integral and fractional parts
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"dc\": 1.2e3 }");
    assertQuery(fmt("SELECT dc FROM %s", tableName),
        "Row[1.2E+3]");
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"dc\": 1.2e-3 }");
    assertQuery(fmt("SELECT dc FROM %s", tableName),
        "Row[0.0012]");
  }

  /** Missing fields in UDT should always be set to NULL, even with DEFAULT UNSET */
  @Test
  public void partialUdtWithDefaultNullUnset() throws Throwable {
    String tableName = generateTableName();
    String typeName = generateTypeName();
    session.execute(fmt("CREATE TYPE %s (a int, b int)", typeName));
    session.execute(fmt("CREATE TABLE %s (", tableName)
        + "k int primary key, "
        + "v " + typeName + ")");

    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"v\": {\"a\": 10, \"b\": 20}}");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, {a:10,b:20}]");

    // No default specified
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"v\": {\"b\": 21}}");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, {a:NULL,b:21}]");

    // DEFAULT NULL
    session.execute(fmt("INSERT INTO %s JSON ? DEFAULT NULL", tableName),
        "{\"k\": 0, \"v\": {\"a\": 12}}");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, {a:12,b:NULL}]");

    // DEFAULT UNSET
    session.execute(fmt("INSERT INTO %s JSON ? DEFAULT UNSET", tableName),
        "{\"k\": 0, \"v\": {\"b\": 22}}");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, {a:NULL,b:22}]");
  }

  @Test
  public void stringsAsValues() throws Throwable {
    String tableName = generateTableName();
    String typeName = generateTypeName();
    session.execute(fmt("CREATE TYPE %s (b boolean)", typeName));
    session.execute(fmt("CREATE TABLE %s (", tableName)
        + "k int PRIMARY KEY, "
        + "i int, "
        + "f float, "
        + "b boolean, "
        // TODO: Enable after #936
        // + "t tuple<int, boolean>, "
        + "blist list<boolean>, "
        + "intmap map<int, boolean>, "
        + "udtmap map<frozen<" + typeName + ">, boolean>)");

    // integer
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"i\": \"1\"}");
    assertQuery(fmt("SELECT i FROM %s", tableName),
        "Row[1]");

    // float
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"f\": \"12.34\"}");
    assertQuery(fmt("SELECT f FROM %s", tableName),
        "Row[12.34]");

    // boolean (should be case-insensitive)
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"b\": \"TrUe\"}");
    assertQuery(fmt("SELECT b FROM %s", tableName),
        "Row[true]");

    // boolean list
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"blist\": \"[\\\"fAlSe\\\", \\\"FaLsE\\\", \\\"trUE\\\"]\"}");
    assertQuery(fmt("SELECT blist FROM %s", tableName),
        "Row[[false, false, true]]");

    // integer map
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"k\": 0, \"intmap\": \"{\\\"1\\\": \\\"fAlSe\\\"}\"}");
    assertQuery(fmt("SELECT intmap FROM %s", tableName),
        "Row[{1=false}]");

    // UDT map
    Gson gson = new Gson();
    String udt = "{\"b\": \"fAlSe\"}";
    String udtMap = fmt("{%s: \"trUE\"}", gson.toJson(udt));
    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        fmt("{\"k\": 0, \"udtmap\": %s}", gson.toJson(udtMap)));
    assertQuery(fmt("SELECT udtmap FROM %s", tableName),
        "Row[{{b:false}=true}]");
  }

  @Test
  public void keywordsAsNamesAreAllowed() throws Throwable {
    String tableName = generateTableName();
    String typeName = generateTypeName();
    session.execute(fmt("CREATE TYPE %s (\"table\" int, \"true\" int)", typeName));
    session.execute(fmt("CREATE TABLE %s (", tableName)
        + "\"null\" int primary key, "
        + "\"column\" frozen<" + typeName + ">)");

    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"null\": 0, \"column\": {\"table\": 0, \"true\": 0}}");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, {\"table\":0,true:0}]");
    // TODO: Uncomment/adapt after SELECT JSON in #890 is implemented
    //    assertRows(execute("SELECT JSON \"null\", \"column\" FROM %s"),
    //        row("{\"null\": 0, \"column\": {\"table\": 0, \"true\": 0}}"));
  }

  @Test
  public void duplicateColumns() throws Throwable {
    String tableName = generateTableName();
    session.execute(fmt("CREATE TABLE %s (pk int PRIMARY KEY, t text)", tableName));

    session.execute(fmt("INSERT INTO %s JSON ?", tableName),
        "{\"pk\": 0, \"t\": \"Eeny\", \"t\": \"Meeny\", \"t\": \"Miny\", \"t\": \"Moe\" }");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, Moe]");
  }

  @Test
  public void ttl() throws Throwable {
    // TODO(alex): This very simple test was encountering issue #4663. To work around this failure,
    // I'm increasing TTL from 2 to 10 seconds, but I think that it's reasonable to expect this test
    // to work reliably even with 2 sec TTL. As such, after #4663 is fixed TTL should be reduced
    // back to 2.

    // This is a very simple TTL test, since it piggybacks on the
    // implementation tested in TestInsertValues
    int ttlInSeconds = 10;
    String tableName = generateTableName();
    session.execute(fmt("CREATE TABLE %s (pk int PRIMARY KEY, t text)", tableName));

    session.execute(fmt("INSERT INTO %s JSON ? USING TTL %d", tableName, ttlInSeconds),
        "{\"pk\": 0, \"t\": \"Hello!\" }");
    assertQuery(fmt("SELECT * FROM %s", tableName),
        "Row[0, Hello!]");
    TestUtils.waitForTTL(ttlInSeconds * 1000L);
    assertNoRow(fmt("SELECT * FROM %s", tableName));
  }

  @Test
  public void returnsStatusAsRow() throws Throwable {
    String tableName = generateTableName();
    session.execute(fmt("CREATE TABLE %s (pk int PRIMARY KEY, t varchar)", tableName));

    BoundStatement stmt = session
        .prepare(fmt("INSERT INTO %s JSON ? RETURNS STATUS AS ROW", tableName))
        .bind("{\"pk\": 0, \"t\": \"Hello!\" }");
    assertQuery(stmt,
        "Columns[[applied](boolean), [message](varchar), pk(int), t(varchar)]",
        "Row[true, NULL, NULL, NULL]");
  }

  //
  // Helpers
  //

  /**
   * Generate a randomized table name. For some reason, DROP TABLE isn't blocking, so reusing the
   * same table name causes problem with conflicting schemas.
   */
  private String generateTableName() {
    return "test_insert_json_" + RandomStringUtils.randomAlphanumeric(10);
  }

  private String generateTypeName() {
    return "test_type_" + RandomStringUtils.randomAlphanumeric(10);
  }

  private String fmt(String format, Object... args) {
    return String.format(format, args);
  }
}
