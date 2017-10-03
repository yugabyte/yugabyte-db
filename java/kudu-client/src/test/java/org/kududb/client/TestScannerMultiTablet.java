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

import com.google.common.collect.Lists;
import com.stumbleupon.async.Deferred;
import org.kududb.ColumnSchema;
import org.kududb.Schema;
import org.junit.BeforeClass;
import org.junit.Test;

import java.util.ArrayList;

import static org.junit.Assert.assertNull;
import static org.kududb.Type.STRING;
import static org.junit.Assert.assertEquals;

public class TestScannerMultiTablet extends BaseKuduTest {
  // Generate a unique table name
  private static final String TABLE_NAME =
      TestScannerMultiTablet.class.getName()+"-"+System.currentTimeMillis();

  private static Schema schema = getSchema();
  private static KuduTable table;

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    BaseKuduTest.setUpBeforeClass();
    // create a 4-tablets table for scanning
    CreateTableOptions builder = new CreateTableOptions();

    for (int i = 1; i < 4; i++){
      PartialRow splitRow = schema.newPartialRow();
      splitRow.addString("key1", "" + i);
      splitRow.addString("key2", "");
      builder.addSplitRow(splitRow);
    }

    createTable(TABLE_NAME, schema, builder);

    table = openTable(TABLE_NAME);

    AsyncKuduSession session = client.newSession();
    session.setFlushMode(AsyncKuduSession.FlushMode.AUTO_FLUSH_SYNC);

