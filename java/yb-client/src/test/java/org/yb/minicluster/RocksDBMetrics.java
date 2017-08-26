// Copyright (c) YugaByte, Inc.
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

package org.yb.minicluster;

/**
 * The metrics to track RocksDB metrics.
 */
public class RocksDBMetrics {

  public int seekCount;
  public int nextCount;

  private static final String METRIC_PREFIX = "rocksdb_number_db_";

  /**
   * Constructs an empty {@code RocksDBMetrics}.
   */
  public RocksDBMetrics() {
  }

  /**
   * Constructs a {@code RocksDBMetrics} using the given metrics data.
   *
   * @param metrics  the metrics data
   */
  public RocksDBMetrics(Metrics metrics) {
    seekCount = getValue(metrics, "seek");
    nextCount = getValue(metrics, "next");
  }

  /**
   * Returns the value from the given counter metrics.
   *
   * @param metrics  the metrics data
   * @param name     the metric name
   * @return         the value
   */
  private static int getValue(Metrics metrics, String name) {
    return metrics.getCounter(METRIC_PREFIX + name).value;
  }

  /**
   * Adds an {@code RocksDBMetrics} to this {@code RocksDBMetrics}.
   *
   * @param other  the {@code RocksDBMetrics} to add
   * @return       this {@code RocksDBMetrics}
   */
  public RocksDBMetrics add(RocksDBMetrics other) {
    this.seekCount += other.seekCount;
    this.nextCount += other.nextCount;
    return this;
  }

  /**
   * Subtracts an {@code RocksDBMetrics} from this {@code RocksDBMetrics}.
   *
   * @param other  the {@code RocksDBMetrics} to subtract
   * @return       this {@code RocksDBMetrics}
   */
  public RocksDBMetrics subtract(RocksDBMetrics other) {
    this.seekCount -= other.seekCount;
    this.nextCount -= other.nextCount;
    return this;
  }

  @Override
  public boolean equals(Object obj) {
    if (obj instanceof RocksDBMetrics) {
      RocksDBMetrics other = (RocksDBMetrics)obj;
      return this.seekCount == other.seekCount && this.nextCount == other.nextCount;
    }
    return false;
  }

  @Override
  public String toString() {
    return "seek = " + seekCount + ", next = " + nextCount;
  }
}
