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

import org.junit.AfterClass;
import org.yb.ColumnSchema;
import org.yb.Common;
import org.yb.Schema;
import org.yb.Type;
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

  public byte[] getKeyInBytes(int i) {
    PartialRow row = schema.newPartialRow();
    row.addInt(0, i);
    return row.encodePrimaryKey();
  }
}
