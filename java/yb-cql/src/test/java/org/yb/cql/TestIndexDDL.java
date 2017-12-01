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

import com.datastax.driver.core.Row;
import com.datastax.driver.core.TableMetadata;
import com.datastax.driver.core.exceptions.InvalidQueryException;
import org.yb.minicluster.MiniYBCluster;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.fail;

public class TestIndexDDL extends BaseCQLTest {

  @Test
  public void testCreateIndex() throws Exception {

    // Create test table.
    session.execute("create table test_create_index " +
                    "(h1 int, h2 text, r1 int, r2 text, " +
                    "c1 int, c2 text, c3 decimal, c4 timestamp, " +
                    "primary key ((h1, h2), r1, r2));");

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
                    "primary key ((h1, h2), r1, r2));");

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
                    "primary key ((h1, h2), r1, r2));");

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
                    "primary key ((h1, h2), r1, r2));");

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
                    "primary key ((h1, h2), r1, r2));");

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
}
