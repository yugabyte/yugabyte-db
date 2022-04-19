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

import org.yb.Common;
import org.yb.CommonTypes.TableType;

import com.google.common.collect.Lists;
import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;
import org.yb.master.MasterDdlOuterClass;

import java.util.List;

/**
 * This is a builder class for all the options that can be provided while creating a table.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class CreateTableOptions {

  private MasterDdlOuterClass.CreateTableRequestPB.Builder pb =
      MasterDdlOuterClass.CreateTableRequestPB.newBuilder();

  public CreateTableOptions() {
    pb.setTableType(TableType.YQL_TABLE_TYPE);
  }

  /**
   * Add a set of hash partitions to the table.
   *
   * Each column must be a part of the table's primary key, and an individual
   * column may only appear in a single hash component.
   *
   * For each set of hash partitions added to the table, the total number of
   * table partitions is multiplied by the number of buckets. For example, if a
   * table is created with 3 split rows, and two hash partitions with 4 and 5
   * buckets respectively, the total number of table partitions will be 80
   * (4 range partitions * 4 hash buckets * 5 hash buckets).
   *
   * @param columns the columns to hash
   * @param buckets the number of buckets to hash into
   * @return this instance
   */
  public CreateTableOptions addHashPartitions(List<String> columns, int buckets) {
    addHashPartitions(columns, buckets, 0);
    return this;
  }

  /**
   * Add a set of hash partitions to the table.
   *
   * This constructor takes a seed value, which can be used to randomize the
   * mapping of rows to hash buckets. Setting the seed may provide some
   * amount of protection against denial of service attacks when the hashed
   * columns contain user provided values.
   *
   * @param columns the columns to hash
   * @param buckets the number of buckets to hash into
   * @param seed a hash seed
   * @return this instance
   */
  public CreateTableOptions addHashPartitions(List<String> columns, int buckets, int seed) {
    Common.PartitionSchemaPB.HashBucketSchemaPB.Builder hashBucket =
        pb.getPartitionSchemaBuilder().addHashBucketSchemasBuilder();
    for (String column : columns) {
      hashBucket.addColumnsBuilder().setName(column);
    }
    hashBucket.setNumBuckets(buckets);
    hashBucket.setSeed(seed);
    return this;
  }

  /**
   * Set the columns on which the table will be range-partitioned.
   *
   * Every column must be a part of the table's primary key. If not set, the
   * table will be created with the primary-key columns as the range-partition
   * columns. If called with an empty vector, the table will be created without
   * range partitioning.
   *
   * @param columns the range partitioned columns
   * @return this instance
   */
  public CreateTableOptions setRangePartitionColumns(List<String> columns) {
    Common.PartitionSchemaPB.RangeSchemaPB.Builder rangePartition =
        pb.getPartitionSchemaBuilder().getRangeSchemaBuilder();
    for (String column : columns) {
      rangePartition.addColumnsBuilder().setName(column);
    }
    return this;
  }

  /**
   * Sets the number of tablets.
   *
   * @param numTablets the number of tablets to split the table.
   * @return this instance
   */
  public CreateTableOptions setNumTablets(int numTablets) {
    pb.getSchemaBuilder().getTablePropertiesBuilder().setNumTablets(numTablets);
    return this;
  }

  /**
   * Set the table type.
   *
   * @param tableType the table type to create
   * @return this instance
   */
  public CreateTableOptions setTableType(TableType tableType) {
    pb.setTableType(tableType);
    return this;
  }

  /**
   * Return the table type.
   *
   * @return the table type of current table.
   */
  public TableType getTableType() {
    return pb.getTableType();
  }

  MasterDdlOuterClass.CreateTableRequestPB.Builder getBuilder() {
    return pb;
  }
}