    // The data layout ends up like this:
    // tablet '', '1': no rows
    // tablet '1', '2': '111', '122', '133'
    // tablet '2', '3': '211', '222', '233'
    // tablet '3', '': '311', '322', '333'
    String[] keys = new String[] {"1", "2", "3"};
    for (String key1 : keys) {
      for (String key2 : keys) {
        Insert insert = table.newInsert();
        PartialRow row = insert.getRow();
        row.addString(0, key1);
        row.addString(1, key2);
        row.addString(2, key2);
        Deferred<OperationResponse> d = session.apply(insert);
        d.join(DEFAULT_SLEEP);
      }
    }
  }

  // Test various combinations of start/end row keys.
  @Test(timeout = 100000)
  public void testKeyStartEnd() throws Exception {
    assertEquals(0,
        countRowsInScan(getScanner("", "", "1", ""))); // There's nothing in the 1st tablet
    assertEquals(1, countRowsInScan(getScanner("", "", "1", "2"))); // Grab the very first row
    assertEquals(3, countRowsInScan(getScanner("1", "1", "1", "4"))); // Grab the whole 2nd tablet
    assertEquals(3, countRowsInScan(getScanner("1", "1", "2", ""))); // Same, and peek at the 3rd
    assertEquals(3, countRowsInScan(getScanner("1", "1", "2", "0"))); // Same, different peek
    assertEquals(4,
        countRowsInScan(getScanner("1", "2", "2", "3"))); // Middle of 2nd to middle of 3rd
    assertEquals(3,
        countRowsInScan(getScanner("1", "4", "2", "4"))); // Peek at the 2nd then whole 3rd
    assertEquals(6, countRowsInScan(getScanner("1", "5", "3", "4"))); // Whole 3rd and 4th
    assertEquals(9, countRowsInScan(getScanner("", "", "4", ""))); // Full table scan

    assertEquals(9,
        countRowsInScan(getScanner("", "", null, null))); // Full table scan with empty upper
    assertEquals(9,
        countRowsInScan(getScanner(null, null, "4", ""))); // Full table scan with empty lower
    assertEquals(9,
        countRowsInScan(getScanner(null, null, null, null))); // Full table scan with empty bounds

    // Test that we can close a scanner while in between two tablets. We start on the second
    // tablet and our first nextRows() will get 3 rows. At that moment we want to close the scanner
    // before getting on the 3rd tablet.
    AsyncKuduScanner scanner = getScanner("1", "", null, null);
    Deferred<RowResultIterator> d = scanner.nextRows();
    RowResultIterator rri = d.join(DEFAULT_SLEEP);
    assertEquals(3, rri.getNumRows());
    d = scanner.close();
    rri = d.join(DEFAULT_SLEEP);
    assertNull(rri);
  }

  // Test mixing start/end row keys with predicates.
  @Test(timeout = 100000)
  public void testKeysAndPredicates() throws Exception {
    // First row from the 2nd tablet.
    ColumnRangePredicate predicate = new ColumnRangePredicate(schema.getColumnByIndex(2));
    predicate.setLowerBound("1");
    predicate.setUpperBound("1");
    assertEquals(1, countRowsInScan(getScanner("1", "", "2", "", predicate)));

    // All the 2nd tablet.
    predicate = new ColumnRangePredicate(schema.getColumnByIndex(2));
    predicate.setLowerBound("1");
    predicate.setUpperBound("3");
    assertEquals(3, countRowsInScan(getScanner("1", "", "2", "", predicate)));

    // Value that doesn't exist.
    predicate = new ColumnRangePredicate(schema.getColumnByIndex(2));
    predicate.setLowerBound("4");
    assertEquals(0, countRowsInScan(getScanner("1", "", "2", "", predicate)));

    // First row from every tablet.
    predicate = new ColumnRangePredicate(schema.getColumnByIndex(2));
    predicate.setLowerBound("1");
    predicate.setUpperBound("1");
    assertEquals(3, countRowsInScan(getScanner(null, null, null, null, predicate)));

    // All the rows.
    predicate = new ColumnRangePredicate(schema.getColumnByIndex(2));
    predicate.setLowerBound("1");
    assertEquals(9, countRowsInScan(getScanner(null, null, null, null, predicate)));
  }

  @Test(timeout = 100000)
  public void testProjections() throws Exception {
    // Test with column names.
    AsyncKuduScanner.AsyncKuduScannerBuilder builder = client.newScannerBuilder(table);
    builder.setProjectedColumnNames(Lists.newArrayList(schema.getColumnByIndex(0).getName(),
        schema.getColumnByIndex(1).getName()));
    buildScannerAndCheckColumnsCount(builder, 2);

    // Test with column indexes.
    builder = client.newScannerBuilder(table);
    builder.setProjectedColumnIndexes(Lists.newArrayList(0, 1));
    buildScannerAndCheckColumnsCount(builder, 2);

    // Test with column names overriding indexes.
    builder = client.newScannerBuilder(table);
    builder.setProjectedColumnIndexes(Lists.newArrayList(0, 1));
    builder.setProjectedColumnNames(Lists.newArrayList(schema.getColumnByIndex(0).getName()));
    buildScannerAndCheckColumnsCount(builder, 1);
  }

  private AsyncKuduScanner getScanner(String lowerBoundKeyOne,
                                      String lowerBoundKeyTwo,
                                      String exclusiveUpperBoundKeyOne,
                                      String exclusiveUpperBoundKeyTwo) {
    return getScanner(lowerBoundKeyOne, lowerBoundKeyTwo,
        exclusiveUpperBoundKeyOne, exclusiveUpperBoundKeyTwo, null);
  }

  private AsyncKuduScanner getScanner(String lowerBoundKeyOne,
                                      String lowerBoundKeyTwo,
                                      String exclusiveUpperBoundKeyOne,
                                      String exclusiveUpperBoundKeyTwo,
                                      ColumnRangePredicate predicate) {
    AsyncKuduScanner.AsyncKuduScannerBuilder builder = client.newScannerBuilder(table);

    if (lowerBoundKeyOne != null) {
      PartialRow lowerBoundRow = schema.newPartialRow();
      lowerBoundRow.addString(0, lowerBoundKeyOne);
      lowerBoundRow.addString(1, lowerBoundKeyTwo);
      builder.lowerBound(lowerBoundRow);
    }

    if (exclusiveUpperBoundKeyOne != null) {
      PartialRow upperBoundRow = schema.newPartialRow();
      upperBoundRow.addString(0, exclusiveUpperBoundKeyOne);
      upperBoundRow.addString(1, exclusiveUpperBoundKeyTwo);
      builder.exclusiveUpperBound(upperBoundRow);
    }

    if (predicate != null) {
      builder.addColumnRangePredicate(predicate);
    }

    return builder.build();
  }

  private void buildScannerAndCheckColumnsCount(AsyncKuduScanner.AsyncKuduScannerBuilder builder,
                                                int count) throws Exception {
    AsyncKuduScanner scanner = builder.build();
    scanner.nextRows().join(DEFAULT_SLEEP);
    RowResultIterator rri = scanner.nextRows().join(DEFAULT_SLEEP);
    assertEquals(count, rri.next().getSchema().getColumns().size());
  }

  private static Schema getSchema() {
    ArrayList<ColumnSchema> columns = new ArrayList<>(3);
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key1", STRING)
        .key(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key2", STRING)
        .key(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("val", STRING)
        .build());
    return new Schema(columns);
  }
}
