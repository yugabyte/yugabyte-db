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
import static org.junit.Assert.assertTrue;

import com.google.common.base.Charsets;
import com.google.common.collect.ImmutableList;
import org.junit.Test;
import org.kududb.ColumnSchema;
import org.kududb.ColumnSchema.ColumnSchemaBuilder;
import org.kududb.Common;
import org.kududb.Schema;
import org.kududb.Type;
import org.kududb.client.PartitionSchema.HashBucketSchema;
import org.kududb.client.PartitionSchema.RangeSchema;

import java.util.ArrayList;
import java.util.List;

public class TestKeyEncoding {

  private static Schema buildSchema(ColumnSchemaBuilder... columns) {
    int i = 0;
    Common.SchemaPB.Builder pb = Common.SchemaPB.newBuilder();
    for (ColumnSchemaBuilder column : columns) {
      Common.ColumnSchemaPB.Builder columnPb =
          ProtobufHelper.columnToPb(column.build()).toBuilder();
      columnPb.setId(i++);
      pb.addColumns(columnPb);
    }
    return ProtobufHelper.pbToSchema(pb.build());
  }

  private static void assertBytesEquals(byte[] actual, byte[] expected) {
    assertTrue(String.format("expected: '%s', got '%s'",
                             Bytes.pretty(expected),
                             Bytes.pretty(actual)),
               Bytes.equals(expected, actual));
  }

  private static void assertBytesEquals(byte[] actual, String expected) {
    assertBytesEquals(actual, expected.getBytes(Charsets.UTF_8));
  }

  /**
   * Builds the default partition schema for a schema.
   * @param schema the schema
   * @return a default partition schema
   */
  private PartitionSchema defaultPartitionSchema(Schema schema) {
    List<Integer> columnIds = new ArrayList<>();
    for (int i = 0; i < schema.getPrimaryKeyColumnCount(); i++) {
      // Schema does not provide a way to lookup a column ID by column index,
      // so instead we assume that the IDs for the primary key columns match
      // their respective index, which holds up when the schema is created
      // with buildSchema.
      columnIds.add(i);
    }
    return new PartitionSchema(
        new PartitionSchema.RangeSchema(columnIds),
        ImmutableList.<PartitionSchema.HashBucketSchema>of(), schema);
  }

  @Test
  public void testPrimaryKeys() {
    Schema schemaOneString =
        buildSchema(new ColumnSchema.ColumnSchemaBuilder("key", Type.STRING).key(true));
    KuduTable table = new KuduTable(null, "one", "one", schemaOneString,
                                    defaultPartitionSchema(schemaOneString));
    Insert oneKeyInsert = new Insert(table);
    PartialRow row = oneKeyInsert.getRow();
    row.addString("key", "foo");
    assertBytesEquals(row.encodePrimaryKey(), "foo");

    Schema schemaTwoString = buildSchema(
        new ColumnSchema.ColumnSchemaBuilder("key", Type.STRING).key(true),
        new ColumnSchema.ColumnSchemaBuilder("key2", Type.STRING).key(true));
    KuduTable table2 = new KuduTable(null, "two", "two", schemaTwoString,
                                     defaultPartitionSchema(schemaTwoString));
    Insert twoKeyInsert = new Insert(table2);
    row = twoKeyInsert.getRow();
    row.addString("key", "foo");
    row.addString("key2", "bar");
    assertBytesEquals(row.encodePrimaryKey(), "foo\0\0bar");

    Insert twoKeyInsertWithNull = new Insert(table2);
    row = twoKeyInsertWithNull.getRow();
    row.addString("key", "xxx\0yyy");
    row.addString("key2", "bar");
    assertBytesEquals(row.encodePrimaryKey(), "xxx\0\1yyy\0\0bar");

    // test that we get the correct memcmp result, the bytes are in big-endian order in a key
    Schema schemaIntString = buildSchema(
        new ColumnSchema.ColumnSchemaBuilder("key", Type.INT32).key(true),
        new ColumnSchema.ColumnSchemaBuilder("key2", Type.STRING).key(true));
    PartitionSchema partitionSchemaIntString = defaultPartitionSchema(schemaIntString);
    KuduTable table3 = new KuduTable(null, "three", "three",
        schemaIntString, partitionSchemaIntString);
    Insert small = new Insert(table3);
    row = small.getRow();
    row.addInt("key", 20);
    row.addString("key2", "data");
    byte[] smallPK = small.getRow().encodePrimaryKey();
    assertEquals(0, Bytes.memcmp(smallPK, smallPK));

    Insert big = new Insert(table3);
    row = big.getRow();
    row.addInt("key", 10000);
    row.addString("key2", "data");
    byte[] bigPK = big.getRow().encodePrimaryKey();
    assertTrue(Bytes.memcmp(smallPK, bigPK) < 0);
    assertTrue(Bytes.memcmp(bigPK, smallPK) > 0);

    // The following tests test our assumptions on unsigned data types sorting from KeyEncoder
    byte four = 4;
    byte onHundredTwentyFour = -4;
    four = Bytes.xorLeftMostBit(four);
    onHundredTwentyFour = Bytes.xorLeftMostBit(onHundredTwentyFour);
    assertTrue(four < onHundredTwentyFour);

    byte[] threeHundred = Bytes.fromInt(300);
    byte[] reallyBigNumber = Bytes.fromInt(-300);
    threeHundred[0] = Bytes.xorLeftMostBit(threeHundred[0]);
    reallyBigNumber[3] = Bytes.xorLeftMostBit(reallyBigNumber[3]);
    assertTrue(Bytes.memcmp(threeHundred, reallyBigNumber) < 0);
  }

