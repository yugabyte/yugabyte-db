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

import org.kududb.Common;
import com.google.common.collect.Lists;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.master.Master;

import java.util.List;

/**
 * This is a builder class for all the options that can be provided while creating a table.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class CreateTableOptions {

  private Master.CreateTableRequestPB.Builder pb = Master.CreateTableRequestPB.newBuilder();
  private final List<PartialRow> splitRows = Lists.newArrayList();

  /**
   * Add a split point for the table. The table in the end will have splits + 1 tablets.
   * The row may be reused or modified safely after this call without changing the split point.
   *
   * @param row a key row for the split point
   * @return this instance
   */
  public CreateTableOptions addSplitRow(PartialRow row) {
    splitRows.add(new PartialRow(row));
    return this;
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
   * Sets the number of replicas that each tablet will have. If not specified, it uses the
   * server-side default which is usually 3 unless changed by an administrator.
   *
   * @param numReplicas the number of replicas to use
   * @return this instance
   */
  public CreateTableOptions setNumReplicas(int numReplicas) {
    pb.setNumReplicas(numReplicas);
    return this;
  }

  Master.CreateTableRequestPB.Builder getBuilder() {
    if (!splitRows.isEmpty()) {
      pb.setSplitRows(new Operation.OperationsEncoder().encodeSplitRows(splitRows));
    }
    return pb;
  }
}
