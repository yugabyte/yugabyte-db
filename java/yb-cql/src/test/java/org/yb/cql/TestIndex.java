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

import org.junit.BeforeClass;
import org.junit.Test;

import com.datastax.driver.core.ColumnDefinitions;
import com.datastax.driver.core.ColumnDefinitions.Definition;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSetFuture;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.TableMetadata;
import com.datastax.driver.core.exceptions.InvalidQueryException;
import org.yb.minicluster.MiniYBCluster;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.fail;

public class TestIndex extends BaseCQLTest {

  @BeforeClass
  public static void SetUpBeforeClass() throws Exception {
    BaseCQLTest.tserverArgs = Arrays.asList("--allow_index_table_read_write");
    BaseCQLTest.setUpBeforeClass();
  }

  @Test
  public void testReadWriteIndexTable() throws Exception {
    // TODO: remove this test case and the "allow_index_table_read_write" flag above after
    // secondary index feature is complete.
    session.execute("create table test_index (h int, r1 int, r2 int, c int, " +
                    "primary key ((h), r1, r2)) with transactions = { 'enabled' : true};");
    session.execute("create index i on test_index (h, r2, r1) covering (c);");

    session.execute("insert into test_index (h, r1, r2, c) values (1, 2, 3, 4);");
    session.execute("insert into i (h, r2, r1, c) values (1, 3, 2, 4);");
    assertQuery("select * from test_index;", "Row[1, 2, 3, 4]");
    assertQuery("select * from i;", "Row[1, 3, 2, 4]");
  }

  @Test
  public void testCreateIndex() throws Exception {

    // Create test table.
    session.execute("create table test_create_index " +
                    "(h1 int, h2 text, r1 int, r2 text, " +
                    "c1 int, c2 text, c3 decimal, c4 timestamp, " +
                    "primary key ((h1, h2), r1, r2)) " +
                    "with transactions = {'enabled' : true};");

    // Create test indexes with range and non-primary-key columns.
    session.execute("create index i1 on test_create_index (r1, r2) covering (c1, c4);");
    session.execute("create index i2 on test_create_index (c4) covering (c1, c2);");

    // Wait to ensure the partitions metadata was updated.
    // Schema change should trigger a refresh but playing it safe in case debouncer will delay it.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);

    // Verify the indexes.
    TableMetadata table = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE)
                          .getTable("test_create_index");
    assertEquals("CREATE INDEX i1 ON cql_test_keyspace.test_create_index (r1, r2, h1, h2);",
                 table.getIndex("i1").asCQLQuery());
    assertEquals("CREATE INDEX i2 ON cql_test_keyspace.test_create_index (c4, h1, h2, r1, r2);",
                 table.getIndex("i2").asCQLQuery());

    // Verify the covering columns.
    assertIndexOptions("test_create_index", "i1",
                       "Row[{target=r1, r2, h1, h2, covering=c1, c4}]");
    assertIndexOptions("test_create_index", "i2",
                       "Row[{target=c4, h1, h2, r1, r2, covering=c1, c2}]");

    // Test retrieving non-existent index.
    assertNull(table.getIndex("i3"));

    // Test create index with duplicate name.
    try {
      session.execute("create index i1 on test_create_index (r1) covering (c1);");
      fail("Duplicate index created");
    } catch (InvalidQueryException e) {
      LOG.info("Expected exception " + e.getMessage());
    }

    // Test create index on non-existent table.
    try {
      session.execute("create index i3 on non_existent_table (k) covering (v);");
      fail("Index on non-existent table created");
    } catch (InvalidQueryException e) {
      LOG.info("Expected exception " + e.getMessage());
    }

    // Test create index if not exists. Verify i1 is still the same.
    session.execute("create index if not exists i1 on test_create_index (r1) covering (c1);");
    assertIndexOptions("test_create_index", "i1", "Row[{target=r1, r2, h1, h2, covering=c1, c4}]");

    // Create another test table.
    session.execute("create table test_create_index_2 " +
                    "(h1 int, h2 text, r1 int, r2 text, " +
                    "c1 int, c2 text, c3 decimal, c4 timestamp, " +
                    "primary key ((h1, h2), r1, r2)) " +
                    "with transactions = {'enabled' : true};");

