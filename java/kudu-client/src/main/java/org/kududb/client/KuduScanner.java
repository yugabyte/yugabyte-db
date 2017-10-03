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

import com.stumbleupon.async.Deferred;

import org.kududb.Schema;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.client.AsyncKuduScanner.ReadMode;

/**
 * Synchronous version of {@link AsyncKuduScanner}. Offers the same API but with blocking methods.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class KuduScanner {

  private final AsyncKuduScanner asyncScanner;

  KuduScanner(AsyncKuduScanner asyncScanner) {
    this.asyncScanner = asyncScanner;
  }

  /**
   * Tells if the last rpc returned that there might be more rows to scan.
   * @return true if there might be more data to scan, else false
   */
  public boolean hasMoreRows() {
    return asyncScanner.hasMoreRows();
  }

  /**
   * Scans a number of rows.
   * <p>
   * Once this method returns {@code null} once (which indicates that this
   * {@code Scanner} is done scanning), calling it again leads to an undefined
   * behavior.
   * @return a list of rows.
   */
  public RowResultIterator nextRows() throws Exception {
    Deferred<RowResultIterator> d = asyncScanner.nextRows();
    return d.join(asyncScanner.scanRequestTimeout);
  }

  /**
   * Closes this scanner (don't forget to call this when you're done with it!).
   * <p>
   * Closing a scanner already closed has no effect.
   * @return a deferred object that indicates the completion of the request
   */
  public RowResultIterator close() throws Exception {
    Deferred<RowResultIterator> d = asyncScanner.close();
    return d.join(asyncScanner.scanRequestTimeout);
  }

  /**
   * Returns the maximum number of rows that this scanner was configured to return.
   * @return a long representing the maximum number of rows that can be returned
   */
  public long getLimit() {
    return asyncScanner.getLimit();
  }

  /**
   * Returns if this scanner was configured to cache data blocks or not.
   * @return true if this scanner will cache blocks, else else.
   */
  public boolean getCacheBlocks() {
    return asyncScanner.getCacheBlocks();
  }

  /**
   * Returns the maximum number of bytes returned by the scanner, on each batch.
   * @return a long representing the maximum number of bytes that a scanner can receive at once
   * from a tablet server
   */
  public long getBatchSizeBytes() {
    return asyncScanner.getBatchSizeBytes();
  }

  /**
   * Returns the ReadMode for this scanner.
   * @return the configured read mode for this scanner
   */
  public ReadMode getReadMode() {
    return asyncScanner.getReadMode();
  }

  /**
   * Returns the projection schema of this scanner. If specific columns were
   * not specified during scanner creation, the table schema is returned.
   * @return the projection schema for this scanner
   */
  public Schema getProjectionSchema() {
    return asyncScanner.getProjectionSchema();
  }

  /**
   * A Builder class to build {@link KuduScanner}.
   * Use {@link KuduClient#newScannerBuilder} in order to get a builder instance.
   */
  @InterfaceAudience.Public
  @InterfaceStability.Evolving
  public static class KuduScannerBuilder
      extends AbstractKuduScannerBuilder<KuduScannerBuilder, KuduScanner> {

    KuduScannerBuilder(AsyncKuduClient client, KuduTable table) {
      super(client, table);
    }

    /**
     * Builds a {@link KuduScanner} using the passed configurations.
     * @return a new {@link KuduScanner}
     */
    public KuduScanner build() {
      return new KuduScanner(new AsyncKuduScanner(
          client, table, projectedColumnNames, projectedColumnIndexes, readMode,
          scanRequestTimeout, columnRangePredicates, limit, cacheBlocks,
          prefetching, lowerBoundPrimaryKey, upperBoundPrimaryKey,
          lowerBoundPartitionKey, upperBoundPartitionKey,
          htTimestamp, batchSizeBytes));
    }
  }
}
