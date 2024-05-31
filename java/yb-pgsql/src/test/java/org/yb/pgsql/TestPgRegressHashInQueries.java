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

package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.yb.YBTestRunner;
import org.yb.util.RegexMatcher;

import java.sql.Connection;
import java.sql.Statement;

import java.util.HashSet;
import java.util.Set;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestPgRegressHashInQueries extends BasePgSQLTest {

  @Override
  protected Integer getYsqlRequestLimit() {
    // This is so number of roundtrips equals the number of request operators created.
    return 1;
  }

  @Test
  public void testInQueryBatchingOnHashKey() throws Exception {

    String createTable = "CREATE TABLE t1 (a int PRIMARY KEY, b int) SPLIT INTO 3 TABLETS";
    String insertTable = "INSERT INTO t1  SELECT i, i FROM (SELECT generate_series(1, 1024) i) t";

    try (Statement statement = connection.createStatement()) {
      statement.execute(createTable);
      statement.execute(insertTable);
    }

    // Generate select query required to run batched IN.
    // SELECT * FROM t1 WHERE a IN (1, 2, 3, .... 511, 512);
    int num_rows = 512;
    String query = "SELECT * FROM t1 WHERE a IN (";
    for(int i = 1; i < num_rows; ++i) {
      query += i + ", ";
    }
    query += num_rows + ")";
    Set<Row> expectedRows = new HashSet<>();
    for (int i = 1; i <= num_rows; i++) {
      expectedRows.add(new Row(i, i));
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute("SET yb_enable_hash_batch_in = false");
      long noBatchingNumRequests = getNumStorageRoundtrips(statement, query);
      assertEquals(512, noBatchingNumRequests);
      assertRowSet(statement, query, expectedRows);

      statement.execute("SET yb_enable_hash_batch_in = true");
      long batchingNumRequests = getNumStorageRoundtrips(statement, query);
      assertRowSet(statement, query, expectedRows);
      // We send three requests as the number of tablets created are three.
      assertEquals(3, batchingNumRequests);
    }
  }

  @Test
  public void testInQueryBatchingOnMixedKey() throws Exception {

    String createTable =
        "CREATE TABLE t1 (a int, b int, PRIMARY KEY(a hash, b asc)) SPLIT INTO 3 TABLETS";
    String insertTable1 =
        "INSERT INTO t1  SELECT i, i FROM (SELECT generate_series(1, 1024) i) t";
    String insertTable2 =
        "INSERT INTO t1  SELECT i, i+1 FROM (SELECT generate_series(1, 1024) i) t";

    try (Statement statement = connection.createStatement()) {
      statement.execute(createTable);
      statement.execute(insertTable1);
      statement.execute(insertTable2);
    }

    // Generate select query required to run batched IN.
    // SELECT * FROM t1 WHERE a IN (1, 2, 3, .... 511, 512);
    int upper_limit = 512;
    String query = "SELECT * FROM t1 WHERE a IN (";
    for(int i = 1; i < upper_limit; ++i) {
      query += i + ", ";
    }
    query += upper_limit + ") AND b IN (";

    for(int i = 1; i < upper_limit; ++i) {
      if ((i % 2) == 0) {
        query += i + ", ";
      }
    }
    query += upper_limit + ")";

    Set<Row> expectedRows = new HashSet<>();
    for (int i = 1; i <= upper_limit; i++) {
      if ((i % 2) == 1) {
        expectedRows.add(new Row(i, i+1));
      } else {
        expectedRows.add(new Row(i, i));
      }
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute("SET yb_enable_hash_batch_in = false");
      long noBatchingNumRequests = getNumStorageRoundtrips(statement, query);
      assertEquals(512, noBatchingNumRequests);
      assertRowSet(statement, query, expectedRows);

      statement.execute("SET yb_enable_hash_batch_in = true");
      long batchingNumRequests = getNumStorageRoundtrips(statement, query);
      assertRowSet(statement, query, expectedRows);
      // We send three requests as the number of tablets created are three.
      assertEquals(3, batchingNumRequests);
    }
  }

  @Test
  public void testInQueryBatchingNestLoopHashKey() throws Exception {
    String createTable1 = "CREATE TABLE x (a int PRIMARY KEY, b int) SPLIT INTO 3 TABLETS";
    String insertTable1 = "INSERT INTO x  SELECT i*2, i FROM (SELECT generate_series(1, 4096) i) t";
    String createTable2 = "CREATE TABLE y (a int PRIMARY KEY, b int) SPLIT INTO 3 TABLETS";
    String insertTable2 = "INSERT INTO y  SELECT i*5, i FROM (SELECT generate_series(1, 4096) i) t";

    try (Statement statement = connection.createStatement()) {
      statement.execute(createTable1);
      statement.execute(insertTable1);
      statement.execute(createTable2);
      statement.execute(insertTable2);
    }

    // Generate NL Join query and enable NL Join batching in it with different batch sizes.
    // These get automatically converted to batched IN queries. We should expect the best
    // performance when we enable IN batching.
    String query = "SELECT * FROM x t1 JOIN y t2 ON t1.a = t2.a";

    Set<Row> expectedRows = new HashSet<>();
    for (int i = 1; i <= 819; i++) {
      expectedRows.add(new Row(i*10, i*5, i*10, i*2));
    }

    try (Statement statement = connection.createStatement()) {
      // Enabling NL Join batching
      statement.execute("SET enable_hashjoin = off");
      statement.execute("SET enable_mergejoin = off");
      statement.execute("SET enable_seqscan = off");
      statement.execute("SET enable_material = off");

      statement.execute("SET yb_bnl_batch_size = 3;");
      statement.execute("SET yb_enable_hash_batch_in = false");
      long noBatchingSmallBatchSizeNumRPCs = getNumStorageRoundtrips(statement, query);
      assertEquals(4102, noBatchingSmallBatchSizeNumRPCs);
      assertRowSet(statement, query, expectedRows);

      statement.execute("SET yb_bnl_batch_size = 1024;");
      statement.execute("SET yb_enable_hash_batch_in = false");
      long noBatchingLargeBatchSizeNumRPCs = getNumStorageRoundtrips(statement, query);
      assertEquals(4102, noBatchingLargeBatchSizeNumRPCs);
      assertRowSet(statement, query, expectedRows);

      statement.execute("SET yb_bnl_batch_size = 1024;");
      statement.execute("SET yb_enable_hash_batch_in = true");
      long batchingLargeBatchSizeNumRPCs = getNumStorageRoundtrips(statement, query);
      assertEquals(12, batchingLargeBatchSizeNumRPCs);
      assertRowSet(statement, query, expectedRows);
    }
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest("yb_hash_in_schedule");
  }
}
