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

import org.yb.Schema;
import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;
import org.yb.Common.PartitionSchemaPB.HashSchema;

import java.util.List;

/**
 * A partition schema describes how the rows of a table are distributed among
 * tablets.
 *
 * Primarily, a table's partition schema is responsible for translating the
 * primary key column values of a row into a partition key that can be used to
 * find the tablet containing the key.
 *
 * The partition schema is made up of zero or more hash bucket components,
 * followed by a single range component.
 *
 * Each hash bucket component includes one or more columns from the primary key
 * column set, with the restriction that an individual primary key column may
 * only be included in a single hash component.
 *
 * This class is new, and not considered stable or suitable for public use.
 */
@InterfaceAudience.LimitedPrivate("Impala")
@InterfaceStability.Unstable
public class PartitionSchema {

  private final RangeSchema rangeSchema;
  private final List<HashBucketSchema> hashBucketSchemas;
  private final boolean isSimple;
  private final HashSchema hashSchema;

  /**
   * Creates a new partition schema from the range and hash bucket schemas.
   *
   * @param rangeSchema the range schema
   * @param hashBucketSchemas the hash bucket schemas
   * @param schema the table schema
   * @param hashSchema the hash schema
   */
  PartitionSchema(RangeSchema rangeSchema,
                  List<HashBucketSchema> hashBucketSchemas,
                  Schema schema,
                  HashSchema hashSchema) {
    this.rangeSchema = rangeSchema;
    this.hashBucketSchemas = hashBucketSchemas;
    this.hashSchema = hashSchema;

    boolean isSimple = hashBucketSchemas.isEmpty()
        && rangeSchema.columns.size() == schema.getPrimaryKeyColumnCount();
    if (isSimple) {
      int i = 0;
      for (Integer id : rangeSchema.columns) {
        if (schema.getColumnIndex(id) != i++) {
          isSimple = false;
          break;
        }
      }
    }
    this.isSimple = isSimple;
  }

  /**
   * Returns the encoded partition key of the row.
   * @return a byte array containing the encoded partition key of the row
   */
  public byte[] encodePartitionKey(PartialRow row) {
    return new KeyEncoder().encodePartitionKey(row, this);
  }

  public RangeSchema getRangeSchema() {
    return rangeSchema;
  }

  public List<HashBucketSchema> getHashBucketSchemas() {
    return hashBucketSchemas;
  }

  public HashSchema getHashSchema() { return hashSchema; }

  /**
   * Returns true if the partition schema if the partition schema does not include any hash
   * components, and the range columns match the table's primary key columns.
   *
   * @return whether the partition schema is the default simple range partitioning.
   */
  boolean isSimpleRangePartitioning() {
    return isSimple;
  }

  public static class RangeSchema {
    private final List<Integer> columns;

    RangeSchema(List<Integer> columns) {
      this.columns = columns;
    }

    public List<Integer> getColumns() {
      return columns;
    }
  }

  public static class HashBucketSchema {
    private final List<Integer> columnIds;
    private int numBuckets;
    private int seed;

    HashBucketSchema(List<Integer> columnIds, int numBuckets, int seed) {
      this.columnIds = columnIds;
      this.numBuckets = numBuckets;
      this.seed = seed;
    }

    /**
     * Gets the column IDs of the columns in the hash partition.
     * @return the column IDs of the columns in the has partition
     */
    public List<Integer> getColumnIds() {
      return columnIds;
    }

    public int getNumBuckets() {
      return numBuckets;
    }

    public int getSeed() {
      return seed;
    }
  }
}
