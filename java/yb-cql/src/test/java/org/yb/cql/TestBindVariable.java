// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.util.Date;
import java.util.HashMap;
import java.util.Map;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.SyntaxError;

import org.junit.Test;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

public class TestBindVariable extends TestBase {

  private void testInvalidBindStatement(String stmt, Object... values) {
    try {
      session.execute(stmt, values);
      fail("Statement \"" + stmt + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.SyntaxError e) {
      LOG.info("Expected exception", e);
    }
  }

  private void testInvalidBindStatement(String stmt, Map<String,Object> values) {
    try {
      session.execute(stmt, values);
      fail("Statement \"" + stmt + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.SyntaxError e) {
      LOG.info("Expected exception", e);
    }
  }

  @Test
  public void testSelectBind() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    SetupTable("test_bind", 10 /* num_rows */);

    {
      // Select data from the test table. Bind by position.
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ?;";
      ResultSet rs = session.execute(select_stmt, new Integer(7), "h7", new Integer(107));
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
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ?;";
      ResultSet rs = session.execute(select_stmt,
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
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3;";
      ResultSet rs = session.execute(select_stmt,
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

    LOG.info("End test");
  }

  @Test
  public void testInsertBind() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    SetupTable("test_bind", 0 /* num_rows */);

    {
      // insert data into the test table. Bind by position.
      String insert_stmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (?, ?, ?, ?, ?, ?);";
      session.execute(insert_stmt,
                      new Integer(1), "h2",
                      new Integer(1), "r1",
                      new Integer(1), "v1");
    }

    {
      // insert data into the test table. Bind by name.
      String insert_stmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (?, ?, ?, ?, ?, ?);";
      session.execute(insert_stmt,
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
      String insert_stmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (:b1, :b2, :b3, :b4, :b5, :b6);";
      session.execute(insert_stmt,
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
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(select_stmt);

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
    SetupTable("test_bind", 0 /* num_rows */);

    {
      // update data in the test table. Bind by position.
      String update_stmt = "UPDATE test_bind set v1 = ?, v2 = ?" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      session.execute(update_stmt,
                      new Integer(1), "v1",
                      new Integer(1), "h2",
                      new Integer(1), "r1");
    }

    {
      // update data in the test table. Bind by name.
      String update_stmt = "UPDATE test_bind set v1 = ?, v2 = ?" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      session.execute(update_stmt,
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
      String update_stmt = "UPDATE test_bind set v1 = :b5, v2 = :b6" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3 AND r2 = :b4;";
      session.execute(update_stmt,
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
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(select_stmt);

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
    SetupTable("test_bind", 0 /* num_rows */);

    // Insert 4 rows.
    for (int i = 1; i <= 4; i++) {
      String insert_stmt = String.format("INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                                         "VALUES (%d, 'h%s', %d, 'r%s', %d, 'v%s');",
                                         1, 2, i, i, i, i);
      session.execute(insert_stmt);
    }

    {
      // delete 1 row in the test table. Bind by position.
      String delete_stmt = "DELETE FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      session.execute(delete_stmt,
                      new Integer(1), "h2",
                      new Integer(1), "r1");
    }

    {
      // delete 1 row in the test table. Bind by name.
      String delete_stmt = "DELETE FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      session.execute(delete_stmt,
                      new HashMap<String, Object>() {{
                        put("h1", new Integer(1));
                        put("h2", "h2");
                        put("r1", new Integer(2));
                        put("r2", "r2");
                      }});
    }

    {
      // delete 1 row in the test table. Bind by name with named markers.
      String delete_stmt = "DELETE FROM test_bind" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3 AND r2 = :b4;";
      session.execute(delete_stmt,
                      new HashMap<String, Object>() {{
                        put("b1", new Integer(1));
                        put("b2", "h2");
                        put("b3", new Integer(3));
                        put("b4", "r3");
                      }});
    }

    {
      // Select data from the test table.
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(select_stmt);
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
    SetupTable("test_bind", 10 /* num_rows */);

    {
      // Select data from the test table. Bind by position.
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ?;";
      PreparedStatement stmt = session.prepare(select_stmt);
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
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ?;";
      PreparedStatement stmt = session.prepare(select_stmt);
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
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3;";
      PreparedStatement stmt = session.prepare(select_stmt);
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
    SetupTable("test_bind", 0 /* num_rows */);

    {
      // insert data into the test table. Bind by position.
      String insert_stmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (?, ?, ?, ?, ?, ?);";
      PreparedStatement stmt = session.prepare(insert_stmt);
      session.execute(stmt.bind(new Integer(1), "h2", new Integer(1), "r1", new Integer(1), "v1"));
    }

    {
      // insert data into the test table. Bind by name.
      String insert_stmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (?, ?, ?, ?, ?, ?);";
      PreparedStatement stmt = session.prepare(insert_stmt);
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
      // insert data into the test table. Bind by name with named markers.
      String insert_stmt = "INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (:b1, :b2, :b3, :b4, :b5, :b6);";
      PreparedStatement stmt = session.prepare(insert_stmt);
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
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(select_stmt);

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
  public void testPrepareUpdateBind() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    SetupTable("test_bind", 0 /* num_rows */);

    {
      // update data in the test table. Bind by position.
      String update_stmt = "UPDATE test_bind set v1 = ?, v2 = ?" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      PreparedStatement stmt = session.prepare(update_stmt);
      session.execute(stmt.bind(new Integer(1), "v1", new Integer(1), "h2", new Integer(1), "r1"));
    }

    {
      // update data in the test table. Bind by name.
      String update_stmt = "UPDATE test_bind set v1 = ?, v2 = ?" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      PreparedStatement stmt = session.prepare(update_stmt);
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
      String update_stmt = "UPDATE test_bind set v1 = :b5, v2 = :b6" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3 AND r2 = :b4;";
      PreparedStatement stmt = session.prepare(update_stmt);
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
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(select_stmt);

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
    SetupTable("test_bind", 0 /* num_rows */);

    // Insert 4 rows.
    for (int i = 1; i <= 4; i++) {
      String insert_stmt = String.format("INSERT INTO test_bind (h1, h2, r1, r2, v1, v2) " +
                                         "VALUES (%d, 'h%s', %d, 'r%s', %d, 'v%s');",
                                         1, 2, i, i, i, i);
      session.execute(insert_stmt);
    }

    {
      // delete 1 row in the test table. Bind by position.
      String delete_stmt = "DELETE FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      PreparedStatement stmt = session.prepare(delete_stmt);
      session.execute(stmt.bind(new Integer(1), "h2", new Integer(1), "r1"));
    }

    {
      // delete 1 row in the test table. Bind by name.
      String delete_stmt = "DELETE FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 = ? AND r2 = ?;";
      PreparedStatement stmt = session.prepare(delete_stmt);
      session.execute(stmt.bind()
                      .setInt("h1", 1)
                      .setString("h2", "h2")
                      .setInt("r1", 2)
                      .setString("r2", "r2"));
    }

    {
      // delete 1 row in the test table. Bind by name with named markers.
      String delete_stmt = "DELETE FROM test_bind" +
                           " WHERE h1 = :b1 AND h2 = :b2 AND r1 = :b3 AND r2 = :b4;";
      PreparedStatement stmt = session.prepare(delete_stmt);
      session.execute(stmt
                      .bind()
                      .setInt("b1", 1)
                      .setString("b2", "h2")
                      .setInt("b3", 3)
                      .setString("b4", "r3"));
    }

    {
      // Select data from the test table.
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = 1 AND h2 = 'h2';";
      ResultSet rs = session.execute(select_stmt);
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
    String create_stmt = "CREATE TABLE test_bind " +
                         "(c1 tinyint, c2 smallint, c3 integer, c4 bigint, " +
                         "c5 float, c6 double, " +
                         "c7 varchar, " +
                         "c8 boolean, " +
                         "c9 timestamp, " +
                         "primary key (c1));";
    session.execute(create_stmt);

    // Insert data of all supported datatypes with bind by position.
    String insert_stmt = "INSERT INTO test_bind (c1, c2, c3, c4, c5, c6, c7, c8, c9) " +
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    // For CQL <-> Java datatype mapping, see
    // http://docs.datastax.com/en/drivers/java/3.1/com/datastax/driver/core/TypeCodec.html
    session.execute(insert_stmt,
                    new Byte((byte)1), new Short((short)2), new Integer(3), new Long(4),
                    new Float(5.0), new Double(6.0),
                    "c7",
                    new Boolean(true),
                    new Date(2017, 1, 1));

    {
      // Select data from the test table.
      String select_stmt = "SELECT * FROM test_bind WHERE c1 = 1;";
      ResultSet rs = session.execute(select_stmt);
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
    }

    // Update data of all supported datatypes with bind by position.
    String update_stmt = "UPDATE test_bind SET " +
                         "c2 = ?, " +
                         "c3 = ?, " +
                         "c4 = ?, " +
                         "c5 = ?, " +
                         "c6 = ?, " +
                         "c7 = ?, " +
                         "c8 = ?, " +
                         "c9 = ? " +
                         "WHERE c1 = ?;";
    session.execute(update_stmt,
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
                    }});

    {
      // Select data from the test table.
      String select_stmt = "SELECT * FROM test_bind WHERE c1 = 11;";
      ResultSet rs = session.execute(select_stmt);
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
    }

    LOG.info("End test");
  }

  public void testBindWithVariousOperators() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    SetupTable("test_bind", 10 /* num_rows */);

    {
      // Select bind marker with ">=" and "<=".
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 >= ? AND r1 <= ?;";
      ResultSet rs = session.execute(select_stmt,
                                     new Integer(7), "h7",
                                     new Integer(107), new Integer(107));
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
      // Select bind marker with ">" and "<".
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 > ? AND r1 < ?;";
      ResultSet rs = session.execute(select_stmt,
                                     new Integer(7), "h7",
                                     new Integer(107), new Integer(107));
      Row row = rs.one();
      // Assert no row is returned.
      assertNull(row);
    }

    {
      // Select bind marker with "<>".
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = :1 AND h2 = :2 AND r1 <> :3;";
      ResultSet rs = session.execute(select_stmt, new Integer(7), "h7", new Integer(107));
      Row row = rs.one();
      // Assert no row is returned.
      assertNull(row);
    }

    {
      // Select bind marker with BETWEEN and NOT BETWEEN.
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = :1 AND h2 = :2 AND" +
                           " r1 BETWEEN ? AND ? AND r1 NOT BETWEEN ? AND ?;";
      ResultSet rs = session.execute(select_stmt,
                                     new Integer(7), "h7",
                                     new Integer(106), new Integer(108),
                                     new Integer(1000), new Integer(2000));
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

  public void testBindMisc() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    SetupTable("test_bind", 10 /* num_rows */);

    {
      // Select position bind marker with mixed order.
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = :2 AND h2 = :3 AND r1 = :1;";
      ResultSet rs = session.execute(select_stmt, new Integer(107), new Integer(7), "h7");
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
      // Select named markers with quoted identifier and space between colon and id.
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = :\"Bind1\" AND h2 = :  \"Bind2\" AND r1 = :  \"Bind3\";";
      ResultSet rs = session.execute(select_stmt,
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

    LOG.info("End test");
  }

  @Test
  public void testInvalidBindStatements() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    SetupTable("test_bind", 0 /* num_rows */);

    // Illegal (non-positive) bind position marker.
    testInvalidBindStatement("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                             " WHERE h1 = :0 AND h2 = :1 AND r1 = :2;",
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

    LOG.info("End test");
  }
}
