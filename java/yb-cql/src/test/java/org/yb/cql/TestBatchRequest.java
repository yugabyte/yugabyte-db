// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.SimpleStatement;

import org.junit.Test;

import java.util.Arrays;
import java.util.HashSet;

public class TestBatchRequest extends BaseCQLTest {

  @Test
  public void testPreparedStatement() throws Exception {
    // Setup table.
    setupTable("t", 0 /* num_rows */);

    // Test batch of preapred statements.
    PreparedStatement stmt = session.prepare("INSERT INTO t (h1, h2, r1, r2, v1, v2) " +
                                             "VALUES (?, ?, ?, ?, ?, ?);");
    BatchStatement batch = new BatchStatement();
    for (int i = 1; i <= 10; i++) {
      batch.add(stmt.bind(new Integer(i), "h" + i,
                          new Integer(i), "r" + i,
                          new Integer(i), "v" + i));
    }
    session.execute(batch);

    assertQuery("SELECT * FROM t",
                new HashSet<String>(Arrays.asList("Row[1, h1, 1, r1, 1, v1]",
                                                  "Row[2, h2, 2, r2, 2, v2]",
                                                  "Row[3, h3, 3, r3, 3, v3]",
                                                  "Row[4, h4, 4, r4, 4, v4]",
                                                  "Row[5, h5, 5, r5, 5, v5]",
                                                  "Row[6, h6, 6, r6, 6, v6]",
                                                  "Row[7, h7, 7, r7, 7, v7]",
                                                  "Row[8, h8, 8, r8, 8, v8]",
                                                  "Row[9, h9, 9, r9, 9, v9]",
                                                  "Row[10, h10, 10, r10, 10, v10]")));
  }

  @Test
  public void testSimpleStatement() throws Exception {
    // Setup table.
    setupTable("t", 0 /* num_rows */);

    // Test batch of regular statements.
    PreparedStatement stmt = session.prepare("INSERT INTO t (h1, h2, r1, r2, v1, v2) " +
                                             "VALUES (?, ?, ?, ?, ?, ?);");
    BatchStatement batch = new BatchStatement();
    for (int i = 1; i <= 10; i++) {
      batch.add(new SimpleStatement(String.format("INSERT INTO t (h1, h2, r1, r2, v1, v2) " +
                                                  "VALUES (%d, 'h%d', %d, 'r%d', %d, 'v%d');",
                                                  i, i, i, i, i, i)));
    }
    session.execute(batch);

    assertQuery("SELECT * FROM t",
                new HashSet<String>(Arrays.asList("Row[1, h1, 1, r1, 1, v1]",
                                                  "Row[2, h2, 2, r2, 2, v2]",
                                                  "Row[3, h3, 3, r3, 3, v3]",
                                                  "Row[4, h4, 4, r4, 4, v4]",
                                                  "Row[5, h5, 5, r5, 5, v5]",
                                                  "Row[6, h6, 6, r6, 6, v6]",
                                                  "Row[7, h7, 7, r7, 7, v7]",
                                                  "Row[8, h8, 8, r8, 8, v8]",
                                                  "Row[9, h9, 9, r9, 9, v9]",
                                                  "Row[10, h10, 10, r10, 10, v10]")));
  }

  @Test
  public void testMixedStatements() throws Exception {
    // Setup table.
    setupTable("t", 0 /* num_rows */);

    // Test batch of mixed statements.
    PreparedStatement ins1 = session.prepare("INSERT INTO t (h1, h2, r1, r2, v1, v2) " +
                                             "VALUES (?, ?, ?, ?, ?, ?);");
    PreparedStatement ins2 = session.prepare("INSERT INTO t (h1, h2, r1, r2) " +
                                             "VALUES (?, ?, ?, ?);");
    BatchStatement batch = new BatchStatement();
    for (int i = 1; i <= 5; i++) {
      batch.add(ins1.bind(new Integer(i), "h" + i, new Integer(1), "r1", new Integer(1), "v1"));
      batch.add(ins2.bind(new Integer(i), "h" + i, new Integer(2), "r2"));
      batch.add(new SimpleStatement(String.format("UPDATE t SET v1 = 3, v2 = 'v3' WHERE " +
                                                  "h1 = %d AND h2 = 'h%d' AND " +
                                                  "r1 = 3 AND r2 = 'r3';",
                                                  i, i)));
    }
    session.execute(batch);

    assertQuery("SELECT * FROM t",
                new HashSet<String>(Arrays.asList("Row[1, h1, 1, r1, 1, v1]",
                                                  "Row[1, h1, 2, r2, NULL, NULL]",
                                                  "Row[1, h1, 3, r3, 3, v3]",
                                                  "Row[2, h2, 1, r1, 1, v1]",
                                                  "Row[2, h2, 2, r2, NULL, NULL]",
                                                  "Row[2, h2, 3, r3, 3, v3]",
                                                  "Row[3, h3, 1, r1, 1, v1]",
                                                  "Row[3, h3, 2, r2, NULL, NULL]",
                                                  "Row[3, h3, 3, r3, 3, v3]",
                                                  "Row[4, h4, 1, r1, 1, v1]",
                                                  "Row[4, h4, 2, r2, NULL, NULL]",
                                                  "Row[4, h4, 3, r3, 3, v3]",
                                                  "Row[5, h5, 1, r1, 1, v1]",
                                                  "Row[5, h5, 2, r2, NULL, NULL]",
                                                  "Row[5, h5, 3, r3, 3, v3]")));
  }

  @Test
  public void testInvalidStatement() throws Exception {
    // Setup table.
    setupTable("t", 0 /* num_rows */);

    // Test mix of valid and invalid statements. Expect error.
    BatchStatement batch = new BatchStatement();
    batch.add(new SimpleStatement("INSERT INTO t (h1, h2, r1, r2, v1, v2) " +
                                  "VALUES (1, 'h1', 1, 'r1', 1, 'v1');"));
    batch.add(new SimpleStatement("INSERT INTO t (h1, h2, v1, v2) " +
                                  "VALUES (1, 'h1', 1, 'v1');"));
    runInvalidStmt(batch);

    // TODO (Robert): batch statement guarantees either all statements are executed or none is.
    // Should return no rows.
    assertQuery("SELECT * from t",
                "Row[1, h1, 1, r1, 1, v1]");
  }
}
