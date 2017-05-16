// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.util.Arrays;
import java.util.Vector;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.PreparedStatement;

import org.junit.BeforeClass;
import org.junit.Test;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertEquals;


public class TestPrepareExecute extends BaseCQLTest {

  @BeforeClass
  public static void SetUpBeforeClass() throws Exception {
    // Set up prepare statement cache size. Each SELECT statement below takes about 16kB (two 4kB
    // memory page for the parse tree and two for semantic analysis results). A 64kB cache
    // should be small enough to force prepared statements to be freed and reprepared.
    // Note: add "--v=1" below to see the prepared statement cache usage in trace output.
    BaseCQLTest.tserverArgs = Arrays.asList("--cql_service_max_prepared_statement_size_bytes=65536");
    BaseCQLTest.setUpBeforeClass();
  }

  @Test
  public void testBasicPrepareExecute() throws Exception {
    LOG.info("Begin test");

    // Setup table.
    setupTable("test_prepare", 0 /* num_rows */);

    String insert_stmt =
        "insert into test_prepare (h1, h2, r1, r2, v1, v2) values (1, 'a', 2, 'b', 3, 'c');";
    session.execute(insert_stmt);

    // Prepare and execute statement.
    String select_stmt =
        "select * from test_prepare where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';";
    PreparedStatement stmt = session.prepare(select_stmt);
    ResultSet rs = session.execute(stmt.bind());
    Row row = rs.one();

    // Assert that the row is returned.
    assertNotNull(row);
    assertEquals(1, row.getInt("h1"));
    assertEquals("a", row.getString("h2"));
    assertEquals(2, row.getInt("r1"));
    assertEquals("b", row.getString("r2"));
    assertEquals(3, row.getInt("v1"));
    assertEquals("c", row.getString("v2"));

    LOG.info("End test");
  }

  @Test
  public void testMultiplePrepareExecute() throws Exception {
    LOG.info("Begin test");

    // Setup table.
    setupTable("test_prepare", 0 /* num_rows */);

    // Insert 10 rows.
    for (int i = 0; i < 10; i++) {
      String insert_stmt = String.format(
          "insert into test_prepare (h1, h2, r1, r2, v1, v2) values (%d, 'a', 2, 'b', %d, 'c');",
          i, i + 1);
      session.execute(insert_stmt);
    }

    // Prepare 10 statements, each for each of the 10 rows.
    Vector<PreparedStatement> stmts = new Vector<PreparedStatement>();
    for (int i = 0; i < 10; i++) {
      String select_stmt = String.format(
          "select * from test_prepare where h1 = %d and h2 = 'a' and r1 = 2 and r2 = 'b';", i);
      stmts.add(i, session.prepare(select_stmt));
    }

    // Execute the 10 prepared statements round-robin. Loop for 3 times.
    for (int j = 0; j < 3; j++) {
      for (int i = 0; i < 10; i++) {
        ResultSet rs = session.execute(stmts.get(i).bind());
        Row row = rs.one();

        // Assert that the expected row is returned.
        assertNotNull(row);
        assertEquals(i, row.getInt("h1"));
        assertEquals("a", row.getString("h2"));
        assertEquals(2, row.getInt("r1"));
        assertEquals("b", row.getString("r2"));
        assertEquals(i + 1, row.getInt("v1"));
        assertEquals("c", row.getString("v2"));
      }
    }

    LOG.info("End test");
  }
}
