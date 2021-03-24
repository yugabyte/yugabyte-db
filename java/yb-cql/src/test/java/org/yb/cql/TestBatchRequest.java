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
import com.datastax.driver.core.SimpleStatement;
import com.datastax.driver.core.exceptions.NoHostAvailableException;

import org.yb.minicluster.IOMetrics;
import org.yb.minicluster.MiniYBDaemon;

import org.junit.Test;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

import java.util.Map;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestBatchRequest extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestBatchRequest.class);

  @Override
  public int getTestMethodTimeoutSec() {
    // No need to adjust for TSAN vs. non-TSAN here, it will be done automatically.
    return 180;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("allow_index_table_read_write", "true");
    return flagMap;
  }

  @Test
  public void testPreparedStatement() throws Exception {
    LOG.info("Setup table.");
    setupTable("t", 0 /* num_rows */);

    LOG.info("Test batch of prepared statements.");
    PreparedStatement stmt = session.prepare("INSERT INTO t (h1, h2, r1, r2, v1, v2) " +
                                             "VALUES (?, ?, ?, ?, ?, ?);");
    BatchStatement batch = new BatchStatement();
    for (int i = 1; i <= 10; i++) {
      batch.add(stmt.bind(new Integer(i), "h" + i,
                          new Integer(i), "r" + i,
                          new Integer(i), "v" + i));
    }
    session.execute(batch);

    assertQueryRowsUnordered("SELECT * FROM t",
        "Row[1, h1, 1, r1, 1, v1]",
        "Row[2, h2, 2, r2, 2, v2]",
        "Row[3, h3, 3, r3, 3, v3]",
        "Row[4, h4, 4, r4, 4, v4]",
        "Row[5, h5, 5, r5, 5, v5]",
        "Row[6, h6, 6, r6, 6, v6]",
        "Row[7, h7, 7, r7, 7, v7]",
        "Row[8, h8, 8, r8, 8, v8]",
        "Row[9, h9, 9, r9, 9, v9]",
        "Row[10, h10, 10, r10, 10, v10]");
  }

  @Test
  public void testSimpleStatement() throws Exception {
    LOG.info("Setup table.");
    setupTable("t", 0 /* num_rows */);

    LOG.info("Test batch of regular statements.");
    BatchStatement batch = new BatchStatement();
    for (int i = 1; i <= 10; i++) {
      batch.add(new SimpleStatement(String.format("INSERT INTO t (h1, h2, r1, r2, v1, v2) " +
                                                  "VALUES (%d, 'h%d', %d, 'r%d', %d, 'v%d');",
                                                  i, i, i, i, i, i)));
    }
    session.execute(batch);

    assertQueryRowsUnordered("SELECT * FROM t",
        "Row[1, h1, 1, r1, 1, v1]",
        "Row[2, h2, 2, r2, 2, v2]",
        "Row[3, h3, 3, r3, 3, v3]",
        "Row[4, h4, 4, r4, 4, v4]",
        "Row[5, h5, 5, r5, 5, v5]",
        "Row[6, h6, 6, r6, 6, v6]",
        "Row[7, h7, 7, r7, 7, v7]",
        "Row[8, h8, 8, r8, 8, v8]",
        "Row[9, h9, 9, r9, 9, v9]",
        "Row[10, h10, 10, r10, 10, v10]");
  }

  @Test
  public void testDMLInBatchWith2Indexes() throws Exception {
    session.execute("create table test_batch (k int primary key, v1 int, v2 int) " +
                    "with transactions = {'enabled' : true};");
    session.execute("create index test_batch_by_v1 on test_batch (v1);");
    session.execute("create index test_batch_by_v2 on test_batch (v2);");

    BatchStatement batch = new BatchStatement();
    batch.add(new SimpleStatement("insert into test_batch (k, v1) values (1, 101);"));
    batch.add(new SimpleStatement("insert into test_batch (k, v2) values (1, 201);"));
    session.execute(batch);

    assertQuery("select * from test_batch;", "Row[1, 101, 201]");
    assertQuery("select * from test_batch_by_v1;", "Row[1, 101]");
    assertQuery("select * from test_batch_by_v2;", "Row[1, 201]");
    // Verify rows can be selected by the index columns.
    assertQuery("select * from test_batch where v1 = 101;", "Row[1, 101, 201]");
    assertQuery("select * from test_batch where v2 = 201;", "Row[1, 101, 201]");
  }

  @Test
  public void testMixedStatements() throws Exception {
    LOG.info("Setup table.");
    setupTable("t", 0 /* num_rows */);

    LOG.info("Test batch of mixed statements.");
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

    assertQueryRowsUnordered("SELECT * FROM t",
        "Row[1, h1, 1, r1, 1, v1]",
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
        "Row[5, h5, 3, r3, 3, v3]");
  }

  @Test
  public void testInvalidStatement() throws Exception {
    LOG.info("Setup table.");
    setupTable("t", 0 /* num_rows */);

    LOG.info("Test mix of valid and invalid statements. Expect error.");
    BatchStatement batch = new BatchStatement();
    batch.add(new SimpleStatement("INSERT INTO t (h1, h2, r1, r2, v1, v2) " +
                                  "VALUES (1, 'h1', 1, 'r1', 1, 'v1');"));
    batch.add(new SimpleStatement("INSERT INTO t (h1, h2, v1, v2) " +
                                  "VALUES (1, 'h1', 1, 'v1');"));
    runInvalidStmt(batch);

    LOG.info("Batch statement guarantees either all statements are executed or none is.");
    LOG.info("Should return no rows.");
    LOG.info("TODO: add test for batch writes across hash keys.");
    assertQuery("SELECT * from t", "");
  }

  @Test
  public void testHashKeyBatch() throws Exception {
    LOG.info("Setup table.");
    setupTable("t", 0 /* num_rows */);

    LOG.info("Get the initial metrics.");
    Map<MiniYBDaemon, IOMetrics> initialMetrics = getTSMetrics();

    LOG.info("Test batch of prepared statements.");
    PreparedStatement stmt = session.prepare("INSERT INTO t (h1, h2, r1, r2, v1, v2) " +
                                             "VALUES (?, ?, ?, ?, ?, ?);");
    BatchStatement batch = new BatchStatement();
    final int NUM_HASH_KEYS = 3;
    for (int i = 1; i <= NUM_HASH_KEYS; i++) {
      for (int j = 1; j <= 5; j++) {
        batch.add(stmt.bind(new Integer(i), "h" + i,
                            new Integer(j), "r" + j,
                            new Integer(j), "v" + j));
      }
    }
    session.execute(batch);

    LOG.info("Check the metrics again.");
    IOMetrics totalMetrics = getCombinedMetrics(initialMetrics);

    LOG.info("Verify writes are batched by the hash keys. So the total writes should equal just ");
    LOG.info("the number of hash keys, not number of rows inserted.");
    LOG.info("localWriteCount=" + totalMetrics.localWriteCount);
    LOG.info("remoteWriteCount=" + totalMetrics.remoteWriteCount);
    assertEquals(miniCluster.getNumShardsPerTserver(),
                 totalMetrics.localWriteCount + totalMetrics.remoteWriteCount);

    LOG.info("Verify the rows are inserted.");
    assertQueryRowsUnordered("SELECT * FROM t",
        "Row[1, h1, 1, r1, 1, v1]",
        "Row[1, h1, 2, r2, 2, v2]",
        "Row[1, h1, 3, r3, 3, v3]",
        "Row[1, h1, 4, r4, 4, v4]",
        "Row[1, h1, 5, r5, 5, v5]",
        "Row[3, h3, 1, r1, 1, v1]",
        "Row[3, h3, 2, r2, 2, v2]",
        "Row[3, h3, 3, r3, 3, v3]",
        "Row[3, h3, 4, r4, 4, v4]",
        "Row[3, h3, 5, r5, 5, v5]",
        "Row[2, h2, 1, r1, 1, v1]",
        "Row[2, h2, 2, r2, 2, v2]",
        "Row[2, h2, 3, r3, 3, v3]",
        "Row[2, h2, 4, r4, 4, v4]",
        "Row[2, h2, 5, r5, 5, v5]");
  }

  @Test
  public void testRecreateTable1() throws Exception {

    LOG.info("Test executing batch statements with recreated tables to verify metadata cache is");
    LOG.info("flushed to handle new table definition.");
    for (int t = 0; t < 3; t++) {

      // Create test table.
      session.execute("CREATE TABLE test_batch (h INT, r TEXT, PRIMARY KEY ((h), r));");

      // Test batch of prepared statements.
      PreparedStatement stmt = session.prepare("INSERT INTO test_batch (h, r) VALUES (?, ?);");
      BatchStatement batch = new BatchStatement();
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 5; j++) {
          batch.add(stmt.bind(Integer.valueOf(i), "r" + j));
        }
      }
      session.execute(batch);

      // Verify the rows are inserted.
      assertQueryRowsUnordered("SELECT * FROM test_batch",
          "Row[2, r1]",
          "Row[2, r2]",
          "Row[2, r3]",
          "Row[2, r4]",
          "Row[2, r5]",
          "Row[3, r1]",
          "Row[3, r2]",
          "Row[3, r3]",
          "Row[3, r4]",
          "Row[3, r5]",
          "Row[1, r1]",
          "Row[1, r2]",
          "Row[1, r3]",
          "Row[1, r4]",
          "Row[1, r5]");

      // Drop test table.
      session.execute("DROP TABLE test_batch;");
    }
  }

  @Test
  public void testRecreateTable2() throws Exception {
    for (int t = 0; t < 3; t++) {
      {
        // Create test table.
        session.execute("CREATE TABLE test_batch (h INT, r BIGINT, PRIMARY KEY ((h), r));");

        // Test batch of prepared statements.
        BatchStatement batch = new BatchStatement();
        for (int i = 1; i <= 3; i++) {
          for (int j = 1; j <= 5; j++) {
            batch.add(
                new SimpleStatement(String.format(
                    "INSERT INTO test_batch (h, r) VALUES (%d, %d);", i, j)));
          }
        }
        session.execute(batch);

        // Verify the rows are inserted.
        assertQueryRowsUnordered("SELECT * FROM test_batch",
            "Row[2, 1]",
            "Row[2, 2]",
            "Row[2, 3]",
            "Row[2, 4]",
            "Row[2, 5]",
            "Row[3, 1]",
            "Row[3, 2]",
            "Row[3, 3]",
            "Row[3, 4]",
            "Row[3, 5]",
            "Row[1, 1]",
            "Row[1, 2]",
            "Row[1, 3]",
            "Row[1, 4]",
            "Row[1, 5]");

        // Drop test table.
        session.execute("DROP TABLE test_batch;");
      }

      {
        // Create test table.
        session.execute("CREATE TABLE test_batch (h INT, r TEXT, PRIMARY KEY ((h), r));");

        // Test batch of prepared statements.
        BatchStatement batch = new BatchStatement();
        for (int i = 1; i <= 3; i++) {
          for (int j = 1; j <= 5; j++) {
            batch.add(
                new SimpleStatement(
                    "INSERT INTO test_batch (h, r) VALUES (?, ?);", Integer.valueOf(i), "R" + j));
          }
        }
        session.execute(batch);

        // Verify the rows are inserted.
        assertQueryRowsUnordered("SELECT * FROM test_batch","Row[2, R1]",
            "Row[2, R2]",
            "Row[2, R3]",
            "Row[2, R4]",
            "Row[2, R5]",
            "Row[3, R1]",
            "Row[3, R2]",
            "Row[3, R3]",
            "Row[3, R4]",
            "Row[3, R5]",
            "Row[1, R1]",
            "Row[1, R2]",
            "Row[1, R3]",
            "Row[1, R4]",
            "Row[1, R5]");

        // Drop test table.
        session.execute("DROP TABLE test_batch;");
      }

      {
        // Recreate test table with same name but different schema to verify metadata cache is
        // flushed to handle new table definition.
        session.execute("CREATE TABLE test_batch (h INT, r INT, PRIMARY KEY ((h), r));");

        // Test batch of prepared statements.
        BatchStatement batch = new BatchStatement();
        for (int i = 1; i <= 3; i++) {
          for (int j = 1; j <= 5; j++) {
            batch.add(
                new SimpleStatement(String.format(
                    "INSERT INTO test_batch (h, r) VALUES (%d, %d);", i, j)));
          }
        }
        session.execute(batch);

        // Verify the rows are inserted.
        assertQueryRowsUnordered("SELECT * FROM test_batch",
            "Row[2, 1]",
            "Row[2, 2]",
            "Row[2, 3]",
            "Row[2, 4]",
            "Row[2, 5]",
            "Row[3, 1]",
            "Row[3, 2]",
            "Row[3, 3]",
            "Row[3, 4]",
            "Row[3, 5]",
            "Row[1, 1]",
            "Row[1, 2]",
            "Row[1, 3]",
            "Row[1, 4]",
            "Row[1, 5]");

        // Drop test table.
        session.execute("DROP TABLE test_batch;");
      }
    }
  }

  private void runInvalidPreparedBatchStmt(BatchStatement stmt) {
    try {
      session.execute(stmt);
      fail(String.format("Statement did not fail: %s", stmt));
    } catch (NoHostAvailableException e) {
      // NoHostAvailableException is raised when a previously prepared statement fails to
      // reprepare again.
      LOG.info("Expected exception", e);
    }
  }

  @Test
  public void testBatchWithErrors() throws Exception {

    {
      // Create test table.
      session.execute("CREATE TABLE test_batch_error (h INT, r INT, PRIMARY KEY ((h), r));");

      // Test batch of prepared statements.
      PreparedStatement stmt =
          session.prepare("INSERT INTO test_batch_error (h, r) VALUES (?, ?);");
      BatchStatement batch = new BatchStatement();
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 5; j++) {
          batch.add(stmt.bind(Integer.valueOf(i), Integer.valueOf(j)));
        }
      }
      session.execute(batch);

      // Verify the rows are inserted.
      assertQueryRowsUnordered("SELECT * FROM test_batch_error",
          "Row[2, 1]",
          "Row[2, 2]",
          "Row[2, 3]",
          "Row[2, 4]",
          "Row[2, 5]",
          "Row[3, 1]",
          "Row[3, 2]",
          "Row[3, 3]",
          "Row[3, 4]",
          "Row[3, 5]",
          "Row[1, 1]",
          "Row[1, 2]",
          "Row[1, 3]",
          "Row[1, 4]",
          "Row[1, 5]");

      // Drop test table.
      session.execute("DROP TABLE test_batch_error;");

      // Rerun the batch. Verify that error is raised.
      runInvalidPreparedBatchStmt(batch);
    }

    {
      // Create test table.
      session.execute("CREATE TABLE test_batch_error (h INT, r INT, PRIMARY KEY ((h), r));");

      // Test batch of simple statements.
      BatchStatement batch = new BatchStatement();
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 5; j++) {
          batch.add(
              new SimpleStatement(
                  "INSERT INTO test_batch_error (h, r) VALUES (?, ?);",
                  Integer.valueOf(i), Integer.valueOf(j)));
        }
      }
      session.execute(batch);

      // Verify the rows are inserted.
      assertQueryRowsUnordered("SELECT * FROM test_batch_error",
          "Row[2, 1]",
          "Row[2, 2]",
          "Row[2, 3]",
          "Row[2, 4]",
          "Row[2, 5]",
          "Row[3, 1]",
          "Row[3, 2]",
          "Row[3, 3]",
          "Row[3, 4]",
          "Row[3, 5]",
          "Row[1, 1]",
          "Row[1, 2]",
          "Row[1, 3]",
          "Row[1, 4]",
          "Row[1, 5]");

      // Drop test table.
      session.execute("DROP TABLE test_batch_error;");

      // Rerun the batch. Verify that error is raised.
      runInvalidStmt(batch);
    }
  }

  @Test
  public void testReadWriteSameRow() throws Exception {

    LOG.info("Create test table.");
    session.execute("CREATE TABLE test_batch (h INT, r TEXT, v INT, PRIMARY KEY ((h), r));");

    LOG.info("Test writing to the same row twice.");
    BatchStatement batch = new BatchStatement();
    batch.add(new SimpleStatement("INSERT INTO test_batch (h, r, v) VALUES (1, 'r1', 11);"));
    batch.add(new SimpleStatement("INSERT INTO test_batch (h, r, v) VALUES (1, 'r2', 12);"));
    batch.add(new SimpleStatement("INSERT INTO test_batch (h, r, v) VALUES (1, 'r2', 13);"));
    session.execute(batch);


    LOG.info("Create test table with counter.");
    session.execute("CREATE TABLE test_counter (h INT, r TEXT, c COUNTER, PRIMARY KEY ((h), r));");

    LOG.info("Test reading and writing different rows are allowed in the same batch.");
    batch.clear();
    batch.add(new SimpleStatement("UPDATE test_counter SET c = c + 1 WHERE h = 1 and r = 'r1';"));
    batch.add(new SimpleStatement("UPDATE test_counter SET c = c + 1 WHERE h = 1 and r = 'r2';"));
    session.execute(batch);

    LOG.info("Verify the rows are inserted.");
    assertQueryRowsUnordered("SELECT * FROM test_counter",
        "Row[1, r1, 1]",
        "Row[1, r2, 1]");

    LOG.info("Test reading and writing the same rows (new and existing).");
    batch.clear();
    batch.add(new SimpleStatement("UPDATE test_counter SET c = c + 2 WHERE h = 1 and r = 'r1';"));
    batch.add(new SimpleStatement("UPDATE test_counter SET c = c + 3 WHERE h = 1 and r = 'r1';"));
    batch.add(new SimpleStatement("UPDATE test_counter SET c = c + 4 WHERE h = 1 and r = 'r2';"));
    batch.add(new SimpleStatement("UPDATE test_counter SET c = c + 5 WHERE h = 1 and r = 'r1';"));
    batch.add(new SimpleStatement("UPDATE test_counter SET c = c + 6 WHERE h = 1 and r = 'r3';"));
    batch.add(new SimpleStatement("UPDATE test_counter SET c = c + 7 WHERE h = 1 and r = 'r2';"));
    batch.add(new SimpleStatement("UPDATE test_counter SET c = c + 8 WHERE h = 1 and r = 'r3';"));
    session.execute(batch);

    LOG.info("Verify the counter columns are updated properly.");
    assertQueryRowsUnordered("SELECT * FROM test_counter",
        "Row[1, r1, 11]",
        "Row[1, r2, 12]",
        "Row[1, r3, 14]");
  }
}
