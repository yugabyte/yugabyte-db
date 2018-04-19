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
import org.json.*;

import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class TestJson extends BaseCQLTest {

  private void verifyEmptyRows(ResultSet rs, int expected_rows) {
    List<Row> rows = rs.all();
    assertEquals(expected_rows, rows.size());
    for (Row row : rows) {
      assertTrue(row.isNull(0));
    }
  }

  private void verifyResultSet(ResultSet rs) {
    assertEquals(1, rs.getAvailableWithoutFetching());
    Row row = rs.one();
    JSONObject jsonObject = new JSONObject(row.getJson("c2"));
    assertEquals(1, jsonObject.getInt("b"));
    assertEquals(false, jsonObject.getJSONArray("a1").getBoolean(3));
    assertEquals(3.0, jsonObject.getJSONArray("a1").getDouble(2), 1e-9);
    assertEquals(200, jsonObject.getJSONArray("a1").getJSONObject(5).getJSONArray("k2").getInt(1));
    assertEquals(2147483647, jsonObject.getJSONObject("a").getJSONObject("q").getInt("s"));
    assertEquals("hello", jsonObject.getJSONObject("a").getString("f"));
  }

  private void testScalar(String json, int c1) {
    Row row = session.execute(String.format("SELECT * FROM test_json WHERE c2 = '%s'", json)).one();
    assertEquals(c1, row.getInt("c1"));
    assertEquals(json, row.getJson("c2"));
  }

  @Test
  public void testJson() throws Exception {
    String json =
    "{ " +
      "\"b\" : 1," +
      "\"a2\" : {}," +
      "\"a3\" : \"\"," +
      "\"a1\" : [1, 2, 3.0, false, true, { \"k1\" : 1, \"k2\" : [100, 200, 300], \"k3\" : true}]," +
      "\"a\" :" +
      "{" +
        "\"d\" : true," +
        "\"q\" :" +
          "{" +
            "\"p\" : 4294967295," +
            "\"r\" : -2147483648," +
            "\"s\" : 2147483647" +
          "}," +
        "\"g\" : -100," +
        "\"c\" : false," +
        "\"f\" : \"hello\"," +
        "\"x\" : 2.0," +
        "\"y\" : 9223372036854775807," +
        "\"z\" : -9223372036854775808," +
        "\"u\" : 18446744073709551615," +
        "\"l\" : 2147483647.123123e+75," +
        "\"e\" : null" +
      "}" +
    "}";

    session.execute("CREATE TABLE test_json(c1 int, c2 jsonb, PRIMARY KEY(c1))");
    session.execute(String.format("INSERT INTO test_json(c1, c2) values (1, '%s');", json));
    session.execute("INSERT INTO test_json(c1, c2) values (2, '\"abc\"');");
    session.execute("INSERT INTO test_json(c1, c2) values (3, '3');");
    session.execute("INSERT INTO test_json(c1, c2) values (4, 'true');");
    session.execute("INSERT INTO test_json(c1, c2) values (5, 'false');");
    session.execute("INSERT INTO test_json(c1, c2) values (6, 'null');");
    session.execute("INSERT INTO test_json(c1, c2) values (7, '2.0');");
    session.execute("INSERT INTO test_json(c1, c2) values (8, '{\"b\" : 1}');");
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c1 = 1"));

    // Test operators.
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a'->'q'->'p' = " +
        "'4294967295'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a'->'q'->>'p' = " +
        "'4294967295'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a1'->5->'k2'->1 = '200'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a1'->5->'k3' = 'true'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a1'->0 = '1'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a2' = '{}'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a3' = '\"\"'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a'->'e' = 'null'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a'->'c' = 'false'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a'->>'f' = '\"hello\"'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a'->>'x' = '2.000000'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a'->'q' = " +
        "'{\"r\": -2147483648, \"p\": 4294967295,  \"s\": 2147483647}'"));
    verifyResultSet(session.execute("SELECT * FROM test_json WHERE c2->'a'->>'q' = " +
        "'{\"p\":4294967295,\"r\":-2147483648,\"s\":2147483647}'"));

    testScalar("\"abc\"", 2);
    testScalar("3", 3);
    testScalar("true", 4);
    testScalar("false", 5);
    testScalar("null", 6);
    testScalar("2.0", 7);
    assertEquals(2, session.execute("SELECT * FROM test_json WHERE c2->'b' = '1'")
        .getAvailableWithoutFetching());

    // Test invalid operators. We should never return errors, just return an empty result (this
    // is what postgres does).
    assertEquals(0, session.execute("SELECT * FROM test_json WHERE c2->'b'->'c' = '1'")
        .getAvailableWithoutFetching());
    assertEquals(0, session.execute("SELECT * FROM test_json WHERE c2->'z' = '1'")
        .getAvailableWithoutFetching());
    assertEquals(0, session.execute("SELECT * FROM test_json WHERE c2->2 = '1'")
        .getAvailableWithoutFetching());
    assertEquals(0, session.execute("SELECT * FROM test_json WHERE c2->'a'->2 = '1'")
        .getAvailableWithoutFetching());
    assertEquals(0, session.execute("SELECT * FROM test_json WHERE c2->'a1'->'b' = '1'")
        .getAvailableWithoutFetching());
    assertEquals(0, session.execute("SELECT * FROM test_json WHERE c2->'a1'->6 = '1'")
        .getAvailableWithoutFetching());
    assertEquals(0, session.execute("SELECT * FROM test_json WHERE c2->'a2'->'a' = '1'")
        .getAvailableWithoutFetching());
    assertEquals(0, session.execute("SELECT * FROM test_json WHERE c2->'a3'->'a' = '1'")
        .getAvailableWithoutFetching());
    assertEquals(0, session.execute("SELECT * FROM test_json WHERE c2->'a3'->2 = '1'")
        .getAvailableWithoutFetching());

    // Test json operators in select clause.
    assertEquals("4294967295",
        session.execute(
            "SELECT c2->'a'->'q'->>'p' FROM test_json WHERE c1 = 1").one().getString(0));
    assertEquals("200",
        session.execute(
            "SELECT c2->'a1'->5->'k2'->1 FROM test_json WHERE c1 = 1").one().getString(0));
    assertEquals("true",
        session.execute(
            "SELECT c2->'a1'->5->'k3' FROM test_json WHERE c1 = 1").one().getString(0));
    assertEquals("2.000000",
        session.execute(
            "SELECT c2->'a'->>'x' FROM test_json WHERE c1 = 1").one().getString(0));
    assertEquals("{\"p\":4294967295,\"r\":-2147483648,\"s\":2147483647}",
        session.execute(
            "SELECT c2->'a'->'q' FROM test_json WHERE c1 = 1").one().getString(0));
    assertEquals("{\"p\":4294967295,\"r\":-2147483648,\"s\":2147483647}",
        session.execute(
            "SELECT c2->'a'->>'q' FROM test_json WHERE c1 = 1").one().getString(0));
    assertEquals("\"abc\"",
        session.execute(
            "SELECT c2 FROM test_json WHERE c1 = 2").one().getString(0));
    assertEquals("true",
        session.execute(
            "SELECT c2 FROM test_json WHERE c1 = 4").one().getString(0));
    assertEquals("false",
        session.execute(
            "SELECT c2 FROM test_json WHERE c1 = 5").one().getString(0));

    // Json operators in both select and where clause.
    assertEquals("4294967295",
        session.execute(
            "SELECT c2->'a'->'q'->>'p' FROM test_json WHERE c2->'a1'->5->'k2'->1 = '200'").one()
            .getString(0));
    assertEquals("{\"p\":4294967295,\"r\":-2147483648,\"s\":2147483647}",
        session.execute(
            "SELECT c2->'a'->'q' FROM test_json WHERE c2->'a1'->5->'k3' = 'true'").one()
            .getString(0));

    assertEquals("{\"p\":4294967295,\"r\":-2147483648,\"s\":2147483647}",
        session.execute(
            "SELECT c2->'a'->'q' FROM test_json WHERE c2->'a1'->5->'k3' = 'true'").one()
            .getJson("expr"));

    // Test select with invalid operators, which should result in empty rows.
    verifyEmptyRows(session.execute("SELECT c2->'b'->'c' FROM test_json WHERE c1 = 1"), 1);
    verifyEmptyRows(session.execute("SELECT c2->'z' FROM test_json"), 8);
    verifyEmptyRows(session.execute("SELECT c2->2 FROM test_json"), 8);
    verifyEmptyRows(session.execute("SELECT c2->'a'->2 FROM test_json"), 8);
    verifyEmptyRows(session.execute("SELECT c2->'a1'->'b' FROM test_json"), 8);
    verifyEmptyRows(session.execute("SELECT c2->'a1'->6 FROM test_json"), 8);
    verifyEmptyRows(session.execute("SELECT c2->'a1'->'a' FROM test_json"), 8);
    verifyEmptyRows(session.execute("SELECT c2->'a3'->'a' FROM test_json"), 8);
    verifyEmptyRows(session.execute("SELECT c2->'a3'->2 FROM test_json"), 8);
  }
}
