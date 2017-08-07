// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.util.*;

import org.junit.Test;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.SyntaxError;
import com.datastax.driver.core.exceptions.InvalidQueryException;

import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertEquals;

import org.junit.rules.ExpectedException;
import org.junit.Rule;

public class TestDelete extends BaseCQLTest {
  @Rule
  public ExpectedException exception = ExpectedException.none();  

  @Test
  public void testDeleteOneColumn() throws Exception {
    LOG.info("TEST CQL DELETE - Start");

    // Setup test table.
    setupTable("test_delete", 2);

    // Select data from the test table.
    String delete_stmt = "DELETE v1 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0' AND r1 = 100 AND r2 = 'r100';";
    session.execute(delete_stmt);
		String select_stmt_1 = "SELECT v1 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0';";
    ResultSet rs = session.execute(select_stmt_1);
    
    List<Row> rows = rs.all(); 
    
    assertEquals(1, rows.size());
		Row row = rows.get(0);
	  assertTrue(row.isNull(0));  

		String select_stmt_2 = "SELECT v2 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0';";
    rs = session.execute(select_stmt_2);
    rows = rs.all(); 
    assertEquals(1, rows.size());
    row = rows.get(0);
	  assertEquals("v1000",  row.getString(0));
	}
  
  @Test
  public void testDeleteMultipleColumns() throws Exception {
    LOG.info("TEST CQL DELETE - Start");

    // Setup test table.
    setupTable("test_delete", 2);

    // Select data from the test table.
    String delete_stmt = "DELETE v1, v2 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0' AND r1 = 100 AND r2 = 'r100';";
    session.execute(delete_stmt);
		String select_stmt_1 = "SELECT v1, v2 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0';";
    ResultSet rs = session.execute(select_stmt_1);
    
    List<Row> rows = rs.all(); 
    
    assertEquals(1, rows.size());
		Row row = rows.get(0);
	  assertTrue(row.isNull(0));  
	  assertTrue(row.isNull(1));
	}
  
  @Test
  public void testStarDeleteSyntaxError() throws Exception {
    LOG.info("TEST CQL SyntaxError DELETE * - Start");

    // Setup test table.
    setupTable("test_delete", 2);
    String delete_stmt = "DELETE * FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0' AND r1 = 100 AND r2 = 'r100';";
    exception.expect(SyntaxError.class);
		session.execute(delete_stmt);
	}
  
  @Test
  public void testPrimaryDeleteSyntaxError() throws Exception {
    LOG.info("TEST CQL SyntaxError DELETE primary - Start");

    // Setup test table.
    setupTable("test_delete", 2);
    String delete_stmt = "DELETE h1 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0' AND r1 = 100 AND r2 = 'r100';";
    exception.expect(InvalidQueryException.class);
		session.execute(delete_stmt);
	}
}
  
	