  @Test
  public void testPrimaryKeyEncoding() {
    Schema schema = buildSchema(
        new ColumnSchemaBuilder("int8", Type.INT8).key(true),
        new ColumnSchemaBuilder("int16", Type.INT16).key(true),
        new ColumnSchemaBuilder("int32", Type.INT32).key(true),
        new ColumnSchemaBuilder("int64", Type.INT64).key(true),
        new ColumnSchemaBuilder("string", Type.STRING).key(true),
        new ColumnSchemaBuilder("binary", Type.BINARY).key(true));

    PartialRow rowA = schema.newPartialRow();
    rowA.addByte("int8", Byte.MIN_VALUE);
    rowA.addShort("int16", Short.MIN_VALUE);
    rowA.addInt("int32", Integer.MIN_VALUE);
    rowA.addLong("int64", Long.MIN_VALUE);
    rowA.addString("string", "");
    rowA.addBinary("binary", "".getBytes(Charsets.UTF_8));

    assertBytesEquals(rowA.encodePrimaryKey(),
                      "\0"
                    + "\0\0"
                    + "\0\0\0\0"
                    + "\0\0\0\0\0\0\0\0"
                    + "\0\0"
                    + "");

    PartialRow rowB = schema.newPartialRow();
    rowB.addByte("int8", Byte.MAX_VALUE);
    rowB.addShort("int16", Short.MAX_VALUE);
    rowB.addInt("int32", Integer.MAX_VALUE);
    rowB.addLong("int64", Long.MAX_VALUE);
    rowB.addString("string", "abc\1\0def");
    rowB.addBinary("binary", "\0\1binary".getBytes(Charsets.UTF_8));

    assertBytesEquals(rowB.encodePrimaryKey(),
                      new byte[] {
                          -1,
                          -1, -1,
                          -1, -1, -1, -1,
                          -1, -1, -1, -1, -1, -1, -1, -1,
                          'a', 'b', 'c', 1, 0, 1, 'd', 'e', 'f', 0, 0,
                          0, 1, 'b', 'i', 'n', 'a', 'r', 'y',
                      });

    PartialRow rowC = schema.newPartialRow();
    rowC.addByte("int8", (byte) 1);
    rowC.addShort("int16", (short) 2);
    rowC.addInt("int32", 3);
    rowC.addLong("int64", 4);
    rowC.addString("string", "abc\n123");
    rowC.addBinary("binary", "\0\1\2\3\4\5".getBytes(Charsets.UTF_8));

    assertBytesEquals(rowC.encodePrimaryKey(),
                      new byte[] {
                          (byte) 0x81,
                          (byte) 0x80, 2,
                          (byte) 0x80, 0, 0, 3,
                          (byte) 0x80, 0, 0, 0, 0, 0, 0, 4,
                          'a', 'b', 'c', '\n', '1', '2', '3', 0, 0,
                          0, 1, 2, 3, 4, 5,
                      });
  }

  @Test
  public void testPartitionKeyEncoding() {
    KeyEncoder encoder = new KeyEncoder();
    Schema schema = buildSchema(
        new ColumnSchemaBuilder("a", Type.INT32).key(true),
        new ColumnSchemaBuilder("b", Type.STRING).key(true),
        new ColumnSchemaBuilder("c", Type.STRING).key(true));

    PartitionSchema partitionSchema =
        new PartitionSchema(new RangeSchema(ImmutableList.of(0, 1, 2)),
                            ImmutableList.of(
                                new HashBucketSchema(ImmutableList.of(0, 1), 32, 0),
                                new HashBucketSchema(ImmutableList.of(2), 32, 42)),
                            schema);

    PartialRow rowA = schema.newPartialRow();
    rowA.addInt("a", 0);
    rowA.addString("b", "");
    rowA.addString("c", "");
    assertBytesEquals(encoder.encodePartitionKey(rowA, partitionSchema),
                      new byte[]{
                          0, 0, 0, 0,           // hash(0, "")
                          0, 0, 0, 0x14,        // hash("")
                          (byte) 0x80, 0, 0, 0, // a = 0
                          0, 0,                 // b = ""; c is elided
                      });

    PartialRow rowB = schema.newPartialRow();
    rowB.addInt("a", 1);
    rowB.addString("b", "");
    rowB.addString("c", "");
    assertBytesEquals(encoder.encodePartitionKey(rowB, partitionSchema),
                      new byte[]{
                          0, 0, 0, 0x5,         // hash(1, "")
                          0, 0, 0, 0x14,        // hash("")
                          (byte) 0x80, 0, 0, 1, // a = 0
                          0, 0,                 // b = ""; c is elided
                      });

    PartialRow rowC = schema.newPartialRow();
    rowC.addInt("a", 0);
    rowC.addString("b", "b");
    rowC.addString("c", "c");
    assertBytesEquals(encoder.encodePartitionKey(rowC, partitionSchema),
                      new byte[]{
                          0, 0, 0, 0x1A,        // hash(0, "b")
                          0, 0, 0, 0x1D,        // hash("c")
                          (byte) 0x80, 0, 0, 0, // a = 1
                          'b', 0, 0,            // b = "b"
                          'c'                   // b = "c"
                      });

    PartialRow rowD = schema.newPartialRow();
    rowD.addInt("a", 1);
    rowD.addString("b", "b");
    rowD.addString("c", "c");
    assertBytesEquals(encoder.encodePartitionKey(rowD, partitionSchema),
                      new byte[]{
                          0, 0, 0, 0,           // hash(1, "b")
                          0, 0, 0, 0x1D,        // hash("c")
                          (byte) 0x80, 0, 0, 1, // a = 0
                          'b', 0, 0,            // b = "b"
                          'c'                   // b = "c"
                      });
  }
}