    // Test create index by the same name on another table.
    try {
      session.execute("create index i1 on test_create_index_2 (r1, r2) covering (c1, c4);");
      fail("Index by the same name created on another table");
    } catch (InvalidQueryException e) {
      LOG.info("Expected exception " + e.getMessage());
    }
  }

  @Test
  public void testCreateIndexWithClusteringOrder() throws Exception {

    // Create test table.
    session.execute("create table test_clustering_index " +
                    "(h1 int, h2 text, r1 int, r2 text, " +
                    "c1 int, c2 text, c3 decimal, c4 timestamp, c5 uuid, " +
                    "primary key ((h1, h2), r1, r2)) " +
                    "with transactions = {'enabled' : true};");

    // Create test indexes with range and non-primary-key columns.
    session.execute("create index i1 on test_clustering_index (r1, r2, c1, c2) " +
                    "with clustering order by (r2 desc, c1 asc, c2 desc) covering (c3, c4);");
    session.execute("create index i2 on test_clustering_index ((c1, c2), c3, c4) " +
                    "with clustering order by (c3 asc, c4 desc) covering (c5);");

    // Wait to ensure the partitions metadata was updated.
    // Schema change should trigger a refresh but playing it safe in case debouncer will delay it.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);

    // Verify the indexes.
    TableMetadata table = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE)
                          .getTable("test_clustering_index");
    assertEquals("CREATE INDEX i1 ON cql_test_keyspace.test_clustering_index " +
                 "(r1, r2, c1, c2, h1, h2);", table.getIndex("i1").asCQLQuery());
    assertEquals("CREATE INDEX i2 ON cql_test_keyspace.test_clustering_index " +
                 "((c1, c2), c3, c4, h1, h2, r1, r2);", table.getIndex("i2").asCQLQuery());

    assertIndexColumns("i1",
                       "Row[r1, none, partition_key, 0, int]" +
                       "Row[r2, desc, clustering, 0, text]" +
                       "Row[c1, asc, clustering, 1, int]" +
                       "Row[c2, desc, clustering, 2, text]" +
                       "Row[h1, asc, clustering, 3, int]" +
                       "Row[h2, asc, clustering, 4, text]" +
                       "Row[c3, none, regular, -1, decimal]" +
                       "Row[c4, none, regular, -1, timestamp]");
    assertIndexColumns("i2",
                       "Row[c1, none, partition_key, 0, int]" +
                       "Row[c2, none, partition_key, 1, text]" +
                       "Row[c3, asc, clustering, 0, decimal]" +
                       "Row[c4, desc, clustering, 1, timestamp]" +
                       "Row[h1, asc, clustering, 2, int]" +
                       "Row[h2, asc, clustering, 3, text]" +
                       "Row[r1, asc, clustering, 4, int]" +
                       "Row[r2, asc, clustering, 5, text]" +
                       "Row[c5, none, regular, -1, uuid]");
  }

  @Test
  public void testDropIndex() throws Exception {

    // Create test table.
    session.execute("create table test_drop_index " +
                    "(h1 int, h2 text, r1 int, r2 text, " +
                    "c1 int, c2 text, c3 decimal, c4 timestamp, " +
                    "primary key ((h1, h2), r1, r2)) " +
                    "with transactions = {'enabled' : true};");

    // Create test index.
    session.execute("create index i1 on test_drop_index (r1, r2) covering (c1, c2);");

    // Wait to ensure the partitions metadata was updated.
    // Schema change should trigger a refresh but playing it safe in case debouncer will delay it.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);

    // Verify the index.
    TableMetadata table = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE)
                          .getTable("test_drop_index");
    assertEquals("CREATE INDEX i1 ON cql_test_keyspace.test_drop_index (r1, r2, h1, h2);",
                 table.getIndex("i1").asCQLQuery());
    assertIndexOptions("test_drop_index", "i1", "Row[{target=r1, r2, h1, h2, covering=c1, c2}]");

    // Drop test index.
    session.execute("drop index i1;");

    // Wait to ensure the partitions metadata was updated.
    // Schema change should trigger a refresh but playing it safe in case debouncer will delay it.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);

    table = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getTable("test_drop_index");
    assertNull(table.getIndex("i1"));
    assertNull(session.execute("select options from system_schema.indexes " +
                               "where keyspace_name = ? and table_name = ? and index_name = ?",
                               DEFAULT_TEST_KEYSPACE, "test_drop_index", "i1").one());

    // Create another test index by the same name. Verify new index is created.
    session.execute("create index i1 on test_drop_index (c1, c2) covering (c3, c4);");

    // Wait to ensure the partitions metadata was updated.
    // Schema change should trigger a refresh but playing it safe in case debouncer will delay it.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);

    table = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getTable("test_drop_index");
    assertEquals("CREATE INDEX i1 ON cql_test_keyspace.test_drop_index (c1, c2, h1, h2, r1, r2);",
                 table.getIndex("i1").asCQLQuery());
    assertIndexOptions("test_drop_index", "i1",
                       "Row[{target=c1, c2, h1, h2, r1, r2, covering=c3, c4}]");
  }

  @Test
  public void testDropTableCascade() throws Exception {

    // Create test table.
    session.execute("create table test_drop_cascade " +
                    "(h1 int, h2 text, r1 int, r2 text, " +
                    "c1 int, c2 text, c3 decimal, c4 timestamp, " +
                    "primary key ((h1, h2), r1, r2)) " +
                    "with transactions = {'enabled' : true};");

    // Create test index.
    session.execute("create index i1 on test_drop_cascade (r1, r2) covering (c1, c2);");
    session.execute("create index i2 on test_drop_cascade (c4) covering (c1);");
    assertIndexOptions("test_drop_cascade", "i1", "Row[{target=r1, r2, h1, h2, covering=c1, c2}]");
    assertIndexOptions("test_drop_cascade", "i2", "Row[{target=c4, h1, h2, r1, r2, covering=c1}]");

    // Drop test table. Verify the index is cascade-deleted.
    session.execute("drop table test_drop_cascade;");
    assertNull(session.execute("select options from system_schema.indexes " +
                               "where keyspace_name = ? and table_name = ? and index_name = ?",
                               DEFAULT_TEST_KEYSPACE, "test_drop_cascade", "i1").one());
    assertNull(session.execute("select options from system_schema.indexes " +
                               "where keyspace_name = ? and table_name = ? and index_name = ?",
                               DEFAULT_TEST_KEYSPACE, "test_drop_cascade", "i2").one());
  }

  private void assertIndexOptions(String table, String index, String options) throws Exception {
    Row row = session.execute("select options from system_schema.indexes " +
                              "where keyspace_name = ? and table_name = ? and index_name = ?",
                              DEFAULT_TEST_KEYSPACE, table, index).one();
    assertEquals(options, row.toString());
  }

  private void assertIndexColumns(String index, String columns) throws Exception {
    String actual = "";
    for (Row row : session.execute("select column_name, clustering_order, kind, position, type " +
                                   "from system_schema.columns " +
                                   "where keyspace_name = ? and table_name = ?",
                                   DEFAULT_TEST_KEYSPACE, index)) {
      actual += row.toString();
    }
    assertEquals(columns, actual);
  }

  Map<String, String> columnMap;

  @Test
  public void testIndexUpdate() throws Exception {
    // Create test table and indexes.
    session.execute("create table test_update " +
                    "(h1 int, h2 text, r1 int, r2 text, c1 int, c2 text, " +
                    "primary key ((h1, h2), r1, r2)) " +
                    "with transactions = {'enabled' : true};");
    session.execute("create index i1 on test_update (h1);");
    session.execute("create index i2 on test_update ((r1, r2)) covering (c2);");
    session.execute("create index i3 on test_update (r2, r1) covering (c1, c2);");
    session.execute("create index i4 on test_update (c1);");
    session.execute("create index i5 on test_update (c2) covering (c1);");

    columnMap = new HashMap<String, String>() {{
        put("i1", "h1, h2, r1, r2");
        put("i2", "r1, r2, h1, h2, c2");
        put("i3", "r2, r1, h1, h2, c1, c2");
        put("i4", "c1, h1, h2, r1, r2");
        put("i5", "c2, h1, h2, r1, r2, c1");
      }};

    // test_update: Row[1, a, 2, b, 3]
    assertIndexUpdate("insert into test_update (h1, h2, r1, r2, c1) values (1, 'a', 2, 'b', 3);");

    // test_update: Row[1, a, 2, b, 3, c]
    assertIndexUpdate("update test_update set c2 = 'c' " +
                      "where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';");

    // test_update: Row[1, a, 2, b, 4, d]
    assertIndexUpdate("update test_update set c1 = 4, c2 = 'd' " +
                      "where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';");

    // test_update: Row[1, a, 2, b, 4, e]
    assertIndexUpdate("update test_update set c2 = 'e' " +
                      "where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';");

    // test_update: Row[1, a, 2, b, 4, e]
    //              Row[1, a, 12, bb, 6]
    assertIndexUpdate("insert into test_update (h1, h2, r1, r2, c1) values (1, 'a', 12, 'bb', 6);");

    // test_update: Row[1, a, 12, bb, 6]
    assertIndexUpdate("delete from test_update " +
                      "where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';");

    // test_update: empty
    assertIndexUpdate("delete from test_update where h1 = 1 and h2 = 'a';");

    // test_update: Row[11, aa, 22, bb, 3]
    assertIndexUpdate("update test_update set c1 = 3 " +
                      "where h1 = 11 and h2 = 'aa' and r1 = 22 and r2 = 'bb';");

    // test_update: empty
    assertIndexUpdate("update test_update set c1 = null " +
                      "where h1 = 11 and h2 = 'aa' and r1 = 22 and r2 = 'bb';");

    // test_update: Row[11, aa, 222, bbb, 3, c]
    assertIndexUpdate("update test_update set c1 = 3, c2 = 'c' " +
                      "where h1 = 11 and h2 = 'aa' and r1 = 222 and r2 = 'bbb';");

    // test_update: empty
    assertIndexUpdate("delete c1, c2 from test_update " +
                      "where h1 = 11 and h2 = 'aa' and r1 = 222 and r2 = 'bbb';");
  }

  private Set<String> queryTable(String table, String columns) {
    Set<String> rows = new HashSet<String>();
    for (Row row : session.execute(String.format("select %s from %s;", columns, table))) {
      rows.add(row.toString());
    }
    return rows;
  }

  private void assertIndexUpdate(String query) throws Exception {
    session.execute(query);
    for (Map.Entry<String, String> entry : columnMap.entrySet()) {
      assertEquals("Index " + entry.getKey() + " after " + query,
                   queryTable("test_update", entry.getValue()),
                   queryTable(entry.getKey(), entry.getValue()));
    }
  }


  private void assertRoutingVariables(String query,
                                      List<String> expectedVars,
                                      Object[] values,
                                      String expectedRow) {
    PreparedStatement stmt = session.prepare(query);
    int hashIndexes[] = stmt.getRoutingKeyIndexes();
    if (expectedVars == null) {
      assertNull(hashIndexes);
    } else {
      List<String> actualVars = new Vector<String>();
      ColumnDefinitions vars = stmt.getVariables();
      for (int hashIndex : hashIndexes) {
        actualVars.add(vars.getTable(hashIndex) + "." + vars.getName(hashIndex));
      }
      assertEquals(expectedVars, actualVars);
    }
    assertEquals(expectedRow, session.execute(stmt.bind(values)).one().toString());
  }

  @Test
  public void testPreparedStatement() throws Exception {
    // Create test table and indexes.
    session.execute("create table test_prepare " +
                    "(h1 int, h2 text, r1 int, r2 text, c1 int, c2 text, " +
                    "primary key ((h1, h2), r1, r2)) " +
                    "with transactions = {'enabled' : true};");
    session.execute("create index i1 on test_prepare (h1);");
    session.execute("create index i2 on test_prepare ((r1, r2));");
    session.execute("create index i3 on test_prepare (r2, r1);");
    session.execute("create index i4 on test_prepare (c1);");
    session.execute("create index i5 on test_prepare (c2) covering (c1);");

    // Insert a row.
    session.execute("insert into test_prepare (h1, h2, r1, r2, c1, c2) " +
                    "values (1, 'a', 2, 'b', 3, 'c');");

    // Select using index i1.
    assertRoutingVariables("select h1, h2, r1, r2 from test_prepare where h1 = ?;",
                           Arrays.asList("i1.h1"),
                           new Object[] {Integer.valueOf(1)},
                           "Row[1, a, 2, b]");

    // Select using base table because i1 does not cover c1.
    assertRoutingVariables("select h1, h2, r1, r2, c1 from test_prepare where h1 = ?;",
                           null,
                           new Object[] {Integer.valueOf(1)},
                           "Row[1, a, 2, b, 3]");

    // Select using base table because there is no index on h2.
    assertRoutingVariables("select h1, h2, r1, r2 from test_prepare where h2 = ?;",
                           null,
                           new Object[] {"a"},
                           "Row[1, a, 2, b]");

    // Select using index i2 because (r1, r2) is more selective than i1 alone.
    assertRoutingVariables("select h1, h2, r1, r2 from test_prepare " +
                           "where h1 = ? and r1 = ? and r2 = ?;",
                           Arrays.asList("i2.r1", "i2.r2"),
                           new Object[] {Integer.valueOf(1), Integer.valueOf(2), "b"},
                           "Row[1, a, 2, b]");

    // Select using index i3.
    assertRoutingVariables("select h1, h2, r1, r2 from test_prepare where h2 = ? and r2 = ?;",
                           Arrays.asList("i3.r2"),
                           new Object[] {"a", "b"},
                           "Row[1, a, 2, b]");

    // Select using index i4.
    assertRoutingVariables("select h1, h2, r1, r2 from test_prepare where h2 = ? and c1 = ?;",
                           Arrays.asList("i4.c1"),
                           new Object[] {"a", Integer.valueOf(3)},
                           "Row[1, a, 2, b]");

    // Select using index i5 covering c1.
    assertRoutingVariables("select h1, h2, r1, r2, c1, c2 from test_prepare where c2 = ?;",
                           Arrays.asList("i5.c2"),
                           new Object[] {"c"},
                           "Row[1, a, 2, b, 3, c]");
  }

  @Test
  public void testRestarts() throws Exception {

    // Test concurrent inserts into a table with secondary index, which require a read of the
    // table in order to update the index. For consistent update of the index, the read and write
    // operations are executed in a distributed transaction. With multiple writes happening in
    // parallel, some reads may require a restart. Verify that there are restarts and the inserts
    // are retried without error.
    session.execute("create table test_restart (k int primary key, v int) " +
                    "with transactions = {'enabled' : true};");
    session.execute("create index test_restart_by_v on test_restart (v);");

    PreparedStatement insertStmt = session.prepare(
        "insert into test_restart (k, v) values (1, 1000);");

    final int PARALLEL_WRITE_COUNT = 5;

    int initialRestarts = getRestartsCount("test_restart");
    int initialRetries = getRetriesCount();
    LOG.info("Initial restarts = {}, retries = {}", initialRestarts, initialRetries);

    while (true) {
      Set<ResultSetFuture> results = new HashSet<ResultSetFuture>();
      for (int i = 0; i < PARALLEL_WRITE_COUNT; i++) {
        results.add(session.executeAsync(insertStmt.bind()));
      }
      for (ResultSetFuture result : results) {
        result.get();
      }
      int currentRestarts = getRestartsCount("test_restart");
      int currentRetries = getRetriesCount();
      LOG.info("Current restarts = {}, retries = {}", currentRestarts, currentRetries);
      if (currentRestarts > initialRestarts && currentRetries > initialRetries)
        break;
    }

    // Also verify that the rows are inserted indeed.
    assertQuery("select k, v from test_restart", "Row[1, 1000]");
  }
}
