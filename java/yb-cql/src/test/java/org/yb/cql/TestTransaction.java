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

import java.util.*;

import org.junit.Test;

import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

import com.datastax.driver.core.exceptions.InvalidQueryException;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.ResultSetFuture;
import com.datastax.driver.core.Row;

import org.yb.YBTestRunner;
import org.yb.util.BuildTypeUtil;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestTransaction extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestTransaction.class);

  @Override
  public int getTestMethodTimeoutSec() {
    // Extend timeout for testBasicReadWrite stress test.
    // No need to adjust for TSAN vs. non-TSAN here, it will be done automatically.
    return 300;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("allow_index_table_read_write", "true");
    return flagMap;
  }

  private void createTable(String name, String columns, boolean transactional) {
    session.execute(String.format("create table %s (%s) with transactions = { 'enabled' : %b };",
                                  name, columns, transactional));
  }

  private void createTables() throws Exception {
    createTable("test_txn1", "k int primary key, c1 int, c2 text", true);
    createTable("test_txn2", "k int primary key, c1 int, c2 text", true);
    createTable("test_txn3", "k int primary key, c1 int, c2 text", true);
  }

  @Test
  public void testInsertMultipleTables() throws Exception {
    createTables();

    // Insert into multiple tables and ensure all rows are written with same writetime.
    session.execute("begin transaction" +
                    "  insert into test_txn1 (k, c1, c2) values (?, ?, ?);" +
                    "  insert into test_txn2 (k, c1, c2) values (?, ?, ?);" +
                    "  insert into test_txn3 (k, c1, c2) values (?, ?, ?);" +
                    "end transaction;",
                    Integer.valueOf(1), Integer.valueOf(1), "v1",
                    Integer.valueOf(2), Integer.valueOf(2), "v2",
                    Integer.valueOf(3), Integer.valueOf(3), "v3");

    Vector<Row> rows = new Vector<Row>();
    for (int i = 1; i <= 3; i++) {
      rows.add(session.execute(String.format("select c1, c2, writetime(c1), writetime(c2) " +
                                             "from test_txn%d where k = ?;", i),
                               Integer.valueOf(i)).one());
      assertNotNull(rows.get(i - 1));
      assertEquals(i, rows.get(i - 1).getInt("c1"));
      assertEquals("v" + i, rows.get(i - 1).getString("c2"));
    }

    // Verify writetimes are same.
    assertEquals(rows.get(0).getLong("writetime(c1)"), rows.get(1).getLong("writetime(c1)"));
    assertEquals(rows.get(0).getLong("writetime(c1)"), rows.get(2).getLong("writetime(c1)"));
    assertEquals(rows.get(0).getLong("writetime(c2)"), rows.get(1).getLong("writetime(c2)"));
    assertEquals(rows.get(0).getLong("writetime(c2)"), rows.get(2).getLong("writetime(c2)"));
  }

  @Test
  public void testInsertUpdateSameTable() throws Exception {
    createTables();

    // Insert multiple keys into same table and ensure all rows are written with same writetime.
    session.execute("start transaction;" +
                    "insert into test_txn1 (k, c1, c2) values (?, ?, ?);" +
                    "insert into test_txn1 (k, c1, c2) values (?, ?, ?);" +
                    "update test_txn1 set c1 = ?, c2 = ? where k = ?;" +
                    "commit;",
                    Integer.valueOf(1), Integer.valueOf(1), "v1",
                    Integer.valueOf(2), Integer.valueOf(2), "v2",
                    Integer.valueOf(3), "v3", Integer.valueOf(3));

    Vector<Row> rows = new Vector<Row>();
    HashSet<String> values = new HashSet<String>();
    for (Row row : session.execute("select c1, c2, writetime(c1), writetime(c2) " +
                                   "from test_txn1 where k in ?;",
                                   Arrays.asList(Integer.valueOf(1),
                                                 Integer.valueOf(2),
                                                 Integer.valueOf(3)))) {
      rows.add(row);
      values.add(row.getInt("c1") + "," + row.getString("c2"));
    }
    assertEquals(3, rows.size());
    assertEquals(new HashSet<String>(Arrays.asList("1,v1", "2,v2", "3,v3")), values);

    // Verify writetimes are same.
    assertEquals(rows.get(0).getLong("writetime(c1)"), rows.get(1).getLong("writetime(c1)"));
    assertEquals(rows.get(0).getLong("writetime(c1)"), rows.get(2).getLong("writetime(c1)"));
    assertEquals(rows.get(0).getLong("writetime(c2)"), rows.get(1).getLong("writetime(c2)"));
    assertEquals(rows.get(0).getLong("writetime(c2)"), rows.get(2).getLong("writetime(c2)"));
  }

  @Test
  public void testMixDML() throws Exception {
    createTables();

    // Test non-transactional writes to transaction-enabled table.
    for (int i = 1; i <= 2; i++) {
      session.execute("insert into test_txn1 (k, c1, c2) values (?, ?, ?);",
                      Integer.valueOf(i), Integer.valueOf(i), "v" + i);
    }
    assertQueryRowsUnordered("select * from test_txn1;", "Row[1, 1, v1]", "Row[2, 2, v2]");

    // Test a mix of insert/update/delete in the same transaction.
    session.execute("begin transaction" +
                    "  insert into test_txn1 (k, c1, c2) values (?, ?, ?);" +
                    "  update test_txn1 set c1 = 0, c2 = 'v0' where k = ?;" +
                    "  delete from test_txn1 where k = ?;" +
                    "end transaction;",
                    Integer.valueOf(3), Integer.valueOf(3), "v3",
                    Integer.valueOf(2),
                    Integer.valueOf(1));

    // Verify the rows.
    Vector<Row> rows = new Vector<Row>();
    HashSet<String> values = new HashSet<String>();
    for (Row row : session.execute("select k, c1, c2, writetime(c1), writetime(c2) " +
                                   "from test_txn1 where k in ?;",
                                   Arrays.asList(Integer.valueOf(1),
                                                 Integer.valueOf(2),
                                                 Integer.valueOf(3)))) {
      rows.add(row);
      values.add(row.getInt("k") + "," + row.getInt("c1") + "," + row.getString("c2"));
    }
    assertEquals(2, rows.size());
    assertEquals(new HashSet<String>(Arrays.asList("2,0,v0", "3,3,v3")), values);

    // Verify writetimes are same.
    assertEquals(rows.get(0).getLong("writetime(c1)"), rows.get(1).getLong("writetime(c1)"));
    assertEquals(rows.get(0).getLong("writetime(c2)"), rows.get(1).getLong("writetime(c2)"));

    // Test writes to the same row.
    session.execute("truncate test_txn1;");
    session.execute("begin transaction" +
                    "  insert into test_txn1 (k, c1, c2) values (1, 1, 'v1');" +
                    "  insert into test_txn1 (k, c1, c2) values (2, 2, 'v2');" +
                    "  insert into test_txn1 (k, c1, c2) values (2, 22, 'v2');" +
                    "  insert into test_txn1 (k, c1, c2) values (3, 3, 'v3');" +
                    "  delete from test_txn1 where k = 1;" +
                    "  update test_txn1 set c2 = 'v22' where k = 2;" +
                    "end transaction;");

    // Verify the rows.
    rows = new Vector<Row>();
    values = new HashSet<String>();
    for (Row row : session.execute("select k, c1, c2, writetime(c1), writetime(c2) " +
                                   "from test_txn1;")) {
      rows.add(row);
      values.add(row.getInt("k") + "," + row.getInt("c1") + "," + row.getString("c2"));
    }
    assertEquals(new HashSet<String>(Arrays.asList("2,22,v22",
                                                   "3,3,v3")), values);

    // Verify writetimes are same.
    assertEquals(rows.get(0).getLong("writetime(c1)"), rows.get(1).getLong("writetime(c1)"));
    assertEquals(rows.get(0).getLong("writetime(c2)"), rows.get(1).getLong("writetime(c2)"));
  }

  @Test
  public void testPrepareStatement() throws Exception {
    createTable("test_hash", "h1 int, h2 int, r int, c text, primary key ((h1, h2), r)", true);

    // Prepare a transaction statement. Verify the hash key of the whole statement is the first
    // insert statement that has the full hash key specified (third insert).
    PreparedStatement stmt =
        session.prepare("begin transaction" +
                        "  insert into test_hash (h1, h2, r, c) values (1, 1, ?, ?);" +
                        "  insert into test_hash (h1, h2, r, c) values (?, 2, ?, ?);" +
                        "  insert into test_hash (h1, h2, r, c) values (?, ?, ?, ?);" +
                        "end transaction;");
    int hashIndexes[] = stmt.getRoutingKeyIndexes();
    assertEquals(2, hashIndexes.length);
    assertEquals(5, hashIndexes[0]);
    assertEquals(6, hashIndexes[1]);

    session.execute(stmt.bind(Integer.valueOf(1), "v1",
                              Integer.valueOf(2), Integer.valueOf(2), "v2",
                              Integer.valueOf(3), Integer.valueOf(3), Integer.valueOf(3), "v3"));

    // Verify the rows.
    Vector<Row> rows = new Vector<Row>();
    HashSet<String> values = new HashSet<String>();
    for (Row row : session.execute("select h1, h2, r, c, writetime(c) from test_hash;")) {
      rows.add(row);
      values.add(row.getInt("h1")+","+row.getInt("h2")+","+row.getInt("r")+","+row.getString("c"));
    }
    assertEquals(3, rows.size());
    assertEquals(new HashSet<String>(Arrays.asList("1,1,1,v1",
                                                   "2,2,2,v2",
                                                   "3,3,3,v3")), values);
    // Verify writetimes are same.
    assertEquals(rows.get(0).getLong("writetime(c)"), rows.get(1).getLong("writetime(c)"));
    assertEquals(rows.get(0).getLong("writetime(c)"), rows.get(2).getLong("writetime(c)"));
  }

  @Test
  public void testStaticColumn() throws Exception {
    // Multiple writes to the same static row are not allowed
    createTable("test_static", "h int, r int, s int static, c int, primary key ((h), r)", true);
    session.execute("begin transaction" +
                    "  insert into test_static (h, r, s, c) values (1, 1, 1, 1);" +
                    "  insert into test_static (h, r, s, c) values (1, 2, 3, 4);" +
                    "end transaction;");

    // Verify the rows.
    Vector<Row> rows = new Vector<Row>();
    HashSet<String> values = new HashSet<String>();
    for (Row row : session.execute("select h, r, s, c, writetime(s), writetime(c) " +
                                   "from test_static;")) {
      rows.add(row);
      values.add(row.getInt("h")+","+row.getInt("r")+","+row.getInt("s")+","+row.getInt("c"));
    }
    assertEquals(new HashSet<String>(Arrays.asList("1,1,3,1",
                                                   "1,2,3,4")), values);
    // Verify writetimes are same.
    assertEquals(rows.get(0).getLong("writetime(s)"), rows.get(1).getLong("writetime(s)"));
    assertEquals(rows.get(0).getLong("writetime(c)"), rows.get(1).getLong("writetime(c)"));
  }

  @Test
  public void testInvalidStatements() throws Exception {
    createTables();

    // Missing "begin transaction"
    runInvalidStmt("insert into test_txn1 (k, c1, c2) values (?, ?, ?);" +
                   "insert into test_txn2 (k, c1, c2) values (?, ?, ?);" +
                   "commit;");

    // Missing "end transaction"
    runInvalidStmt("begin transaction" +
                   "  insert into test_txn1 (k, c1, c2) values (?, ?, ?);" +
                   "  insert into test_txn2 (k, c1, c2) values (?, ?, ?);");

    // Missing "begin / end transaction"
    runInvalidStmt("insert into test_txn1 (k, c1, c2) values (?, ?, ?);" +
                   "insert into test_txn2 (k, c1, c2) values (?, ?, ?);");

    // Writing to non-transactional table
    createTable("test_non_txn", "k int primary key, c1 int, c2 text", false);
    runInvalidStmt("begin transaction" +
                   "  insert into test_txn1 (k, c1, c2) values (?, ?, ?);" +
                   "  insert into test_non_txn (k, c1, c2) values (?, ?, ?);" +
                   "end transaction;");

    // Conditional DML not supported yet
    runInvalidStmt("begin transaction" +
                   "  insert into test_txn1 (k, c1, c2) values (?, ?, ?) if not exists;" +
                   "end transaction;");
  }

  protected String setupUserAndPrepareTransaction(
      String newUser, String newNumber, boolean InsertAfterDelete) {
    session.execute("create table user (user_id text primary key, phone_number text) " +
                    "with transactions = {'enabled' : true}");
    session.execute("create unique index test_unique on user (phone_number)");

    String s =  "begin transaction";
    if (InsertAfterDelete) {
      session.execute(
          "begin transaction" +
          "  insert into user (user_id, phone_number) values ('A', '22-11');" +
          "  insert into user (user_id, phone_number) values ('C', '22-44');" +
          "end transaction;");

      s += "  delete from user where user_id='C' if exists else error;" +
           "  insert into user (user_id, phone_number) values " +
           "    ('C', '22-44') if not exists else error;" +
           "  delete from user where user_id='A' if exists else error;" +
           "  insert into user (user_id, phone_number) values " +
           "    ('" + newUser + "', '" + newNumber +"') if not exists else error;";
    } else {
      if (newUser != "A") {
        session.execute(
            "begin transaction" +
            "  insert into user (user_id, phone_number) values ('A', '22-11');" +
            "end transaction;");
      }
      s += "  insert into user (user_id, phone_number) values " +
           "    ('C', '22-44') if not exists else error;" +
           "  delete from user where user_id='C' if exists else error;" +
           "  insert into user (user_id, phone_number) values " +
           "    ('" + newUser + "', '" + newNumber +"') if not exists else error;" +
           "  delete from user where user_id='A' if exists else error;";
    }
    return s + "end transaction;";
  }

  protected void checkUserRow(
      int totalRows, int rowIdx, String newUser, String newNumber) throws Exception {
    List<Row> rows = session.execute("select user_id, phone_number from user").all();
    assertEquals("Wrong number of rows", totalRows, rows.size());
    if (totalRows > 0) {
      Row r = rows.get(rowIdx);
      assertEquals("Wrong value", newUser, r.getString("user_id"));
      assertEquals("Wrong value", newNumber, r.getString("phone_number"));
    }
  }

  @Test
  public void testUserInsertAfterDeleteWithoutSerialFlag() throws Exception {
    try {
      restartClusterWithFlag("ycql_serial_operation_in_transaction_block", "false");
      session.execute(setupUserAndPrepareTransaction("A", "22-11", true));
      checkUserRow(2, 0, "A", "22-11");
    } finally {
      destroyMiniCluster(); // Destroy the recreated cluster when done.
    }
  }

  @Test
  public void testUserInsertAfterDelete() throws Exception {
    session.execute(setupUserAndPrepareTransaction("A", "22-11", true));
    checkUserRow(2, 0, "A", "22-11");
  }

  @Test
  public void testUserInsertAfterDeleteDiffNumber() throws Exception {
    session.execute(setupUserAndPrepareTransaction("B", "44-33", true));
    checkUserRow(2, 1, "B", "44-33");
  }

  @Test
  public void testUserDeleteAfterInsert() throws Exception {
    session.execute(setupUserAndPrepareTransaction("A", "22-11", false));
    checkUserRow(0, 0, "", "");
  }

  @Test
  public void testUserDeleteAfterInsertDiffNumber() throws Exception {
    session.execute(setupUserAndPrepareTransaction("B", "44-33", false));
    checkUserRow(1, 0, "B", "44-33");
  }

  protected void doTestInsertAfterDeleteWithUniqueIndex() {
    session.execute("create table test_tbl (h int, r int, v int, primary key (h, r)) " +
                    "with transactions = {'enabled' : true}");
    session.execute("create unique index test_idx on test_tbl (v)");

    session.execute("insert into test_tbl (h, r, v) values (1, 1, 99);");
    session.execute("begin transaction" +
                    "  delete from test_tbl where h = 1 and r = 1;" +
                    "  insert into test_tbl (h, r, v) values (1, 2, 99);" +
                    "end transaction;");
    // Verify the rows.
    assertQuery("select * from test_tbl", "Row[1, 2, 99]");
    assertQuery("select * from test_idx", "Row[1, 2, 99]");
  }

  @Test
  public void testInsertAfterDeleteWithUniqueIndex() throws Exception {
    doTestInsertAfterDeleteWithUniqueIndex();
  }

  @Test
  public void testInsertAfterDeleteWithUniqueIndexWithoutSerialFlag() throws Exception {
    try {
      restartClusterWithFlag("ycql_serial_operation_in_transaction_block", "false");
      try {
        doTestInsertAfterDeleteWithUniqueIndex();
      } catch (InvalidQueryException e) {
        LOG.info("Expected exception", e);
        assertTrue(e.getMessage().contains("Duplicate value disallowed by unique index test_idx"));
      }
    } finally {
      destroyMiniCluster(); // Destroy the recreated cluster when done.
    }
  }

  @Test
  public void testBasicReadWrite() throws Exception {
    // Stress-test multi-key insert in a loop. Each time insert keys in a transaction and read them
    // back immediately to verify the content and writetime are identical for all keys.
    session.execute("create table test_read_write (k int primary key, v int) " +
                    "with transactions = {'enabled' : true};");

    final int BATCH_SIZE = 10;
    final int KEY_COUNT = 5000;

    String insert = "begin transaction";
    for (int j = 0; j < BATCH_SIZE; j++) {
      insert += "  insert into test_read_write (k, v) values (?, ?);";
    }
    insert +=  "end transaction;";

    String select = "select v, writetime(v) from test_read_write where k in ?;";

    PreparedStatement insertStmt = session.prepare(insert);
    PreparedStatement selectStmt = session.prepare(select);

    int failedReadCount = 0;
    for (int i = 0; i < KEY_COUNT; i += BATCH_SIZE) {
      Object[] values = new Object[BATCH_SIZE * 2];
      for (int j = 0; j < BATCH_SIZE; j++) {
        values[j * 2] = Integer.valueOf(i + j); // column k
        values[j * 2 + 1] = Integer.valueOf(i); // column v
      }
      session.execute(insertStmt.bind(values));

      values = new Object[BATCH_SIZE];
      for (int j = 0; j < BATCH_SIZE; j++) {
        values[j] = Integer.valueOf(i + j);
      }
      List<Row> rows = session.execute(selectStmt.bind(Arrays.asList(values))).all();

      boolean match = true;
      if (rows.size() == BATCH_SIZE) {
        Row r0 = rows.get(0);
        for (int j = 1; j < BATCH_SIZE; j++) {
          Row rj = rows.get(j);
          if (r0.getInt("v") != rj.getInt("v") ||
              r0.getLong("writetime(v)") != rj.getLong("writetime(v)")) {
            match = false;
            break;
          }
        }

        if (r0.getInt("v") == i && match)
          continue;
      }

      LOG.info(String.format("Iteration %d: row count = %d", i, rows.size()));
      for (Row row : rows) {
        LOG.info(row.toString());
      }
      failedReadCount++;
    }
    assertEquals("Failed read count", 0, failedReadCount);
  }

  private int getTransactionConflictsCount(String tableName) throws Exception {
    return getTableCounterMetric(DEFAULT_TEST_KEYSPACE, tableName, "transaction_conflicts");
  }

  private int getExpiredTransactionsCount() throws Exception {
    return getTableCounterMetric("system", "transactions", "expired_transactions");
  }

  @Test
  public void testWriteConflicts() throws Exception {
    // Test write transaction conflicts by writing to the same rows in parallel repeatedly until
    // the desired number of conlicts have happened and the writes have been retried successfully
    // without error.
    session.execute("create table test_write_conflicts (k int primary key, v int) " +
                    "with transactions = {'enabled' : true};");

    PreparedStatement insertStmt = session.prepare(
        "begin transaction" +
        "  insert into test_write_conflicts (k, v) values (1, 1000);" +
        "  insert into test_write_conflicts (k, v) values (2, 2000);" +
        "end transaction;");

    final int PARALLEL_WRITE_COUNT = 5;
    final int TOTAL_CONFLICTS = 10;

    int initialConflicts = getTransactionConflictsCount("test_write_conflicts");
    int initialRetries = getRetriesCount();
    LOG.info("Initial transaction conflicts = {}, retries = {}",
             initialConflicts, initialRetries);

    while (true) {
      Set<ResultSetFuture> results = new HashSet<ResultSetFuture>();
      for (int i = 0; i < PARALLEL_WRITE_COUNT; i++) {
        results.add(session.executeAsync(insertStmt.bind()));
      }
      for (ResultSetFuture result : results) {
        result.get();
      }
      int currentConflicts = getTransactionConflictsCount("test_write_conflicts");
      int currentRetries = getRetriesCount();
      LOG.info("Current transaction conflicts = {}, retries = {}",
               currentConflicts, currentRetries);
      if (currentConflicts - initialConflicts >= TOTAL_CONFLICTS &&
          currentRetries > initialRetries)
        break;
    }

    // Also verify that the rows are inserted indeed.
    assertQueryRowsUnordered("select k, v from test_write_conflicts",
                             "Row[1, 1000]",
                             "Row[2, 2000]");
  }

  @Test
  public void testReadRestarts() throws Exception {
    // Test read restart by repeatedly inserting 2 rows into a table with an invariant while
    // reading them back in parallel. Verify that the invariant is maintained and writetimes are
    // consistent while there are read restarts and retries.
    session.execute("create table test_restart (k int primary key, v int) " +
                    "with transactions = {'enabled' : true};");

    final int TOTAL = 100;
    final PreparedStatement insertStmt = session.prepare(
        "begin transaction" +
        "  insert into test_restart (k, v) values (1, ?);" +
        "  insert into test_restart (k, v) values (2, ?);" +
        "end transaction;");
    session.execute(insertStmt.bind(Integer.valueOf(0), Integer.valueOf(TOTAL)));

    // Thread to insert rows in parallel.
    Thread thread = new Thread() {

      public void run() {
        Random rand = new Random();
        do {
          int v1 = rand.nextInt();
          int v2 = TOTAL - v1;
          session.execute(insertStmt.bind(Integer.valueOf(v1), Integer.valueOf(v2)));
        } while (!Thread.interrupted());
      }
    };
    thread.start();

    try {
      PreparedStatement selectStmt = session.prepare(
          "select v, writetime(v) from test_restart where k in (1, 2);");

      int initialRestarts = getRestartsCount("test_restart");
      int initialRetries = getRetriesCount();
      LOG.info("Initial restarts = {}, retries = {}", initialRestarts, initialRetries);

      // Keep reading until we have the desired number of restart requests and retries.
      final int TOTAL_RESTARTS = BuildTypeUtil.nonTsanVsTsan(10, 5);
      final int TOTAL_RETRIES = BuildTypeUtil.nonTsanVsTsan(10, 5);
      int i = 0;
      while (true) {
        i++;
        List<Row> rows = session.execute(selectStmt.bind()).all();
        assertEquals(2, rows.size());
        assertEquals(TOTAL, rows.get(0).getInt("v") + rows.get(1).getInt("v"));
        assertEquals(rows.get(0).getLong("writetime(v)"), rows.get(1).getLong("writetime(v)"));

        int currentRestarts = getRestartsCount("test_restart");
        int currentRetries = getRetriesCount();
        if (currentRestarts - initialRestarts >= TOTAL_RESTARTS &&
            currentRetries - initialRetries >= TOTAL_RETRIES) {
          LOG.info("Current restarts = {}, retries = {} after {} tries",
                   currentRestarts, currentRetries, i);
          break;
        }
      }
    } finally {
      thread.interrupt();
    }
  }

  @Test
  public void testSystemTransactionsTable() throws Exception {
    createTables();
    // Insert into multiple tables and ensure all rows are written with same writetime.
    session.execute("begin transaction" +
                    "  insert into test_txn1 (k, c1, c2) values (?, ?, ?);" +
                    "  insert into test_txn2 (k, c1, c2) values (?, ?, ?);" +
                    "  insert into test_txn3 (k, c1, c2) values (?, ?, ?);" +
                    "end transaction;",
            Integer.valueOf(1), Integer.valueOf(1), "v1",
            Integer.valueOf(2), Integer.valueOf(2), "v2",
            Integer.valueOf(3), Integer.valueOf(3), "v3");

    thrown.expect(com.datastax.driver.core.exceptions.InvalidQueryException.class);
    thrown.expectMessage("Object Not Found");
    Iterator<Row> rows = session.execute("SELECT * FROM system.transactions").iterator();
  }

  @Test
  public void testTimeout() throws Exception {
    try {
      // Test transaction timeout by recreating the cluster with missed heartbeat periods equals 0
      // and executing a transaction. Verify that OperationTimedOutException is raised as CQL proxy
      // keeps retrying the transaction.
      int txnCheckIntervalMilliSecs = 50;
      Map<String, String> tserverFlags = new HashMap<String, String>();
      tserverFlags.put("transaction_max_missed_heartbeat_periods", "0.0");
      tserverFlags.put("transaction_check_interval_usec",
                       String.valueOf(txnCheckIntervalMilliSecs * 1000));

      destroyMiniCluster();
      createMiniCluster(
          Collections.emptyMap(),
          tserverFlags);

      setUpCqlClient();
      session.execute("create table test_timeout (k int primary key, v int) " +
                      "with transactions = {'enabled' : true};");

      int initialExpiredTransactions = getExpiredTransactionsCount();
      LOG.info("Initial expired transactions = {}", initialExpiredTransactions);

      try {
        thrown.expect(com.datastax.driver.core.exceptions.DriverException.class);
        session.execute("begin transaction" +
                        "  insert into test_timeout (k, v) values (1, 1);" +
                        "end transaction;");
      } catch (com.datastax.driver.core.exceptions.DriverException e) {
        // TransactionCoordinator polls for expired/aborted transactions every
        // txnCheckIntervalMilliSecs in case of regular builds, and in intervals of
        // 3 * txnCheckIntervalMilliSecs for tsan builds. Sleep accordingly to see the latest
        // expired_transactions count.
        Thread.sleep(txnCheckIntervalMilliSecs * BuildTypeUtil.nonTsanVsTsan(5, 10));
        int currentExpiredTransactions = getExpiredTransactionsCount();
        LOG.info("Current expired transactions = {}", currentExpiredTransactions);
        assertTrue(currentExpiredTransactions > initialExpiredTransactions);
        throw e;
      }
    } finally {
      // Destroy the recreated cluster when done.
      destroyMiniCluster();
    }
  }

  @Test
  public void testTransactionConditionalDMLWithElseError() throws Exception {
    createTable("t", "h int, r int, v int, primary key ((h), r)", true);
    session.execute("begin transaction" +
                    "  insert into t (h, r, v) values (1, 1, 11) if not exists else error;" +
                    "  insert into t (h, r, v) values (1, 2, 12) if not exists else error;" +
                    "end transaction;");

    assertQueryRowsUnordered("select * from t",
        "Row[1, 1, 11]",
        "Row[1, 2, 12]");

    runInvalidStmt("begin transaction" +
                    "  insert into t (h, r, v) values (2, 2, 22) if not exists else error;" +
                    "  insert into t (h, r, v) values (1, 2, 120) if not exists else error;" +
                   "end transaction;",
                   "Execution Error. Condition on table t was not satisfied.");

    // Check that the table was not changed.
    assertQueryRowsUnordered("select * from t",
        "Row[1, 1, 11]",
        "Row[1, 2, 12]");

    session.execute("begin transaction" +
                    "  insert into t (h, r, v) values (1, 3, 13)" +
                    "    if not exists or v != 0 else error;" +
                    "end transaction;");

    assertQueryRowsUnordered("select * from t",
        "Row[1, 1, 11]",
        "Row[1, 2, 12]",
        "Row[1, 3, 13]");

    session.execute("begin transaction" +
                    "  update t set v = 130 where h = 1 and r = 3" +
                    "    if not exists or v = 13 else error;" +
                    "end transaction;");

    assertQueryRowsUnordered("select * from t",
        "Row[1, 1, 11]",
        "Row[1, 2, 12]",
        "Row[1, 3, 130]");

    session.execute("begin transaction" +
                    "  update t set v = 1300 where h = 1 and r = 3" +
                    "    if v = 130 else error;" +
                    "end transaction;");

    assertQueryRowsUnordered("select * from t",
        "Row[1, 1, 11]",
        "Row[1, 2, 12]",
        "Row[1, 3, 1300]");

    runInvalidStmt("begin transaction" +
                   "  update t set v = 777 where h = 1 and r = 3" +
                   "    if v = 999 else error;" +
                   "end transaction;",
                   "Execution Error. Condition on table t was not satisfied.");

    // Check that the table was not changed.
    assertQueryRowsUnordered("select * from t",
        "Row[1, 1, 11]",
        "Row[1, 2, 12]",
        "Row[1, 3, 1300]");

    // Test 2 tables in one transaction.
    createTable("t2", "h int, r int, primary key ((h), r)", true);
    session.execute("begin transaction" +
                    "  insert into t (h, r, v) values (1, 4, 14) if not exists else error;" +
                    "  insert into t2 (h, r) values (100, 100) if not exists else error;" +
                    "end transaction;");

    assertQueryRowsUnordered("select * from t",
        "Row[1, 1, 11]",
        "Row[1, 2, 12]",
        "Row[1, 3, 1300]",
        "Row[1, 4, 14]");

    assertQueryRowsUnordered("select * from t2",
        "Row[100, 100]");

    runInvalidStmt("begin transaction" +
                   "  insert into t (h, r, v) values (9, 9, 9) if not exists else error;" +
                   "  insert into t2 (h, r) values (100, 100) if not exists else error;" +
                   "end transaction;",
                   "Execution Error. Condition on table t2 was not satisfied.");

    runInvalidStmt("begin transaction" +
                   "  insert into t (h, r, v) values (1, 4, 140) if not exists else error;" +
                   "  insert into t2 (h, r) values (9, 9) if not exists else error;" +
                   "end transaction;",
                   "Execution Error. Condition on table t was not satisfied.");

    // Check that both tables were not changed.
    assertQueryRowsUnordered("select * from t",
        "Row[1, 1, 11]",
        "Row[1, 2, 12]",
        "Row[1, 3, 1300]",
        "Row[1, 4, 14]");

    assertQueryRowsUnordered("select * from t2",
        "Row[100, 100]");

    // Test unique index together with the conditional DMLs.
    session.execute("create unique index test_unique_by_v on t (v);");
    session.execute("begin transaction" +
                    "  insert into t (h, r, v) values (1, 5, 15) if not exists else error;" +
                    "end transaction;");

    assertQueryRowsUnordered("select * from t",
        "Row[1, 1, 11]",
        "Row[1, 2, 12]",
        "Row[1, 3, 1300]",
        "Row[1, 4, 14]",
        "Row[1, 5, 15]");

    runInvalidStmt("begin transaction" +
                   "  insert into t (h, r, v) values (9, 9, 9) if not exists else error;" +
                   "  insert into t2 (h, r) values (100, 100) if not exists else error;" +
                   "end transaction;",
                   "Execution Error. Condition on table t2 was not satisfied.");

    runInvalidStmt("begin transaction" +
                   "  insert into t (h, r, v) values (1, 4, 140) if not exists else error;" +
                   "  insert into t2 (h, r) values (9, 9) if not exists else error;" +
                   "end transaction;",
                   "Execution Error. Condition on table t was not satisfied.");

    // Breaking the index uniqueness.
    runInvalidStmt("begin transaction" +
                   "  insert into t (h, r, v) values (9, 9, 15) if not exists else error;" +
                   "  insert into t2 (h, r) values (9, 9) if not exists else error;" +
                   "end transaction;",
                   "Execution Error. Duplicate value disallowed by unique index test_unique_by_v");
    runInvalidStmt("begin transaction" +
                   "  insert into t (h, r, v) values (9, 9, 15) if not exists else error;" +
                   "end transaction;",
                   "Execution Error. Duplicate value disallowed by unique index test_unique_by_v");

    // Check that both tables were not changed.
    assertQueryRowsUnordered("select * from t",
        "Row[1, 1, 11]",
        "Row[1, 2, 12]",
        "Row[1, 3, 1300]",
        "Row[1, 4, 14]",
        "Row[1, 5, 15]");

    assertQueryRowsUnordered("select * from t2",
        "Row[100, 100]");
  }

  @Test
  public void testTransactionConditionalDMLWithoutElseError() throws Exception {
    createTable("t", "h int, r int, v int, primary key ((h), r)", true);

    // Not supported yet.
    runInvalidStmt("begin transaction" +
                    "  insert into t (h, r, v) values (9, 9, 99) if not exists;" +
                   "end transaction;",
                   "Invalid CQL Statement. Execution of conditional DML statement in " +
                   "transaction block without ELSE ERROR is not supported yet");

    runInvalidStmt("begin transaction" +
                    "  insert into t (h, r, v) values (9, 9, 99) if v = 99;" +
                   "end transaction;",
                   "Invalid CQL Statement. Execution of conditional DML statement in " +
                   "transaction block without ELSE ERROR is not supported yet");

    assertNoRow("select * from t");
  }


  @Test
  public void testTransactionWithReturnsStatus() throws Exception {
    createTable("t", "h int, r int, v int, primary key ((h), r)", true);

    // Not supported yet.
    runInvalidStmt("begin transaction" +
                    "  insert into t (h, r, v) values (9, 9,99)" +
                    "    if not exists else error RETURNS STATUS AS ROW;" +
                   "end transaction;",
                   "Invalid CQL Statement. Execution of statement in transaction block " +
                   "with RETURNS STATUS AS ROW is not supported yet");

    runInvalidStmt("begin transaction" +
                    "  insert into t (h, r, v) values (9, 9,99) RETURNS STATUS AS ROW;" +
                   "end transaction;",
                   "Invalid CQL Statement. Execution of statement in transaction block " +
                   "with RETURNS STATUS AS ROW is not supported yet");

    assertNoRow("select * from t");
  }
}
