// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.nio.ByteBuffer;
import java.util.UUID;
import java.net.InetAddress;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.SyntaxError;
import com.datastax.driver.core.exceptions.InvalidQueryException;

import org.junit.Test;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

public class TestBindVariable extends TestBase {

  private void testBindSyntaxError(String stmt, Object... values) {
    try {
      session.execute(stmt, values);
      fail("Statement \"" + stmt + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.SyntaxError e) {
      LOG.info("Expected exception", e);
    }
  }

  private void testInvalidBindStatement(String stmt, Object... values) {
    try {
      session.execute(stmt, values);
      fail("Statement \"" + stmt + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
      LOG.info("Expected exception", e);
    }
  }

  private void testInvalidBindStatement(String stmt, Map<String,Object> values) {
    try {
      session.execute(stmt, values);
      fail("Statement \"" + stmt + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
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
                         "c10 inet, " +
                         "c11 uuid, " +
                         "c12 timeuuid, " +
                         "c13 blob," +
                         "primary key (c1));";
    session.execute(create_stmt);

    // Insert data of all supported datatypes with bind by position.
    String insert_stmt = "INSERT INTO test_bind " +
                         "(c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13) " +
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    // For CQL <-> Java datatype mapping, see
    // http://docs.datastax.com/en/drivers/java/3.1/com/datastax/driver/core/TypeCodec.html
    LOG.info("EXECUTING");
    session.execute(insert_stmt,
                    new Byte((byte)1), new Short((short)2), new Integer(3), new Long(4),
                    new Float(5.0), new Double(6.0),
                    "c7",
                    new Boolean(true),
                    new Date(2017, 1, 1),
                    InetAddress.getByName("1.2.3.4"),
                    UUID.fromString("11111111-2222-3333-4444-555555555555"),
                    UUID.fromString("f58ba3dc-3422-11e7-a919-92ebcb67fe33"),
                    makeByteBuffer(133143986176L)); // `0000001f00000000` to check zero-bytes
    LOG.info("EXECUTED");

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
      assertEquals(InetAddress.getByName("1.2.3.4"), row.getInet("c10"));
      assertEquals(UUID.fromString("11111111-2222-3333-4444-555555555555"), row.getUUID("c11"));
      assertEquals(UUID.fromString("f58ba3dc-3422-11e7-a919-92ebcb67fe33"), row.getUUID("c12"));
      assertEquals(makeBlobString(makeByteBuffer(133143986176L)),
                   makeBlobString(row.getBytes("c13")));
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
                         "c9 = ?, " +
                         "c10 = ?, " +
                         "c11 = ?, " +
                         "c12 = ?, " +
                         "c13 = ? " +
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
                      put("c10", InetAddress.getByName("1.2.3.4"));
                      put("c11", UUID.fromString("22222222-2222-3333-4444-555555555555"));
                      put("c12", UUID.fromString("f58ba3dc-3422-11e7-a919-92ebcb67fe33"));
                      put("c13", makeByteBuffer(9223372036854775807L)); // max long
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
      assertEquals(InetAddress.getByName("1.2.3.4"), row.getInet("c10"));
      assertEquals(UUID.fromString("22222222-2222-3333-4444-555555555555"), row.getUUID("c11"));
      assertEquals(UUID.fromString("f58ba3dc-3422-11e7-a919-92ebcb67fe33"), row.getUUID("c12"));
      assertEquals(makeBlobString(makeByteBuffer(9223372036854775807L)),
                   makeBlobString(row.getBytes("c13")));
    }

    LOG.info("End test");
  }

  @Test
  public void testBindWithVariousOperators() throws Exception {
    LOG.info("Begin test");

    // Setup test table.
    SetupTable("test_bind", 10 /* num_rows */);

    // ">=" and "<=" not supported in YQL yet.
    //
    // {
    //   // Select bind marker with ">=" and "<=".
    //   String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
    //                        " WHERE h1 = ? AND h2 = ? AND r1 >= ? AND r1 <= ?;";
    //   ResultSet rs = session.execute(select_stmt,
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
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1 = ? AND h2 = ? AND r1 > ? AND r1 < ?;";
      ResultSet rs = session.execute(select_stmt,
                                     new Integer(7), "h7",
                                     new Integer(107), new Integer(107));
      Row row = rs.one();
      // Assert no row is returned.
      assertNull(row);
    }

    // "<>" not supported in YQL yet.
    //
    // {
    //   // Select bind marker with "<>".
    //   String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
    //                        " WHERE h1 = :1 AND h2 = :2 AND r1 <> :3;";
    //   ResultSet rs = session.execute(select_stmt, new Integer(7), "h7", new Integer(107));
    //   Row row = rs.one();
    //   // Assert no row is returned.
    //   assertNull(row);
    // }

    // BETWEEN and NOT BETWEEN not supported in YQL yet.
    //
    // {
    //   // Select bind marker with BETWEEN and NOT BETWEEN.
    //   String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
    //                        " WHERE h1 = :1 AND h2 = :2 AND" +
    //                        " r1 BETWEEN ? AND ? AND r1 NOT BETWEEN ? AND ?;";
    //   ResultSet rs = session.execute(select_stmt,
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
    SetupTable("test_bind", 10 /* num_rows */);

    {
      // Position bind marker with mixed order.
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
      // Named markers with quoted identifier and space between colon and id.
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

    {
      // Named bind marker with unreserved keywords ("key", "type" and "partition").
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=:key AND h2=:type AND r1=:partition;";
      ResultSet rs = session.execute(select_stmt,
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
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=? AND h2=? AND r1=?;";
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
      // Number bind marker no space between "col=:number".
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=:1 AND h2=:2 AND r1=:3;";
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
      // Named bind marker no space between "col=:id".
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=:b1 AND h2=:b2 AND r1=:b3;";
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

    // ">=" and "<=" not supported in YQL yet.
    //
    // {
    //   // Bind marker with ">=" and "<=" and no space in between column, operator and bind marker.
    //   String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
    //                        " WHERE h1=? AND h2=? AND r1>=? AND r1<=?;";
    //   ResultSet rs = session.execute(select_stmt,
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
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=? AND h2=? AND r1>? AND r1<?;";
      ResultSet rs = session.execute(select_stmt,
                                     new Integer(7), "h7",
                                     new Integer(107), new Integer(107));
      Row row = rs.one();
      // Assert no row is returned.
      assertNull(row);
    }

    // "<>" not supported in YQL yet.
    //
    // {
    //   // Bind marker with "<>" and no space in between column, operator and bind marker.
    //   String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
    //                        " WHERE h1=:1 AND h2=:2 AND r1<>:3;";
    //   ResultSet rs = session.execute(select_stmt, new Integer(7), "h7", new Integer(107));
    //   Row row = rs.one();
    //   // Assert no row is returned.
    //   assertNull(row);
    // }

    {
      // Named, case-insensitive bind markers.
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
                           " WHERE h1=:Bind1 AND h2=:Bind2 AND r1=:Bind3;";
      ResultSet rs = session.execute(select_stmt,
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
    SetupTable("test_bind", 0 /* num_rows */);

    // Illegal (non-positive) bind position marker.
    testBindSyntaxError("SELECT h1, h2, r1, r2, v1, v2 FROM test_bind" +
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

  @Test
  public void testNull() throws Exception {
    LOG.info("Begin test");

    // Create table
    String create_stmt = "CREATE TABLE test_bind " +
                         "(k int primary key, " +
                         "c1 tinyint, c2 smallint, c3 integer, c4 bigint, " +
                         "c5 float, c6 double, " +
                         "c7 varchar, " +
                         "c8 boolean, " +
                         "c9 timestamp, " +
                         "c10 inet, " +
                         "c11 uuid, " +
                         "c12 blob);";
    session.execute(create_stmt);

    // Insert data of all supported datatypes with bind by position.
    String insert_stmt = "INSERT INTO test_bind " +
                         "(k, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12) " +
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    // For CQL <-> Java datatype mapping, see
    // http://docs.datastax.com/en/drivers/java/3.1/com/datastax/driver/core/TypeCodec.html
    LOG.info("EXECUTING");
    session.execute(insert_stmt,
                    new Integer(1),
                    null, null, null, null,
                    null, null,
                    null,
                    null,
                    null,
                    null,
                    null,
                    null);
    LOG.info("EXECUTED");

    {
      // Select data from the test table.
      String select_stmt = "SELECT * FROM test_bind WHERE k = 1;";
      ResultSet rs = session.execute(select_stmt);
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
    }

    LOG.info("End test");
  }

  @Test
  public void testEmptyValues() throws Exception {
    LOG.info("Begin test");

    // Create table
    String create_stmt = "CREATE TABLE test_bind (k int primary key, c1 varchar, c2 blob);";
    session.execute(create_stmt);

    // Insert data of all supported datatypes with bind by position.
    String insert_stmt = "INSERT INTO test_bind (k, c1, c2) VALUES (?, ?, ?);";
    // For CQL <-> Java datatype mapping, see
    // http://docs.datastax.com/en/drivers/java/3.1/com/datastax/driver/core/TypeCodec.html
    LOG.info("EXECUTING");
    session.execute(insert_stmt,
                    new Integer(1),
                    "", ByteBuffer.allocate(0));
    LOG.info("EXECUTED");

    {
      // Select data from the test table.
      String select_stmt = "SELECT * FROM test_bind WHERE k = 1;";
      ResultSet rs = session.execute(select_stmt);
      Row row = rs.one();
      // Assert 1 row is returned with expected column values.
      assertNotNull(row);
      assertEquals("", row.getString("c1"));
      assertEquals(0, row.getBytes("c2").array().length);
    }

    LOG.info("End test");
  }

}
