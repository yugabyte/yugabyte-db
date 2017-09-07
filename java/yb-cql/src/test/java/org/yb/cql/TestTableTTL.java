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

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import org.junit.Test;

import java.util.Iterator;

import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertEquals;

public class TestTableTTL extends BaseCQLTest {

  private Iterator<Row> execQuery(String tableName, int primaryKey) {
    ResultSet rs = session.execute(String.format("SELECT c1, c2, c3 FROM %s WHERE c1 = %d;",
      tableName, primaryKey));
    return rs.iterator();
  }

  private void assertNoRow(String tableName, int primaryKey) {
    String select_stmt = String.format("SELECT c1, c2, c3 FROM %s WHERE c1 = %d;",
      tableName, primaryKey);
    assertNoRow(select_stmt);
  }

  private Row getFirstRow(String tableName, int primaryKey) {
    Iterator<Row> iter = execQuery(tableName, primaryKey);
    assertTrue(iter.hasNext());
    return iter.next();
  }

  private String getCreateTableStmt(String tableName, long ttl) {
    return String.format("CREATE TABLE %s (c1 int, c2 int, c3 int, PRIMARY KEY(c1)) " +
        "WITH bloom_filter_fp_chance = 0.01 " +
        "AND comment = '' " +
        "AND crc_check_chance = 1.0 " +
        "AND dclocal_read_repair_chance = 0.1 " +
        "AND gc_grace_seconds = 864000 " +
        "AND max_index_interval = 2048 " +
        "AND memtable_flush_period_in_ms = 0 " +
        "AND min_index_interval = 128 " +
        "AND read_repair_chance = 0.0 " +
        "AND speculative_retry = '99.0PERCENTILE' " +
        "AND caching = { " +
        "    'keys' : 'ALL', " +
        "    'rows_per_partition' : 'NONE' " +
        "} " +
        "AND compression = { " +
        "    'chunk_length_in_kb' : 64, " +
        "    'class' : 'LZ4Compressor', " +
        "    'enabled' : true " +
        "} " +
        "AND compaction = { " +
        "    'base_time_seconds' : 60, " +
        "    'class' : 'DateTieredCompactionStrategy', " +
        "    'enabled' : true, " +
        "    'max_sstable_age_days' : 365, " +
        "    'max_threshold' : 32, " +
        "    'min_threshold' : 4, " +
        "    'timestamp_resolution' : 'MICROSECONDS', " +
        "    'tombstone_compaction_interval' : 86400, " +
        "    'tombstone_threshold' : 0.2, " +
        "    'unchecked_tombstone_compaction' : false " +
        "} " +
        "AND default_time_to_live = %d;", tableName, ttl);
  }

  private void createTable(String tableName, long ttl) {
    session.execute(getCreateTableStmt(tableName, ttl));
  }

  private void createTableInvalid(String tableName, long ttl) {
    runInvalidStmt(getCreateTableStmt(tableName, ttl));
  }

  @Test
  public void testSimpleTableTTL() throws Exception {
    String tableName = "testSimpleTableTTL";

    // Create table with TTL.
    createTable(tableName, 1);

    // Insert a row.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3);", tableName));

    // Verify row is present.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(3, row.getInt(2));

    // Now row should expire.
    Thread.sleep(1050);

    // Verify row has expired.
    assertNoRow(tableName, 1);
  }

