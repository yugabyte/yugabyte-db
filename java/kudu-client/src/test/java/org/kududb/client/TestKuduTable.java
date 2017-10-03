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

import org.kududb.ColumnSchema;
import org.kududb.Schema;
import org.kududb.Type;
import org.junit.BeforeClass;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;

import static org.junit.Assert.*;

public class TestKuduTable extends BaseKuduTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestKuduTable.class);

  private static final String BASE_TABLE_NAME = TestKuduTable.class.getName();

  private static Schema schema = getBasicSchema();

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    BaseKuduTest.setUpBeforeClass();
  }

  @Test(expected = IllegalArgumentException.class)
  public void testBadSchema() {
    // Test creating a table with keys in the wrong order
    List<ColumnSchema> badColumns = new ArrayList<ColumnSchema>(2);
    badColumns.add(new ColumnSchema.ColumnSchemaBuilder("not_key", Type.STRING).build());
    badColumns.add(new ColumnSchema.ColumnSchemaBuilder("key", Type.STRING)
        .key(true)
        .build());
    new Schema(badColumns);
  }

  @Test(timeout = 100000)
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
  @Test
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
    KuduTable kuduTable = createTable(tableWithDefault, schemaWithDefault, builder);
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
    KuduTable kuduTableWithoutDefaults = createTableWithSplitsAndTest(0);
    // finish testing read defaults
    assertNull(kuduTableWithoutDefaults.getSchema().getColumnByIndex(0).getDefaultValue());
    createTableWithSplitsAndTest(3);
    createTableWithSplitsAndTest(10);

    KuduTable table = createTableWithSplitsAndTest(30);

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

  public KuduTable createTableWithSplitsAndTest(int splitsCount) throws Exception {
    String tableName = BASE_TABLE_NAME + System.currentTimeMillis();
    CreateTableOptions builder = new CreateTableOptions();

    if (splitsCount != 0) {
      for (int i = 1; i <= splitsCount; i++) {
        PartialRow row = schema.newPartialRow();
        row.addInt(0, i);
        builder.addSplitRow(row);
      }
    }
    KuduTable table = createTable(tableName, schema, builder);

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
