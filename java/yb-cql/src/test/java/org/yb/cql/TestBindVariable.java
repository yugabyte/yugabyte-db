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

import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.util.*;
import java.math.BigDecimal;
import java.net.InetAddress;

import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.SimpleStatement;
import com.google.common.collect.Lists;

import org.junit.Test;
import org.yb.client.TestUtils;
import org.yb.util.Pair;
import org.json.JSONObject;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestBindVariable extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestBindVariable.class);

  protected static enum BindCollAssignmentByColName { ON, OFF };

  @Override
  protected Map<String, String> getTServerFlags() {
    // testPrepareInsertBindLongJson needs more memory than the default of 5%.
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("read_buffer_memory_limit", "-100");
    return flagMap;
  }

  private void testInvalidBindStatement(String stmt, Object... values) {
    try {
      session.execute(stmt, values);
      fail("Statement \"" + stmt + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.QueryValidationException e) {
      LOG.info("Expected exception", e);
    }
  }

  private void testInvalidBindStatement(String stmt, Map<String,Object> values) {
    try {
      session.execute(stmt, values);
      fail("Statement \"" + stmt + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.QueryValidationException e) {
      LOG.info("Expected exception", e);
    }
  }

  @Test
  public void testSelectBind() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    setupTable("test_bind", 10 /* num_rows */);

    {
      // Select data from the test table. Bind by position.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ?;";
      ResultSet rs = session.execute(selectStmt, new Integer(7), "h7", new Integer(107));
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    {
      // Select data from the test table. Bind by name.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ?;";
      ResultSet rs = session.execute(selectStmt,
                                     new HashMap<String, Object>() {{
                                         put("h1", new Integer(7));
                                         put("h2", "h7");
                                         put("r1", new Integer(107));
                                       }});
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    {
      // Select data from the test table. Bind by name with named markers.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3;";
      ResultSet rs = session.execute(selectStmt,
                                     new HashMap<String, Object>() {{
                                         put("b1", new Integer(7));
                                         put("b2", "h7");
                                         put("b3", new Integer(107));
                                       }});
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    {
      // Ensure that this doesn't crash the tserver
      String selectStmt = "SELECT ? FROM test_bind;";
      try {
        ResultSet rs = session.execute(selectStmt, new Integer(1));
        fail("Statement \"" + selectStmt + "\" did not fail undefined bind variable name");
      } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
        LOG.info("Expected exception ", e);
      }
    }

    LOG.info("End test");
  }

  @Test
  public void testInsertBind() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    setupTable("test_bind", 0 /* num_rows */);

    {
      // insert data into the test table. Bind by position.
      String insertStmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (?, ?, ?, ?, ?, ?);";
      session.execute(insertStmt,
                      new Integer(1), "h2",
                      new Integer(1), "r1",
                      new Integer(1), "v1");
    }

    {
      // insert data into the test table. Bind by name.
      String insertStmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (?, ?, ?, ?, ?, ?);";
      session.execute(insertStmt,
                      new HashMap<String, Object>() {{
                        put("h1", new Integer(1));
                        put("h2", "h2");
                        put("r1", new Integer(2));
                        put("r2", "r2");
                        put("v1", new Integer(2));
                        put("v2", "v2");
                      }});
    }

    {
      // insert data into the test table. Bind by name with named markers.
      String insertStmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (:b1, :b2, :b3, :b4, :b5, :b6);";
      session.execute(insertStmt,
                      new HashMap<String, Object>() {{
                        put("b1", new Integer(1));
                        put("b2", "h2");
                        put("b3", new Integer(3));
                        put("b4", "r3");
                        put("b5", new Integer(3));
                        put("b6", "v3");
                      }});
    }

    {
      // Select data from the test table.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(selectStmt);

      for (int i = 1; i <= 3; i++) {
        Row row = rs.one();
        // Assert exactly 1 row is returned each time with expected column values.
        assertNotNull(row);
        assertEquals(1, row.getInt(0));
        assertEquals("h2", row.getString(1));
        assertEquals(i, row.getInt(2));
        assertEquals("r" + i, row.getString(3));
        assertEquals(i, row.getInt(4));
        assertEquals("v" + i, row.getString(5));
      }
    }

    LOG.info("End test");
  }

  @Test
  public void testUpdateBind() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    setupTable("test_bind", 0 /* num_rows */);

    {
      // update data in the test table. Bind by position.
      String updateStmt = "UPDATE test_bind set v1 = ?, v2 = ?" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      session.execute(updateStmt,
                      new Integer(1), "v1",
                      new Integer(1), "h2",
                      new Integer(1), "r1");
    }

    {
      // update data in the test table. Bind by name.
      String updateStmt = "UPDATE test_bind set v1 = ?, v2 = ?" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      session.execute(updateStmt,
                      new HashMap<String, Object>() {{
                        put("h1", new Integer(1));
                        put("h2", "h2");
                        put("r1", new Integer(2));
                        put("r2", "r2");
                        put("v1", new Integer(2));
                        put("v2", "v2");
                      }});
    }

    {
      // update data in the test table. Bind by name with named markers.
      String updateStmt = "UPDATE test_bind set v1 = :b5, v2 = :b6" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3 AND r2 = :b4;";
      session.execute(updateStmt,
                      new HashMap<String, Object>() {{
                        put("b1", new Integer(1));
                        put("b2", "h2");
                        put("b3", new Integer(3));
                        put("b4", "r3");
                        put("b5", new Integer(3));
                        put("b6", "v3");
                      }});
    }

    {
      // Select data from the test table.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(selectStmt);

      for (int i = 1; i <= 3; i++) {
        Row row = rs.one();
        // Assert exactly 1 row is returned with expected column values.
        assertNotNull(row);
        assertEquals(1, row.getInt(0));
        assertEquals("h2", row.getString(1));
        assertEquals(i, row.getInt(2));
        assertEquals("r" + i, row.getString(3));
        assertEquals(i, row.getInt(4));
        assertEquals("v" + i, row.getString(5));
      }
    }

    LOG.info("End test");
  }

  @Test
  public void testDeleteBind() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    setupTable("test_bind", 0 /* num_rows */);

    // Insert 4 rows.
    for (int i = 1; i <= 4; i++) {
      String insertStmt = String.format("INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                                         "VALUES (%d, 'h%s', %d, 'r%s', %d, 'v%s');",
                                         1, 2, i, i, i, i);
      session.execute(insertStmt);
    }

    {
      // delete 1 row in the test table. Bind by position.
      String deleteStmt = "DELETE FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      session.execute(deleteStmt,
                      new Integer(1), "h2",
                      new Integer(1), "r1");
    }

    {
      // delete 1 row in the test table. Bind by name.
      String deleteStmt = "DELETE FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      session.execute(deleteStmt,
                      new HashMap<String, Object>() {{
                        put("h1", new Integer(1));
                        put("h2", "h2");
                        put("r1", new Integer(2));
                        put("r2", "r2");
                      }});
    }

    {
      // delete 1 row in the test table. Bind by name with named markers.
      String deleteStmt = "DELETE FROM test_bind" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3 AND r2 = :b4;";
      session.execute(deleteStmt,
                      new HashMap<String, Object>() {{
                        put("b1", new Integer(1));
                        put("b2", "h2");
                        put("b3", new Integer(3));
                        put("b4", "r3");
                      }});
    }

    {
      // Select data from the test table.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(selectStmt);
      Row row = rs.one();
      // Assert only 1 row is left.
      assertNotNull(row);
      assertEquals(1, row.getInt(0));
      assertEquals("h2", row.getString(1));
      assertEquals(4, row.getInt(2));
      assertEquals("r4", row.getString(3));
      assertEquals(4, row.getInt(4));
      assertEquals("v4", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    LOG.info("End test");
  }

  @Test
  public void testPrepareSelectBind() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    setupTable("test_bind", 10 /* num_rows */);

    {
      // Select data from the test table. Bind by position.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ?;";
      PreparedStatement stmt = session.prepare(selectStmt);
      ResultSet rs = session.execute(stmt.bind(new Integer(7), "h7", new Integer(107)));
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    {
      // Select data from the test table. Bind by name.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ?;";
      PreparedStatement stmt = session.prepare(selectStmt);
      ResultSet rs = session.execute(stmt
                                     .bind()
                                     .setInt("h1", 7)
                                     .setString("h2", "h7")
                                     .setInt("r1", 107));
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    {
      // Select data from the test table. Bind by name with named markers.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3;";
      PreparedStatement stmt = session.prepare(selectStmt);
      ResultSet rs = session.execute(stmt
                                     .bind()
                                     .setInt("b1", 7)
                                     .setString("b2", "h7")
                                     .setInt("b3", 107));
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    LOG.info("End test");
  }

  @Test
  public void testPrepareInsertBind() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    setupTable("test_bind", 0 /* num_rows */);

    {
      // Insert data into the test table. Bind by position.
      String insertStmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                          "VALUES (?, ?, ?, ?, ?, ?);";
      PreparedStatement stmt = session.prepare(insertStmt);
      session.execute(stmt.bind(new Integer(1), "h2", new Integer(1), "r1", new Integer(1), "v1"));

      try {
        session.execute(stmt.bind(null, "h2", new Integer(1), "r1", new Integer(1), "v1"));
        fail("Statement \"" + insertStmt + "\" did not fail with Null hash PK");
      } catch (java.lang.NullPointerException e) {
        LOG.info("Expected exception", e);
      }

      try {
        session.execute(stmt.bind(new Integer(1), "h2", null, "r1", new Integer(1), "v1"));
        fail("Statement \"" + insertStmt + "\" did not fail with Null range PK");
      } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
        LOG.info("Expected exception", e);
      }
    }

    {
      // Insert data into the test table. Bind by name.
      String insertStmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                          "VALUES (?, ?, ?, ?, ?, ?);";
      PreparedStatement stmt = session.prepare(insertStmt);
      session.execute(stmt
                      .bind()
                      .setInt("h1", 1)
                      .setString("h2", "h2")
                      .setInt("r1", 2)
                      .setString("r2", "r2")
                      .setInt("v1", 2)
                      .setString("v2", "v2"));

      try {
        session.execute(stmt
                .bind()
                .setToNull("h1")  // NULL hash key
                .setString("h2", "h2")
                .setInt("r1", 2)
                .setString("r2", "r2")
                .setInt("v1", 2)
                .setString("v2", "v2"));
        fail("Statement \"" + insertStmt + "\" did not fail with Null hash PK");
      } catch (java.lang.NullPointerException e) {
        LOG.info("Expected exception", e);
      }

      try {
        session.execute(stmt
                .bind()
                .setInt("h1", 1)
                .setString("h2", "h2")
                .setToNull("r1") // NULL range key
                .setString("r2", "r2")
                .setInt("v1", 2)
                .setString("v2", "v2"));
        fail("Statement \"" + insertStmt + "\" did not fail with Null range PK");
      } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
        LOG.info("Expected exception", e);
      }
    }

    {
      // Insert data into the test table. Bind by name with named markers.
      String insertStmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                          "VALUES (:b1, :b2, :b3, :b4, :b5, :b6);";
      PreparedStatement stmt = session.prepare(insertStmt);
      session.execute(stmt
                      .bind()
                      .setInt("b1", 1)
                      .setString("b2", "h2")
                      .setInt("b3", 3)
                      .setString("b4", "r3")
                      .setInt("b5", 3)
                      .setString("b6", "v3"));

      try {
        session.execute(stmt
                .bind()
                .setToNull("b1") // NULL hash key
                .setString("b2", "h2")
                .setInt("b3", 3)
                .setString("b4", "r3")
                .setInt("b5", 3)
                .setString("b6", "v3"));
        fail("Statement \"" + insertStmt + "\" did not fail with Null hash PK");
      } catch (java.lang.NullPointerException e) {
        LOG.info("Expected exception", e);
      }

      try {
        session.execute(stmt
                .bind()
                .setInt("b1", 1)
                .setString("b2", "h2")
                .setToNull("b3") // NULL range key
                .setString("b4", "r3")
                .setInt("b5", 3)
                .setString("b6", "v3"));
        fail("Statement \"" + insertStmt + "\" did not fail with Null range PK");
      } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
        LOG.info("Expected exception", e);
      }
    }

    {
      // Insert data into the test table. Bind by column index.
      String insertStmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                          "VALUES (?, ?, ?, ?, ?, ?);";
      PreparedStatement stmt = session.prepare(insertStmt);
      session.execute(stmt
              .bind()
              .setInt(0, 1)
              .setString(1, "h2")
              .setInt(2, 4)
              .setString(3, "r4")
              .setInt(4, 4)
              .setString(5, "v4"));

      try {
        session.execute(stmt
                .bind()
                .setToNull(0) // NULL hash key
                .setString(1, "h2")
                .setInt(2, 4)
                .setString(3, "r4")
                .setInt(4, 4)
                .setString(5, "v4"));
        fail("Statement \"" + insertStmt + "\" did not fail with Null hash PK");
      } catch (java.lang.NullPointerException e) {
        LOG.info("Expected exception", e);
      }

      try {
        session.execute(stmt
                .bind()
                .setInt(0, 1)
                .setString(1, "h2")
                .setToNull(2) // NULL range key
                .setString(3, "r4")
                .setInt(4, 4)
                .setString(5, "v4"));
        fail("Statement \"" + insertStmt + "\" did not fail with Null range PK");
      } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
        LOG.info("Expected exception", e);
      }
    }

    {
      // Select data from the test table.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind " +
                          "WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(selectStmt);

      for (int i = 1; i <= 4; i++) {
        Row row = rs.one();
        // Assert exactly 1 row is returned each time with expected column values.
        assertNotNull(row);
        assertEquals(1, row.getInt(0));
        assertEquals("h2", row.getString(1));
        assertEquals(i, row.getInt(2));
        assertEquals("r" + i, row.getString(3));
        assertEquals(i, row.getInt(4));
        assertEquals("v" + i, row.getString(5));
      }
    }

    LOG.info("End test");
  }

  @Test
  public void testPrepareInsertBindLongJson() throws Exception {
    LOG.info("Begin test");

    // Create table
    session.execute("CREATE TABLE test_bind (id int PRIMARY KEY, data jsonb, v int);");

    // Insert data into the test table via prepared statement. Bind by name.
    String insertStmt = "INSERT INTO test_bind (id, data, v) VALUES (?, ?, ?);";
    PreparedStatement stmt = session.prepare(insertStmt);

    {
      session.execute(stmt.bind().setInt("id", 0)
                                 .setString("data", "{ \"a\" : 1 }")
                                 .setInt("v", 100));

      // Select data from the test table.
      ResultSet rs = session.execute("SELECT * FROM test_bind WHERE id = 0;");
      Row row = rs.one();
      assertNull(rs.one()); // Assert exactly 1 row is returned.
      assertNotNull(row);
      assertEquals(0, row.getInt(0));
      assertEquals("{\"a\":1}", row.getJson("data"));
      assertEquals(100, row.getInt(2));
    }
    {
      StringBuilder builder = new StringBuilder();
      builder.append("{\"0000000\":\"1234567890\"");
      for (int i = 1; i < 100; ++i) {
        builder.append(String.format(",\"%07d\":\"1234567890\"", i));
      }
      String jsonStr = builder.toString() + "}";
      LOG.info("Executing INSERT for JSON data length = " + jsonStr.length());

      // Insert long JSON value.
      session.execute(stmt.bind().setInt("id", 1)
                                 .setString("data", jsonStr)
                                 .setInt("v", 101));
      LOG.info("INSERT executed");

      // Select data from the test table.
      ResultSet rs = session.execute("SELECT * FROM test_bind WHERE id = 1;");
      Row row = rs.one();
      assertNull(rs.one()); // Assert exactly 1 row is returned.
      assertNotNull(row);
      assertEquals(1, row.getInt(0));
      assertEquals(jsonStr, row.getJson("data"));
      assertEquals(101, row.getInt(2));
    }
    {
      StringBuilder builder = new StringBuilder();
      builder.append("{\"0000000\":\"1234567890\"");
      for (int i = 1; i < 3000000; ++i) {
        builder.append(String.format(",\"%07d\":\"1234567890\"", i));
      }
      String jsonStr = builder.toString() + "}";
      LOG.info("Executing INSERT for JSON data length = " + jsonStr.length());

      // Insert huge JSON value - expecting 'value too long' exception.
      try {
        session.execute(stmt.bind().setInt("id", 2)
                                   .setString("data", jsonStr)
                                   .setInt("v", 102));
        fail("Statement \"" + insertStmt + "\" did not fail");
      } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
        LOG.info("Prepared INSERT failed. Expected exception", e);
      }
    }
    {
      // Change column type.
      session.execute("ALTER TABLE test_bind DROP v;");
      session.execute("ALTER TABLE test_bind ADD v text;");

      // Test the new text column.
      session.execute("INSERT INTO test_bind (id, data, v) VALUES (9, '{}', 'abc');");
      ResultSet rs = session.execute("SELECT * FROM test_bind WHERE id = 9;");
      Row row = rs.one();
      assertNull(rs.one()); // Assert exactly 1 row is returned.
      assertNotNull(row);
      assertEquals(9, row.getInt(0));
      assertEquals("{}", row.getJson("data"));
      assertEquals("abc", row.getString(2));

      // Insert INT instead of TEXT - expecting 'Datatype Mismatch' exception.
      try {
        String invalidStmt = "INSERT INTO test_bind (id, data, v) VALUES (8, '{}', 123);";
        session.execute(invalidStmt);
        fail("Statement \"" + invalidStmt + "\" did not fail");
      } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
        LOG.info("INSERT failed. Expected exception", e);
      }
    }
    {
      // Create new prepared INSERT for the new TEXT type and try to set INT into the TEXT.
      PreparedStatement newStmt =
        session.prepare("INSERT INTO test_bind (id, data, v) VALUES (?, ?, ?);");

      // TOFIX: EXPECTING EXCEPTION HERE
      //        https://github.com/yugabyte/yugabyte-db/issues/2446
      session.execute(newStmt.bind().setInt("id", 3)
                                    .setString("data", "{}")
                                    .setInt("v", 0x41414141)); // 0x41 == 'A'

      // Select data from the test table.
      String selectStmt = "SELECT * FROM test_bind WHERE id = 3;";
      ResultSet rs = session.execute(selectStmt);
      Row row = rs.one();
      assertNull(rs.one()); // Assert exactly 1 row is returned.
      assertNotNull(row);
      assertEquals(3, row.getInt(0));
      assertEquals("{}", row.getJson("data"));
      assertEquals("AAAA", row.getString(2));
    }
    {
      // Try to use old prepared statement with 'int' type for column 'v'.
      // TOFIX: EXPECTING EXCEPTION HERE
      //        https://github.com/yugabyte/yugabyte-db/issues/2446
      session.execute(stmt.bind().setInt("id", 4)
                                 .setString("data", "{ \"b\" : 2 }")
                                 .setInt("v", 0x42424242)); // 0x42 == 'B'

      // Select data from the test table.
      String selectStmt = "SELECT * FROM test_bind WHERE id = 4;";
      ResultSet rs = session.execute(selectStmt);
      Row row = rs.one();
      assertNull(rs.one()); // Assert exactly 1 row is returned.
      assertNotNull(row);
      assertEquals(4, row.getInt(0));
      assertEquals("{\"b\":2}", row.getJson("data"));
      assertEquals("BBBB", row.getString(2));
    }

    LOG.info("End test");
  }

  @Test
  public void testPrepareUpdateBind() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    setupTable("test_bind", 0 /* num_rows */);

    {
      // update data in the test table. Bind by position.
      String updateStmt = "UPDATE test_bind set v1 = ?, v2 = ?" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      PreparedStatement stmt = session.prepare(updateStmt);
      session.execute(stmt.bind(new Integer(1), "v1", new Integer(1), "h2", new Integer(1), "r1"));
    }

    {
      // update data in the test table. Bind by name.
      String updateStmt = "UPDATE test_bind set v1 = ?, v2 = ?" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      PreparedStatement stmt = session.prepare(updateStmt);
      session.execute(stmt
                      .bind()
                      .setInt("h1", 1)
                      .setString("h2", "h2")
                      .setInt("r1", 2)
                      .setString("r2", "r2")
                      .setInt("v1", 2)
                      .setString("v2", "v2"));
    }

    {
      // update data in the test table. Bind by name with named markers.
      String updateStmt = "UPDATE test_bind set v1 = :b5, v2 = :b6" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3 AND r2 = :b4;";
      PreparedStatement stmt = session.prepare(updateStmt);
      session.execute(stmt
                      .bind()
                      .setInt("b1", 1)
                      .setString("b2", "h2")
                      .setInt("b3", 3)
                      .setString("b4", "r3")
                      .setInt("b5", 3)
                      .setString("b6", "v3"));
    }

    {
      // Select data from the test table.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(selectStmt);

      for (int i = 1; i <= 3; i++) {
        Row row = rs.one();
        // Assert exactly 1 row is returned with expected column values.
        assertNotNull(row);
        assertEquals(1, row.getInt(0));
        assertEquals("h2", row.getString(1));
        assertEquals(i, row.getInt(2));
        assertEquals("r" + i, row.getString(3));
        assertEquals(i, row.getInt(4));
        assertEquals("v" + i, row.getString(5));
      }
    }

    LOG.info("End test");
  }

  @Test
  public void testPrepareDeleteBind() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    setupTable("test_bind", 0 /* num_rows */);

    // Insert 4 rows.
    for (int i = 1; i <= 4; i++) {
      String insertStmt = String.format("INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                                         "VALUES (%d, 'h%s', %d, 'r%s', %d, 'v%s');",
                                         1, 2, i, i, i, i);
      session.execute(insertStmt);
    }

    {
      // delete 1 row in the test table. Bind by position.
      String deleteStmt = "DELETE FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      PreparedStatement stmt = session.prepare(deleteStmt);
      session.execute(stmt.bind(new Integer(1), "h2", new Integer(1), "r1"));
    }

    {
      // delete 1 row in the test table. Bind by name.
      String deleteStmt = "DELETE FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      PreparedStatement stmt = session.prepare(deleteStmt);
      session.execute(stmt.bind()
                      .setInt("h1", 1)
                      .setString("h2", "h2")
                      .setInt("r1", 2)
                      .setString("r2", "r2"));
    }

    {
      // delete 1 row in the test table. Bind by name with named markers.
      String deleteStmt = "DELETE FROM test_bind" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3 AND r2 = :b4;";
      PreparedStatement stmt = session.prepare(deleteStmt);
      session.execute(stmt
                      .bind()
                      .setInt("b1", 1)
                      .setString("b2", "h2")
                      .setInt("b3", 3)
                      .setString("b4", "r3"));
    }

    {
      // Select data from the test table.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(selectStmt);
      Row row = rs.one();
      // Assert only 1 row is left.
      assertNotNull(row);
      assertEquals(1, row.getInt(0));
      assertEquals("h2", row.getString(1));
      assertEquals(4, row.getInt(2));
      assertEquals("r4", row.getString(3));
      assertEquals(4, row.getInt(4));
      assertEquals("v4", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    LOG.info("End test");
  }

  @Test
  public void testDatatypeBind() throws Exception {
    LOG.info("Begin test");

    // Create table
    String createStmt = "CREATE TABLE test_bind " +
                         "(c1 tinyint, c2 smallint, c3 integer, c4 bigint, " +
                         "c5 float, c6 double, " +
                         "c7 varchar, " +
                         "c8 boolean, " +
                         "c9 timestamp, " +
                         "c10 inet, " +
                         "c11 uuid, " +
                         "c12 timeuuid, " +
                         "c13 blob," +
                         "c14 decimal, " +
                         "c15 varint, " +
                         "c16 jsonb, " +
                         "primary key (c1));";
    session.execute(createStmt);

    // Insert data of all supported datatypes with bind by position.
    String insertStmt =
        "INSERT INTO test_bind " +
            "(c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14, c15, c16) " +
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    // For CQL <-> Java datatype mapping, see
    // http://docs.datastax.com/en/drivers/java/3.1/com/datastax/driver/core/TypeCodec.html
    LOG.info("EXECUTING");
    session.execute(insertStmt,
                    new Byte((byte)1), new Short((short)2), new Integer(3), new Long(4),
                    new Float(5.0), new Double(6.0),
                    "c7",
                    new Boolean(true),
                    new Date(2017, 1, 1),
                    InetAddress.getByName("1.2.3.4"),
                    UUID.fromString("11111111-2222-3333-4444-555555555555"),
                    UUID.fromString("f58ba3dc-3422-11e7-a919-92ebcb67fe33"),
                    makeByteBuffer(133143986176L), // `0000001f00000000` to check zero-bytes
                    new BigDecimal("12.34"),
                    new BigInteger("5425271716563447368291929487567690209186364832966"),
                    "{ \"a\" : 1 }");
    LOG.info("EXECUTED");

    {
      // Select data from the test table.
      String selectStmt = "SELECT * FROM test_bind WHERE c1 = 1;";
      ResultSet rs = session.execute(selectStmt);
      Row row = rs.one();
      // Assert 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(1, row.getByte("c1"));
      assertEquals(2, row.getShort("c2"));
      assertEquals(3, row.getInt("c3"));
      assertEquals(4, row.getLong("c4"));
      assertEquals(5.0, row.getFloat("c5"), 0.0 /* delta */);
      assertEquals(6.0, row.getDouble("c6"), 0.0 /* delta */);
      assertEquals("c7", row.getString("c7"));
      assertTrue(row.getBool("c8"));
      assertEquals(new Date(2017, 1, 1), row.getTimestamp("c9"));
      assertEquals(InetAddress.getByName("1.2.3.4"), row.getInet("c10"));
      assertEquals(UUID.fromString("11111111-2222-3333-4444-555555555555"), row.getUUID("c11"));
      assertEquals(UUID.fromString("f58ba3dc-3422-11e7-a919-92ebcb67fe33"), row.getUUID("c12"));
      assertEquals(makeBlobString(makeByteBuffer(133143986176L)),
                   makeBlobString(row.getBytes("c13")));
      assertEquals(0, row.getDecimal("c14").compareTo(new BigDecimal("12.34")));
      BigInteger myVarInt = new BigInteger("5425271716563447368291929487567690209186364832966");
      assertEquals(myVarInt, row.getVarint("c15"));
      assertEquals("{\"a\":1}", row.getJson("c16"));
    }

    LOG.info("Select done!");

    // Update data of all supported datatypes with bind by position.
    String updateStmt = "UPDATE test_bind SET " +
                         "c2 = ?, " +
                         "c3 = ?, " +
                         "c4 = ?, " +
                         "c5 = ?, " +
                         "c6 = ?, " +
                         "c7 = ?, " +
                         "c8 = ?, " +
                         "c9 = ?, " +
                         "c10 = ?, " +
                         "c11 = ?, " +
                         "c12 = ?, " +
                         "c13 = ?, " +
                         "c14 = ?, " +
                         "c15 = ?, " +
                         "c16 = ? " +
                         "WHERE c1 = ?;";
    session.execute(updateStmt,
                    new HashMap<String, Object>() {{
                      put("c1", new Byte((byte)11));
                      put("c2", new Short((short)12));
                      put("c3", new Integer(13));
                      put("c4", new Long(14));
                      put("c5", new Float(15.0));
                      put("c6", new Double(16.0));
                      put("c7", "c17");
                      put("c8", new Boolean(false));
                      put("c9", new Date(2017, 11, 11));
                      put("c10", InetAddress.getByName("1.2.3.4"));
                      put("c11", UUID.fromString("22222222-2222-3333-4444-555555555555"));
                      put("c12", UUID.fromString("f58ba3dc-3422-11e7-a919-92ebcb67fe33"));
                      put("c13", makeByteBuffer(9223372036854775807L)); // max long
                      put("c14", new BigDecimal(100.0));
                      put("c15", BigInteger.valueOf(-90087));
                      put("c16", "{ \"b\" : 1 }");
                    }});

    LOG.info("Update done!");
    {
      // Select data from the test table.
      String selectStmt = "SELECT * FROM test_bind WHERE c1 = 11;";
      ResultSet rs = session.execute(selectStmt);
      Row row = rs.one();
      // Assert 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(11, row.getByte("c1"));
      assertEquals(12, row.getShort("c2"));
      assertEquals(13, row.getInt("c3"));
      assertEquals(14, row.getLong("c4"));
      assertEquals(15.0, row.getFloat("c5"), 0.0 /* delta */);
      assertEquals(16.0, row.getDouble("c6"), 0.0 /* delta */);
      assertEquals("c17", row.getString("c7"));
      assertFalse(row.getBool("c8"));
      assertEquals(new Date(2017, 11, 11), row.getTimestamp("c9"));
      assertEquals(InetAddress.getByName("1.2.3.4"), row.getInet("c10"));
      assertEquals(UUID.fromString("22222222-2222-3333-4444-555555555555"), row.getUUID("c11"));
      assertEquals(UUID.fromString("f58ba3dc-3422-11e7-a919-92ebcb67fe33"), row.getUUID("c12"));
      assertEquals(makeBlobString(makeByteBuffer(9223372036854775807L)),
                   makeBlobString(row.getBytes("c13")));
      assertEquals(0, row.getDecimal("c14").compareTo(new BigDecimal("100.0")));
      assertEquals(BigInteger.valueOf(-90087), row.getVarint("c15"));
      assertEquals("{\"b\":1}", row.getJson("c16"));
    }

    LOG.info("End test");
  }

  @Test
  public void testBindWithVariousOperators() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    setupTable("test_bind", 10 /* num_rows */);

    // ">=" and "<=" not supported in QL yet.
    //
    // {
    //   // Select bind marker with ">=" and "<=".
    //   String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
    //                        " WHERE h1 = ? AND h2 = ? AND r1 >= ? AND r1 <= ?;";
    //   ResultSet rs = session.execute(selectStmt,
    //                                  new Integer(7), "h7",
    //                                  new Integer(107), new Integer(107));
    //   Row row = rs.one();
    //   // Assert exactly 1 row is returned with expected column values.
    //   assertNotNull(row);
    //   assertEquals(7, row.getInt(0));
    //   assertEquals("h7", row.getString(1));
    //   assertEquals(107, row.getInt(2));
    //   assertEquals("r107", row.getString(3));
    //   assertEquals(1007, row.getInt(4));
    //   assertEquals("v1007", row.getString(5));
    //   row = rs.one();
    //   assertNull(row);
    // }

    {
      // Select bind marker with ">" and "<".
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 > ? AND r1 < ?;";
      ResultSet rs = session.execute(selectStmt,
                                     new Integer(7), "h7",
                                     new Integer(107), new Integer(107));
      Row row = rs.one();
      // Assert no row is returned.
      assertNull(row);
    }

    // "<>" not supported in QL yet.
    //
    // {
    //   // Select bind marker with "<>".
    //   String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
    //                        " WHERE h1 = :1 AND h2 = :2 AND r1 <> :3;";
    //   ResultSet rs = session.execute(selectStmt, new Integer(7), "h7", new Integer(107));
    //   Row row = rs.one();
    //   // Assert no row is returned.
    //   assertNull(row);
    // }

    // BETWEEN and NOT BETWEEN not supported in QL yet.
    //
    // {
    //   // Select bind marker with BETWEEN and NOT BETWEEN.
    //   String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
    //                        " WHERE h1 = :1 AND h2 = :2 AND" +
    //                        " r1 BETWEEN ? AND ? AND r1 NOT BETWEEN ? AND ?;";
    //   ResultSet rs = session.execute(selectStmt,
    //                                  new Integer(7), "h7",
    //                                  new Integer(106), new Integer(108),
    //                                  new Integer(1000), new Integer(2000));
    //   Row row = rs.one();
    //   // Assert exactly 1 row is returned with expected column values.
    //   assertNotNull(row);
    //   assertEquals(7, row.getInt(0));
    //   assertEquals("h7", row.getString(1));
    //   assertEquals(107, row.getInt(2));
    //   assertEquals("r107", row.getString(3));
    //   assertEquals(1007, row.getInt(4));
    //   assertEquals("v1007", row.getString(5));
    //   row = rs.one();
    //   assertNull(row);
    // }

    LOG.info("End test");
  }

  @Test
  public void testBindMisc() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    setupTable("test_bind", 10 /* num_rows */);

    {
      // Position bind marker with mixed order.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = :2 AND h2 = :3 AND r1 = :1;";
      ResultSet rs = session.execute(selectStmt, new Integer(107), new Integer(7), "h7");
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    {
      // Named markers with quoted identifier and space between colon and id.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = :\"Bind1\" AND h2 = :  \"Bind2\" AND r1 = :  \"Bind3\";";
      ResultSet rs = session.execute(selectStmt,
                                     new HashMap<String, Object>() {{
                                         put("Bind1", new Integer(7));
                                         put("Bind2", "h7");
                                         put("Bind3", new Integer(107));
                                       }});
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    {
      // Named bind marker with unreserved keywords ("key", "type" and "partition").
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=:key AND h2=:type AND r1=:partition;";
      ResultSet rs = session.execute(selectStmt,
                                     new HashMap<String, Object>() {{
                                         put("key", new Integer(7));
                                         put("type", "h7");
                                         put("partition", new Integer(107));
                                       }});
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    {
      // Position bind marker no space between "col=?".
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=? AND h2=? AND r1=?;";
      ResultSet rs = session.execute(selectStmt, new Integer(7), "h7", new Integer(107));
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    {
      // Number bind marker no space between "col=:number".
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=:1 AND h2=:2 AND r1=:3;";
      ResultSet rs = session.execute(selectStmt, new Integer(7), "h7", new Integer(107));
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    {
      // Named bind marker no space between "col=:id".
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=:b1 AND h2=:b2 AND r1=:b3;";
      ResultSet rs = session.execute(selectStmt,
                                     new HashMap<String, Object>() {{
                                         put("b1", new Integer(7));
                                         put("b2", "h7");
                                         put("b3", new Integer(107));
                                       }});
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    // ">=" and "<=" not supported in QL yet.
    //
    // {
    //   // Bind marker with ">=" and "<=" and no space in between column, operator and bind marker.
    //   String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
    //                        " WHERE h1=? AND h2=? AND r1>=? AND r1<=?;";
    //   ResultSet rs = session.execute(selectStmt,
    //                                  new Integer(7), "h7",
    //                                  new Integer(107), new Integer(107));
    //   Row row = rs.one();
    //   // Assert exactly 1 row is returned with expected column values.
    //   assertNotNull(row);
    //   assertEquals(7, row.getInt(0));
    //   assertEquals("h7", row.getString(1));
    //   assertEquals(107, row.getInt(2));
    //   assertEquals("r107", row.getString(3));
    //   assertEquals(1007, row.getInt(4));
    //   assertEquals("v1007", row.getString(5));
    //   row = rs.one();
    //   assertNull(row);
    // }

    {
      // Bind marker with ">" and "<" and no space in between column, operator and bind marker.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=? AND h2=? AND r1>? AND r1<?;";
      ResultSet rs = session.execute(selectStmt,
                                     new Integer(7), "h7",
                                     new Integer(107), new Integer(107));
      Row row = rs.one();
      // Assert no row is returned.
      assertNull(row);
    }

    // "<>" not supported in QL yet.
    //
    // {
    //   // Bind marker with "<>" and no space in between column, operator and bind marker.
    //   String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
    //                        " WHERE h1=:1 AND h2=:2 AND r1<>:3;";
    //   ResultSet rs = session.execute(selectStmt, new Integer(7), "h7", new Integer(107));
    //   Row row = rs.one();
    //   // Assert no row is returned.
    //   assertNull(row);
    // }

    {
      // Named, case-insensitive bind markers.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=:Bind1 AND h2=:Bind2 AND r1=:Bind3;";
      ResultSet rs = session.execute(selectStmt,
                                     new HashMap<String, Object>() {{
                                         put("bind1", new Integer(7));
                                         put("bind2", "h7");
                                         put("bind3", new Integer(107));
                                       }});
      Row row = rs.one();
      // Assert exactly 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
      row = rs.one();
      assertNull(row);
    }

    LOG.info("End test");
  }

  @Test
  public void testInvalidBindStatements() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    setupTable("test_bind", 0 /* num_rows */);

    // Illegal (non-positive) bind position marker.
    testInvalidBindStatement("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                             " WHERE h1 = :0 AND h2 = :1 AND r1 = :2;",
                             new Integer(7), "h7", new Integer(107));

    // Illegal (too large) bind position marker.
    testInvalidBindStatement("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                             " WHERE h1 = :9223372036854775808 AND h2 = :1 AND r1 = :2;",
                             new Integer(7), "h7", new Integer(107));


    // Missing bind variable at position 3.
    testInvalidBindStatement("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                             " WHERE h1 = ? AND h2 = ? AND r1 = ?;",
                             new Integer(7), "h7");

    // Missing bind variable "r1".
    testInvalidBindStatement("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                             " WHERE h1 = ? AND h2 = ? AND r1 = ?;",
                             new HashMap<String, Object>() {{
                               put("h1", new Integer(7));
                               put("h2", "h7");
                             }});

    // Missing bind variable "b1".
    testInvalidBindStatement("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                             " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3;",
                             new HashMap<String, Object>() {{
                               put("b2", "h7");
                               put("b3", new Integer(107));
                             }});

    // Bind variable at position 1 with the wrong type and byte size
    // (an int requires 4 byte whereas "h1" is only 2-byte long).
    testInvalidBindStatement("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                             " WHERE h1 = ? AND h2 = ? AND r1 = ?;",
                             "h1", "h7", 107);

    // Bind variable not supported in an expression (yet).
    testInvalidBindStatement("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                             " WHERE h1 = (- :1) AND h2 = :2 AND r1 = :3;",
                             new Integer(7), "h7", new Integer(107));
    testInvalidBindStatement("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                             " WHERE h1 = (- ?) AND h2 = :2 AND r1 = :3;",
                             new Integer(7), "h7", new Integer(107));

    // Insert nulls not allowed in primary key: hash with bind-by-position
    testInvalidBindStatement("INSERT INTO test_bind(h1, h2, r1, r2, v1, v2) " +
                             " values(?,?,?,?,?,?);",
                             null, "x",
                             new Integer(11), "y",
                             new Integer(17), "z" );

    // Insert nulls not allowed in primary key: range with bind-by-position
    testInvalidBindStatement("INSERT INTO test_bind(h1, h2, r1, r2, v1, v2) " +
                              " values(?,?,?,?,?,?);",
                             new Integer(5), "x",
                             new Integer(11), null,
                             new Integer(17), "z" );

    // Insert nulls not allowed in primary key: range with bind-by-name
    Map<String, Object> valueMap = new HashMap<>();
    valueMap.put("h1", new Integer(5));
    valueMap.put("h2", null);
    valueMap.put("r1", new Integer(11));
    valueMap.put("r2", "y");
    valueMap.put("v1", new Integer(17));
    valueMap.put("v2", "z");
    testInvalidBindStatement("INSERT INTO test_bind(h1, h2, r1, r2, v1, v2) " +
                             " values(:h1,:h2,:r1,:r2,:v1,:v2);", valueMap);

    // Insert nulls not allowed in primary key: range with bind-by-name
    valueMap.clear();
    valueMap.put("h1", new Integer(5));
    valueMap.put("h2", "x");
    valueMap.put("r1", null);
    valueMap.put("r2", "y");
    valueMap.put("v1", new Integer(17));
    valueMap.put("v2", "z");
    testInvalidBindStatement("INSERT INTO test_bind(h1, h2, r1, r2, v1, v2) " +
                             " values(:h1,:h2,:r1,:r2,:v1,:v2);", valueMap);

    LOG.info("End test");
  }

  @Test
  public void testNull() throws Exception {
    LOG.info("Begin test");

    // Create table
    String createStmt = "CREATE TABLE test_bind " +
                         "(k int primary key, " +
                         "c1 tinyint, c2 smallint, c3 integer, c4 bigint, " +
                         "c5 float, c6 double, " +
                         "c7 varchar, " +
                         "c8 boolean, " +
                         "c9 timestamp, " +
                         "c10 inet, " +
                         "c11 uuid, " +
                         "c12 blob, " +
                         "c13 decimal);";
    session.execute(createStmt);

    // Insert data of all supported datatypes with bind by position.
    String insertStmt = "INSERT INTO test_bind " +
                         "(k, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13) " +
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    // For CQL <-> Java datatype mapping, see
    // http://docs.datastax.com/en/drivers/java/3.1/com/datastax/driver/core/TypeCodec.html
    LOG.info("EXECUTING");
    session.execute(insertStmt,
                    new Integer(1),
                    null, null, null, null,
                    null, null,
                    null,
                    null,
                    null,
                    null,
                    null,
                    null,
                    null);
    LOG.info("EXECUTED");

    {
      // Select data from the test table.
      String selectStmt = "SELECT * FROM test_bind WHERE k = 1;";
      ResultSet rs = session.execute(selectStmt);
      Row row = rs.one();
      // Assert 1 row is returned with expected column values.
      assertNotNull(row);
      assertTrue(row.isNull("c1"));
      assertTrue(row.isNull("c2"));
      assertTrue(row.isNull("c3"));
      assertTrue(row.isNull("c4"));
      assertTrue(row.isNull("c5"));
      assertTrue(row.isNull("c6"));
      assertTrue(row.isNull("c7"));
      assertTrue(row.isNull("c8"));
      assertTrue(row.isNull("c9"));
      assertTrue(row.isNull("c10"));
      assertTrue(row.isNull("c11"));
      assertTrue(row.isNull("c12"));
      assertTrue(row.isNull("c13"));
    }

    LOG.info("End test");
  }

  @Test
  public void testEmptyValues() throws Exception {
    LOG.info("Begin test");

    // Create table
    String createStmt = "CREATE TABLE test_bind (k int primary key, c1 varchar, c2 blob);";
    session.execute(createStmt);

    // Insert data of all supported datatypes with bind by position.
    String insertStmt = "INSERT INTO test_bind (k, c1, c2) VALUES (?, ?, ?);";
    // For CQL <-> Java datatype mapping, see
    // http://docs.datastax.com/en/drivers/java/3.1/com/datastax/driver/core/TypeCodec.html
    LOG.info("EXECUTING");
    session.execute(insertStmt,
                    new Integer(1),
                    "", ByteBuffer.allocate(0));
    LOG.info("EXECUTED");

    {
      // Select data from the test table.
      String selectStmt = "SELECT * FROM test_bind WHERE k = 1;";
      ResultSet rs = session.execute(selectStmt);
      Row row = rs.one();
      // Assert 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals("", row.getString("c1"));
      assertEquals(0, row.getBytes("c2").array().length);
    }

    LOG.info("End test");
  }

  @Test
  public void testBindingLimit() throws Exception {
    LOG.info("Begin test");

    // this is the (virtual column) name CQL uses for binding the LIMIT clause
    String limitVcolName = "[limit]";

    // Setup test table.
    setupTable("test_bind", 10 /* num_rows */);

    {
      // Simple bind (by position) for limit.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind where r1 <= ? LIMIT ?;";
      ResultSet rs = session.execute(selectStmt, new Integer(109), new Integer(7));

      // Checking result.
      assertEquals(7, rs.all().size());
    }

    {
      // Simple bind (by position) for limit.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind where r1 <= ? LIMIT ?;";
      PreparedStatement stmt = session.prepare(selectStmt);

      ResultSet rs = session.execute(stmt.bind(new Integer(109), new Integer(7)));

      // Checking result.
      assertEquals(7, rs.all().size());
    }

    {
      // Prepare named bind (referenced by name).
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind WHERE r1 > :b1 LIMIT :b2;";
      PreparedStatement stmt = session.prepare(selectStmt);
      ResultSet rs = session.execute(stmt.bind().setInt("b1", 102).setInt("b2", 5));

      // Checking result.
      assertEquals(5, rs.all().size());
    }

    {
      // Prepare named bind (referenced by position).
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind WHERE r1 > :b1 LIMIT :b2;";
      PreparedStatement stmt = session.prepare(selectStmt);
      ResultSet rs = session.execute(stmt.bind(new Integer(106), new Integer(6)));

      // Checking result: only 3 rows (107, 108, 109) satisfy condition so limit is redundant.
      assertEquals(3, rs.all().size());
    }

    {
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind where r1 > ? LIMIT ?;";
      PreparedStatement stmt = session.prepare(selectStmt);
      ResultSet rs = session.execute(stmt.bind()
                                         .setInt("r1", 99)
                                         .setInt(limitVcolName, 8));

      // Checking result.
      assertEquals(8, rs.all().size());
    }

    // Negative test: limit values should be non-null
    testInvalidBindStatement("SELECT * FROM test_bind WHERE h2 = ? LIMIT ?", "1", null);

    LOG.info("End test");
  }

  @Test
  public void testBindingOffset() throws Exception {
    LOG.info("Begin test");

    // this is the (virtual column) name CQL uses for binding the OFFSET clause
    String offsetVcolName = "[offset]";

    // Setup test table.
    setupTable("test_bind", 10 /* num_rows */);

    {
      // Simple bind (by position) for offset.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind where r1 <= ? OFFSET ?;";
      ResultSet rs = session.execute(selectStmt, new Integer(109), new Integer(7));

      // Checking result.
      assertEquals(3, rs.all().size());
    }

    {
      // Simple bind (by position) for offset.
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind where r1 <= ? OFFSET ?;";
      PreparedStatement stmt = session.prepare(selectStmt);

      ResultSet rs = session.execute(stmt.bind(new Integer(109), new Integer(7)));

      // Checking result.
      assertEquals(3, rs.all().size());
    }

    {
      // Prepare named bind (referenced by name).
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind WHERE r1 > :b1 OFFSET :b2;";
      PreparedStatement stmt = session.prepare(selectStmt);
      ResultSet rs = session.execute(stmt.bind().setInt("b1", 102).setInt("b2", 5));

      // Checking result (only rows 108 and 109).
      assertEquals(2, rs.all().size());
    }

    {
      // Prepare named bind (referenced by position).
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind WHERE r1 > :b1 OFFSET :b2;";
      PreparedStatement stmt = session.prepare(selectStmt);
      ResultSet rs = session.execute(stmt.bind(new Integer(106), new Integer(1)));

      // Checking result: only 3 rows (107, 108, 109) satisfy condition, so with offset of 1, we
      // return two rows.
      assertEquals(2, rs.all().size());
    }

    {
      String selectStmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind where r1 > ? OFFSET ?;";
      PreparedStatement stmt = session.prepare(selectStmt);
      ResultSet rs = session.execute(stmt.bind()
          .setInt("r1", 99)
          .setInt(offsetVcolName, 6));

      // Checking result.
      assertEquals(4, rs.all().size());
    }

    // Negative test: offset values should be non-null
    testInvalidBindStatement("SELECT * FROM test_bind WHERE h2 = ? OFFSET ?", "1", null);

    LOG.info("End test");
  }

  private void verifyBindUserTimestamp(String selectStmt, int v1, String v2, long writeTimeV1,
                                       long writeTimeV2) {
    ResultSet rs = session.execute(selectStmt);
    List <Row> rows = rs.all();
    assertEquals(1, rows.size());
    Row row = rows.get(0);
    assertEquals(v1, row.getInt("v1"));
    assertEquals(v2, row.getString("v2"));
    assertEquals(writeTimeV1, row.getLong(2));
    assertEquals(writeTimeV2, row.getLong(3));
  }

  private String insertStmtBindUserTimestamp(String tableName, String v1, String v2,
                                             Long timestamp) {
    return String.format("INSERT INTO %s (h1, h2, r1, r2, v1, v2) " +
        "VALUES (1, '1', 1, '1', %s, %s) USING TIMESTAMP %s", tableName, v1, v2,
        (timestamp == null) ? "?" : Long.toString(timestamp));
  }

  @Test
  public void testBindingUserTimestamp() throws Exception {
    LOG.info("Begin test");

    // This is the (virtual column) name CQL uses for binding the USING TIMESTAMP clause
    String timestampVColName = "[timestamp]";

    // Setup test table.
    String tableName = "test_bind_timestamp";
    createTable(tableName);

    String selectStmt = String.format("SELECT v1, v2, writetime(v1), writetime(v2) FROM %s WHERE" +
        " h1 = 1 AND h2 = '1' AND r1 = 1 AND r2 = '1'", tableName);

    //---------------------------------- Testing Insert ------------------------------------------

    // simple bind
    {
      session.execute(insertStmtBindUserTimestamp(tableName, "?", "?", null),
        new Integer(2), "2", new Long(1000));

      // checking result
      verifyBindUserTimestamp(selectStmt, 2, "2", 1000, 1000);
    }

    // named bind -- using default names
    {
      PreparedStatement stmt = session.prepare(insertStmtBindUserTimestamp(tableName, "?", "?",
        null));
      session.execute(stmt.bind()
        .setInt("v1", 3)
        .setString("v2", "3")
        .setLong(timestampVColName, 1000));

      // checking result
      verifyBindUserTimestamp(selectStmt, 3, "3", 1000, 1000);
    }

    //---------------------------------- Testing Update ------------------------------------------

    // prepare bind
    {
      // setting up row to be updated
      session.execute(insertStmtBindUserTimestamp(tableName, "0", "'0'", 1000L));

      String updateStmt = String.format("UPDATE %s USING TIMESTAMP ? SET v1 = ?, v2 = ? " +
          "WHERE h1 = 1 AND h2 = '1' AND r1 = 1 and r2 = '1'", tableName);
      PreparedStatement stmt = session.prepare(updateStmt);
      session.execute(stmt.bind(new Long(2000), new Integer(4), "4"));

      // checking row is updated
      verifyBindUserTimestamp(selectStmt, 4, "4", 2000, 2000);
    }

    // named bind -- using given names
    {
      // setting up row to be updated
      session.execute(insertStmtBindUserTimestamp(tableName, "0", "'0'", 1000L));

      String updateStmt = String.format("UPDATE %s USING TIMESTAMP :b1 SET v1 = :b2, v2 = :b3 " +
          "WHERE h1 = 1 AND h2 = '1' AND r1 = 1 and r2 = '1'", tableName);

      PreparedStatement stmt = session.prepare(updateStmt);
      session.execute(stmt.bind()
        .setLong("b1", 2000)
        .setInt("b2", 5)
        .setString("b3", "5"));

      // checking row is update
      verifyBindUserTimestamp(selectStmt, 5, "5", 2000, 2000);
    }

    //------------------------------- Testing Invalid Stmts --------------------------------------

    // null timestamp values
    testInvalidBindStatement(String.format("INSERT INTO %s (h1, h2, r1, r2, v1, v2) " +
        "VALUES (0, '0', 0, '0', 0, ?) USING TIMESTAMP ?", tableName), "0", null);

    // invalid timestamp value.
    testInvalidBindStatement(String.format("INSERT INTO %s (h1, h2, r1, r2, v1, v2) " +
        "VALUES (0, '0', 0, '0', 0, ?) USING TIMESTAMP ?", tableName), "0", Long.MIN_VALUE);

    // timestamp value of wrong types.
    testInvalidBindStatement(String.format("INSERT INTO %s (h1, h2, r1, r2, v1, v2) " +
        "VALUES (0, '0', 0, '0', 0, ?) USING TIMESTAMP ?", tableName), "0", new Integer(100));
    testInvalidBindStatement(String.format("INSERT INTO %s (h1, h2, r1, r2, v1, v2) " +
        "VALUES (0, '0', 0, '0', 0, ?) USING TIMESTAMP ?", tableName), "0", "abc");
    testInvalidBindStatement(String.format("INSERT INTO %s (h1, h2, r1, r2, v1, v2) " +
        "VALUES (0, '0', 0, '0', 0, ?) USING TIMESTAMP ?", tableName), "0", new Float(3.0));

    LOG.info("End test");
  }

  @Test
  public void testBindingTTL() throws Exception {
    LOG.info("Begin test");

    // this is the (virtual column) name CQL uses for binding the USING TTL clause
    String ttlVcolName = "[ttl]";

    // Setup test table.
    createTable("test_bind");

    String selectStmt = "SELECT v1, v2 FROM test_bind WHERE " +
            "h1 = 1 AND h2 = '1' AND r1 = 1 AND r2 = '1'";

    //---------------------------------- Testing Insert ------------------------------------------

    // simple bind
    {
      Integer ttlSeconds = Integer.valueOf(2);
      String insertStmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
              "VALUES (1, '1', 1, '1', ?, ?) USING TTL ?";
      session.execute(insertStmt, new Integer(2), "2", ttlSeconds);

      // checking result
      ResultSet rs = session.execute(selectStmt);
      List <Row> rows = rs.all();
      assertEquals(1, rows.size());
      Row row = rows.get(0);
      assertEquals(2, row.getInt("v1"));
      assertEquals("2", row.getString("v2"));

      // checking value expires
      TestUtils.waitForTTL(ttlSeconds.intValue() * 1000);
      rs = session.execute(selectStmt);
      assertEquals(0, rs.all().size());
    }

    // named bind -- using default names
    {
      String insertStmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
              "VALUES (1, '1', 1, '1', ?, ?) USING TTL ?";
      PreparedStatement stmt = session.prepare(insertStmt);
      session.execute(stmt.bind()
                          .setInt("v1", 3)
                          .setString("v2", "3")
                          .setInt(ttlVcolName, 1));

      // checking result
      ResultSet rs = session.execute(selectStmt);
      List <Row> rows = rs.all();
      assertEquals(1, rows.size());
      Row row = rows.get(0);
      assertEquals(3, row.getInt("v1"));
      assertEquals("3", row.getString("v2"));

      // checking value expires
      TestUtils.waitForTTL(1000L);
      rs = session.execute(selectStmt);
      assertEquals(0, rs.all().size());
    }

    //---------------------------------- Testing Update ------------------------------------------

    // prepare bind
    {
      // setting up row to be updated
      String insertStmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
              "VALUES (1, '1', 1, '1', 0, '0')";
      session.execute(insertStmt);

      String updateStmt = "UPDATE test_bind USING TTL ? SET v1 = ?, v2 = ? " +
              "WHERE h1 = 1 AND h2 = '1' AND r1 = 1 and r2 = '1'";
      PreparedStatement stmt = session.prepare(updateStmt);
      session.execute(stmt.bind(new Integer(2), new Integer(4), "4"));

      // checking row is updated
      ResultSet rs = session.execute(selectStmt);
      List <Row> rows = rs.all();
      assertEquals(1, rows.size());
      Row row = rows.get(0);
      assertEquals(4, row.getInt("v1"));
      assertEquals("4", row.getString("v2"));

      // checking updated values expire (row should remain, values should be null)
      TestUtils.waitForTTL(2000L);
      rs = session.execute(selectStmt);
      rows = rs.all();
      assertEquals(1, rows.size());
      row = rows.get(0);
      assertTrue(row.isNull("v1"));
      assertTrue(row.isNull("v2"));
    }

    // named bind -- using given names
    {
      // setting up row to be updated
      String insertStmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
              "VALUES (1, '1', 1, '1', 0, '0')";
      session.execute(insertStmt);

      String updateStmt = "UPDATE test_bind USING TTL :b1 SET v1 = :b2, v2 = :b3 " +
              "WHERE h1 = 1 AND h2 = '1' AND r1 = 1 and r2 = '1'";

      PreparedStatement stmt = session.prepare(updateStmt);
      session.execute(stmt.bind()
                          .setInt("b1", 1)
                          .setInt("b2", 5)
                          .setString("b3", "5"));

      // checking row is update
      ResultSet rs = session.execute(selectStmt);
      List <Row> rows = rs.all();
      assertEquals(1, rows.size());
      Row row = rows.get(0);
      assertEquals(5, row.getInt("v1"));
      assertEquals("5", row.getString("v2"));

      // checking updated values expire (row should remain, values should be null)
      TestUtils.waitForTTL(1000L);
      rs = session.execute(selectStmt);
      rows = rs.all();
      assertEquals(1, rows.size());
      row = rows.get(0);
      assertTrue(row.isNull("v1"));
      assertTrue(row.isNull("v2"));
    }

    //------------------------------- Testing Invalid Stmts --------------------------------------

    // null ttl values
    testInvalidBindStatement("INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
            "VALUES (0, '0', 0, '0', 0, ?) USING TTL ?", "0", null);

    // ttl values below minimum allowed (i.e. below 0)
    testInvalidBindStatement("INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
            "VALUES (0, '0', 0, '0', 0, ?) USING TTL ?", "0", new Integer(-1));

    // ttl value above maximum allowed
    testInvalidBindStatement("INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
            "VALUES (0, '0', 0, '0', 0, ?) USING TTL ?", "0", new Integer(MAX_TTL_SEC + 1));

    LOG.info("End test");
  }

  private void testPartitionOps(String func_name, String bind_var_name,
                                boolean returnsLong) throws Exception {
    LOG.info("Begin test");

    //----------------------------------------------------------------------------------------------
    // Testing function as partition key reference -- e.g. "func(h1, h2, h3)"
    //----------------------------------------------------------------------------------------------

    // Setup test table.
    setupTable("test_bind", 10 /* num_rows */);

    {
      // Simple bind (by position).
      String selectStmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
          " WHERE h1 = ? AND h2 = ? AND %s(h1, h2) >= ?;", func_name);
      ResultSet rs = (returnsLong) ?
          session.execute(selectStmt, new Integer(7), "h7", Long.MIN_VALUE) :
          session.execute(selectStmt, new Integer(7), "h7", new Integer(0));

      // Checking result.
      List <Row> rows = rs.all();
      assertEquals(1, rows.size());
      Row row = rows.get(0);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
    }

    {
      // Simple bind (by name).
      String selectStmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
          " WHERE h1 = ? AND h2 = ? AND %s(h1, h2) >= ?;", func_name);
      ResultSet rs = (returnsLong) ? session.execute(selectStmt,
          new HashMap<String, Object>() {{
            put("h1", new Integer(7));
            put("h2", "h7");
            put(bind_var_name, Long.MIN_VALUE);
          }}) : session.execute(selectStmt,
          new HashMap<String, Object>() {{
            put("h1", new Integer(7));
            put("h2", "h7");
            put(bind_var_name, new Integer(0));
          }});

      // Checking result.
      List <Row> rows = rs.all();
      assertEquals(1, rows.size());
      Row row = rows.get(0);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
    }

    {
      // Prepare bind (by position).
      String selectStmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
          " WHERE h1 = ? AND h2 = ? AND %s(h1, h2) >= ?;", func_name);
      PreparedStatement stmt = session.prepare(selectStmt);
      ResultSet rs = (returnsLong) ?
          session.execute(stmt.bind(new Integer(7), "h7", Long.MIN_VALUE)) :
          session.execute(stmt.bind(new Integer(7), "h7", new Integer(0)));
      // Checking result.
      List <Row> rows = rs.all();
      assertEquals(1, rows.size());
      Row row = rows.get(0);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
    }

    {
      // Prepare bind (by name).
      String selectStmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
          " WHERE h1 = ? AND h2 = ? AND %s(h1, h2) >= ?;", func_name);
      PreparedStatement stmt = session.prepare(selectStmt);
      BoundStatement bstmt = stmt.bind()
          .setInt("h1", 7)
          .setString("h2", "h7");
      if (returnsLong) {
        bstmt.setLong(bind_var_name, Long.MIN_VALUE);
      } else {
        bstmt.setInt(bind_var_name, new Integer(0));
      }
      ResultSet rs = session.execute(bstmt);
      // Checking result.
      List <Row> rows = rs.all();
      assertEquals(1, rows.size());
      Row row = rows.get(0);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
    }

    {
      // Prepare bind (by name with named markers).
      String selectStmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
          " WHERE h1 = :b1 AND h2 = :b2 AND %s(h1, h2) >= :b3;", func_name);
      PreparedStatement stmt = session.prepare(selectStmt);
      BoundStatement bstmt = stmt
          .bind()
          .setInt("b1", 7)
          .setString("b2", "h7");
      if (returnsLong) {
        bstmt.setLong("b3", Long.MIN_VALUE);
      } else {
        bstmt.setInt("b3", new Integer(0));
      }
      ResultSet rs = session.execute(bstmt);
      // Checking result.
      List <Row> rows = rs.all();
      assertEquals(1, rows.size());
      Row row = rows.get(0);
      assertEquals(7, row.getInt(0));
      assertEquals("h7", row.getString(1));
      assertEquals(107, row.getInt(2));
      assertEquals("r107", row.getString(3));
      assertEquals(1007, row.getInt(4));
      assertEquals("v1007", row.getString(5));
    }

    //----------------------------------------------------------------------------------------------
    // Testing token as builtin call -- e.g. "token(2, '3', 4)"
    //----------------------------------------------------------------------------------------------

    // This is the name template CQL uses for binding args of token builtin call.
    String argNameTemplate = "arg%d(system." + func_name + ")";

    {
      // Bind by position for bcall arguments.
      String selectStmt = String.format("SELECT * FROM test_bind WHERE %s(h1, h2) = %s(?, ?)",
          func_name, func_name);
      Iterator<Row> rows = session.execute(selectStmt, new Integer(6), "h6").iterator();

      assertTrue(rows.hasNext());
      // Checking result.
      Row row = rows.next();
      assertEquals(6, row.getInt(0));
      assertEquals("h6", row.getString(1));
      assertEquals(106, row.getInt(2));
      assertEquals("r106", row.getString(3));
      assertEquals(1006, row.getInt(4));
      assertEquals("v1006", row.getString(5));
      assertFalse(rows.hasNext());
    }

    {
      // Bind by name -- simple bind, all args.
      String selectStmt = String.format("SELECT * FROM test_bind WHERE %s(h1, h2) = " +
          "%s(?, ?);", func_name, func_name);

      Map<String, Object> bvarMap = new HashMap<>();
      bvarMap.put(String.format(argNameTemplate, 0), new Integer(5));
      bvarMap.put(String.format(argNameTemplate, 1), "h5");

      Iterator<Row> rows = session.execute(selectStmt, bvarMap).iterator();

      assertTrue(rows.hasNext());
      // Checking result.
      Row row = rows.next();
      assertEquals(5, row.getInt(0));
      assertEquals("h5", row.getString(1));
      assertEquals(105, row.getInt(2));
      assertEquals("r105", row.getString(3));
      assertEquals(1005, row.getInt(4));
      assertEquals("v1005", row.getString(5));
      assertFalse(rows.hasNext());
    }

    {
      // Bind by name -- prepare bind, some of the args.
      String selectStmt = String.format("SELECT * FROM test_bind WHERE %s(h1, h2) = " +
          "%s(3, ?);", func_name, func_name);

      Map<String, Object> bvarMap = new HashMap<>();
      bvarMap.put(String.format(argNameTemplate, 1), "h3");

      Iterator<Row> rows = session.execute(selectStmt, bvarMap).iterator();

      assertTrue(rows.hasNext());
      // Checking result.
      Row row = rows.next();
      assertEquals(3, row.getInt(0));
      assertEquals("h3", row.getString(1));
      assertEquals(103, row.getInt(2));
      assertEquals("r103", row.getString(3));
      assertEquals(1003, row.getInt(4));
      assertEquals("v1003", row.getString(5));
      assertFalse(rows.hasNext());
    }

    {
      // Bind by name -- prepare bind, mixed un-named and named markers.
      String selectStmt = String.format("SELECT * FROM test_bind WHERE %s(h1, h2) = " +
          "%s(?, :second);", func_name, func_name);

      Map<String, Object> bvarMap = new HashMap<>();
      bvarMap.put(String.format(argNameTemplate, 0), new Integer(8));
      bvarMap.put("second", "h8");

      Iterator<Row> rows = session.execute(selectStmt, bvarMap).iterator();

      assertTrue(rows.hasNext());
      // Checking result.
      Row row = rows.next();
      assertEquals(8, row.getInt(0));
      assertEquals("h8", row.getString(1));
      assertEquals(108, row.getInt(2));
      assertEquals("r108", row.getString(3));
      assertEquals(1008, row.getInt(4));
      assertEquals("v1008", row.getString(5));
      assertFalse(rows.hasNext());
    }

    LOG.info("End test");
  }

  @Test
  public void testBindingToken() throws Exception {
    testPartitionOps("token", "partition key token", /* returnsLong */ true);
  }

  @Test
  public void testBindingPartitionHash() throws Exception {
    testPartitionOps("partition_hash", "[partition_hash]", /* returnsLong */ false);
  }

  @Test
  public void testInKeywordWithBind() throws Exception {
    LOG.info("TEST IN KEYWORD - Start");
    setupTable("in_bind_test", 10);


    // Test basic bind.
    {
      List<Integer> intList = new LinkedList<>();
      intList.add(1);
      intList.add(-1);
      intList.add(3);
      intList.add(7);

      ResultSet rs = session.execute("SELECT * FROM in_bind_test WHERE h1 IN ?", intList);
      Set<Integer> expectedValues = new HashSet<>();
      expectedValues.add(1);
      expectedValues.add(3);
      expectedValues.add(7);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expectedValues.contains(h1));
        expectedValues.remove(h1);
      }
      assertTrue(expectedValues.isEmpty());
    }

    // Test binding full hash key.
    {
      List<Integer> intList = new LinkedList<>();
      intList.add(1);
      intList.add(-1);
      intList.add(3);
      intList.add(7);

      List<String> strList = new LinkedList<>();
      strList.add("h1");
      strList.add("h3");
      strList.add("h7");
      strList.add("h10");

      {
        ResultSet rs = session.execute("SELECT * FROM in_bind_test WHERE h1 IN ? AND h2 IN ?",
                                       intList, strList);
        Set<Integer> expectedValues = new HashSet<>();
        expectedValues.add(1);
        expectedValues.add(3);
        expectedValues.add(7);
        // Check rows
        for (Row row : rs) {
          Integer h1 = row.getInt("h1");
          assertTrue(expectedValues.contains(h1));
          expectedValues.remove(h1);
        }
        assertTrue(expectedValues.isEmpty());
      }

      {
        ResultSet rs = session.execute("SELECT * FROM in_bind_test WHERE h1 IN :b1 AND h2 IN :b2",
                                       new HashMap<String, Object>() {{
                                           put("b1", intList);
                                           put("b2", strList);
                                         }});
        Set<Integer> expectedValues = new HashSet<>();
        expectedValues.add(1);
        expectedValues.add(3);
        expectedValues.add(7);
        // Check rows
        for (Row row : rs) {
          Integer h1 = row.getInt("h1");
          assertTrue(expectedValues.contains(h1));
          expectedValues.remove(h1);
        }
        assertTrue(expectedValues.isEmpty());
      }
    }

    // Test prepare bind.
    {
      List<String> stringList = new LinkedList<>();
      stringList.add("r106");
      stringList.add("r104");
      stringList.add("r_non_existent");
      stringList.add("r101");

      PreparedStatement prepared =
          session.prepare("SELECT * FROM in_bind_test WHERE r2 IN ?");
      ResultSet rs = session.execute(prepared.bind(stringList));
      Set<Integer> expectedValues = new HashSet<>();
      expectedValues.add(1);
      expectedValues.add(4);
      expectedValues.add(6);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expectedValues.contains(h1));
        expectedValues.remove(h1);
      }
      assertTrue(expectedValues.isEmpty());
    }

    // Test binding IN elems individually - one element
    {
      ResultSet rs = session.execute("SELECT * FROM in_bind_test WHERE h1 IN (?)",
                                     new Integer(2));
      Set<Integer> expectedValues = new HashSet<>();
      expectedValues.add(2);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expectedValues.contains(h1));
        expectedValues.remove(h1);
      }
      assertTrue(expectedValues.isEmpty());
    }

    // Test binding IN elems individually - multiple conditions
    {
      ResultSet rs = session.execute("SELECT * FROM in_bind_test WHERE h1 IN (?) AND r1 IN (?)",
                                     new Integer(5), new Integer(105));
      Set<Integer> expectedValues = new HashSet<>();
      expectedValues.add(5);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expectedValues.contains(h1));
        expectedValues.remove(h1);
      }
      assertTrue(expectedValues.isEmpty());
    }

    // Test binding IN elems individually - several elements
    {
      ResultSet rs = session.execute("SELECT * FROM in_bind_test WHERE h1 IN (?, ?, ?)",
                                     new Integer(9), new Integer(4), new Integer(-1));
      Set<Integer> expectedValues = new HashSet<>();
      expectedValues.add(4);
      expectedValues.add(9);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expectedValues.contains(h1));
        expectedValues.remove(h1);
      }
      assertTrue(expectedValues.isEmpty());
    }
  }

  private Row expected_one_row(ResultSet rs) {
      LOG.info(String.format("Result: %s", rs.toString()));
      Row row = rs.one();
      LOG.info(row.toString());
      assertNotNull(row);
      assertNull(rs.one()); // Assert exactly 1 row.
      return row;
  }

  interface Checker {
    void check(ResultSet rs);
  };

  @Test
  public void testSelectBindWithExpr() throws Exception {
    LOG.info("Begin test");

    Checker myjson = (ResultSet rs) -> {
      // Assert exactly 1 row is returned with expected column values.
      Row row = expected_one_row(rs);
      assertEquals(1, row.getInt(0));
      assertEquals(5, new JSONObject(row.getJson("j")).getInt("y"));
    };

    session.execute("CREATE TABLE test_tbl (h int PRIMARY KEY, j jsonb);");
    session.execute("INSERT INTO test_tbl (h, j) VALUES (1, '{\"y\":5}');");
    {
      String sel_stmt = "SELECT * FROM test_tbl WHERE h = 1 ;";
      myjson.check(session.execute(sel_stmt));
    }
    {
      String sel_stmt = "SELECT * FROM test_tbl WHERE h = ? ;";
      myjson.check(session.execute(sel_stmt, new Integer(1)));
    }
    {
      String sel_stmt = "SELECT * FROM test_tbl WHERE j = ? ;";
      myjson.check(session.execute(sel_stmt, new String("{\"y\":5}")));
    }
    {
      // Bind using expression based on JSONB.
      String sel_stmt = "SELECT * FROM test_tbl WHERE j->>'y' = ? ;";
      myjson.check(session.execute(sel_stmt, new String("5")));
    }
    {
      // Bind using expression based on JSONB with binded variable name.
      String sel_stmt = "SELECT * FROM test_tbl WHERE j->>'y' = :my_var_name ;";
      myjson.check(session.execute(sel_stmt, new HashMap<String, Object>()
          {{ put("my_var_name", "5"); }}));
    }
    // Test internal names for the bind variables.
    {
      String sel_stmt = "SELECT * FROM test_tbl WHERE h = ? ;";
      PreparedStatement prep_stmt = session.prepare(sel_stmt);
      myjson.check(session.execute(prep_stmt.bind().setInt("h", 1)));
    }
    {
      String sel_stmt = "SELECT * FROM test_tbl WHERE j->>'y' = ? ;";
      PreparedStatement prep_stmt = session.prepare(sel_stmt);
      myjson.check(session.execute(prep_stmt.bind().setString("json_attr(j)", "5")));
    }

    Checker mymap = (ResultSet rs) -> {
      // Assert exactly 1 row is returned with expected column values.
      Row row = expected_one_row(rs);
      Map<Integer, String> map_value = row.getMap(1, Integer.class, String.class);
      assertEquals(2, map_value.size());
      assertTrue(map_value.containsKey(2));
      assertEquals("b", map_value.get(2));
      assertTrue(map_value.containsKey(3));
      assertEquals("c", map_value.get(3));
    };

    session.execute("CREATE TABLE test_map (h int PRIMARY KEY, m map<int, varchar>);");
    session.execute("INSERT INTO test_map (h, m) VALUES (1, {2:'b', 3:'c'});");
    {
      String sel_stmt = "SELECT * FROM test_map WHERE h = 1 ;";
      mymap.check(session.execute(sel_stmt));
    }
    {
      // Bind using expression based on MAP.
      String sel_stmt = "SELECT * FROM test_map WHERE m[2] = ? ;";
      mymap.check(session.execute(sel_stmt, new String("b")));
    }
    // Test PreparedStatement API.
    {
      String sel_stmt = "SELECT * FROM test_map WHERE m[2] = ? ;";
      PreparedStatement prep_stmt = session.prepare(sel_stmt);
      mymap.check(session.execute(prep_stmt.bind(new String("b"))));
    }
    {
      String sel_stmt = "SELECT * FROM test_map WHERE m[2] = :my_var_name ;";
      PreparedStatement prep_stmt = session.prepare(sel_stmt);
      mymap.check(session.execute(prep_stmt.bind().setString("my_var_name", "b")));
    }
    {
      String sel_stmt = "SELECT * FROM test_map WHERE h = ? ;";
      PreparedStatement prep_stmt = session.prepare(sel_stmt);
      mymap.check(session.execute(prep_stmt.bind(new Integer(1))));
    }
    // Test internal names for the bind variables.
    {
      String sel_stmt = "SELECT * FROM test_map WHERE h = ? ;";
      PreparedStatement prep_stmt = session.prepare(sel_stmt);
      mymap.check(session.execute(prep_stmt.bind().setInt("h", 1)));
    }
    {
      String sel_stmt = "SELECT * FROM test_map WHERE m[2] = ? ;";
      PreparedStatement prep_stmt = session.prepare(sel_stmt);
      mymap.check(session.execute(prep_stmt.bind().setString("value(m)", "b")));
    }

    LOG.info("End test");
  }

  // yb-cql-4x/src/test/java/org/yb/loadtest/TestTupleOperators.java contains
  // the tests for the unsupported multi-column IN bind formats.
  @Test
  public void testMultiColumnInWithBind() throws Exception {
    LOG.info("TEST IN KEYWORD - Start");
    setupTable("in_bind_test", 10);

    // Test basic bind.
    {
      ResultSet rs = session.execute("SELECT * FROM in_bind_test WHERE h1 = 1 AND h2 = 'h1' AND " +
          "(r1, r2) IN ((?, ?), (?, ?), (?, ?))",
          new Integer(101), new String("r101"), new Integer(103),
          new String("r103"), new Integer(107), new String("r107"));
      Set<Pair<Integer, String>> expectedValues = new HashSet<>();
      expectedValues.add(new Pair(101, "r101"));
      // Check rows
      for (Row row : rs) {
        Integer r1 = row.getInt("r1");
        String r2 = row.getString("r2");
        Pair<Integer, String> value = new Pair(r1, r2);
        assertTrue(expectedValues.contains(value));
        expectedValues.remove(value);
      }
      assertTrue(expectedValues.isEmpty());
    }

    // Test prepare bind.
    {
      PreparedStatement prepared = session
          .prepare("SELECT * FROM in_bind_test WHERE h1 = 1 AND h2 = 'h1' AND (r1, r2) IN " +
              "((?, ?), (?, ?), (?, ?))");
      ResultSet rs = session
          .execute(prepared.bind(new Integer(101), new String("r101"),
              new Integer(103), new String("r103"),
              new Integer(107), new String("r107")));
      Set<Pair<Integer, String>> expectedValues = new HashSet<>();
      expectedValues.add(new Pair(101, "r101"));
      // Check rows
      for (Row row : rs) {
        Integer r1 = row.getInt("r1");
        String r2 = row.getString("r2");
        Pair<Integer, String> value = new Pair(r1, r2);
        assertTrue(expectedValues.contains(value));
        expectedValues.remove(value);
      }
      assertTrue(expectedValues.isEmpty());
    }

    // Test bind by name.
    {
      HashMap values = new HashMap<String, Object>() {
        {
          put("b1", new Integer(101));
          put("b2", new String("r101"));
          put("b3", new Integer(103));
          put("b4", new String("r103"));
          put("b5", new Integer(107));
          put("b6", new String("r107"));
        }
      };
      ResultSet rs = session.execute("SELECT * FROM in_bind_test WHERE h1 = 1 AND h2 = 'h1' AND " +
          "(r1, r2) IN ((:b1, :b2), (:b3, :b4), (:b5, :b6))", values);
      Set<Pair<Integer, String>> expectedValues = new HashSet<>();
      expectedValues.add(new Pair(101, "r101"));
      // Check rows
      for (Row row : rs) {
        Integer r1 = row.getInt("r1");
        String r2 = row.getString("r2");
        Pair<Integer, String> value = new Pair(r1, r2);
        assertTrue(expectedValues.contains(value));
        expectedValues.remove(value);
      }
      assertTrue(expectedValues.isEmpty());
    }
  }

  @Test
  public void testCollectionBindInserts() throws Exception {
    session.execute(
        "CREATE TABLE test_tbl (h int PRIMARY KEY, m map<int, varchar>, ll list<varchar>);");

    //---------------------------- Testing Binding Only Value ----------------------------------\\
    {
      String bindByPosition =
          "INSERT INTO test_tbl (h, m, ll) VALUES (1, {100: 'map_value_1'}, ['list_value_1']) "
              + "IF m[100] != ? AND ll[0] != ?;";
      String bindByName =
          "INSERT INTO test_tbl (h, m, ll) VALUES (2, {200: 'map_value_2'}, ['list_value_2']) "
              + "IF m[200] != ? AND ll[0] != ?;";
      String bindByNamedMarkers =
          "INSERT INTO test_tbl (h, m, ll) VALUES (3, {300: 'map_value_3'}, ['list_value_3']) "
              + "IF m[300] != :mv AND ll[0] != :lv;";

      // Direct binding
      {
        // Bind by position
        for (boolean applied : Arrays.asList(true, false)) {
          ResultSet rs = session.execute(bindByPosition, "map_value_1", "list_value_1");
          assertEquals(applied, expected_one_row(rs).getBool(0));
        }

        assertQuery(
            "SELECT * from test_tbl where h = 1", "Row[1, {100=map_value_1}, [list_value_1]]");

        session.execute("TRUNCATE TABLE test_tbl;");

        // Bind by name
        {
          HashMap bindings = new HashMap<String, Object>() {{
              put("value(m)", "map_value_2");
              put("value(ll)", "list_value_2");
            }};
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(bindByName, bindings);
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }

        assertQuery(
            "SELECT * from test_tbl where h = 2", "Row[2, {200=map_value_2}, [list_value_2]]");

        session.execute("TRUNCATE TABLE test_tbl;");

        // Bind by named markers
        {
          HashMap bindings = new HashMap<String, Object>() {{
              put("mv", "map_value_3");
              put("lv", "list_value_3");
            }};
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(bindByNamedMarkers, bindings);
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }

        assertQuery(
            "SELECT * from test_tbl where h = 3", "Row[3, {300=map_value_3}, [list_value_3]]");
      }

      session.execute("truncate table test_tbl;");

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(prepared.bind("map_value_1", "list_value_1"));
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }

        assertQuery(
            "SELECT * from test_tbl where h = 1", "Row[1, {100=map_value_1}, [list_value_1]]");

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(
                prepared.bind().setString("value(m)", "map_value_2")
                               .setString("value(ll)", "list_value_2"));
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }

        assertQuery(
            "SELECT * from test_tbl where h = 2", "Row[2, {200=map_value_2}, [list_value_2]]");

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(
                prepared.bind().setString("mv", "map_value_3").setString("lv", "list_value_3"));
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }

        assertQuery(
            "SELECT * from test_tbl where h = 3", "Row[3, {300=map_value_3}, [list_value_3]]");
      }
    }

    session.execute("TRUNCATE table test_tbl");

    //---------------------------- Testing Binding Only Key ----------------------------------\\
    {
      String bindByPosition =
          "INSERT INTO test_tbl (h, m, ll) VALUES (1, {100: 'map_value_1'}, ['list_value_1']) "
              + "IF m[?] != 'map_value_1' AND ll[?] != 'list_value_1';";
      String bindByName =
          "INSERT INTO test_tbl (h, m, ll) VALUES (2, {200: 'map_value_2'}, ['list_value_2']) "
              + "IF m[?] != 'map_value_2' AND ll[?] != 'list_value_2';";
      String bindByNamedMarkers =
          "INSERT INTO test_tbl (h, m, ll) VALUES (3, {300: 'map_value_3'}, ['list_value_3']) "
              + "IF m[:mk] != 'map_value_3' AND ll[:li] != 'list_value_3';";

      // Direct binding
      {
        // Bind by position
        for (boolean applied : Arrays.asList(true, false)) {
          ResultSet rs = session.execute(bindByPosition, 100, 0);
          assertEquals(applied, expected_one_row(rs).getBool(0));
        }
        assertQuery(
            "SELECT * from test_tbl where h = 1", "Row[1, {100=map_value_1}, [list_value_1]]");

        session.execute("TRUNCATE TABLE test_tbl;");

        // Bind by name
        {
          HashMap bindings = new HashMap<String, Object>() {{
              put("key(m)", 200);
              put("idx(ll)", 0);
            }};
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(bindByName, bindings);
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }
        assertQuery(
            "SELECT * from test_tbl where h = 2", "Row[2, {200=map_value_2}, [list_value_2]]");

        session.execute("TRUNCATE TABLE test_tbl;");

        // Bind by named markers
        {
          HashMap bindings = new HashMap<String, Object>() {{
              put("mk", 300);
              put("li", 0);
            }};
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(bindByNamedMarkers, bindings);
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }
        assertQuery(
            "SELECT * from test_tbl where h = 3", "Row[3, {300=map_value_3}, [list_value_3]]");
      }

      session.execute("truncate table test_tbl;");

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(prepared.bind(100, 0));
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }
        assertQuery(
            "SELECT * from test_tbl where h = 1", "Row[1, {100=map_value_1}, [list_value_1]]");

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs =
                session.execute(prepared.bind().setInt("key(m)", 200).setInt("idx(ll)", 0));
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }
        assertQuery(
            "SELECT * from test_tbl where h = 2", "Row[2, {200=map_value_2}, [list_value_2]]");

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(prepared.bind().setInt("mk", 300).setInt("li", 1));
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }
        assertQuery(
            "SELECT * from test_tbl where h = 3", "Row[3, {300=map_value_3}, [list_value_3]]");
      }
    }

    session.execute("TRUNCATE table test_tbl");

    //------------------------ Testing Binding both Key & Value ----------------------------\\
    {
      String bindByPosition =
          "INSERT INTO test_tbl (h, m, ll) VALUES (1, {100: 'map_value_1'}, ['list_value_1']) "
              + "IF m[?] != ? AND ll[?] != ?;";
      String bindByName =
          "INSERT INTO test_tbl (h, m, ll) VALUES (2, {200: 'map_value_2'}, ['list_value_2']) "
              + "IF m[?] != ? AND ll[?] != ?;";
      String bindByNamedMarkers =
          "INSERT INTO test_tbl (h, m, ll) VALUES (3, {300: 'map_value_3'}, ['list_value_3']) "
              + "IF m[:mk] != :mv AND ll[:li] != :lv;";

      // Direct binding
      {
        // Bind by position
        for (boolean applied : Arrays.asList(true, false)) {
          ResultSet rs = session.execute(bindByPosition, 100, "map_value_1", 0, "list_value_1");
          assertEquals(applied, expected_one_row(rs).getBool(0));
        }
        assertQuery(
            "SELECT * from test_tbl where h = 1", "Row[1, {100=map_value_1}, [list_value_1]]");
        session.execute("TRUNCATE TABLE test_tbl;");

        // Bind by name
        {
          HashMap bindings = new HashMap<String, Object>() {{
              put("key(m)", 200);
              put("value(m)", "map_value_2");
              put("idx(ll)", 0);
              put("value(ll)", "list_value_2");
            }};
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(bindByName, bindings);
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }
        assertQuery(
            "SELECT * from test_tbl where h = 2", "Row[2, {200=map_value_2}, [list_value_2]]");
        session.execute("TRUNCATE TABLE test_tbl;");

        // Bind by named markers
        {
          HashMap bindings = new HashMap<String, Object>() {{
              put("mv", "map_value_3");
              put("lv", "list_value_3");
              put("mk", 300);
              put("li", 0);
            }};
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(bindByNamedMarkers, bindings);
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }
        assertQuery(
            "SELECT * from test_tbl where h = 3", "Row[3, {300=map_value_3}, [list_value_3]]");
      }

      session.execute("truncate table test_tbl;");

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(prepared.bind(100, "map_value_1", 0, "list_value_1"));
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }
        assertQuery(
            "SELECT * from test_tbl where h = 1", "Row[1, {100=map_value_1}, [list_value_1]]");

            // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(prepared.bind().setInt("key(m)", 200)
                                                          .setString("value(m)", "map_value_2")
                                                          .setInt("idx(ll)", 0)
                                                          .setString("value(ll)", "list_value_2"));
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }
        assertQuery(
            "SELECT * from test_tbl where h = 2", "Row[2, {200=map_value_2}, [list_value_2]]");

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          for (boolean applied : Arrays.asList(true, false)) {
            ResultSet rs = session.execute(prepared.bind().setInt("mk", 300)
                                                          .setString("mv", "map_value_3")
                                                          .setInt("li", 0)
                                                          .setString("lv", "list_value_3"));
            assertEquals(applied, expected_one_row(rs).getBool(0));
          }
        }
        assertQuery(
            "SELECT * from test_tbl where h = 3", "Row[3, {300=map_value_3}, [list_value_3]]");
      }
    }
  }

  public void testCollectionBindUpdates(BindCollAssignmentByColName useColName)
      throws Exception {
    session.execute(
        "CREATE TABLE test_tbl (h int PRIMARY KEY, m map<int, varchar>, ll list<varchar>);");

    // Insert 10 rows
    for (int h = 1; h <= 10; h++) {
      session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
          h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1", "list_value_2",
          "list_value_3", "list_value_4");
    }

    // Binding only value
    {
      String bindByPosition = "UPDATE test_tbl set m[100] = ?, ll[1] = ? WHERE h = ?";
      String bindByName = "UPDATE test_tbl set m[200] = ?, ll[2] = ? WHERE h = ?";
      String bindByNamedMarkers = "UPDATE test_tbl set m[300] = :mv, ll[3] = :lv WHERE h = :pk";

      // Bind by position
      session.execute(bindByPosition, "map_updated_value_1", "list_updated_value_1", 1);
      assertQuery("SELECT * from test_tbl where h = 1",
          "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, list_value_2, "
              + "list_value_3, list_value_4]]");

      // Direct binding
      {
        // Bind by position
        session.execute(bindByPosition, "map_updated_value_1", "list_updated_value_1", 1);
        assertQuery("SELECT * from test_tbl where h = 1",
            "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, list_value_2,"
                + " list_value_3, list_value_4]]");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("value(m)", "map_updated_value_2");
              put("value(ll)", "list_updated_value_2");
            }};
          session.execute(bindByName, values);
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, "
                  + "list_updated_value_2, list_value_3, list_value_4]]");

          if (useColName == BindCollAssignmentByColName.ON) {
            // Assert backwards compatibility for syntax "UPDATE .. SET m[100] = ?, ll[2] = ? ..".
            // We also support using the column name i.e. "m" and "ll" when the GFlag
            // ycql_bind_collection_assignment_using_column_name is true.
            HashMap valuesForBackwardsCompatibility = new HashMap<String, Object>() {{
                put("h", 4);
                put("m", "map_updated_value_backwards");
                put("ll", "list_updated_value_backwards");
              }};
            session.execute(bindByName, valuesForBackwardsCompatibility);
            assertQuery("SELECT * from test_tbl where h = 4",
                "Row[4, {200=map_updated_value_backwards, 400=map_value_4}, [list_value_0, "
                    + "list_value_1, list_updated_value_backwards, list_value_3, list_value_4]]");
          }
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("mv", "map_updated_value_3");
              put("lv", "list_updated_value_3");
              put("pk", 3);
            }};
          session.execute(bindByNamedMarkers, values);
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }

      session.execute("truncate table test_tbl;");
      // Insert 10 rows
      for (int h = 1; h <= 10; h++) {
        session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : 'b'}, [?, ?, ?, ?, ?]);",
            h, h * 100, "list_value_0", "list_value_1", "list_value_2", "list_value_3",
            "list_value_4");
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          session.execute(prepared.bind("map_updated_value_1", "list_updated_value_1", 1));
          assertQuery("SELECT * from test_tbl where h = 1",
              "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, list_value_2,"
                  + " list_value_3, list_value_4]]");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          // We can only support one kind of bindvar name for PreparedStatement. When the flag
          // ycql_bind_collection_assignment_using_column_name is on, we allow using the column
          // name.
          String mvName =
              useColName == BindCollAssignmentByColName.ON ? "m" : "value(m)";
          String lvName =
              useColName == BindCollAssignmentByColName.ON ? "ll" : "value(ll)";
          session.execute(prepared.bind()
                              .setInt("h", 2)
                              .setString(mvName, "map_updated_value_2")
                              .setString(lvName, "list_updated_value_2"));
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, "
                  + "list_updated_value_2, list_value_3, list_value_4]]");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          session.execute(prepared.bind().setInt("pk", 3)
                                         .setString("mv", "map_updated_value_3")
                                         .setString("lv", "list_updated_value_3"));
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }
    }

    session.execute("TRUNCATE TABLE test_tbl;");
    for (int h = 1; h <= 10; h++) {
      session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
          h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1", "list_value_2",
          "list_value_3", "list_value_4");
    }

    // Binding only key
    {
      String bindByPosition = "UPDATE test_tbl set m[?] = 'map_updated_value_1', ll[?] = "
          + "'list_updated_value_1' WHERE h = ?";
      String bindByName = "UPDATE test_tbl set m[?] = 'map_updated_value_2', ll[?] = "
          + "'list_updated_value_2' WHERE h = ?";
      String bindByNamedMarkers = "UPDATE test_tbl set m[:mk] = 'map_updated_value_3', ll[:li] = "
          + "'list_updated_value_3' WHERE h = :pk";

      // Direct binding
      {
        // Bind by position
        session.execute(bindByPosition, 100, 1, 1);
        assertQuery("SELECT * from test_tbl where h = 1",
            "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, "
                + "list_value_2, list_value_3, list_value_4]]");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("key(m)", 200);
              put("idx(ll)", 2);
            }};
          session.execute(bindByName, values);
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, "
                  + "list_updated_value_2, list_value_3, list_value_4]]");
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("mk", 300);
              put("li", 3);
              put("pk", 3);
            }};
          session.execute(bindByNamedMarkers, values);
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }

      session.execute("truncate table test_tbl;");
      for (int h = 1; h <= 10; h++) {
        session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : 'b'}, [?, ?, ?, ?, ?]);",
            h, h * 100, "list_value_0", "list_value_1", "list_value_2", "list_value_3",
            "list_value_4");
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          session.execute(prepared.bind(100, 1, 1));
          assertQuery("SELECT * from test_tbl where h = 1",
              "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, "
                  + "list_value_2, list_value_3, list_value_4]]");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          session.execute(
              prepared.bind().setInt("h", 2).setInt("key(m)", 200).setInt("idx(ll)", 2));
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, "
                  + "list_updated_value_2, list_value_3, list_value_4]]");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          session.execute(prepared.bind().setInt("pk", 3).setInt("mk", 300).setInt("li", 3));
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }
    }

    session.execute("TRUNCATE TABLE test_tbl;");
    for (int h = 1; h <= 10; h++) {
      session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
          h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1", "list_value_2",
          "list_value_3", "list_value_4");
    }

    // Binding both key and value
    {
      String bindByPosition = "UPDATE test_tbl set m[?] = ?, ll[?] = ? WHERE h = ?";
      String bindByName = "UPDATE test_tbl set m[?] = ?, ll[?] = ? WHERE h = ?";
      String bindByNamedMarkers = "UPDATE test_tbl set m[:mk] = :mv, ll[:li] = :lv WHERE h = :pk";

      // Direct binding
      {
        // Bind by position
        session.execute(bindByPosition, 100, "map_updated_value_1", 1, "list_updated_value_1", 1);
        assertQuery("SELECT * from test_tbl where h = 1",
            "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, "
                + "list_value_2, list_value_3, list_value_4]]");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("key(m)", 200);
              put("value(m)", "map_updated_value_2");
              put("idx(ll)", 2);
              put("value(ll)", "list_updated_value_2");
            }};
          session.execute(bindByName, values);
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, "
                  + "list_updated_value_2, list_value_3, list_value_4]]");
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("mk", 300);
              put("li", 3);
              put("pk", 3);
              put("mv", "map_updated_value_3");
              put("lv", "list_updated_value_3");
            }};
          session.execute(bindByNamedMarkers, values);
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }

      session.execute("truncate table test_tbl;");
      for (int h = 1; h <= 10; h++) {
        session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : 'b'}, [?, ?, ?, ?, ?]);",
            h, h * 100, "list_value_0", "list_value_1", "list_value_2", "list_value_3",
            "list_value_4");
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          session.execute(prepared.bind(100, "map_updated_value_1", 1, "list_updated_value_1", 1));
          assertQuery("SELECT * from test_tbl where h = 1",
              "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, "
                  + "list_value_2, list_value_3, list_value_4]]");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          session.execute(prepared.bind().setInt("h", 2)
                                         .setInt("key(m)", 200)
                                         .setInt("idx(ll)", 2)
                                         .setString("value(m)", "map_updated_value_2")
                                         .setString("value(ll)", "list_updated_value_2"));
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, list_updated_value_2,"
                  + " list_value_3, list_value_4]]");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          session.execute(prepared.bind().setInt("pk", 3)
                                         .setInt("mk", 300)
                                         .setInt("li", 3)
                                         .setString("mv", "map_updated_value_3")
                                         .setString("lv", "list_updated_value_3"));
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }
    }
  }

  @Test
  public void testCollectionBindUpdates() throws Exception {
    testCollectionBindUpdates(BindCollAssignmentByColName.OFF);
  }

  @Test
  public void testCollectionBindDeletes() throws Exception {
    session.execute(
        "CREATE TABLE test_tbl (h int PRIMARY KEY, m map<int, varchar>, ll list<varchar>);");

    // Insert 10 rows
    for (int h = 1; h <= 10; h++) {
      session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
          h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1", "list_value_2",
          "list_value_3", "list_value_4");
    }

    // Binding only value
    {
      String bindByPosition = "DELETE FROM test_tbl WHERE h = ? IF m[100] = ? AND ll[1] = ?";
      String bindByName = "DELETE FROM test_tbl WHERE h = ? IF m[200] = ? AND ll[2] = ?";
      String bindByNamedMarkers =
          "DELETE FROM test_tbl WHERE h = :pk IF m[300] = :mv AND ll[3] = :lv";

      // Direct binding
      {
        // Bind by position
        session.execute(bindByPosition, 1, "map_value_1", "list_value_1");
        assertNoRow("SELECT * from test_tbl where h = 1");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("value(m)", "map_value_2");
              put("value(ll)", "list_value_2");
            }};
          session.execute(bindByName, values);
          assertNoRow("SELECT * from test_tbl where h = 2");
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("mv", "map_value_3");
              put("lv", "list_value_3");
              put("pk", 3);
            }};
          session.execute(bindByNamedMarkers, values);
          assertNoRow("SELECT * from test_tbl where h = 3");
        }
      }

      session.execute("truncate table test_tbl;");
      // Insert 10 rows
      for (int h = 1; h <= 10; h++) {
        session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
            h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1",
            "list_value_2", "list_value_3", "list_value_4");
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          session.execute(prepared.bind(1, "map_value_1", "list_value_1"));
          assertNoRow("SELECT * from test_tbl where h = 1");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          session.execute(prepared.bind().setInt("h", 2)
                                         .setString("value(m)", "map_value_2")
                                         .setString("value(ll)", "list_value_2"));
          assertNoRow("SELECT * from test_tbl where h = 2");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          session.execute(prepared.bind().setInt("pk", 3)
                                         .setString("mv", "map_value_3")
                                         .setString("lv", "list_value_3"));
          assertNoRow("SELECT * from test_tbl where h = 3");
        }
      }
    }

    session.execute("TRUNCATE TABLE test_tbl;");
    for (int h = 1; h <= 10; h++) {
      session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
          h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1", "list_value_2",
          "list_value_3", "list_value_4");
    }

    // Binding only key
    {
      String bindByPosition =
          "DELETE FROM test_tbl WHERE h = ? IF m[?] = 'map_value_1' AND ll[?] = 'list_value_1'";
      String bindByName =
          "DELETE FROM test_tbl WHERE h = ? IF m[?] = 'map_value_2' AND ll[?] = 'list_value_2'";
      String bindByNamedMarkers =
          "DELETE FROM test_tbl WHERE h = :pk IF m[:mk]='map_value_3' AND ll[:li]='list_value_3'";

      // Direct binding
      {
        // Bind by position
        session.execute(bindByPosition, 1, 100, 1);
        assertNoRow("SELECT * from test_tbl where h = 1");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("key(m)", 200);
              put("idx(ll)", 2);
            }};
          session.execute(bindByName, values);
          assertNoRow("SELECT * from test_tbl where h = 2");
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("pk", 3);
              put("mk", 300);
              put("li", 3);
            }};
          session.execute(bindByNamedMarkers, values);
          assertNoRow("SELECT * from test_tbl where h = 3");
        }
      }

      session.execute("truncate table test_tbl;");
      for (int h = 1; h <= 10; h++) {
        session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
            h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1",
            "list_value_2", "list_value_3", "list_value_4");
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          session.execute(prepared.bind(1, 100, 1));
          assertNoRow("SELECT * from test_tbl where h = 1");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          session.execute(
              prepared.bind().setInt("h", 2).setInt("key(m)", 200).setInt("idx(ll)", 2));
          assertNoRow("SELECT * from test_tbl where h = 2");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          session.execute(prepared.bind().setInt("pk", 3).setInt("mk", 300).setInt("li", 3));
          assertNoRow("SELECT * from test_tbl where h = 3");
        }
      }
    }

    session.execute("TRUNCATE TABLE test_tbl;");
    for (int h = 1; h <= 10; h++) {
      session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
          h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1", "list_value_2",
          "list_value_3", "list_value_4");
    }

    // Binding both key and value
    {
      String bindByPosition = "DELETE FROM test_tbl WHERE h = ? IF m[?] = ? AND ll[?] = ?";
      String bindByName = "DELETE FROM test_tbl WHERE h = ? IF m[?] = ? AND ll[?] = ?";
      String bindByNamedMarkers =
          "DELETE FROM test_tbl WHERE h = :pk IF m[:mk] = :mv AND ll[:li] = :lv";

      // Direct binding
      {
        // Bind by position
        session.execute(bindByPosition, 1, 100, "map_value_1", 1, "list_value_1");
        assertNoRow("SELECT * from test_tbl where h = 1");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("key(m)", 200);
              put("value(m)", "map_value_2");
              put("idx(ll)", 2);
              put("value(ll)", "list_value_2");
            }};
          session.execute(bindByName, values);
          assertNoRow("SELECT * from test_tbl where h = 2");
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("mv", "map_value_3");
              put("lv", "list_value_3");
              put("pk", 3);
              put("mk", 300);
              put("li", 3);
            }};
          session.execute(bindByNamedMarkers, values);
          assertNoRow("SELECT * from test_tbl where h = 3");
        }
      }

      session.execute("truncate table test_tbl;");
      // Insert 10 rows
      for (int h = 1; h <= 10; h++) {
        session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
            h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1",
            "list_value_2", "list_value_3", "list_value_4");
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          session.execute(prepared.bind(1, 100, "map_value_1", 1, "list_value_1"));
          assertNoRow("SELECT * from test_tbl where h = 1");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          session.execute(prepared.bind().setInt("h", 2)
                                         .setInt("key(m)", 200)
                                         .setString("value(m)", "map_value_2")
                                         .setInt("idx(ll)", 2)
                                         .setString("value(ll)", "list_value_2"));
          assertNoRow("SELECT * from test_tbl where h = 2");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          session.execute(prepared.bind().setInt("pk", 3)
                                         .setString("mv", "map_value_3")
                                         .setString("lv", "list_value_3")
                                         .setInt("mk", 300)
                                         .setInt("li", 3));
          assertNoRow("SELECT * from test_tbl where h = 3");
        }
      }
    }
  }

  @Test
  public void testCollectionBindSelects() throws Exception {
    session.execute(
        "CREATE TABLE test_tbl (h int PRIMARY KEY, m map<int, varchar>, ll list<varchar>);");

    // Insert 10 rows
    for (int h = 1; h <= 10; h++) {
      session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
          h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1", "list_value_2",
          "list_value_3", "list_value_4");
    }

    // Binding only value
    {
      String bindByPosition = "SELECT * FROM test_tbl WHERE h = ? AND m[100] = ? AND ll[1] = ?";
      String bindByName = "SELECT * FROM test_tbl WHERE h = ? AND m[200] = ? AND ll[2] = ?";
      String bindByNamedMarkers =
          "SELECT * FROM test_tbl WHERE h = :pk AND m[300] = :mv AND ll[3] = :lv";

      // Direct binding
      {
        // Bind by position
        assertQuery(new SimpleStatement(bindByPosition, 1, "map_value_1", "list_value_1"),
            "Row[1, {100=map_value_1}, [list_value_0, list_value_1, list_value_2, list_value_3, " +
                "list_value_4]]");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("value(m)", "map_value_2");
              put("value(ll)", "list_value_2");
            }};
          assertQuery(new SimpleStatement(bindByName, values),
              "Row[2, {200=map_value_2}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("mv", "map_value_3");
              put("lv", "list_value_3");
              put("pk", 3);
            }};
          assertQuery(new SimpleStatement(bindByNamedMarkers, values),
              "Row[3, {300=map_value_3}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          assertQuery(prepared.bind(1, "map_value_1", "list_value_1"),
              "Row[1, {100=map_value_1}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          assertQuery(prepared.bind().setInt("h", 2)
                                     .setString("value(m)", "map_value_2")
                                     .setString("value(ll)", "list_value_2"),
              "Row[2, {200=map_value_2}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          assertQuery(prepared.bind().setInt("pk", 3)
                                     .setString("mv", "map_value_3")
                                     .setString("lv", "list_value_3"),
              "Row[3, {300=map_value_3}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }
      }
    }

    // Binding only key
    {
      String bindByPosition =
          "SELECT * FROM test_tbl WHERE h = ? AND m[?] = 'map_value_1' AND ll[?] = 'list_value_1'";
      String bindByName =
          "SELECT * FROM test_tbl WHERE h = ? AND m[?] = 'map_value_2' AND ll[?] = 'list_value_2'";
      String bindByNamedMarkers =
          "SELECT * FROM test_tbl WHERE h = :pk AND m[:mk] = 'map_value_3' AND ll[:li] = " +
              "'list_value_3'";

      // Direct binding
      {
        // Bind by position
        assertQuery(new SimpleStatement(bindByPosition, 1, 100, 1),
            "Row[1, {100=map_value_1}, [list_value_0, list_value_1, list_value_2, list_value_3, " +
                "list_value_4]]");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("key(m)", 200);
              put("idx(ll)", 2);
            }};
          assertQuery(new SimpleStatement(bindByName, values),
              "Row[2, {200=map_value_2}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("mk", 300);
              put("li", 3);
              put("pk", 3);
            }};
          assertQuery(new SimpleStatement(bindByNamedMarkers, values),
              "Row[3, {300=map_value_3}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          assertQuery(prepared.bind(1, 100, 1),
              "Row[1, {100=map_value_1}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          assertQuery(prepared.bind().setInt("h", 2)
                                     .setInt("key(m)", 200)
                                     .setInt("idx(ll)", 2),
              "Row[2, {200=map_value_2}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          assertQuery(prepared.bind().setInt("pk", 3)
                                     .setInt("mk", 300)
                                     .setInt("li", 3),
              "Row[3, {300=map_value_3}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }
      }
    }

    // Binding both key and value
    {
      String bindByPosition =
          "SELECT * FROM test_tbl WHERE h = ? AND m[?] = ? AND ll[?] = ?";
      String bindByName =
          "SELECT * FROM test_tbl WHERE h = ? AND m[?] = ? AND ll[?] = ?";
      String bindByNamedMarkers =
          "SELECT * FROM test_tbl WHERE h = :pk AND m[:mk] = :mv AND ll[:li] = :lv";

      // Direct binding
      {
        // Bind by position
        assertQuery(new SimpleStatement(bindByPosition, 1, 100, "map_value_1", 1, "list_value_1"),
            "Row[1, {100=map_value_1}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                " list_value_4]]");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("key(m)", 200);
              put("value(m)", "map_value_2");
              put("idx(ll)", 2);
              put("value(ll)", "list_value_2");
            }};
          assertQuery(new SimpleStatement(bindByName, values),
              "Row[2, {200=map_value_2}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("mk", 300);
              put("li", 3);
              put("pk", 3);
              put("mv", "map_value_3");
              put("lv", "list_value_3");
            }};
          assertQuery(new SimpleStatement(bindByNamedMarkers, values),
              "Row[3, {300=map_value_3}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          assertQuery(prepared.bind(1, 100, "map_value_1", 1, "list_value_1"),
              "Row[1, {100=map_value_1}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          assertQuery(prepared.bind().setInt("h", 2)
                                     .setInt("key(m)", 200)
                                     .setString("value(m)", "map_value_2")
                                     .setInt("idx(ll)", 2)
                                     .setString("value(ll)", "list_value_2"),
              "Row[2, {200=map_value_2}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          assertQuery(prepared.bind().setInt("pk", 3)
                                     .setInt("mk", 300)
                                     .setInt("li", 3)
                                     .setString("mv", "map_value_3")
                                     .setString("lv", "list_value_3"),
              "Row[3, {300=map_value_3}, [list_value_0, list_value_1, list_value_2, list_value_3," +
                  " list_value_4]]");
        }
      }
    }
  }

  /*
    @Test
    public void testTransactionUnboundArg() throws Exception {
      LOG.info("Start test: " + getCurrentTestMethodName());

      session.execute("CREATE TABLE productkey (key text," +
                      "                         key_type text," +
                      "                         tenant text," +
                      "                         PRIMARY KEY (key)) " +
                      "WITH TRANSACTIONS = {'enabled' : true};");
      String stmt =
          "BEGIN TRANSACTION " +
          "  insert into productkey (key, tenant) values (:key, :timestamp); " +
          "  insert into productkey (key, key_type, tenant) values (:key, :keyType, :tenant); " +
          "END TRANSACTION;";
      PreparedStatement preparedStatement = session.prepare(stmt);
      BoundStatement boundStatement  = preparedStatement.bind();
      boundStatement.setString("key", "test");

      try {
        ResultSet rs = session.execute(boundStatement);
        assertNull(rs.one());
        fail("Prepared statement \"" + stmt + "\" did not fail");
      } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
        LOG.info("Expected exception", e);
      }

      LOG.info("End test: " + getCurrentTestMethodName());
    }
  */
}