  @Test
  public void testTableTTLOverride() throws Exception {
    String tableName = "testTableTTLOverride";

    // Create table with TTL.
    createTable(tableName, 1);

    // Insert a row.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3);", tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (2, 3, 4) USING TTL 2;",
      tableName));

    Thread.sleep(1050);

    // c1=1 should have expired.
    assertNoRow(tableName, 1);

    // c1=2 is still alive.
    Row row = getFirstRow(tableName, 2);
    assertEquals(2, row.getInt(0));
    assertEquals(3, row.getInt(1));
    assertEquals(4, row.getInt(2));

    Thread.sleep(1000);

    // c1 = 2 should have expired.
    assertNoRow(tableName, 2);

    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (3, 4, 5) USING TTL 1;",
      tableName));
    Thread.sleep(1050);
    // c1 = 3 should have expired.
    assertNoRow(tableName, 3);
  }

  @Test
  public void testTableTTLWithTTLZero() throws Exception {
    String tableName = "testTableTTLWithTTLZero";

    // Create table with TTL.
    createTable(tableName, 1);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TTL 0;",
      tableName));

    Thread.sleep(1050);

    // Row should not have expired.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(3, row.getInt(2));
  }

  @Test
  public void testTableTTLZero() throws Exception {
    String tableName = "testTableTTLZero";

    // Create table with TTL.
    createTable(tableName, 0);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TTL 1;",
      tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (2, 2, 3);",
      tableName));

    Thread.sleep(1050);

    // TTL 1 should have expired.
    assertNoRow(tableName, 1);

    // Row with no TTL should survive
    Row row = getFirstRow(tableName, 2);
    assertEquals(2, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(3, row.getInt(2));
  }

  @Test
  public void testTableTTLAndColumnTTL() throws Exception {
    String tableName = "testTableTTLAndColumnTTL";

    // Create table with TTL.
    createTable(tableName, 2);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TTL 1;",
      tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (2, 2, 3);",
      tableName));

    Thread.sleep(1050);

    // c1 = 1 should have expired.
    assertNoRow(tableName, 1);

    // Row with no TTL should survive
    Row row = getFirstRow(tableName, 2);
    assertEquals(2, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(3, row.getInt(2));

    Thread.sleep(1000);

    // Row c1 = 2 should now expire, with init marker.
    assertNoRow(tableName, 2);

    // Row c1 = 1, should also expire.
    assertNoRow(tableName, 1);
  }

  @Test
  public void testTableTTLWithDeletes() throws Exception {
    String tableName = "testTableTTLWithDeletes";

    // Create table with TTL.
    createTable(tableName, 2);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TTL 1;",
      tableName));
    session.execute(String.format("DELETE FROM %s WHERE c1 = 1;", tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 4);", tableName));

    Thread.sleep(1050);

    // Verify row still exists.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(4, row.getInt(2));

    Thread.sleep(1050);

    // Now verify row is gone due to table level TTL.
    assertNoRow(tableName, 1);
  }

  @Test
  public void testTableTTLWithOverwrites() throws Exception {
    String tableName = "testTableTTLWithOverwrites";

    // Create table with TTL.
    createTable(tableName, 2);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3);",
      tableName));

    Thread.sleep(1050);

    // Overwrite the row.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 4);", tableName));

    Thread.sleep(1000);

    // Row shouldn't expire since a new liveness column is written.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(4, row.getInt(2));
  }

  @Test
  public void testValidInvalidTableTTL() throws Exception {
    String tableName = "testValidInvalidTableTTL";

    // Valid create tables.
    createTable(tableName, 0);
    createTable(tableName + 1, MAX_TTL_SEC);

    // Invalid create tables.
    createTableInvalid(tableName + 1, MAX_TTL_SEC + 1);
    createTableInvalid(tableName + 1, Long.MAX_VALUE);
    createTableInvalid(tableName + 1, Long.MIN_VALUE);
    createTableInvalid(tableName + 1, -1);
  }

  @Test
  public void testTableTTLWithSingleColumnSurvival() throws Exception {
    String tableName = "testTableTTLWithSingleColumnSurvival";

    // Create table with TTL.
    createTable(tableName, 1);

    // Insert a row.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3);",
      tableName));

    // Update a single column.
    session.execute(String.format("UPDATE %s USING TTL 60 SET c2 = 20 WHERE c1 = 1", tableName));

    Thread.sleep(1050);

    // Verify primary key and one column survive.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertEquals(20, row.getInt(1));
    assertTrue(row.isNull(2));
  }

  @Test
  public void testTableTTLAfterAlter() throws Exception {
    String tableName = "testTableTTLAfterAlter";

    // Create table with TTL.
    createTable(tableName, 1000);

    // Insert a row.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3);", tableName));

    // Update Table TTL
    session.execute(String.format("ALTER TABLE %s WITH default_time_to_live=1;", tableName));

    // Insert a row.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (4, 5, 6);", tableName));

    // Verify first row is present.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(3, row.getInt(2));


    // Verify second row is present.
    row = getFirstRow(tableName, 4);
    assertEquals(4, row.getInt(0));
    assertEquals(5, row.getInt(1));
    assertEquals(6, row.getInt(2));

    // Wait for rows to expire.
    Thread.sleep(1050);

    // Verify rows have expired.
    assertNoRow(tableName, 1);
    assertNoRow(tableName, 4);
  }

  @Test
  public void testRowTTLAfterAlter() throws Exception {
    String tableName = "testRowTTLAfterAlter";

    // Create table with TTL.
    createTable(tableName, 1000);

    // Insert rows.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3);", tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (4, 5, 6) " +
          "USING TTL 3;", tableName));

    // Update Table TTL
    session.execute(String.format("ALTER TABLE %s WITH default_time_to_live=1;", tableName));

    // Wait for rows to expire.
    Thread.sleep(1050);

    // Verify first row is gone.
    assertNoRow(tableName, 1);

    // Verify second row is present.
    Row row = getFirstRow(tableName, 4);
    assertEquals(4, row.getInt(0));
    assertEquals(5, row.getInt(1));
    assertEquals(6, row.getInt(2));

    // Insert rows without TTL.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (7, 8, 9);", tableName));

    // Update Table TTL to be higher.
    session.execute(String.format("ALTER TABLE %s WITH default_time_to_live=10;", tableName));

    // Wait for rows to expire.
    Thread.sleep(2050);

    // Verify second row is gone.
    assertNoRow(tableName, 4);

    // Verify third row is still there.
    row = getFirstRow(tableName, 7);
    assertEquals(7, row.getInt(0));
    assertEquals(8, row.getInt(1));
    assertEquals(9, row.getInt(2));
  }

  @Test
  public void testColumnTTLAfterIncrease() throws Exception {
    String tableName = "testColumnTTLAfterIncrease";

    // Create table with TTL.
    createTable(tableName, 1);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3);", tableName));

    // Update a single column.
    session.execute(String.format("UPDATE %s USING TTL 1 SET c2 = 20 WHERE c1 = 1", tableName));

    // Update Table TTL
    session.execute(String.format("ALTER TABLE %s WITH default_time_to_live=50;", tableName));

    // Wait for columns to expire.
    Thread.sleep(1050);

    // Verify that only c2 is deleted.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertTrue(row.isNull(1));
    assertEquals(3, row.getInt(2));
  }

  @Test
  public void testColumnTTLAfterDecrease() throws Exception {
    String tableName = "testColumnTTLAfterDecrease";

    // Create table with TTL.
    createTable(tableName, 5);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3);", tableName));

    // Update a single column.
    session.execute(String.format("UPDATE %s USING TTL 60 SET c2 = 20 WHERE c1 = 1", tableName));

    // Update Table TTL
    session.execute(String.format("ALTER TABLE %s WITH default_time_to_live=1;", tableName));

    // Wait for columns to expire.
    Thread.sleep(1050);

    // Verify that only c3 is deleted.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertEquals(20, row.getInt(1));
    assertTrue(row.isNull(2));
  }
}
