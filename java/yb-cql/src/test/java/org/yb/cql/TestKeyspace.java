// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.util.Iterator;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.Token;

import org.junit.Test;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class TestKeyspace extends TestBase {
  public ResultSet execute(String statement) throws Exception {
    LOG.info("EXEC CQL: " + statement);
    return session.execute(statement);
  }

  public void setupTable(String test_table) throws Exception {
    LOG.info("Create & setup table: " + test_table);
    super.SetupTable(test_table, 2 /* num_rows */);
  }

  public void dropTable(String test_table) throws Exception {
    LOG.info("Drop table: " + test_table);
    super.DropTable(test_table);

    // Supposing the drop operation is asyncronous.
    // That's why let the system finish the deletion to prevent races.
    // TODO: Check ENG-985 and try to remove all sleeps.
    Thread.sleep(500);
  }

  public void checkTable(String test_table) throws Exception {
    // Select data from the test table.
    String selectStmt = "SELECT * FROM " + test_table + " WHERE h1 = 1 AND h2 = 'h1';";
    ResultSet rs = execute(selectStmt);

    int rowCount = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      LOG.info(row.toString());
      // Check default row.
      assertEquals("Row[1, h1, 101, r101, 1001, v1001]", row.toString());
      rowCount++;
    }
    assertEquals(1, rowCount);
  }

  public void createKeyspace(String test_keyspace) throws Exception {
    String createKeyspaceStmt = "CREATE KEYSPACE " + test_keyspace + ";";
    execute(createKeyspaceStmt);

    // Supposing the create operation is asyncronous.
    // That's why let the system finish the operation to prevent races.
    // TODO: Check ENG-985 and try to remove all sleeps.
    Thread.sleep(200);
  }

  public void dropKeyspace(String test_keyspace) throws Exception {
    String deleteKeyspaceStmt = "DROP KEYSPACE " + test_keyspace + ";";
    execute(deleteKeyspaceStmt);

    // Supposing the drop operation is asyncronous.
    // That's why let the system finish the deletion to prevent races.
    // TODO: Check ENG-985 and try to remove all sleeps.
    Thread.sleep(200);
  }

  public void useKeyspace(String test_keyspace) throws Exception {
    String useKeyspaceStmt = "USE " + test_keyspace + ";";
    execute(useKeyspaceStmt);
  }

  @Test
  public void testNoKeyspace() throws Exception {
    LOG.info("--- TEST CQL: NO KEYSPACE - Start");

    String tableName = "test_table";

    // The table's NOT been created yet.
    String invalidStmt = String.format("SELECT * FROM %s WHERE h1 = 1 AND h2 = 'h1';", tableName);
    RunInvalidStmt(invalidStmt);

    setupTable(tableName);
    checkTable(tableName);
    dropTable(tableName);

    // The table's been already deleted.
    RunInvalidStmt(invalidStmt);

    LOG.info("--- TEST CQL: NO KEYSPACE - End");
  }

  @Test
  public void testCustomKeyspace() throws Exception {
    LOG.info("--- TEST CQL: CUSTOM KEYSPACE - Start");

    String keyspaceName = "my_keyspace";
    String tableName = "test_table";
    String longTableName = keyspaceName + "." + tableName;

    createKeyspace(keyspaceName);

    // The table's NOT been created yet.
    String invalidStmt = String.format("SELECT * FROM %s WHERE h1 = 1 AND h2 = 'h1';",
        longTableName);
    RunInvalidStmt(invalidStmt);

    setupTable(longTableName);
    checkTable(longTableName);

    // Short table name cannot be used without 'USE <keyspace>'.
    invalidStmt = String.format("SELECT * FROM %s WHERE h1 = 1 AND h2 = 'h1';", tableName);
    RunInvalidStmt(invalidStmt);
    // Cannot delete non-empty keyspace.
    invalidStmt = String.format("DROP KEYSPACE ", keyspaceName);
    RunInvalidStmt(invalidStmt);

    dropTable(longTableName);

    // The table's been already deleted.
    invalidStmt = String.format("SELECT * FROM %s WHERE h1 = 1 AND h2 = 'h1';", longTableName);
    RunInvalidStmt(invalidStmt);
    invalidStmt = String.format("SELECT * FROM %s WHERE h1 = 1 AND h2 = 'h1';", tableName);
    RunInvalidStmt(invalidStmt);

    dropKeyspace(keyspaceName);

    LOG.info("--- TEST CQL: CUSTOM KEYSPACE - End");
  }

  @Test
  public void testUseKeyspace() throws Exception {
    LOG.info("--- TEST CQL: USE KEYSPACE - Start");

    String keyspaceName = "my_keyspace";
    String tableName = "test_table";
    String longTableName = keyspaceName + "." + tableName;

    createKeyspace(keyspaceName);
    useKeyspace(keyspaceName);

    // The table's NOT been created yet.
    String invalidStmt = String.format("SELECT * FROM %s WHERE h1 = 1 AND h2 = 'h1';", tableName);
    RunInvalidStmt(invalidStmt);
    invalidStmt = String.format("SELECT * FROM %s WHERE h1 = 1 AND h2 = 'h1';", longTableName);
    RunInvalidStmt(invalidStmt);

    setupTable(tableName);
    checkTable(tableName); // Check short table name.
    checkTable(longTableName); // Check long name.
    dropTable(tableName);

    dropKeyspace(keyspaceName);

    LOG.info("--- TEST CQL: USE KEYSPACE - End");
  }

  @Test
  public void testTwoKeyspaces() throws Exception {
    LOG.info("--- TEST CQL: TWO KEYSPACES - Start");

    String keyspaceName1 = "my_keyspace1";
    String keyspaceName2 = "my_keyspace2";
    String tableName = "test_table";
    String longTableName1 = keyspaceName1 + "." + tableName; // Table1.
    String longTableName2 = keyspaceName2 + "." + tableName; // Table2.

    // Using Keyspace1.
    createKeyspace(keyspaceName1);
    useKeyspace(keyspaceName1);

    setupTable(tableName);
    checkTable(tableName); // Check short table name.
    checkTable(longTableName1); // Check long name.

    // Table2 has NOT been created yet.
    String invalidStmt = String.format("SELECT * FROM %s WHERE h1 = 1 AND h2 = 'h1';",
        longTableName2);
    RunInvalidStmt(invalidStmt);

    // Using Keyspace2.
    createKeyspace(keyspaceName2);
    useKeyspace(keyspaceName2);

    setupTable(tableName);
    checkTable(tableName); // Check short table name.
    checkTable(longTableName2); // Check long name.
    checkTable(longTableName1); // Check table from keyspace1.

    // Insert new rows to the tables.
    useKeyspace(keyspaceName1);
    // Table1 (my_keyspace1.test_table).
    String insertStmt = String.format(
            "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
            tableName, 1, 1, 3+100, 3+100, 3+1000, 3+1000);
    execute(insertStmt);

    useKeyspace(keyspaceName2);
    // Table2 (my_keyspace2.test_table).
    insertStmt = String.format(
            "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
            tableName, 1, 1, 5+100, 5+100, 5+1000, 5+1000);
    execute(insertStmt);

    // Table1 (my_keyspace1.test_table).
    insertStmt = String.format(
            "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
            longTableName1, 1, 1, 7+100, 7+100, 7+1000, 7+1000);
    execute(insertStmt);

    // Check the tables.
    // Check Table2 (my_keyspace2.test_table) - using SHORT name.
    String selectStmt = "SELECT * FROM " + tableName + " WHERE h1 = 1 AND h2 = 'h1';";
    ResultSet rs = execute(selectStmt);

    int rowCount = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      LOG.info(row.toString());

      switch (rowCount++) {
        case 0:
          assertEquals("Row[1, h1, 101, r101, 1001, v1001]", row.toString());
          break;
        case 1:
          assertEquals("Row[1, h1, 105, r105, 1005, v1005]", row.toString());
          break;
      };
    }
    assertEquals(2, rowCount);

    // Check Table2 (my_keyspace2.test_table) - using LONG name.
    selectStmt = "SELECT * FROM " + longTableName2 + " WHERE h1 = 1 AND h2 = 'h1';";
    rs = execute(selectStmt);

    rowCount = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      LOG.info(row.toString());

      switch (rowCount++) {
        case 0:
          assertEquals("Row[1, h1, 101, r101, 1001, v1001]", row.toString());
          break;
        case 1:
          assertEquals("Row[1, h1, 105, r105, 1005, v1005]", row.toString());
          break;
      };
    }
    assertEquals(2, rowCount);

    // Check Table1 (my_keyspace1.test_table) - using LONG name.
    selectStmt = "SELECT * FROM " + longTableName1 + " WHERE h1 = 1 AND h2 = 'h1';";
    rs = execute(selectStmt);

    rowCount = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      LOG.info(row.toString());

      switch (rowCount++) {
        case 0:
          assertEquals("Row[1, h1, 101, r101, 1001, v1001]", row.toString());
          break;
        case 1:
          assertEquals("Row[1, h1, 103, r103, 1003, v1003]", row.toString());
          break;
        case 2:
          assertEquals("Row[1, h1, 107, r107, 1007, v1007]", row.toString());
          break;
      };
    }
    assertEquals(3, rowCount);

    dropTable(tableName);
    dropKeyspace(keyspaceName2);
    dropTable(longTableName1);
    dropKeyspace(keyspaceName1);

    LOG.info("--- TEST CQL: TWO KEYSPACES - End");
  }

  @Test
  public void testUpdateAndDelete() throws Exception {
    LOG.info("--- TEST CQL: UPDATE & DELETE - Start");

    String keyspaceName1 = "my_keyspace1";
    String keyspaceName2 = "my_keyspace2";
    String tableName = "test_table";
    String longTableName1 = keyspaceName1 + "." + tableName; // Table1.
    String longTableName2 = keyspaceName2 + "." + tableName; // Table2.

    // Table1 (my_keyspace1.test_table).
    createKeyspace(keyspaceName1);
    useKeyspace(keyspaceName1);

    setupTable(tableName);
    checkTable(tableName); // Check short table name.
    checkTable(longTableName1); // Check long name.

    // Table2 (my_keyspace2.test_table).
    createKeyspace(keyspaceName2);
    setupTable(longTableName2);
    checkTable(longTableName2);

    // Update the tables.
    String updateStmt = String.format("UPDATE %s SET v1 = %d, v2 = '%s' WHERE " +
        "h1 = 1 AND h2 = 'h1' AND r1 = 101 AND r2 = 'r101';", tableName, 3000, "v3000");
    execute(updateStmt);

    updateStmt = String.format("UPDATE %s SET v1 = %d, v2 = '%s' WHERE " +
        "h1 = 1 AND h2 = 'h1' AND r1 = 101 AND r2 = 'r101';", longTableName2, 5000, "v5000");
    execute(updateStmt);

    // Check the tables.
    // Check Table1 (my_keyspace1.test_table) - using SHORT name.
    String selectStmt = "SELECT * FROM " + tableName + " WHERE h1 = 1 AND h2 = 'h1';";
    ResultSet rs = execute(selectStmt);
    Row row = rs.one();
    assertNotNull(row);
    LOG.info(row.toString());
    assertEquals("Row[1, h1, 101, r101, 3000, v3000]", row.toString());
    assertNull(rs.one());

    // Check Table2 (my_keyspace2.test_table) - using LONG name.
    selectStmt = "SELECT * FROM " + longTableName2 + " WHERE h1 = 1 AND h2 = 'h1';";
    rs = execute(selectStmt);
    row = rs.one();
    assertNotNull(row);
    LOG.info(row.toString());
    assertEquals("Row[1, h1, 101, r101, 5000, v5000]", row.toString());
    assertNull(rs.one());

    // Delete rows from the tables.
    String deleteStmt = "DELETE FROM " + tableName +
        " WHERE h1 = 1 AND h2 = 'h1' AND r1 = 101 AND r2 = 'r101';";
    execute(deleteStmt);
    selectStmt = "SELECT * FROM " + tableName + " WHERE h1 = 1 AND h2 = 'h1';";
    rs = execute(selectStmt);
    assertFalse(rs.iterator().hasNext());

    deleteStmt = "DELETE FROM " + longTableName2 +
        " WHERE h1 = 1 AND h2 = 'h1' AND r1 = 101 AND r2 = 'r101';";
    execute(deleteStmt);
    selectStmt = "SELECT * FROM " + longTableName2 + " WHERE h1 = 1 AND h2 = 'h1';";
    rs = execute(selectStmt);
    assertFalse(rs.iterator().hasNext());

    dropTable(tableName);
    dropKeyspace(keyspaceName1);
    dropTable(longTableName2);
    dropKeyspace(keyspaceName2);

    LOG.info("--- TEST CQL: UPDATE & DELETE - End");
  }
}
