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

import com.google.common.base.Objects;
import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;

import java.util.Arrays;
import java.util.List;

/**
 * A Partition describes the set of rows that a Tablet is responsible for
 * serving. Each tablet is assigned a single Partition.<p>
 *
 * Partitions consist primarily of a start and end partition key. Every row with
 * a partition key that falls in a Tablet's Partition will be served by that
 * tablet.<p>
 *
 * In addition to the start and end partition keys, a Partition holds metadata
 * to determine if a scan can prune, or skip, a partition based on the scan's
 * start and end primary keys, and predicates.
 *
 * This class is new, and not considered stable or suitable for public use.
 */
@InterfaceAudience.LimitedPrivate("Impala")
@InterfaceStability.Unstable
public class Partition implements Comparable<Partition> {
  final byte[] partitionKeyStart;
  final byte[] partitionKeyEnd;

  final byte[] rangeKeyStart;
  final byte[] rangeKeyEnd;

  final List<Integer> hashBuckets;

  /**
   * Size of an encoded hash bucket component in a partition key.
   */
  private static final int ENCODED_BUCKET_SIZE = 4;

  /**
   * Creates a new partition with the provided start and end keys, and hash buckets.
   * @param partitionKeyStart the start partition key
   * @param partitionKeyEnd the end partition key
   * @param hashBuckets the partition hash buckets
   */
  Partition(byte[] partitionKeyStart,
            byte[] partitionKeyEnd,
            List<Integer> hashBuckets) {
    this.partitionKeyStart = partitionKeyStart;
    this.partitionKeyEnd = partitionKeyEnd;
    this.hashBuckets = hashBuckets;
    this.rangeKeyStart = rangeKey(partitionKeyStart, hashBuckets.size());
    this.rangeKeyEnd = rangeKey(partitionKeyEnd, hashBuckets.size());
  }

  /**
   * Gets the start partition key.
   * @return the start partition key
   */
  public byte[] getPartitionKeyStart() {
    return partitionKeyStart;
  }

  /**
   * Gets the end partition key.
   * @return the end partition key
   */
  public byte[] getPartitionKeyEnd() {
    return partitionKeyEnd;
  }

  /**
   * Gets the start range key.
   * @return the start range key
   */
  public byte[] getRangeKeyStart() {
    return rangeKeyStart;
  }

  /**
   * Gets the end range key.
   * @return the end range key
   */
  public byte[] getRangeKeyEnd() {
    return rangeKeyEnd;
  }

  /**
   * Gets the partition hash buckets.
   * @return the partition hash buckets
   */
  public List<Integer> getHashBuckets() {
    return hashBuckets;
  }

  /**
   * @return true if the partition is the absolute end partition
   */
  public boolean isEndPartition() {
    return partitionKeyEnd.length == 0;
  }

  /**
   * Equality only holds for partitions from the same table. Partition equality only takes into
   * account the partition keys, since there is a 1 to 1 correspondence between partition keys and
   * the hash buckets and range keys.
   *
   * @return the hash code
   */
  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    Partition partition = (Partition) o;
    return Arrays.equals(partitionKeyStart, partition.partitionKeyStart)
        && Arrays.equals(partitionKeyEnd, partition.partitionKeyEnd);
  }

  /**
   * The hash code only takes into account the partition keys, since there is a 1 to 1
   * correspondence between partition keys and the hash buckets and range keys.
   *
   * @return the hash code
   */
  @Override
  public int hashCode() {
    return Objects.hashCode(Arrays.hashCode(partitionKeyStart), Arrays.hashCode(partitionKeyEnd));
  }

  /**
   * Partition comparison is only reasonable when comparing partitions from the same table, and
   * since YB does not yet allow partition splitting, no two distinct partitions can have the
   * same start partition key. Accordingly, partitions are compared strictly by the start partition
   * key.
   *
   * @param other the other partition of the same table
   * @return the comparison of the partitions
   */
  @Override
  public int compareTo(Partition other) {
    return Bytes.memcmp(this.partitionKeyStart, other.partitionKeyStart);
  }

  /**
   * Returns the range key portion of a partition key given the number of buckets in the partition
   * schema.
   * @param partitionKey the partition key containing the range key
   * @param numHashBuckets the number of hash bucket components of the table
   * @return the range key
   */
  private static byte[] rangeKey(byte[] partitionKey, int numHashBuckets) {
    int bucketsLen = numHashBuckets * ENCODED_BUCKET_SIZE;
    if (partitionKey.length > bucketsLen) {
      return Arrays.copyOfRange(partitionKey, bucketsLen, partitionKey.length);
    } else {
      return AsyncYBClient.EMPTY_ARRAY;
    }
  }

  @Override
  public String toString() {
    return String.format("[%s, %s)",
                         Bytes.pretty(partitionKeyStart),
                         Bytes.pretty(partitionKeyEnd));
  }
}
