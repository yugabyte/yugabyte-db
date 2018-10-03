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

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.ColumnDefinitions;
import com.datastax.driver.core.ColumnDefinitions.Definition;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSetFuture;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.TableMetadata;
import com.datastax.driver.core.exceptions.InvalidQueryException;
import org.yb.minicluster.MiniYBCluster;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
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
    session.execute("create index i on test_index (h, r2, r1) include (c);");

    session.execute("insert into test_index (h, r1, r2, c) values (1, 2, 3, 4);");
    session.execute("insert into i (h, r2, r1, c) values (1, 3, 2, 4);");
    assertQuery("select * from test_index;", "Row[1, 2, 3, 4]");
    assertQuery("select * from i;", "Row[1, 2, 3, 4]");
  }

  @Test
  public void testCreateIndex() throws Exception {

    // Create test table.
    session.execute("create table test_create_index " +
                    "(h1 int, h2 text, r1 int, r2 text, " +
                    "c1 int, c2 text, c3 decimal, c4 timestamp, c5 boolean, " +
                    "primary key ((h1, h2), r1, r2)) " +
                    "with transactions = {'enabled' : true};");

    // Create test indexes with range and non-primary-key columns.
    session.execute("create index i1 on test_create_index (r1, r2) include (c1, c4);");
    session.execute("create index i2 on test_create_index (c4) include (c1, c2);");
    session.execute("create index i4 on test_create_index (c5) include (c4);");
    session.execute("create index i5 on test_create_index (c1, c5) include (c2, c3);");

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
    assertEquals("CREATE INDEX i4 ON cql_test_keyspace.test_create_index (c5, h1, h2, r1, r2);",
                 table.getIndex("i4").asCQLQuery());
    assertEquals("CREATE INDEX i5 ON cql_test_keyspace.test_create_index (c1, c5, h1, h2, r1, r2);",
                 table.getIndex("i5").asCQLQuery());

    // Verify the covering columns.
    assertIndexOptions("test_create_index", "i1", "r1, r2, h1, h2", "c1, c4");
    assertIndexOptions("test_create_index", "i2", "c4, h1, h2, r1, r2", "c1, c2");
    assertIndexOptions("test_create_index", "i4", "c5, h1, h2, r1, r2", "c4");
    assertIndexOptions("test_create_index", "i5", "c1, c5, h1, h2, r1, r2", "c2, c3");

    // Test retrieving non-existent index.
    assertNull(table.getIndex("i3"));

    // Test create index with duplicate name.
    try {
      session.execute("create index i1 on test_create_index (r1) include (c1);");
      fail("Duplicate index created");
    } catch (InvalidQueryException e) {
      LOG.info("Expected exception " + e.getMessage());
    }

    // Test create index on non-existent table.
    try {
      session.execute("create index i3 on non_existent_table (k) include (v);");
      fail("Index on non-existent table created");
    } catch (InvalidQueryException e) {
      LOG.info("Expected exception " + e.getMessage());
    }

    // Test create index if not exists. Verify i1 is still the same.
    session.execute("create index if not exists i1 on test_create_index (r1) include (c1);");
    assertIndexOptions("test_create_index", "i1", "r1, r2, h1, h2", "c1, c4");

    // Create another test table.
    session.execute("create table test_create_index_2 " +
                    "(h1 int, h2 text, r1 int, r2 text, " +
                    "c1 int, c2 text, c3 decimal, c4 timestamp, " +
                    "primary key ((h1, h2), r1, r2)) " +
                    "with transactions = {'enabled' : true};");

    // Test create index by the same name on another table.
    try {
      session.execute("create index i1 on test_create_index_2 (r1, r2) include (c1, c4);");
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
                    "include (c3, c4) with clustering order by (r2 desc, c1 asc, c2 desc);");
    session.execute("create index i2 on test_clustering_index ((c1, c2), c3, c4) " +
                    "include (c5) with clustering order by (c3 asc, c4 desc);");

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
    session.execute("create index i1 on test_drop_index (r1, r2) include (c1, c2);");

    // Wait to ensure the partitions metadata was updated.
    // Schema change should trigger a refresh but playing it safe in case debouncer will delay it.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);

    // Verify the index.
    TableMetadata table = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE)
                          .getTable("test_drop_index");
    assertEquals("CREATE INDEX i1 ON cql_test_keyspace.test_drop_index (r1, r2, h1, h2);",
                 table.getIndex("i1").asCQLQuery());
    assertIndexOptions("test_drop_index", "i1", "r1, r2, h1, h2", "c1, c2");

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
    session.execute("create index i1 on test_drop_index (c1, c2) include (c3, c4);");

    // Wait to ensure the partitions metadata was updated.
    // Schema change should trigger a refresh but playing it safe in case debouncer will delay it.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);

    table = cluster.getMetadata().getKeyspace(DEFAULT_TEST_KEYSPACE).getTable("test_drop_index");
    assertEquals("CREATE INDEX i1 ON cql_test_keyspace.test_drop_index (c1, c2, h1, h2, r1, r2);",
                 table.getIndex("i1").asCQLQuery());
    assertIndexOptions("test_drop_index", "i1", "c1, c2, h1, h2, r1, r2", "c3, c4");
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
    session.execute("create index i1 on test_drop_cascade (r1, r2) include (c1, c2);");
    session.execute("create index i2 on test_drop_cascade (c4) include (c1);");
    assertIndexOptions("test_drop_cascade", "i1", "r1, r2, h1, h2", "c1, c2");
    assertIndexOptions("test_drop_cascade", "i2", "c4, h1, h2, r1, r2", "c1");

    // Drop test table. Verify the index is cascade-deleted.
    session.execute("drop table test_drop_cascade;");
    assertNull(session.execute("select options from system_schema.indexes " +
                               "where keyspace_name = ? and table_name = ? and index_name = ?",
                               DEFAULT_TEST_KEYSPACE, "test_drop_cascade", "i1").one());
    assertNull(session.execute("select options from system_schema.indexes " +
                               "where keyspace_name = ? and table_name = ? and index_name = ?",
                               DEFAULT_TEST_KEYSPACE, "test_drop_cascade", "i2").one());
  }

  private void assertIndexOptions(String table, String index, String target, String include)
      throws Exception {
    Row row = session.execute("select options, transactions, is_unique " +
                              "from system_schema.indexes " +
                              "where keyspace_name = ? and table_name = ? and index_name = ?",
                              DEFAULT_TEST_KEYSPACE, table, index).one();
    Map<String, String> options = row.getMap("options", String.class, String.class);
    assertEquals(target, options.get("target"));
    assertEquals(include, options.get("include"));
    Map<String, String> transactions = row.getMap("transactions", String.class, String.class);
    assertEquals("true", transactions.get("enabled"));
    assertFalse(row.getBool("is_unique"));
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

  private void createTable(String statement, boolean strongConsistency) throws Exception {
    session.execute(
        statement + (strongConsistency ? " with transactions = {'enabled' : true};" : ";"));
  }

  private void createIndex(String statement, boolean strongConsistency) throws Exception {
    session.execute(
        statement + (strongConsistency ? ";" :
                     " with transactions = {'enabled' : false, " +
                     "'consistency_level' : 'user_enforced'};"));
  }

  private Set<String> queryTable(String table, String columns) {
    Set<String> rows = new HashSet<String>();
    for (Row row : session.execute(String.format("select %s from %s;", columns, table))) {
      rows.add(row.toString());
    }
    return rows;
  }

  private void assertIndexUpdate(Map<String, String> columnMap, String query) throws Exception {
    session.execute(query);
    for (Map.Entry<String, String> entry : columnMap.entrySet()) {
      assertEquals("Index " + entry.getKey() + " after " + query,
                   queryTable("test_update", entry.getValue()),
                   queryTable(entry.getKey(), entry.getValue()));
    }
  }

  private void testIndexUpdate(boolean strongConsistency) throws Exception {
    // Create test table and indexes.
    createTable("create table test_update " +
                "(h1 int, h2 text, r1 int, r2 text, c1 int, c2 text, " +
                "primary key ((h1, h2), r1, r2))", strongConsistency);
    createIndex("create index i1 on test_update (h1)", strongConsistency);
    createIndex("create index i2 on test_update ((r1, r2)) include (c2)", strongConsistency);
    createIndex("create index i3 on test_update (r2, r1) include (c1, c2)", strongConsistency);
    createIndex("create index i4 on test_update (c1)", strongConsistency);
    createIndex("create index i5 on test_update (c2) include (c1)", strongConsistency);

    Map<String, String> columnMap = new HashMap<String, String>() {{
        put("i1", "h1, h2, r1, r2");
        put("i2", "r1, r2, h1, h2, c2");
        put("i3", "r2, r1, h1, h2, c1, c2");
        put("i4", "c1, h1, h2, r1, r2");
        put("i5", "c2, h1, h2, r1, r2, c1");
      }};

    // test_update: Row[1, a, 2, b, 3]
    assertIndexUpdate(columnMap,
                      "insert into test_update (h1, h2, r1, r2, c1) values (1, 'a', 2, 'b', 3);");

    // test_update: Row[1, a, 2, b, 3, c]
    assertIndexUpdate(columnMap,
                      "update test_update set c2 = 'c' " +
                      "where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';");

    // test_update: Row[1, a, 2, b, 4, d]
    assertIndexUpdate(columnMap,
                      "update test_update set c1 = 4, c2 = 'd' " +
                      "where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';");

    // test_update: Row[1, a, 2, b, 4, e]
    assertIndexUpdate(columnMap,
                      "update test_update set c2 = 'e' " +
                      "where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';");

    // test_update: Row[1, a, 2, b, 4, e]
    //              Row[1, a, 12, bb, 6]
    assertIndexUpdate(columnMap,
                      "insert into test_update (h1, h2, r1, r2, c1) values (1, 'a', 12, 'bb', 6);");

    // test_update: Row[1, a, 12, bb, 6]
    assertIndexUpdate(columnMap,
                      "delete from test_update " +
                      "where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';");

    // test_update: empty
    assertIndexUpdate(columnMap,
                      "delete from test_update where h1 = 1 and h2 = 'a';");

    // test_update: Row[11, aa, 22, bb, 3]
    assertIndexUpdate(columnMap,
                      "update test_update set c1 = 3 " +
                      "where h1 = 11 and h2 = 'aa' and r1 = 22 and r2 = 'bb';");

    // test_update: empty
    assertIndexUpdate(columnMap,
                      "update test_update set c1 = null " +
                      "where h1 = 11 and h2 = 'aa' and r1 = 22 and r2 = 'bb';");

    // test_update: Row[11, aa, 222, bbb, 3, c]
    assertIndexUpdate(columnMap,
                      "update test_update set c1 = 3, c2 = 'c' " +
                      "where h1 = 11 and h2 = 'aa' and r1 = 222 and r2 = 'bbb';");

    // test_update: empty
    assertIndexUpdate(columnMap,
                      "delete c1, c2 from test_update " +
                      "where h1 = 11 and h2 = 'aa' and r1 = 222 and r2 = 'bbb';");

    // test_update: Row[1, a, 2, b]
    assertIndexUpdate(columnMap,
                      "insert into test_update (h1, h2, r1, r2) values (1, 'a', 2, 'b');");
    assertQuery("select h1, h2, r1, r2, c1 from test_update where c1 = null;",
                "Row[1, a, 2, b, NULL]");
    assertQuery("select h1, h2, r1, r2, c1, c2 from test_update where c2 = null;",
                "Row[1, a, 2, b, NULL, NULL]");

    // test_update: Row[1, a, 2, b, 4]
    assertIndexUpdate(columnMap,
                      "update test_update set c1 = 4 " +
                      "where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';");
    assertQuery("select h1, h2, r1, r2, c1 from test_update where c1 = null;",
                "");
    assertQuery("select h1, h2, r1, r2, c1 from test_update where c1 = 4;",
                "Row[1, a, 2, b, 4]");
    assertQuery("select h1, h2, r1, r2, c1, c2 from test_update where c2 = null;",
                "Row[1, a, 2, b, 4, NULL]");

    // test_update: Row[1, a, 2, b, 4, c]
    assertIndexUpdate(columnMap,
                      "update test_update set c2 = 'c' " +
                      "where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';");
    assertQuery("select h1, h2, r1, r2, c1 from test_update where c1 = 4;",
                "Row[1, a, 2, b, 4]");
    assertQuery("select h1, h2, r1, r2, c1, c2 from test_update where c2 = null;",
                "");
    assertQuery("select h1, h2, r1, r2, c1, c2 from test_update where c2 = 'c';",
                "Row[1, a, 2, b, 4, c]");

    // test_update: Row[1, a, 2, b]
    assertIndexUpdate(columnMap,
                      "delete c1, c2 from test_update " +
                      "where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';");
    assertQuery("select h1, h2, r1, r2, c1 from test_update where c1 = null;",
                "Row[1, a, 2, b, NULL]");
    assertQuery("select h1, h2, r1, r2, c1 from test_update where c1 = 4;",
                "");
    assertQuery("select h1, h2, r1, r2, c1, c2 from test_update where c2 = null;",
                "Row[1, a, 2, b, NULL, NULL]");
    assertQuery("select h1, h2, r1, r2, c1, c2 from test_update where c2 = 'c';",
                "");
  }

  @Test
  public void testIndexUpdate() throws Exception {
    testIndexUpdate(true);
  }

  @Test
  public void testWeakIndexUpdate() throws Exception {
    testIndexUpdate(false);
  }

  @Test
  public void testWeakIndexBatchUpdate() throws Exception {
    // Test batch insert into a table with secondary index.
    session.execute("create table test_batch (k int primary key, v text);");
    session.execute("create index test_batch_by_v on test_batch (v) " +
                    "with transactions = {'enabled' : false, " +
                    "'consistency_level' : 'user_enforced'};");

    assertQuery(String.format("select options, transactions from system_schema.indexes where "+
                              "keyspace_name = '%s' and " +
                              "table_name = 'test_batch' and " +
                              "index_name = 'test_batch_by_v';",
                              DEFAULT_TEST_KEYSPACE),
                "Row[{target=v, k}, {enabled=false, consistency_level=user_enforced}]");

    final int BATCH_SIZE = 20;
    final int KEY_COUNT = 1000;

    PreparedStatement statement = session.prepare("insert into test_batch (k, v) values (?, ?);");
    int k = 0;
    while (k < KEY_COUNT) {
      BatchStatement batch = new BatchStatement();
      for (int i = 0; i < BATCH_SIZE; i++) {
        batch.add(statement.bind(Integer.valueOf(k), "v" + k));
        k++;
      }
      session.execute(batch);
    }

    // Verify the rows in the index are identical to the indexed table.
    assertEquals(queryTable("test_batch", "k, v"),
                 queryTable("test_batch_by_v", "k, v"));

    // Verify that all the rows can be read.
    statement = session.prepare("select k from test_batch where v = ?;");
    for (int i = 0; i < KEY_COUNT; i++) {
      assertEquals(session.execute(statement.bind("v" + i)).one().getInt("k"), i);
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
    session.execute("create index i5 on test_prepare (c2) include (c1);");

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
  public void testClusterRestart() throws Exception {
    // Destroy existing cluster and recreate a new one before running this test. If the existing
    // cluster is reused without recreating it, a large number of tables may have been created and
    // dropped by other test cases already. In such a case, when a cluster is restarted below, the
    // tservers will take a long time replaying the DeleteTablet ops from the log before getting
    // fully initialized and starting the CQL service, causing the subsequent setUpCqlClient() to
    // time out.
    destroyMiniCluster();
    createMiniCluster();
    setUpCqlClient();

    // Create test table with index.
    session.execute("create table test_cluster_restart (k int primary key, v text) " +
                    "with transactions = {'enabled' : true};");
    session.execute("create index test_cluster_restart_by_v on test_cluster_restart (v);");

    // Insert some rows and query them by the indexed column.
    final int ROW_COUNT = 100;
    PreparedStatement insertStmt = session.prepare(
        "insert into test_cluster_restart (k, v) values (?, ?);");
    for (int i = 0; i < ROW_COUNT; i++) {
      session.execute(insertStmt.bind(Integer.valueOf(i), "v" + i));
    }
    PreparedStatement selectStmt = session.prepare(
        "select k from test_cluster_restart where v = ?;");
    for (int i = 0; i < ROW_COUNT; i++) {
      assertEquals(i, session.execute(selectStmt.bind("v" + i)).one().getInt("k"));
    }

    // Restart the cluster
    miniCluster.restart();
    setUpCqlClient();

    // Update half of the rows. Query them back and verify the unmodified rows still can still
    // be queried while the modified ones have the new value.
    PreparedStatement updateStmt = session.prepare(
        "update test_cluster_restart set v = ? where k = ?;");
    for (int i = ROW_COUNT / 2; i < ROW_COUNT; i++) {
      session.execute(updateStmt.bind("vv" + i, Integer.valueOf(i)));
    }
    selectStmt = session.prepare(
        "select k from test_cluster_restart where v = ?;");
    for (int i = 0; i < ROW_COUNT; i++) {
      assertEquals(i, session.execute(
          selectStmt.bind((i >= ROW_COUNT / 2 ? "vv" : "v") + i)).one().getInt("k"));
    }

    // Also verify that the old index values do not exist any more.
    for (int i = ROW_COUNT / 2; i < ROW_COUNT; i++) {
      assertNull(session.execute(selectStmt.bind("v" + i)).one());
    }
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

  private void assertInvalidUniqueIndexDML(String query, String indexName) {
    try {
      session.execute(query);
      fail("InvalidQueryException not thrown for " + query);
    } catch (InvalidQueryException e) {
      assertTrue(e.getMessage().startsWith(
          String.format("SQL error: Execution Error. Duplicate value disallowed by unique index %s",
                        indexName)));
    }
  }

  @Test
  public void testUniqueIndex() throws Exception {
    // Create test table with 2 unique indexes (single and multiple columns).
    session.execute("create table test_unique (k int primary key, v1 int, v2 text, v3 int) " +
                    "with transactions = {'enabled' : true};");
    session.execute("create unique index test_unique_by_v1 on test_unique (v1);");
    session.execute("create unique index test_unique_by_v2_v3 on test_unique (v2, v3);");

    assertQuery(String.format("select is_unique from system_schema.indexes where "+
                              "keyspace_name = '%s' and table_name = 'test_unique';",
                              DEFAULT_TEST_KEYSPACE),
                "Row[true]" +
                "Row[true]");

    // Test unique constraint on NULL values in v2 and v3.
    session.execute(
        "insert into test_unique (k, v1) values (1, 1);");
    assertInvalidUniqueIndexDML(
        "insert into test_unique (k, v1) values (2, 2);", "test_unique_by_v2_v3");
    session.execute(
        "insert into test_unique (k, v1, v2, v3) values (2, 2, 'b', 2);");

    // Test unique constraint on NULL value in v1.
    session.execute(
        "insert into test_unique (k, v2, v3) values (3, 'c', 3);");
    assertInvalidUniqueIndexDML(
        "insert into test_unique (k, v2, v3) values (4, 'd', 4);", "test_unique_by_v1");

    // Test unique constraint on non-NULL values.
    session.execute(
        "insert into test_unique (k, v1, v2, v3) values (5, 5, 'e', 5);");
    assertInvalidUniqueIndexDML(
        "insert into test_unique (k, v1, v2, v3) values (6, 5, 'f', 6);", "test_unique_by_v1");
    assertInvalidUniqueIndexDML(
        "insert into test_unique (k, v1, v2, v3) values (6, 6, 'e', 5);", "test_unique_by_v2_v3");

    // Test unique constraint with value (v1 = 2) removed and reinserted in another row.
    session.execute("delete from test_unique where k = 2;");
    session.execute("insert into test_unique (k, v1, v2, v3) values (3, 2, 'a', 1);");

    // Test unique constraint with value (v3 = 5) changed and the original value reinserted in
    // another row.
    session.execute("update test_unique set v3 = 6 where k = 5;");
    session.execute("insert into test_unique (k, v1, v2, v3) values (7, 7, 'e', 5);");

    // Test unique constraint with updating values with the same original values.
    session.execute("update test_unique set v1 = 7, v2 = 'e', v3 = 5 where k = 7;");
  }

  @Test
  public void testUniquePrimaryKeyIndex() throws Exception {
    // Test unique index on a primary key column.
    session.execute("create table test_unique_pk (h1 int, h2 int, r int, v int, " +
                    "primary key ((h1, h2), r)) with transactions = {'enabled' : true};");
    session.execute("create unique index test_unique_pk_by_h2 on test_unique_pk (h2);");
    session.execute("create unique index test_unique_pk_by_r on test_unique_pk (r);");

    session.execute("insert into test_unique_pk (h1, h2, r, v) values (1, 1, 1, 1);");

    // Test inserting duplicate h2 and r values.
    assertInvalidUniqueIndexDML(
        "insert into test_unique_pk (h1, h2, r, v) values (1, 1, 2, 2);", "test_unique_pk_by_h2");
    assertInvalidUniqueIndexDML(
        "insert into test_unique_pk (h1, h2, r, v) values (1, 2, 1, 2);", "test_unique_pk_by_r");

    // Restart the cluster
    miniCluster.restart();
    setUpCqlClient();

    // Test inserting duplicate h2 and r values again.
    assertInvalidUniqueIndexDML(
        "insert into test_unique_pk (h1, h2, r, v) values (1, 1, 2, 2);", "test_unique_pk_by_h2");
    assertInvalidUniqueIndexDML(
        "insert into test_unique_pk (h1, h2, r, v) values (1, 2, 1, 2);", "test_unique_pk_by_r");

    // Test inserting non-duplicate h2 and r value.
    session.execute("insert into test_unique_pk (h1, h2, r, v) values (1, 2, 2, 2);");
  }

  @Test
  public void testConditionalDML() throws Exception {
    // Create test 2 test tables. One with normal secondary index and one with an additional unique
    // index.
    session.execute("create table test_cond (k int primary key, v1 int, v2 text) " +
                    "with transactions = {'enabled' : true};");
    session.execute("create index test_cond_by_v1 on test_cond (v1) include (v2);");

    session.execute("create table test_cond_unique (k int primary key, v1 int, v2 text) " +
                    "with transactions = {'enabled' : true};");
    session.execute("create index test_cond_unique_by_v1 on test_cond_unique (v1);");
    session.execute("create unique index test_cond_unique_by_v2 on test_cond_unique (v2) "+
                    "include (v1);");

    // Insert into first table with conditional DML.
    session.execute("insert into test_cond (k, v1, v2) values (1, 1, 'a');");
    assertQuery("insert into test_cond (k, v1, v2) values (1, 1, 'a') if not exists;",
                "Columns[[applied](boolean), k(int), v2(varchar), v1(int)]",
                "Row[false, 1, a, 1]");
    assertQuery("insert into test_cond (k, v1, v2) values (2, 1, 'a') if not exists;",
                "Row[true]");

    // Insert into second table with conditional DML.
    session.execute("insert into test_cond_unique (k, v1, v2) values (1, 1, 'a');");
    assertQuery("insert into test_cond_unique (k, v1, v2) values (1, 1, 'a') if not exists;",
                "Columns[[applied](boolean), k(int), v2(varchar), v1(int)]",
                "Row[false, 1, a, 1]");
    assertInvalidUniqueIndexDML("insert into test_cond_unique (k, v1, v2) values (2, 2, 'a') " +
                                "if not exists;", "test_cond_unique_by_v2");
    assertQueryRowsUnordered("select * from test_cond_unique;",
                             "Row[1, 1, a]");

    assertQuery("insert into test_cond_unique (k, v1, v2) values (2, 2, 'b') if not exists;",
                "Row[true]");
    assertQueryRowsUnordered("select * from test_cond_unique;",
                             "Row[1, 1, a]", "Row[2, 2, b]");
  }

  @Test
  public void testDMLInTranaction() throws Exception {
    // Create 2 tables with secondary indexes and verify they can be updated in the one transaction.
    session.execute("create table test_txn1 (k int primary key, v int) " +
                    "with transactions = {'enabled' : true};");
    session.execute("create index test_txn1_by_v on test_txn1 (v);");

    session.execute("create table test_txn2 (k text primary key, v text) " +
                    "with transactions = {'enabled' : true};");
    session.execute("create index test_txn2_by_v on test_txn2 (v);");

    session.execute("begin transaction" +
                    "  insert into test_txn1 (k, v) values (1, 101);" +
                    "  insert into test_txn2 (k, v) values ('k1', 'v101');" +
                    "end transaction;");

    // Verify the rows.
    assertQuery("select k, v from test_txn1;", "Row[1, 101]");
    assertQuery("select k, v from test_txn2;", "Row[k1, v101]");

    // Verify rows can be selected by the index columns.
    assertQuery("select k, v from test_txn1 where v = 101;", "Row[1, 101]");
    assertQuery("select k, v from test_txn2 where v = 'v101';", "Row[k1, v101]");

    // Verify the writetimes are the same.
    assertEquals(session.execute("select writetime(v) from test_txn1 where k = 1;")
                 .one().getLong("writetime(v)"),
                 session.execute("select writetime(v) from test_txn2 where k = 'k1';")
                 .one().getLong("writetime(v)"));
  }

  @Test
  public void testSelectAll() throws Exception {
    // Create a table with index.
    session.execute("create table test_all (k int primary key, v1 int, v2 int) " +
                    "with transactions = { 'enabled' : true };");
    session.execute("create index test_all_by_v1 on test_all (v1) include (v2);");
    session.execute("insert into test_all (k, v1, v2) values (1, 2, 3);");

    // Select all columns using index and verify the selected columns are returned in the same order
    // as the table columns.
    assertQuery("select * from test_all where v1 = 2;", "Row[1, 2, 3]");
  }
}
