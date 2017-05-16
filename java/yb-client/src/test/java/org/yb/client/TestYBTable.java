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
package org.yb.client;

import org.junit.AfterClass;
import org.yb.ColumnSchema;
import org.yb.Common;
import org.yb.Schema;
import org.yb.Type;
import org.junit.Ignore;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;

import static org.junit.Assert.*;

public class TestYBTable extends BaseYBClientTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestYBTable.class);

  private static final String BASE_TABLE_NAME = TestYBTable.class.getName();

  private static Schema schema = getBasicSchema();

  private static long testTTL = 5000L;

  public static Common.SchemaPB getTTLSchemaPB(boolean defaultTTL) {
    Common.SchemaPB.Builder pb = Common.SchemaPB.newBuilder();
    pb.addColumns(Common.ColumnSchemaPB.newBuilder()
        .setId(0)
        .setName("key")
        .setType(ProtobufHelper.dataTypeToPb(Common.DataType.INT32))
        .setIsKey(true)
        .build());
    pb.addColumns(Common.ColumnSchemaPB.newBuilder()
        .setId(1)
        .setName("column1_i")
        .setType(ProtobufHelper.dataTypeToPb(Common.DataType.INT32))
        .build());
    pb.addColumns(Common.ColumnSchemaPB.newBuilder()
        .setId(2)
        .setName("column2_i")
        .setType(ProtobufHelper.dataTypeToPb(Common.DataType.INT32))
        .build());
    pb.addColumns(Common.ColumnSchemaPB.newBuilder()
        .setId(3)
        .setName("column3_s")
        .setType(ProtobufHelper.dataTypeToPb(Common.DataType.STRING))
        .setIsNullable(true)
        .setCfileBlockSize(4096)
        .setEncoding(Common.EncodingType.DICT_ENCODING)
        .setCompression(Common.CompressionType.LZ4)
        .build());
    pb.addColumns(Common.ColumnSchemaPB.newBuilder()
        .setId(4)
        .setName("column4_b")
        .setType(ProtobufHelper.dataTypeToPb(Common.DataType.BOOL))
        .build());
    if (!defaultTTL) {
      pb.setTableProperties(Common.TablePropertiesPB.newBuilder()
          .setDefaultTimeToLive(testTTL)
          .build());
    }
    return pb.build();
  }

  public static Schema getSortOrderSchema(ColumnSchema.SortOrder sortOrder) {
    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>(5);
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key1", Type.INT32)
        .rangeKey(true, sortOrder)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key2", Type.INT32)
        .key(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column1_i", Type.INT32).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column2_s", Type.STRING)
        .nullable(true)
        .desiredBlockSize(4096)
        .encoding(ColumnSchema.Encoding.DICT_ENCODING)
        .compressionAlgorithm(ColumnSchema.CompressionAlgorithm.LZ4)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column3_b", Type.BOOL).build());
    return new Schema(columns);
  }

  @AfterClass
  public static void tearDownAfterClass() throws Exception {
    BaseYBClientTest.tearDownAfterClass();
  }

  @Test
  public void testDefaultTTL() {
    String tableName = BASE_TABLE_NAME + System.currentTimeMillis();
    Schema defaultSchema = ProtobufHelper.pbToSchema(getTTLSchemaPB(true));
    YBTable table = BaseYBClientTest.createTable(tableName, defaultSchema, null);
    assertEquals(Schema.defaultTTL, table.getSchema().getTimeToLiveInMillis());
  }

  @Test
  public void testCustomTTL() {
    String tableName = BASE_TABLE_NAME + System.currentTimeMillis();
    Schema ttlSchema = ProtobufHelper.pbToSchema(getTTLSchemaPB(false));
    YBTable table = BaseYBClientTest.createTable(tableName, ttlSchema, null);
    assertEquals(testTTL, table.getSchema().getTimeToLiveInMillis());
  }

  @Test
  public void testSortOrderNone() {
    String tableName = BASE_TABLE_NAME + System.currentTimeMillis();
    Schema noneOrderSchema = getSortOrderSchema(ColumnSchema.SortOrder.NONE);
    YBTable table = BaseYBClientTest.createTable(tableName, noneOrderSchema, null);
    for (ColumnSchema columnSchema : table.getSchema().getColumns()) {
      if (columnSchema.getName().equals("key1") || columnSchema.getName().equals("key2")) {
        // Default sort order for range keys is ASC.
        assertEquals(ColumnSchema.SortOrder.ASC, columnSchema.getSortOrder());
      } else {
        assertEquals(ColumnSchema.SortOrder.NONE, columnSchema.getSortOrder());
      }
    }
  }

  @Test
  public void testSortOrderAsc() {
    String tableName = BASE_TABLE_NAME + System.currentTimeMillis();
    Schema ascOrderSchema = getSortOrderSchema(ColumnSchema.SortOrder.ASC);
    YBTable table = BaseYBClientTest.createTable(tableName, ascOrderSchema, null);
    for (ColumnSchema columnSchema : table.getSchema().getColumns()) {
      if (columnSchema.getName().equals("key1") || columnSchema.getName().equals("key2")) {
        // Default sort order for range keys is ASC.
        assertEquals(ColumnSchema.SortOrder.ASC, columnSchema.getSortOrder());
      } else {
        assertEquals(ColumnSchema.SortOrder.NONE, columnSchema.getSortOrder());
      }
    }
  }

  @Test
  public void testSortOrderDesc() {
    String tableName = BASE_TABLE_NAME + System.currentTimeMillis();
    Schema descOrderSchema = getSortOrderSchema(ColumnSchema.SortOrder.DESC);
    YBTable table = BaseYBClientTest.createTable(tableName, descOrderSchema, null);
    for (ColumnSchema columnSchema : table.getSchema().getColumns()) {
      if (columnSchema.getName().equals("key1")) {
        assertEquals(ColumnSchema.SortOrder.DESC, columnSchema.getSortOrder());
      }  else if (columnSchema.getName().equals("key2")) {
        // Default sort order for range keys is ASC.
        assertEquals(ColumnSchema.SortOrder.ASC, columnSchema.getSortOrder());
      } else {
        assertEquals(ColumnSchema.SortOrder.NONE, columnSchema.getSortOrder());
      }
    }
  }

  @Ignore
  public void testBadSchema() {
    // Test creating a table with keys in the wrong order
    List<ColumnSchema> badColumns = new ArrayList<ColumnSchema>(2);
    badColumns.add(new ColumnSchema.ColumnSchemaBuilder("not_key", Type.STRING).build());
    badColumns.add(new ColumnSchema.ColumnSchemaBuilder("key", Type.STRING)
        .key(true)
        .build());
    new Schema(badColumns);
  }

  @Ignore
  public void testAlterTable() throws Exception {
    String tableName = BASE_TABLE_NAME + System.currentTimeMillis();
    createTable(tableName, basicSchema, null);

    // Add a col.
    AlterTableOptions ato = new AlterTableOptions().addColumn("testaddint", Type.INT32, 4);
    submitAlterAndCheck(ato, tableName);

    // Rename that col.
    ato = new AlterTableOptions().renameColumn("testaddint", "newtestaddint");
    submitAlterAndCheck(ato, tableName);

    // Delete it.
    ato = new AlterTableOptions().dropColumn("newtestaddint");
    submitAlterAndCheck(ato, tableName);

    String newTableName = tableName +"new";

    // Rename our table.
    ato = new AlterTableOptions().renameTable(newTableName);
    submitAlterAndCheck(ato, tableName, newTableName);

    // Rename it back.
    ato = new AlterTableOptions().renameTable(tableName);
    submitAlterAndCheck(ato, newTableName, tableName);

    // Try adding two columns, where one is nullable.
    ato = new AlterTableOptions()
        .addColumn("testaddmulticolnotnull", Type.INT32, 4)
        .addNullableColumn("testaddmulticolnull", Type.STRING);
    submitAlterAndCheck(ato, tableName);
  }

  /**
   * Helper method to submit an Alter and wait for it to happen, using the default table name to
   * check.
   */
  private void submitAlterAndCheck(AlterTableOptions ato, String tableToAlter)
      throws Exception {
    submitAlterAndCheck(ato, tableToAlter, tableToAlter);
  }

  private void submitAlterAndCheck(AlterTableOptions ato,
                                         String tableToAlter, String tableToCheck) throws
      Exception {
    if (masterHostPorts.size() > 1) {
      LOG.info("Alter table is not yet supported with multiple masters. Specify " +
          "-DnumMasters=1 on the command line to start a single-master cluster to run this test.");
      return;
    }
    AlterTableResponse alterResponse = syncClient.alterTable(tableToAlter, ato);
    boolean done  = syncClient.isAlterTableDone(tableToCheck);
    assertTrue(done);
  }

  /**
   * Test creating tables of different sizes and see that we get the correct number of tablets back
   * @throws Exception
   */
  @Ignore
  public void testGetLocations() throws Exception {
    String table1 = BASE_TABLE_NAME + System.currentTimeMillis();

    // Test a non-existing table
    try {
      openTable(table1);
      fail("Should receive an exception since the table doesn't exist");
    } catch (Exception ex) {
      // expected
    }
    // Test with defaults
    String tableWithDefault = BASE_TABLE_NAME + "WithDefault" + System.currentTimeMillis();
    CreateTableOptions builder = new CreateTableOptions();
    List<ColumnSchema> columns = new ArrayList<ColumnSchema>(schema.getColumnCount());
    int defaultInt = 30;
    String defaultString = "data";
    for (ColumnSchema columnSchema : schema.getColumns()) {

      Object defaultValue;

      if (columnSchema.getType() == Type.INT32) {
        defaultValue = defaultInt;
      } else if (columnSchema.getType() == Type.BOOL) {
        defaultValue = true;
      } else {
        defaultValue = defaultString;
      }
      columns.add(
          new ColumnSchema.ColumnSchemaBuilder(columnSchema.getName(), columnSchema.getType())
              .key(columnSchema.isKey())
              .nullable(columnSchema.isNullable())
              .defaultValue(defaultValue).build());
    }
    Schema schemaWithDefault = new Schema(columns);
    YBTable kuduTable = createTable(tableWithDefault, schemaWithDefault, builder);
    assertEquals(defaultInt, kuduTable.getSchema().getColumnByIndex(0).getDefaultValue());
    assertEquals(defaultString,
        kuduTable.getSchema().getColumnByIndex(columns.size() - 2).getDefaultValue());
    assertEquals(true,
            kuduTable.getSchema().getColumnByIndex(columns.size() - 1).getDefaultValue());

    // Make sure the table's schema includes column IDs.
    assertTrue(kuduTable.getSchema().hasColumnIds());

    // Test we can open a table that was already created.
    openTable(tableWithDefault);

    // Test splitting and reading those splits
    YBTable kuduTableWithoutDefaults = createTableWithSplitsAndTest(0);
    // finish testing read defaults
    assertNull(kuduTableWithoutDefaults.getSchema().getColumnByIndex(0).getDefaultValue());
    createTableWithSplitsAndTest(3);
    createTableWithSplitsAndTest(10);

    YBTable table = createTableWithSplitsAndTest(30);

    List<LocatedTablet>tablets = table.getTabletsLocations(null, getKeyInBytes(9), DEFAULT_SLEEP);
    assertEquals(10, tablets.size());
    assertEquals(10, table.asyncGetTabletsLocations(null, getKeyInBytes(9), DEFAULT_SLEEP).join().size());

    tablets = table.getTabletsLocations(getKeyInBytes(0), getKeyInBytes(9), DEFAULT_SLEEP);
    assertEquals(10, tablets.size());
    assertEquals(10, table.asyncGetTabletsLocations(getKeyInBytes(0), getKeyInBytes(9), DEFAULT_SLEEP).join().size());

    tablets = table.getTabletsLocations(getKeyInBytes(5), getKeyInBytes(9), DEFAULT_SLEEP);
    assertEquals(5, tablets.size());
    assertEquals(5, table.asyncGetTabletsLocations(getKeyInBytes(5), getKeyInBytes(9), DEFAULT_SLEEP).join().size());

    tablets = table.getTabletsLocations(getKeyInBytes(5), getKeyInBytes(14), DEFAULT_SLEEP);
    assertEquals(10, tablets.size());
    assertEquals(10, table.asyncGetTabletsLocations(getKeyInBytes(5), getKeyInBytes(14), DEFAULT_SLEEP).join().size());

    tablets = table.getTabletsLocations(getKeyInBytes(5), getKeyInBytes(31), DEFAULT_SLEEP);
    assertEquals(26, tablets.size());
    assertEquals(26, table.asyncGetTabletsLocations(getKeyInBytes(5), getKeyInBytes(31), DEFAULT_SLEEP).join().size());

    tablets = table.getTabletsLocations(getKeyInBytes(5), null, DEFAULT_SLEEP);
    assertEquals(26, tablets.size());
    assertEquals(26, table.asyncGetTabletsLocations(getKeyInBytes(5), null, DEFAULT_SLEEP).join().size());

    tablets = table.getTabletsLocations(null, getKeyInBytes(10000), DEFAULT_SLEEP);
    assertEquals(31, tablets.size());
    assertEquals(31, table.asyncGetTabletsLocations(null, getKeyInBytes(10000), DEFAULT_SLEEP).join().size());

    tablets = table.getTabletsLocations(getKeyInBytes(20), getKeyInBytes(10000), DEFAULT_SLEEP);
    assertEquals(11, tablets.size());
    assertEquals(11, table.asyncGetTabletsLocations(getKeyInBytes(20), getKeyInBytes(10000), DEFAULT_SLEEP).join().size());

    // Test listing tables.
    assertEquals(0, client.getTablesList(table1).join(DEFAULT_SLEEP).getTablesList().size());
    assertEquals(1, client.getTablesList(tableWithDefault)
                          .join(DEFAULT_SLEEP).getTablesList().size());
    assertEquals(6, client.getTablesList().join(DEFAULT_SLEEP).getTablesList().size());
    assertFalse(client.getTablesList(tableWithDefault).
        join(DEFAULT_SLEEP).getTablesList().isEmpty());

    assertFalse(client.tableExists(table1).join(DEFAULT_SLEEP));
    assertTrue(client.tableExists(tableWithDefault).join(DEFAULT_SLEEP));
  }

  public byte[] getKeyInBytes(int i) {
    PartialRow row = schema.newPartialRow();
    row.addInt(0, i);
    return row.encodePrimaryKey();
  }

  public YBTable createTableWithSplitsAndTest(int splitsCount) throws Exception {
    String tableName = BASE_TABLE_NAME + System.currentTimeMillis();
    CreateTableOptions builder = new CreateTableOptions();

    if (splitsCount != 0) {
      for (int i = 1; i <= splitsCount; i++) {
        PartialRow row = schema.newPartialRow();
        row.addInt(0, i);
        builder.addSplitRow(row);
      }
    }
    YBTable table = createTable(tableName, schema, builder);

    // calling getTabletsLocation won't wait on the table to be assigned so we trigger the wait
    // by scanning
    countRowsInScan(client.newScannerBuilder(table).build());

    List<LocatedTablet> tablets = table.getTabletsLocations(DEFAULT_SLEEP);
    assertEquals(splitsCount + 1, tablets.size());
    assertEquals(splitsCount + 1, table.asyncGetTabletsLocations(DEFAULT_SLEEP).join().size());
    for (LocatedTablet tablet : tablets) {
      assertEquals(3, tablet.getReplicas().size());
    }
    return table;
  }
}
