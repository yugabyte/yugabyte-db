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
import static org.junit.Assert.fail;

import java.util.ArrayList;

import org.junit.Test;
import org.kududb.ColumnSchema;
import org.kududb.Schema;
import org.kududb.Type;
import org.kududb.WireProtocol.RowOperationsPB;
import org.kududb.client.Operation.ChangeType;
import org.kududb.tserver.Tserver.WriteRequestPBOrBuilder;
import org.mockito.Mockito;

import com.google.common.primitives.Longs;

/**
 * Unit tests for Operation
 */
public class TestOperation {

  private Schema createManyStringsSchema() {
    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>(4);
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c0", Type.STRING).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c1", Type.STRING).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c2", Type.STRING).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c3", Type.STRING).nullable(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c4", Type.STRING).nullable(true).build());
    return new Schema(columns);
  }

  @Test
  public void testSetStrings() {
    KuduTable table = Mockito.mock(KuduTable.class);
    Mockito.doReturn(createManyStringsSchema()).when(table).getSchema();
    Insert insert = new Insert(table);
    PartialRow row = insert.getRow();
    row.addString("c0", "c0_val");
    row.addString("c2", "c2_val");
    row.addString("c1", "c1_val");
    row.addString("c3", "c3_val");
    row.addString("c4", "c4_val");

    {
      WriteRequestPBOrBuilder pb = Operation.createAndFillWriteRequestPB(insert);
      RowOperationsPB rowOps = pb.getRowOperations();
      assertEquals(6 * 5, rowOps.getIndirectData().size());
      assertEquals("c0_valc1_valc2_valc3_valc4_val", rowOps.getIndirectData().toStringUtf8());
      byte[] rows = rowOps.getRows().toByteArray();
      assertEquals(ChangeType.INSERT.toEncodedByte(), rows[0]);
      // The "isset" bitset should have 5 bits set
      assertEquals(0x1f, rows[1]);
      // The "null" bitset should have no bits set
      assertEquals(0, rows[2]);

      // Check the strings.
      int offset = 3;
      for (int i = 0; i <= 4; i++) {
        // The offset into the indirect buffer
        assertEquals(6 * i, Bytes.getLong(rows, offset));
        offset += Longs.BYTES;
        // The length of the pointed-to string.
        assertEquals(6, Bytes.getLong(rows, offset));
        offset += Longs.BYTES;
      }

      // Should have used up whole buffer.
      assertEquals(rows.length, offset);
    }

    // Setting a field to NULL should add to the null bitmap and remove
    // the old value from the indirect buffer.
    row.setNull("c3");
    {
      WriteRequestPBOrBuilder pb = Operation.createAndFillWriteRequestPB(insert);
      RowOperationsPB rowOps = pb.getRowOperations();
      assertEquals(6 * 4, rowOps.getIndirectData().size());
      assertEquals("c0_valc1_valc2_valc4_val", rowOps.getIndirectData().toStringUtf8());
      byte[] rows = rowOps.getRows().toByteArray();
      assertEquals(ChangeType.INSERT.toEncodedByte(), rows[0]);
      // The "isset" bitset should have 5 bits set
      assertEquals(0x1f, rows[1]);
      // The "null" bitset should have 1 bit set for the null column
      assertEquals(1 << 3, rows[2]);

      // Check the strings.
      int offset = 3;
      int indirOffset = 0;
      for (int i = 0; i <= 4; i++) {
        if (i == 3) continue;
        // The offset into the indirect buffer
        assertEquals(indirOffset, Bytes.getLong(rows, offset));
        indirOffset += 6;
        offset += Longs.BYTES;
        // The length of the pointed-to string.
        assertEquals(6, Bytes.getLong(rows, offset));
        offset += Longs.BYTES;
      }
      // Should have used up whole buffer.
      assertEquals(rows.length, offset);
    }
  }

  private Schema createAllTypesKeySchema() {
    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>(7);
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c0", Type.INT8).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c1", Type.INT16).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c2", Type.INT32).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c3", Type.INT64).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c4", Type.TIMESTAMP).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c5", Type.STRING).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("c6", Type.BINARY).key(true).build());
    return new Schema(columns);
  }

  @Test
  public void testRowKeyStringify() {
    KuduTable table = Mockito.mock(KuduTable.class);
    Mockito.doReturn(createAllTypesKeySchema()).when(table).getSchema();
    Insert insert = new Insert(table);
    PartialRow row = insert.getRow();
    row.addByte("c0", (byte) 1);
    row.addShort("c1", (short) 2);
    row.addInt("c2", 3);
    row.addLong("c3", 4);
    row.addLong("c4", 5);
    row.addString("c5", "c5_val");
    row.addBinary("c6", Bytes.fromString("c6_val"));

    assertEquals("(int8 c0=1, int16 c1=2, int32 c2=3, int64 c3=4, timestamp c4=5, string" +
            " c5=c5_val, binary c6=\"c6_val\")",
        insert.getRow().stringifyRowKey());

    // Test an incomplete row key.
    insert = new Insert(table);
    row = insert.getRow();
    row.addByte("c0", (byte) 1);
    try {
      row.stringifyRowKey();
      fail("Should not be able to stringifyRowKey when not all keys are specified");
    } catch (IllegalStateException ise) {
      // Expected.
    }
  }
}
