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

import java.util.Iterator;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import org.junit.Test;
import org.yb.client.TestUtils;

import static org.yb.AssertionWrappers.*;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.util.RandomUtil;
import org.yb.util.BuildTypeUtil;

@RunWith(value=YBTestRunner.class)
public class TestKeyspace extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestKeyspace.class);

  public void setupTable(String test_table) throws Exception {
    LOG.info("Create & setup table: " + test_table);
    super.setupTable(test_table, 2 /* num_rows */);
    LOG.info("Create & setup table: " + test_table + " -- finished");
  }

  public void dropTable(String test_table) throws Exception {
    LOG.info("Drop table: " + test_table);
    super.dropTable(test_table);
    LOG.info("Drop table finished: " + test_table);
  }

  public void checkTableRows(String test_table, int[] ids) throws Exception {
    // Select data from the test table.
    String selectStmt = "SELECT * FROM " + test_table + " WHERE h1 = 1 AND h2 = 'h1';";
    ResultSet rs = execute(selectStmt);

    int rowCount = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      LOG.info(row.toString());
      // Check default row.
      int id = ids[rowCount];
      String str = String.format("Row[1, h1, %d, r%d, %d, v%d]",
          id + 100, id + 100, id + 1000, id + 1000);
      assertEquals(str, row.toString());
      rowCount++;
    }
    assertEquals(ids.length, rowCount);
  }

  public void checkTable(String testTableName) throws Exception {
    LOG.info("Performing some writes and reads against the table " + testTableName);
    checkTableRows(testTableName, new int[]{1});
    LOG.info("checkTable finished for " + testTableName);
  }

  public void assertNoTable(String testTableName) throws Exception {
    LOG.info("Checking that the table " + testTableName + " does not exist after deletion");
    String invalidStmt = "SELECT * FROM " + testTableName + " WHERE h1 = 1 AND h2 = 'h1';";
    runInvalidStmt(invalidStmt);
  }

  public void insertRow(String test_table, int id) throws Exception {
    String insertStmt = String.format(
            "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
            test_table, 1, 1, id + 100, id + 100, id + 1000, id + 1000);
    execute(insertStmt);
  }

  public void useKeyspace(String keyspaceName) throws Exception {
    String useKeyspaceStmt = "USE " + keyspaceName + ";";
    execute(useKeyspaceStmt);
  }

  public int getTestMethodTimeoutSec() {
    // No need to adjust for TSAN vs. non-TSAN here, it will be done automatically.
    return 350;
  }

  // We use a random keyspace name in each test to be sure that the keyspace does not exist in the
  // beginning of the test.
  private static String getRandomKeyspaceName() {
    return "test_keyspace_" + RandomUtil.randomNonNegNumber();
  }

  @Test
  public void testCreateAndDropTableTimeout() throws Exception {
    LOG.info("--- TEST CQL: CREATE & DROP TABLE TIMEOUTS - Start");
    final String keyspaceName = getRandomKeyspaceName();
    final String tableName = "test_table";

    createKeyspace(keyspaceName);
    try {
      useKeyspace(keyspaceName);

      for (int i = 0; i < 4; ++i) {
        final int numRows = BuildTypeUtil.isTSAN() ? 1000 : 7500;
        LOG.info("Create a big table '" + tableName + "' with " + numRows +
            " rows (isTSAN=" + BuildTypeUtil.isTSAN() + ", build type=" + TestUtils.getBuildType() +
            "). Iteration: " + i);

        try {
          super.setupTable(tableName, numRows);
        } finally {
          dropTable(tableName);
        }
        assertNoTable(tableName);
      }
    } finally {
      dropKeyspace(keyspaceName);
    }
    LOG.info("--- TEST CQL: CREATE & DROP TABLE TIMEOUTS - End");
  }

  @Test
  public void testCreateAndUseAndDropKeyspaceTimeout() throws Exception {
    LOG.info("--- TEST CQL: CREATE & USE & DROP KEYSPACE TIMEOUTS - Start");
    final String keyspaceName = getRandomKeyspaceName();

    for (int i = 0; i < 10; ++i) {
      LOG.info("i={}, creating & using keyspace {}", i, keyspaceName);
      createKeyspace(keyspaceName);
      useKeyspace(keyspaceName);

      dropKeyspace(keyspaceName);
      LOG.info("i={}, dropped keyspace {}", i, keyspaceName);
    }

    LOG.info("--- TEST CQL: CREATE & USE & DROP KEYSPACE TIMEOUTS - End");
  }

  @Test
  public void testCreateAndDropKeyspaceTimeout() throws Exception {
    LOG.info("--- TEST CQL: CREATE & DROP KEYSPACE TIMEOUTS - Start");
    final String keyspaceName = getRandomKeyspaceName();

    for (int i = 0; i < 10; ++i) {
      LOG.info("i={}, creating keyspace {}", i, keyspaceName);
      createKeyspace(keyspaceName);

      dropKeyspace(keyspaceName);
      LOG.info("i={}, dropped keyspace {}", i, keyspaceName);
    }

    LOG.info("--- TEST CQL: CREATE & DROP KEYSPACE TIMEOUTS - End");
  }

  @Test
  public void testNoKeyspace() throws Exception {
    LOG.info("--- TEST CQL: NO KEYSPACE - Start");

    final String keyspaceName = getRandomKeyspaceName();
    final String tableName = "test_table";

    LOG.info("The table's NOT been created yet.");
    assertNoTable(tableName);

    setupTable(tableName);
    checkTable(tableName);
    dropTable(tableName);

    LOG.info("The table's been already deleted.");
    assertNoTable(tableName);

    LOG.info("Create and delete a keyspace.");
    createKeyspace(keyspaceName);
    useKeyspace(keyspaceName);
    dropKeyspace(keyspaceName);

    useKeyspace("cql_test_keyspace");

    LOG.info("Test the table with a short name again.");
    setupTable(tableName);
    checkTable(tableName);
    dropTable(tableName);

    // The table's been already deleted.
    assertNoTable(tableName);

    LOG.info("--- TEST CQL: NO KEYSPACE - End");
  }

  @Test
  public void testCustomKeyspace() throws Exception {
    LOG.info("--- TEST CQL: CUSTOM KEYSPACE - Start");

    final String keyspaceName = getRandomKeyspaceName();
    String tableName = "test_table";
    String longTableName = keyspaceName + "." + tableName;

    createKeyspace(keyspaceName);

    // The table's NOT been created yet.
    assertNoTable(longTableName);

    setupTable(longTableName);
    checkTable(longTableName);

    // Short table name cannot be used without 'USE <keyspace>'.
    assertNoTable(tableName);
    // Cannot delete non-empty keyspace.
    String invalidStmt = String.format("DROP KEYSPACE ", keyspaceName);
    runInvalidStmt(invalidStmt);

    dropTable(longTableName);

    // The table's been already deleted.
    assertNoTable(longTableName);
    assertNoTable(tableName);

    dropKeyspace(keyspaceName);

    LOG.info("--- TEST CQL: CUSTOM KEYSPACE - End");
  }

  @Test
  public void testUseKeyspace() throws Exception {
    LOG.info("--- TEST CQL: USE KEYSPACE - Start");

    final String keyspaceName = getRandomKeyspaceName();
    String tableName = "test_table";
    String longTableName = keyspaceName + "." + tableName;

    createKeyspace(keyspaceName);
    useKeyspace(keyspaceName);

    // The table's NOT been created yet.
    assertNoTable(tableName);
    assertNoTable(longTableName);

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

    final String keyspaceName1 = getRandomKeyspaceName() + "_1";
    final String keyspaceName2 = getRandomKeyspaceName() + "_2";
    final String tableName = "test_table";
    final String longTableName1 = keyspaceName1 + "." + tableName; // Table1.
    final String longTableName2 = keyspaceName2 + "." + tableName; // Table2.

    // Using Keyspace1.
    createKeyspace(keyspaceName1);
    useKeyspace(keyspaceName1);

    setupTable(tableName);
    checkTable(tableName); // Check short table name.
    checkTable(longTableName1); // Check long name.

    // Table2 has NOT been created yet.
    assertNoTable(longTableName2);

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
    insertRow(tableName, 3 /* id */);

    useKeyspace(keyspaceName2);
    // Table2 (my_keyspace2.test_table).
    insertRow(tableName, 5 /* id */);

    // Table1 (my_keyspace1.test_table).
    insertRow(longTableName1, 7 /* id */);

    // Check the tables.
    // Check Table2 (my_keyspace2.test_table) - using SHORT name.
    checkTableRows(tableName, new int[]{1, 5});

    // Check Table2 (my_keyspace2.test_table) - using LONG name.
    checkTableRows(longTableName2, new int[]{1, 5});

    // Check Table1 (my_keyspace1.test_table) - using LONG name.
    checkTableRows(longTableName1, new int[]{1, 3, 7});

    dropTable(tableName);
    dropKeyspace(keyspaceName2);
    dropTable(longTableName1);
    dropKeyspace(keyspaceName1);

    LOG.info("--- TEST CQL: TWO KEYSPACES - End");
  }

  @Test
  public void testDriverBugWithTwoKeyspaces() throws Exception {
    LOG.info("--- TEST CQL: DRIVER BUG WITH TWO KEYSPACES - Start");

    final String keyspaceName1 = getRandomKeyspaceName() + "_1";
    final String keyspaceName2 = getRandomKeyspaceName() + "_1";
    final String tableName = "test_table";
    final String longTableName2 = keyspaceName2 + "." + tableName;

    // Create keyspaces.
    createKeyspace(keyspaceName1);
    createKeyspace(keyspaceName2);

    // Create a table in the second keyspace.
    setupTable(longTableName2); // my_keyspace2.test_table

    // Start call 'USE keyspace' to break the driver.
    useKeyspace(keyspaceName2);
    useKeyspace(keyspaceName1);
    useKeyspace(keyspaceName2);

    // Table2 (my_keyspace2.test_table).
    insertRow(tableName, 5 /* id */);

    // Try to delete the table. The command can fail on the broken driver.
    // With the broken driver it will try to delete my_keyspace1.test_table
    // instead of my_keyspace2.test_table.
    dropTable(tableName);

    dropKeyspace(keyspaceName2);
    dropKeyspace(keyspaceName1);

    LOG.info("--- TEST CQL: DRIVER BUG WITH TWO KEYSPACES - End");
  }

  @Test
  public void testUpdateAndDelete() throws Exception {
    LOG.info("--- TEST CQL: UPDATE & DELETE - Start");

    final String keyspaceName1 = getRandomKeyspaceName() + "_1";
    final String keyspaceName2 = getRandomKeyspaceName() + "_2";
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

  @Test
  public void testQuotedNames() throws Exception {
    LOG.info("--- TEST CQL: QUOTED NAMES - Start");

    // Table1 name: "a" . "b.c"
    // Table2 name: "a.b" . "c"
    final long randomId = RandomUtil.randomNonNegNumber();
    final String keyspaceName1 = "a" + randomId;
    final String keyspaceName2 = "a" + randomId + ".b";
    final String longTableName1 = "\"" + keyspaceName1 + "\"." + "\"b.c\""; // Table1.
    final String longTableName2 = "\"" + keyspaceName2 + "\"." + "\"c\""; // Table2.

    // Table1 has NOT been created yet.
    assertNoTable(longTableName1);

    // Table2 has NOT been created yet.
    assertNoTable(longTableName2);

    // Create keyspaces.
    createKeyspace(keyspaceName1);
    createKeyspace(keyspaceName2);

    setupTable(longTableName1);
    checkTable(longTableName1);

    // Table2 has NOT been created yet.
    assertNoTable(longTableName2);

    setupTable(longTableName2);
    checkTable(longTableName2); // Check long name.
    checkTable(longTableName1); // Check table from keyspace1.

    // Insert new rows to the tables.
    insertRow(longTableName1, 3 /* id */);
    insertRow(longTableName2, 5 /* id */);

    // Check the tables.
    checkTableRows(longTableName1, new int[]{1, 3});
    checkTableRows(longTableName2, new int[]{1, 5});

    dropTable(longTableName2);
    dropKeyspace(keyspaceName2);
    dropTable(longTableName1);
    dropKeyspace(keyspaceName1);

    LOG.info("--- TEST CQL: QUOTED NAMES - End");
  }

  @Test
  public void testUnreservedKeywordName() throws Exception {
    // REFERENCE used to be KEYWORD, so we have it here, but this test is for keyword STATIC.
    String unreserved_keywords[] = { "static", "references" };

    // Use the keywords as names.
    for (String kw : unreserved_keywords) {
      createKeyspace(kw);
      useKeyspace(kw);

      // Table static.static(h, r, static STATIC, PRIMARY KEY(h, r)).
      session.execute(String.format("CREATE TABLE %s" +
                                    "  (h int, r float, %s text STATIC, PRIMARY KEY(h, r));",
                                    kw, kw));
      session.execute(String.format("DROP TABLE %s", kw));

      // Table static.static(static, r, s STATIC, PRIMARY KEY(static, r)).
      session.execute(String.format("CREATE TABLE %s" +
                                    "  (%s int, r float, s text STATIC, PRIMARY KEY(%s, r));",
                                    kw, kw, kw));
      session.execute(String.format("DROP TABLE %s", kw));

      // Table static.static(h, static, s STATIC, PRIMARY KEY(h, static)).
      session.execute(String.format("CREATE TABLE %s" +
                                    "  (h int, %s float, s text STATIC, PRIMARY KEY(h, %s));",
                                    kw, kw, kw));
      session.execute(String.format("DROP TABLE %s", kw));

      dropKeyspace(kw);
    }
  }
}
