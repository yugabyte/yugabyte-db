// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
package org.kududb.client;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.kududb.client.RowResult.timestampToString;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executors;

import org.junit.Before;
import org.junit.Test;
import org.kududb.ColumnSchema;
import org.kududb.Schema;
import org.kududb.Type;

public class TestKuduClient extends BaseKuduTest {
  private String tableName;

  @Before
  public void setTableName() {
    tableName = TestKuduClient.class.getName() + "-" + System.currentTimeMillis();
  }

  private Schema createManyStringsSchema() {
    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>(4);
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key", Type.STRING).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c1", Type.STRING).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c2", Type.STRING).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c3", Type.STRING).nullable(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c4", Type.STRING).nullable(true).build());
    return new Schema(columns);
  }

  private Schema createSchemaWithBinaryColumns() {
    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>();
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key", Type.BINARY).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c1", Type.STRING).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c2", Type.DOUBLE).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c3", Type.BINARY).nullable(true).build());
    return new Schema(columns);
  }

  private Schema createSchemaWithTimestampColumns() {
    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>();
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key", Type.TIMESTAMP).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c1", Type.TIMESTAMP).nullable(true).build());
    return new Schema(columns);
  }

  /**
   * Test creating and deleting a table through a KuduClient.
   */
  @Test(timeout = 100000)
  public void testCreateDeleteTable() throws Exception {
    // Check that we can create a table.
    syncClient.createTable(tableName, basicSchema);
    assertFalse(syncClient.getTablesList().getTablesList().isEmpty());
    assertTrue(syncClient.getTablesList().getTablesList().contains(tableName));

    // Check that we can delete it.
    syncClient.deleteTable(tableName);
    assertFalse(syncClient.getTablesList().getTablesList().contains(tableName));

    // Check that we can re-recreate it, with a different schema.
    List<ColumnSchema> columns = new ArrayList<>(basicSchema.getColumns());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("one more", Type.STRING).build());
    Schema newSchema = new Schema(columns);
    syncClient.createTable(tableName, newSchema);

    // Check that we can open a table and see that it has the new schema.
    KuduTable table = syncClient.openTable(tableName);
    assertEquals(newSchema.getColumnCount(), table.getSchema().getColumnCount());
    assertTrue(table.getPartitionSchema().isSimpleRangePartitioning());

    // Check that the block size parameter we specified in the schema is respected.
    assertEquals(4096, newSchema.getColumn("column3_s").getDesiredBlockSize());
    assertEquals(ColumnSchema.Encoding.DICT_ENCODING,
                 newSchema.getColumn("column3_s").getEncoding());
    assertEquals(ColumnSchema.CompressionAlgorithm.LZ4,
                 newSchema.getColumn("column3_s").getCompressionAlgorithm());
  }

  /**
   * Test inserting and retrieving string columns.
   */
  @Test(timeout = 100000)
  public void testStrings() throws Exception {
    Schema schema = createManyStringsSchema();
    syncClient.createTable(tableName, schema);

    KuduSession session = syncClient.newSession();
    KuduTable table = syncClient.openTable(tableName);
    for (int i = 0; i < 100; i++) {
      Insert insert = table.newInsert();
      PartialRow row = insert.getRow();
      row.addString("key", String.format("key_%02d", i));
      row.addString("c2", "c2_" + i);
      if (i % 2 == 1) {
        row.addString("c3", "c3_" + i);
      }
      row.addString("c4", "c4_" + i);
      // NOTE: we purposefully add the strings in a non-left-to-right
      // order to verify that we still place them in the right position in
      // the row.
      row.addString("c1", "c1_" + i);
      session.apply(insert);
      if (i % 50 == 0) {
        session.flush();
      }
    }
    session.flush();

    List<String> rowStrings = scanTableToStrings(table);
    assertEquals(100, rowStrings.size());
    assertEquals(
        "STRING key=key_03, STRING c1=c1_3, STRING c2=c2_3, STRING c3=c3_3, STRING c4=c4_3",
        rowStrings.get(3));
    assertEquals(
        "STRING key=key_04, STRING c1=c1_4, STRING c2=c2_4, STRING c3=NULL, STRING c4=c4_4",
        rowStrings.get(4));
  }

  /**
   * Test to verify that we can write in and read back UTF8.
   */
  @Test(timeout = 100000)
  public void testUTF8() throws Exception {
    Schema schema = createManyStringsSchema();
    syncClient.createTable(tableName, schema);

    KuduSession session = syncClient.newSession();
    KuduTable table = syncClient.openTable(tableName);
    Insert insert = table.newInsert();
    PartialRow row = insert.getRow();
    row.addString("key", "กขฃคฅฆง"); // some thai
    row.addString("c1", "✁✂✃✄✆"); // some icons

    row.addString("c2", "hello"); // some normal chars
    row.addString("c4", "🐱"); // supplemental plane
    session.apply(insert);
    session.flush();

    List<String> rowStrings = scanTableToStrings(table);
    assertEquals(1, rowStrings.size());
    assertEquals(
        "STRING key=กขฃคฅฆง, STRING c1=✁✂✃✄✆, STRING c2=hello, STRING c3=NULL, STRING c4=🐱",
        rowStrings.get(0));
  }

  /**
   * Test inserting and retrieving binary columns.
   */
  @Test(timeout = 100000)
  public void testBinaryColumns() throws Exception {
    Schema schema = createSchemaWithBinaryColumns();
    syncClient.createTable(tableName, schema);

    byte[] testArray = new byte[] {1, 2, 3, 4, 5, 6 ,7, 8, 9};

    KuduSession session = syncClient.newSession();
    KuduTable table = syncClient.openTable(tableName);
    for (int i = 0; i < 100; i++) {
      Insert insert = table.newInsert();
      PartialRow row = insert.getRow();
      row.addBinary("key", String.format("key_%02d", i).getBytes());
      row.addString("c1", "✁✂✃✄✆");
      row.addDouble("c2", i);
      if (i % 2 == 1) {
        row.addBinary("c3", testArray);
      }
      session.apply(insert);
      if (i % 50 == 0) {
        session.flush();
      }
    }
    session.flush();

    List<String> rowStrings = scanTableToStrings(table);
    assertEquals(100, rowStrings.size());
    for (int i = 0; i < rowStrings.size(); i++) {
      StringBuilder expectedRow = new StringBuilder();
      expectedRow.append(String.format("BINARY key=\"key_%02d\", STRING c1=✁✂✃✄✆, DOUBLE c2=%.1f,"
          + " BINARY c3=", i, (double) i));
      if (i % 2 == 1) {
        expectedRow.append(Bytes.pretty(testArray));
      } else {
        expectedRow.append("NULL");
      }
      assertEquals(expectedRow.toString(), rowStrings.get(i));
    }
  }

  /**
   * Test inserting and retrieving timestamp columns.
   */
  @Test(timeout = 100000)
  public void testTimestampColumns() throws Exception {
    Schema schema = createSchemaWithTimestampColumns();
    syncClient.createTable(tableName, schema);

    List<Long> timestamps = new ArrayList<>();

    KuduSession session = syncClient.newSession();
    KuduTable table = syncClient.openTable(tableName);
    long lastTimestamp = 0;
    for (int i = 0; i < 100; i++) {
      Insert insert = table.newInsert();
      PartialRow row = insert.getRow();
      long timestamp = System.currentTimeMillis() * 1000;
      while(timestamp == lastTimestamp) {
        timestamp = System.currentTimeMillis() * 1000;
      }
      timestamps.add(timestamp);
      row.addLong("key", timestamp);
      if (i % 2 == 1) {
        row.addLong("c1", timestamp);
      }
      session.apply(insert);
      if (i % 50 == 0) {
        session.flush();
      }
      lastTimestamp = timestamp;
    }
    session.flush();

    List<String> rowStrings = scanTableToStrings(table);
    assertEquals(100, rowStrings.size());
    for (int i = 0; i < rowStrings.size(); i++) {
      StringBuilder expectedRow = new StringBuilder();
      expectedRow.append(String.format("TIMESTAMP key=%s, TIMESTAMP c1=",
          timestampToString(timestamps.get(i))));
      if (i % 2 == 1) {
        expectedRow.append(timestampToString(timestamps.get(i)));
      } else {
        expectedRow.append("NULL");
      }
      assertEquals(expectedRow.toString(), rowStrings.get(i));
    }
  }

  /**
   * Creates a local client that we auto-close while buffering one row, then makes sure that after
   * closing that we can read the row.
   */
  @Test(timeout = 100000)
  public void testAutoClose() throws Exception {
    try (KuduClient localClient = new KuduClient.KuduClientBuilder(masterAddresses).build()) {
      localClient.createTable(tableName, basicSchema);
      KuduTable table = localClient.openTable(tableName);
      KuduSession session = localClient.newSession();

      session.setFlushMode(SessionConfiguration.FlushMode.MANUAL_FLUSH);
      Insert insert = createBasicSchemaInsert(table, 0);
      session.apply(insert);
    }

    KuduTable table = syncClient.openTable(tableName);
    AsyncKuduScanner scanner = new AsyncKuduScanner.AsyncKuduScannerBuilder(client, table).build();
    assertEquals(1, countRowsInScan(scanner));
  }

  @Test(timeout = 100000)
  public void testCustomNioExecutor() throws Exception {
    long startTime = System.nanoTime();
    final KuduClient localClient = new KuduClient.KuduClientBuilder(masterAddresses)
        .nioExecutors(Executors.newFixedThreadPool(1), Executors.newFixedThreadPool(2))
        .bossCount(1)
        .workerCount(2)
        .build();
    long buildTime = (System.nanoTime() - startTime) / 1000000000L;
    assertTrue("Building KuduClient is slow, maybe netty get stuck", buildTime < 3);
    localClient.createTable(tableName, basicSchema);
    Thread[] threads = new Thread[4];
    for (int t = 0; t < 4; t++) {
      final int id = t;
      threads[t] = new Thread(new Runnable() {
        @Override
        public void run() {
          try {
            KuduTable table = localClient.openTable(tableName);
            KuduSession session = localClient.newSession();
            session.setFlushMode(SessionConfiguration.FlushMode.AUTO_FLUSH_SYNC);
            for (int i = 0; i < 100; i++) {
              Insert insert = createBasicSchemaInsert(table, id * 100 + i);
              session.apply(insert);
            }
            session.close();
          } catch (Exception e) {
            fail("insert thread should not throw exception: " + e);
          }
        }
      });
      threads[t].start();
    }
    for (int t = 0; t< 4;t++) {
      threads[t].join();
    }
    localClient.shutdown();
  }
}
