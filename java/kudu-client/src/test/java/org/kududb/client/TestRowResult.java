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

import org.junit.BeforeClass;
import org.junit.Test;
import org.kududb.Type;

import java.nio.ByteBuffer;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class TestRowResult extends BaseKuduTest {

  // Generate a unique table name
  private static final String TABLE_NAME =
      TestRowResult.class.getName() + "-" + System.currentTimeMillis();

  private static KuduTable table;

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    BaseKuduTest.setUpBeforeClass();
    createTable(TABLE_NAME, allTypesSchema, new CreateTableOptions());

    table = openTable(TABLE_NAME);
  }

  @Test(timeout = 10000)
  public void test() throws Exception {
    Insert insert = table.newInsert();
    PartialRow row = insert.getRow();

    row.addByte(0, (byte) 1);
    row.addShort(1, (short) 2);
    row.addInt(2, 3);
    row.addLong(3, 4l);
    row.addBoolean(4, true);
    row.addFloat(5, 5.6f);
    row.addDouble(6, 7.8);
    row.addString(7, "string-value");
    row.addBinary(8, "binary-array".getBytes());
    ByteBuffer bb = ByteBuffer.wrap("binary-bytebuffer".getBytes());
    bb.position(7); // We're only inserting the bytebuffer part of the original array.
    row.addBinary(9, bb);
    row.setNull(10);
    row.addLong(11, 11l);

    KuduSession session = syncClient.newSession();
    session.apply(insert);

    KuduScanner scanner = syncClient.newScannerBuilder(table).build();
    while (scanner.hasMoreRows()) {
      RowResultIterator it = scanner.nextRows();
      assertTrue(it.hasNext());
      RowResult rr = it.next();

      assertEquals((byte) 1, rr.getByte(0));
      assertEquals((byte) 1, rr.getByte(allTypesSchema.getColumnByIndex(0).getName()));

      assertEquals((short) 2, rr.getShort(1));
      assertEquals((short) 2, rr.getShort(allTypesSchema.getColumnByIndex(1).getName()));

      assertEquals(3, rr.getInt(2));
      assertEquals(3, rr.getInt(allTypesSchema.getColumnByIndex(2).getName()));

      assertEquals(4, rr.getLong(3));
      assertEquals(4, rr.getLong(allTypesSchema.getColumnByIndex(3).getName()));

      assertEquals(true, rr.getBoolean(4));
      assertEquals(true, rr.getBoolean(allTypesSchema.getColumnByIndex(4).getName()));

      assertEquals(5.6f, rr.getFloat(5), .001f);
      assertEquals(5.6f, rr.getFloat(allTypesSchema.getColumnByIndex(5).getName()), .001f);

      assertEquals(7.8, rr.getDouble(6), .001);
      assertEquals(7.8, rr.getDouble(allTypesSchema.getColumnByIndex(6).getName()), .001f);

      assertEquals("string-value", rr.getString(7));
      assertEquals("string-value", rr.getString(allTypesSchema.getColumnByIndex(7).getName()));

      assertArrayEquals("binary-array".getBytes(), rr.getBinaryCopy(8));
      assertArrayEquals("binary-array".getBytes(),
          rr.getBinaryCopy(allTypesSchema.getColumnByIndex(8).getName()));

      ByteBuffer buffer = rr.getBinary(8);
      assertEquals(buffer, rr.getBinary(allTypesSchema.getColumnByIndex(8).getName()));
      byte[] binaryValue = new byte[buffer.remaining()];
      buffer.get(binaryValue);
      assertArrayEquals("binary-array".getBytes(), binaryValue);

      assertArrayEquals("bytebuffer".getBytes(), rr.getBinaryCopy(9));

      assertEquals(true, rr.isNull(10));
      assertEquals(true, rr.isNull(allTypesSchema.getColumnByIndex(10).getName()));

      assertEquals(11, rr.getLong(11));
      assertEquals(11, rr.getLong(allTypesSchema.getColumnByIndex(11).getName()));

      // We test with the column name once since it's the same method for all types, unlike above.
      assertEquals(Type.INT8, rr.getColumnType(allTypesSchema.getColumnByIndex(0).getName()));
      assertEquals(Type.INT8, rr.getColumnType(0));
      assertEquals(Type.INT16, rr.getColumnType(1));
      assertEquals(Type.INT32, rr.getColumnType(2));
      assertEquals(Type.INT64, rr.getColumnType(3));
      assertEquals(Type.BOOL, rr.getColumnType(4));
      assertEquals(Type.FLOAT, rr.getColumnType(5));
      assertEquals(Type.DOUBLE, rr.getColumnType(6));
      assertEquals(Type.STRING, rr.getColumnType(7));
      assertEquals(Type.BINARY, rr.getColumnType(8));
      assertEquals(Type.TIMESTAMP, rr.getColumnType(11));
    }
  }
}
