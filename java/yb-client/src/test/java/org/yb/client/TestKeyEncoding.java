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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
package org.yb.client;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

import com.google.common.base.Charsets;
import com.google.common.collect.ImmutableList;
import org.junit.Ignore;
import org.junit.Test;
import org.yb.ColumnSchema;
import org.yb.ColumnSchema.ColumnSchemaBuilder;
import org.yb.Common;
import org.yb.Common.PartitionSchemaPB.HashSchema;
import org.yb.Schema;
import org.yb.Type;
import org.yb.client.PartitionSchema.HashBucketSchema;
import org.yb.client.PartitionSchema.RangeSchema;

import java.util.ArrayList;
import java.util.List;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
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
                            schema, HashSchema.MULTI_COLUMN_HASH_SCHEMA);

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
