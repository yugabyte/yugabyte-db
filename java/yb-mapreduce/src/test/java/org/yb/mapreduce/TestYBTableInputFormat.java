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
package org.yb.mapreduce;

import com.google.common.collect.Iterables;
import com.google.common.collect.Lists;
import org.yb.Schema;
import org.yb.client.*;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.io.NullWritable;
import org.apache.hadoop.mapreduce.InputSplit;
import org.apache.hadoop.mapreduce.RecordReader;
import org.junit.Test;

import java.io.IOException;
import java.util.List;

import static org.junit.Assert.*;

public class TestYBTableInputFormat extends BaseYBClientTest {

  private static final String TABLE_NAME =
      TestYBTableInputFormat.class.getName() + "-" + System.currentTimeMillis();

  @Test
  public void test() throws Exception {
    createTable(TABLE_NAME, getBasicSchema(), new CreateTableOptions());

    YBTable table = openTable(TABLE_NAME);
    Schema schema = getBasicSchema();
    Insert insert = table.newInsert();
    PartialRow row = insert.getRow();
    row.addInt(0, 1);
    row.addInt(1, 2);
    row.addInt(2, 3);
    row.addString(3, "a string");
    row.addBoolean(4, true);
    AsyncYBSession session = client.newSession();
    session.apply(insert).join(DEFAULT_SLEEP);
    session.close().join(DEFAULT_SLEEP);

    // Test getting all the columns back
    RecordReader<NullWritable, RowResult> reader = createRecordReader("*", null);
    assertTrue(reader.nextKeyValue());
    assertEquals(5, reader.getCurrentValue().getColumnProjection().getColumnCount());
    assertFalse(reader.nextKeyValue());

    // Test getting two columns back
    reader = createRecordReader(schema.getColumnByIndex(3).getName() + "," +
        schema.getColumnByIndex(2).getName(), null);
    assertTrue(reader.nextKeyValue());
    assertEquals(2, reader.getCurrentValue().getColumnProjection().getColumnCount());
    assertEquals("a string", reader.getCurrentValue().getString(0));
    assertEquals(3, reader.getCurrentValue().getInt(1));
    try {
      reader.getCurrentValue().getString(2);
      fail("Should only be getting 2 columns back");
    } catch (IndexOutOfBoundsException e) {
      // expected
    }

    // Test getting one column back
    reader = createRecordReader(schema.getColumnByIndex(1).getName(), null);
    assertTrue(reader.nextKeyValue());
    assertEquals(1, reader.getCurrentValue().getColumnProjection().getColumnCount());
    assertEquals(2, reader.getCurrentValue().getInt(0));
    try {
      reader.getCurrentValue().getString(1);
      fail("Should only be getting 1 column back");
    } catch (IndexOutOfBoundsException e) {
      // expected
    }

    // Test getting empty rows back
    reader = createRecordReader("", null);
    assertTrue(reader.nextKeyValue());
    assertEquals(0, reader.getCurrentValue().getColumnProjection().getColumnCount());
    assertFalse(reader.nextKeyValue());

    // Test getting an unknown table, will not work
    try {
      createRecordReader("unknown", null);
      fail("Should not be able to scan a column that doesn't exist");
    } catch (IllegalArgumentException e) {
      // expected
    }

    // Test using a predicate that filters the row out.
    ColumnRangePredicate pred1 = new ColumnRangePredicate(schema.getColumnByIndex(1));
    pred1.setLowerBound(3);
    reader = createRecordReader("*", Lists.newArrayList(pred1));
    assertFalse(reader.nextKeyValue());
  }

  private RecordReader<NullWritable, RowResult> createRecordReader(String columnProjection,
        List<ColumnRangePredicate> predicates) throws IOException, InterruptedException {
    YBTableInputFormat input = new YBTableInputFormat();
    Configuration conf = new Configuration();
    conf.set(YBTableInputFormat.MASTER_ADDRESSES_KEY, getMasterAddresses());
    conf.set(YBTableInputFormat.INPUT_TABLE_KEY, TABLE_NAME);
    if (columnProjection != null) {
      conf.set(YBTableInputFormat.COLUMN_PROJECTION_KEY, columnProjection);
    }
    if (predicates != null) {
      String encodedPredicates = YBTableMapReduceUtil.base64EncodePredicates(predicates);
      conf.set(YBTableInputFormat.ENCODED_COLUMN_RANGE_PREDICATES_KEY, encodedPredicates);
    }
    input.setConf(conf);
    List<InputSplit> splits = input.getSplits(null);

    // We need to re-create the input format to reconnect the client.
    input = new YBTableInputFormat();
    input.setConf(conf);
    RecordReader<NullWritable, RowResult> reader = input.createRecordReader(null, null);
    reader.initialize(Iterables.getOnlyElement(splits), null);
    return reader;
  }
}
