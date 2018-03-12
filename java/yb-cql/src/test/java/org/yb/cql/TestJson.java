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

import com.datastax.driver.core.Row;
import org.junit.Test;
import org.json.*;

import static org.junit.Assert.assertEquals;

public class TestJson extends BaseCQLTest {

  @Test
  public void testJson() throws Exception {
    String json =
    "{ " +
      "\"b\" : 1," +
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
    Row row = session.execute("SELECT * FROM test_json").one();
    JSONObject jsonObject = new JSONObject(row.getJson("c2"));
    assertEquals(1, jsonObject.getInt("b"));
    assertEquals(false, jsonObject.getJSONArray("a1").getBoolean(3));
    assertEquals(3.0, jsonObject.getJSONArray("a1").getDouble(2), 1e-9);
    assertEquals(200, jsonObject.getJSONArray("a1").getJSONObject(5).getJSONArray("k2").getInt(1));
    assertEquals(2147483647, jsonObject.getJSONObject("a").getJSONObject("q").getInt("s"));
    assertEquals("hello", jsonObject.getJSONObject("a").getString("f"));
  }
}
