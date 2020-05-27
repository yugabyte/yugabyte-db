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
 * The metrics to track read/write RPC.
 */
public class IOMetrics {

  public int localReadCount;
  public int localWriteCount;
  public int remoteReadCount;
  public int remoteWriteCount;

  private static final String METRIC_PREFIX = "handler_latency_yb_client_";

  /**
   * Constructs an empty {@code IOMetrics}.
   */
  public IOMetrics() {
  }

  /**
   * Constructs a {@code IOMetrics} using the given metrics data.
   *
   * @param metrics  the metrics data
   */
  public IOMetrics(Metrics metrics) {
    localReadCount = getTotalCount(metrics, "read_local");
    localWriteCount = getTotalCount(metrics, "write_local");
    remoteReadCount = getTotalCount(metrics, "read_remote");
    remoteWriteCount = getTotalCount(metrics, "write_remote");
  }

  /**
   * Returns the "total_count" from the given metrics.
   *
   * @param metrics  the metrics data
   * @param name     the metric name
   * @return         the "total_count"
   */
  private static int getTotalCount(Metrics metrics, String name) {
    String histogram_name = METRIC_PREFIX + name;
    Metrics.Histogram histogram = metrics.getHistogram(histogram_name);
    if (histogram == null) {
      throw new IllegalArgumentException("Unable for find histogram: " + histogram_name);
    }
    return histogram.totalCount;
  }

  /**
   * Adds an {@code IOMetrics} to this {@code IOMetrics}.
   *
   * @param other  the {@code IOMetrics} to add
   * @return       this {@code IOMetrics}
   */
  public IOMetrics add(IOMetrics other) {
    this.localReadCount += other.localReadCount;
    this.localWriteCount += other.localWriteCount;
    this.remoteReadCount += other.remoteReadCount;
    this.remoteWriteCount += other.remoteWriteCount;
    return this;
  }

  /**
   * Subtracts an {@code IOMetrics} from this {@code IOMetrics}.
   *
   * @param other  the {@code IOMetrics} to subtract
   * @return       this {@code IOMetrics}
   */
  public IOMetrics subtract(IOMetrics other) {
    this.localReadCount -= other.localReadCount;
    this.localWriteCount -= other.localWriteCount;
    this.remoteReadCount -= other.remoteReadCount;
    this.remoteWriteCount -= other.remoteWriteCount;
    return this;
  }

  public int readCount() {
    return localReadCount + remoteReadCount;
  }

  public int writeCount() {
    return localWriteCount + remoteWriteCount;
  }

  @Override
  public String toString() {
    return "local read = " + localReadCount +
        ", local write = " + localWriteCount +
        ", remote read = " + remoteReadCount +
        ", remote write = " + remoteWriteCount;
  }
}
