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

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.SimpleStatement;

import org.junit.Test;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

import java.util.*;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestReturnsClause extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestReturnsClause.class);


  private String getExpectedResultColumns(Map<String, String> columns) {
    StringBuilder sb = new StringBuilder();
    sb.append("Columns[");
    sb.append("[applied](boolean)");
    sb.append(", [message](varchar)");
    for (Map.Entry entry : columns.entrySet()) {
      sb.append(", ");
      sb.append(entry.getKey()).append("(").append(entry.getValue()).append(")");
    }
    sb.append("]");
    return sb.toString();
  }

  private String getExpectedResultRow(Boolean applied,
                                      String message,
                                      String... values) {

    StringBuilder sb = new StringBuilder();
    sb.append("Row[");
    sb.append(applied.toString()).append(", ");
    sb.append(message);
    for (String value : values) {
      sb.append(", ").append(value);
    }
    sb.append("]");
    return sb.toString();
  }

  private void checkReturnStatus(String stmt,
                                 Map<String, String> columns,
                                 Boolean applied,
                                 String message,
                                 String... values) {
    // Compute expected result columns.
    String expectedColumns = getExpectedResultColumns(columns);

    // Compute expected result row.
    String expectedRow = getExpectedResultRow(applied, message, values);

    // Execute stmt and check status.
    assertQuery(stmt + " RETURNS STATUS AS ROW", expectedColumns, expectedRow);
  }

  @Test
  public void testReturnsStatusStmts() throws Exception {
    session.execute("CREATE TABLE test_returns_status(h int, r bigint, " +
        "vl list<varchar>, vd double, primary key (h, r))");

    Map<String, String> columns = new LinkedHashMap<>();
    columns.put("h", "int");
    columns.put("r", "bigint");
    columns.put("vl", "list<varchar>");
    columns.put("vd", "double");

    //----------------------------------------------------------------------------------------------
    // Test simple DML statements.

    // Test error stmt.
    checkReturnStatus("UPDATE test_returns_status SET vl[2] = 'a' where h = 1 and r = 1",
                      columns,
                      false /* applied */,
                      "Unable to replace items in empty list.",
                      "NULL", "NULL", "NULL", "NULL");

    // Test success statement.
    checkReturnStatus("UPDATE test_returns_status SET vl = ['a'] where h = 1 and r = 1",
                      columns,
                      true /* applied */,
                      "NULL",
                      "NULL", "NULL", "NULL", "NULL");

    //----------------------------------------------------------------------------------------------
    // Test conditional DML.

    // Test true condition: always return null row values for consistency with regular IF clause.
    checkReturnStatus("INSERT INTO test_returns_status(h, r, vl, vd) " +
                          "VALUES (1, 2, ['a'], 2.0) IF NOT EXISTS",
                      columns,
                      true /* applied */,
                      "NULL",
                      "NULL", "NULL", "NULL", "NULL");

    checkReturnStatus("DELETE FROM test_returns_status " +
                          "WHERE h = 1 AND r = 2 IF EXISTS",
                      columns,
                      true /* applied */,
                      "NULL",
                      "NULL", "NULL", "NULL", "NULL");

    // Test false condition: return existing row values if any.
    checkReturnStatus("INSERT INTO test_returns_status(h, r, vl, vd) " +
                          "VALUES (1, 1, ['b'], 2.0) IF NOT EXISTS",
                      columns,
                      false /* applied */,
                      "NULL",
                      "1", "1", "[a]", "NULL");

    checkReturnStatus("DELETE FROM test_returns_status " +
                          "WHERE h = 2 AND r = 2 IF EXISTS",
                      columns,
                      false /* applied */,
                      "NULL",
                      "NULL", "NULL", "NULL", "NULL");

    // Test true 'ELSE ERROR' clause.
    checkReturnStatus("INSERT INTO test_returns_status(h, r, vl, vd) " +
                          "VALUES (2, 2, ['a'], 2.0) IF NOT EXISTS ELSE ERROR",
                      columns,
                      true /* applied */,
                      "NULL",
                      "NULL", "NULL", "NULL", "NULL");

    // Test false 'ELSE ERROR' clause.
    checkReturnStatus("INSERT INTO test_returns_status(h, r, vl, vd) " +
                          "VALUES (2, 2, ['a'], 2.0) IF NOT EXISTS ELSE ERROR",
                      columns,
                      false /* applied */,
                      "Condition on table test_returns_status was not satisfied.",
                      "NULL", "NULL", "NULL", "NULL");

    //----------------------------------------------------------------------------------------------
    // Test invalid statements.

    // Check that parsing errors still reported -- typo in 'VALUES'.
    runInvalidQuery("INSERT INTO test_returns_status(h, r, vl, vd) " +
                        "ALUES (1, 1, [], 0.0) RETURNS STATUS AS ROW;");

    // Check analysis errors still reported -- wrong list element type.
    runInvalidQuery("INSERT INTO test_returns_status(h, r, vl, vd) " +
                        "VALUES (1, 1, [1], 0.0) RETURNS STATUS AS ROW;");
  }

  @Test
  public void testReturnsStatusWithIndex() throws Exception {
    session.execute("CREATE TABLE test_rs_trans(h int, r bigint, " +
                        "v1 int, v2 varchar, primary key (h, r)) " +
                        "WITH transactions = { 'enabled' : true };");
    session.execute("CREATE UNIQUE INDEX test_rs_idx ON test_rs_trans(v1) INCLUDE (v2)");
    waitForReadPermsOnAllIndexes("test_rs_trans");

    Map<String, String> columns = new LinkedHashMap<>();
    columns.put("h", "int");
    columns.put("r", "bigint");
    columns.put("v1", "int");
    columns.put("v2", "varchar");

    //----------------------------------------------------------------------------------------------
    // Test simple DML statements.

    // Test success statement.
    checkReturnStatus("INSERT INTO test_rs_trans(h, r, v1, v2) VALUES (1, 1, 2 ,'b')",
                      columns,
                      true /* applied */,
                      "NULL",
                      "NULL", "NULL", "NULL", "NULL");

    // Test error statement.
    checkReturnStatus("INSERT INTO test_rs_trans(h, r, v1, v2) VALUES (2, 2, 2, 'c')",
                      columns,
                      false /* applied */,
                      "Duplicate value disallowed by unique index test_rs_idx",
                      "NULL", "NULL", "NULL", "NULL");

    // Ensure main table write did not get applied due to the index failure.
    assertNoRow("SELECT * FROM test_rs_trans where h = 2 AND r = 2");


    //----------------------------------------------------------------------------------------------
    // Test conditional DML.

    // Test true condition.
    checkReturnStatus("UPDATE test_rs_trans set v1 = 3, v2 = 'c' " +
                          "WHERE h = 2 AND r = 2 IF NOT EXISTS",
                      columns,
                      true /* applied */,
                      "NULL",
                      "NULL", "NULL", "NULL", "NULL");

    // Test false condition.
    checkReturnStatus("UPDATE test_rs_trans set v1 = 2, v2 = 'b' " +
                          "WHERE h = 2 AND r = 2 IF NOT EXISTS",
                      columns,
                      false /* applied */,
                      "NULL",
                      "2", "2", "3", "c");

    // Test true 'ELSE ERROR' clause.
    checkReturnStatus("DELETE FROM test_rs_trans " +
                          "WHERE h = 2 and r = 2 IF EXISTS ELSE ERROR",
                      columns,
                      true /* applied */,
                      "NULL",
                      "NULL", "NULL", "NULL", "NULL");

    // Test false 'ELSE ERROR' clause.
    checkReturnStatus("DELETE v1 FROM test_rs_trans " +
                          "WHERE h = 2 and r = 2 IF EXISTS ELSE ERROR",
                      columns,
                      false /* applied */,
                      "Condition on table test_rs_trans was not satisfied.",
                      "NULL", "NULL", "NULL", "NULL");

    //----------------------------------------------------------------------------------------------
    // Test index on primary key.

    session.execute("CREATE TABLE test_rs_trans_pk(h int, r bigint, " +
                        "v1 int, v2 varchar, primary key (h, r)) " +
                        "WITH transactions = { 'enabled' : true };");
    session.execute("CREATE UNIQUE INDEX test_rs_pk_idx ON test_rs_trans_pk(r) INCLUDE (v1, v2)");

    // Test success statement.
    checkReturnStatus("INSERT INTO test_rs_trans_pk(h, r, v1, v2) VALUES (1, 1, 1 ,'b')",
                      columns,
                      true /* applied */,
                      "NULL",
                      "NULL", "NULL", "NULL", "NULL");

    // Test error statement.
    checkReturnStatus("INSERT INTO test_rs_trans_pk(h, r, v1, v2) VALUES (2, 1, 2, 'c')",
                      columns,
                      false /* applied */,
                      "Duplicate value disallowed by unique index test_rs_pk_idx",
                      "NULL", "NULL", "NULL", "NULL");

    // Ensure main table write did not get applied due to the index failure.
    assertNoRow("SELECT * FROM test_rs_trans_pk where h = 2 AND r = 1");
  }

  @Test
  public void testReturnsStatusWithinBatch() throws Exception {
    session.execute("CREATE TABLE test_rs_batch(h int, r bigint, " +
                        "v1 int, v2 varchar, primary key (h, r))");
    Map<String, String> columns = new LinkedHashMap<>();
    columns.put("h", "int");
    columns.put("r", "bigint");
    columns.put("v1", "int");
    columns.put("v2", "varchar");

    BatchStatement batch = new BatchStatement();
    String expectedColumns = getExpectedResultColumns(columns);
    List<String> expectedRows = new ArrayList<>();
    batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) " +
                                      "VALUES (1, 1, 1 ,'a') RETURNS STATUS AS ROW"));
    expectedRows.add(getExpectedResultRow(true /* applied */,
                                          "NULL",
                                          "NULL", "NULL", "NULL", "NULL"));

    batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) " +
                                      "VALUES (1, 1, 1 ,'a') IF NOT EXISTS ELSE ERROR " +
                                      "RETURNS STATUS AS ROW"));
    expectedRows.add(getExpectedResultRow(false /* applied */,
                                          "Condition on table test_rs_batch was not satisfied.",
                                          "NULL", "NULL", "NULL", "NULL"));

    batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) VALUES " +
                                      "(1, 1, 2 ,'b') IF NOT EXISTS RETURNS STATUS AS ROW"));
    expectedRows.add(getExpectedResultRow(false /* applied */,
                                          "NULL",
                                          "1", "1", "1", "a"));

    batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) " +
                                      "VALUES (2, 2, 2 ,'b') IF NOT EXISTS " +
                                      "RETURNS STATUS AS ROW"));
    expectedRows.add(getExpectedResultRow(true /* applied */,
                                          "NULL",
                                          "NULL", "NULL", "NULL", "NULL"));

    assertQuery(batch, expectedColumns, String.join("", expectedRows));
  }

  @Test
  public void testReturnsStatusInvalidBatch() throws Exception {

    session.execute("CREATE TABLE test_rs_batch(h int, r bigint, " +
                        "v1 int, v2 varchar, primary key (h, r))");

    // Conditional DML statements without return status are not allowed in batch
    {
      BatchStatement batch = new BatchStatement();
      batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) " +
                                        "VALUES (1, 1, 1 ,'a') IF NOT EXISTS"));
      batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) VALUES " +
                                        "(1, 1, 2 ,'b') IF NOT EXISTS"));

      runInvalidQuery(batch);
      // Check that no operations got applied.
      assertNoRow("SELECT * FROM test_rs_batch");
    }

    // If a statement in a batch returns status, all statements must.
    {
      BatchStatement batch = new BatchStatement();
      batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) " +
                                        "VALUES (1, 1, 1 ,'a') RETURNS STATUS AS ROW"));
      // Does not return status.
      batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) VALUES " +
                                        "(1, 1, 1 ,'a')"));
      batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) VALUES " +
                                        "(1, 1, 2 ,'b') IF NOT EXISTS RETURNS STATUS AS ROW"));

      runInvalidQuery(batch);
      // Check that no operations got applied.
      assertNoRow("SELECT * FROM test_rs_batch");
    }

    // All statements in a "return-status" batch must refer to the same table.
    {
      session.execute("CREATE TABLE test_rs_batch2(h int, r bigint, " +
                          "v1 int, v2 varchar, primary key (h, r)) ");

      BatchStatement batch = new BatchStatement();
      batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) " +
                                        "VALUES (1, 1, 1 ,'a') RETURNS STATUS AS ROW"));
      // Different table.
      batch.add(new SimpleStatement("INSERT INTO test_rs_batch2(h, r, v1, v2) VALUES " +
                                        "(1, 1, 1 ,'a') RETURNS STATUS AS ROW"));
      batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) VALUES " +
                                        "(1, 1, 2 ,'b') IF NOT EXISTS RETURNS STATUS AS ROW"));

      runInvalidQuery(batch);
      // Check that no operations got applied.
      assertNoRow("SELECT * FROM test_rs_batch");
      assertNoRow("SELECT * FROM test_rs_batch2");
    }

    // Range deletes are not supported yet.
    {
      BatchStatement batch = new BatchStatement();
      batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) " +
                                        "VALUES (1, 1, 1 ,'a') RETURNS STATUS AS ROW"));
      // Range delete (deletes multiple rows).
      batch.add(new SimpleStatement(" DELETE FROM test_rs_batch WHERE h = 1 " +
                                        "RETURNS STATUS AS ROW"));
      batch.add(new SimpleStatement("INSERT INTO test_rs_batch(h, r, v1, v2) VALUES " +
                                        "(1, 1, 2 ,'b') IF NOT EXISTS RETURNS STATUS AS ROW"));

      runInvalidQuery(batch);
      // Check that no operations got applied.
      assertNoRow("SELECT * FROM test_rs_batch");
    }
  }

  @Test
  public void testBatchDMLWithSecondaryIndex() throws Exception {
    // Test batch DMLs on a table with secondary index.
    session.execute("CREATE TABLE test_rs_batch_trans (h int, r bigint, v1 int, v2 varchar, " +
                    "primary key (h, r)) with transactions = { 'enabled' : true };");
    session.execute(
        "CREATE UNIQUE INDEX test_rs_batch_trans_idx ON test_rs_batch_trans (v1) INCLUDE (v2);");

    waitForReadPermsOnAllIndexes("test_rs_batch_trans");

    session.execute("INSERT INTO test_rs_batch_trans (h, r, v1, v2) VALUES (1, 1, 1, 'a');");
    session.execute("INSERT INTO test_rs_batch_trans (h, r, v1, v2) VALUES (1, 2, 2, 'b');");
    session.execute("INSERT INTO test_rs_batch_trans (h, r, v1, v2) VALUES (2, 1, 3, 'c');");
    session.execute("INSERT INTO test_rs_batch_trans (h, r, v1, v2) VALUES (2, 2, 4, 'd');");

    BatchStatement batch = new BatchStatement();
    String expectedResults = "";
    batch.add(new SimpleStatement("INSERT INTO test_rs_batch_trans (h, r, v1, v2) " +
                                  "VALUES (3, 1, 5 ,'e') RETURNS STATUS AS ROW"));
    expectedResults += "Row[true, NULL, NULL, NULL, NULL, NULL]";

    batch.add(new SimpleStatement("INSERT INTO test_rs_batch_trans (h, r, v1, v2) VALUES " +
                                  "(1, 1, 1 ,'a') IF NOT EXISTS " +
                                  "RETURNS STATUS AS ROW"));
    expectedResults += "Row[false, NULL, 1, 1, 1, a]";

    batch.add(new SimpleStatement("INSERT INTO test_rs_batch_trans (h, r, v1, v2) VALUES " +
                                  "(1, 1, 1 ,'a') IF NOT EXISTS ELSE ERROR " +
                                  "RETURNS STATUS AS ROW"));
    expectedResults += "Row[false, Condition on table test_rs_batch_trans was not satisfied., " +
                       "NULL, NULL, NULL, NULL]";

    batch.add(new SimpleStatement("INSERT INTO test_rs_batch_trans (h, r, v1, v2) VALUES " +
                                  "(3, 2, 1 ,'f') IF NOT EXISTS RETURNS STATUS AS ROW"));
    expectedResults += "Row[false, Duplicate value disallowed by unique index "+
                       "test_rs_batch_trans_idx, NULL, NULL, NULL, NULL]";

    assertQuery(batch, expectedResults);

    // Check that no operations got applied.
    assertQuery("SELECT * FROM test_rs_batch_trans;",
                new HashSet<String>(Arrays.asList("Row[1, 1, 1, a]",
                                                  "Row[1, 2, 2, b]",
                                                  "Row[2, 1, 3, c]",
                                                  "Row[2, 2, 4, d]",
                                                  "Row[3, 1, 5, e]")));
  }

  @Test
  public void testBatchDMLWithUniqueIndex() throws Exception {
    // Test batch insert of a table with unique index. Verify that only 1 insert succeed for the
    // same unique index value.
    session.execute("CREATE TABLE test_unique (k int primary key, v int) " +
                    "with transactions = { 'enabled' : true };");
    session.execute("CREATE UNIQUE INDEX test_unique_idx ON test_unique (v);");

    waitForReadPermsOnAllIndexes("test_unique");

    final int KEY_COUNT = 50;
    HashSet<Integer> allKeys = new HashSet<>();
    HashSet<Integer> allValues = new HashSet<>();
    PreparedStatement stmt = session.prepare("INSERT INTO test_unique (k, v) VALUES (?, ?) " +
                                             "RETURNS STATUS AS ROW;");
    for (int i = 0; i < KEY_COUNT; i++) {
      BatchStatement batch = new BatchStatement();
      batch.add(stmt.bind(Integer.valueOf(100 + i), Integer.valueOf(i)));
      batch.add(stmt.bind(Integer.valueOf(200 + i), Integer.valueOf(i)));
      batch.add(stmt.bind(Integer.valueOf(300 + i), Integer.valueOf(i)));
      allKeys.add(100 + i);
      allKeys.add(200 + i);
      allKeys.add(300 + i);
      allValues.add(i);

      // Verify only 1 insert succeeds and the others are prohibited by the unique index.
      ResultSet rs = session.execute(batch);
      int appliedCount = 0;
      int totalCount = 0;
      for (Row r : rs) {
        if (r.getBool("[applied]")) {
          appliedCount++;
        } else {
          assertEquals("Duplicate value disallowed by unique index test_unique_idx",
                       r.getString("[message]"));
        }
        totalCount++;
      }
      assertEquals(1, appliedCount);
      assertEquals(3, totalCount);
    }

    // Verify all the index values are present and unique.
    HashSet<Integer> foundKeys = new HashSet<>();
    HashSet<Integer> foundValues = new HashSet<>();
    for (Row r : session.execute("select * from test_unique;")) {
      LOG.info(r.toString());
      foundKeys.add(r.getInt("k"));
      foundValues.add(r.getInt("v"));
    }
    assertEquals(KEY_COUNT, foundKeys.size());
    assertTrue(allKeys.containsAll(foundKeys));
    assertEquals(allValues, foundValues);
  }
}
