/*
 * Copyright (C) 2010-2012  The Async HBase Authors.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   - Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   - Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *   - Neither the name of the StumbleUpon nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
package org.kududb.client;

import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.List;

import com.google.protobuf.Message;
import com.google.protobuf.ZeroCopyLiteralByteString;
import org.kududb.ColumnSchema;
import org.kududb.Common;
import org.kududb.Schema;
import com.stumbleupon.async.Callback;
import com.stumbleupon.async.Deferred;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.tserver.Tserver;
import org.kududb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static com.google.common.base.Preconditions.checkArgument;
import static org.kududb.tserver.Tserver.*;

/**
 * Creates a scanner to read data from Kudu.
 * <p>
 * This class is <strong>not synchronized</strong> as it's expected to be
 * used from a single thread at a time. It's rarely (if ever?) useful to
 * scan concurrently from a shared scanner using multiple threads. If you
 * want to optimize large table scans using extra parallelism, create a few
 * scanners and give each of them a partition of the table to scan. Or use
 * MapReduce.
 * <p>
 * There's no method in this class to explicitly open the scanner. It will open
 * itself automatically when you start scanning by calling {@link #nextRows()}.
 * Also, the scanner will automatically call {@link #close} when it reaches the
 * end key. If, however, you would like to stop scanning <i>before reaching the
 * end key</i>, you <b>must</b> call {@link #close} before disposing of the scanner.
 * Note that it's always safe to call {@link #close} on a scanner.
 * <p>
 * A {@code AsyncKuduScanner} is not re-usable. Should you want to scan the same rows
 * or the same table again, you must create a new one.
 *
 * <h1>A note on passing {@code byte} arrays in argument</h1>
 * None of the method that receive a {@code byte[]} in argument will copy it.
 * For more info, please refer to the documentation of {@link KuduRpc}.
 * <h1>A note on passing {@code String}s in argument</h1>
 * All strings are assumed to use the platform's default charset.
 */
@InterfaceAudience.Public
@InterfaceStability.Unstable
public final class AsyncKuduScanner {

  private static final Logger LOG = LoggerFactory.getLogger(AsyncKuduScanner.class);

  /**
   * The possible read modes for scanners.
   */
  @InterfaceAudience.Public
  @InterfaceStability.Evolving
  public enum ReadMode {
    /**
     * When READ_LATEST is specified the server will always return committed writes at
     * the time the request was received. This type of read does not return a snapshot
     * timestamp and is not repeatable.
     *
     * In ACID terms this corresponds to Isolation mode: "Read Committed"
     *
     * This is the default mode.
     */
    READ_LATEST(Common.ReadMode.READ_LATEST),

    /**
     * When READ_AT_SNAPSHOT is specified the server will attempt to perform a read
     * at the provided timestamp. If no timestamp is provided the server will take the
     * current time as the snapshot timestamp. In this mode reads are repeatable, i.e.
     * all future reads at the same timestamp will yield the same data. This is
     * performed at the expense of waiting for in-flight transactions whose timestamp
     * is lower than the snapshot's timestamp to complete, so it might incur a latency
     * penalty.
     *
     * In ACID terms this, by itself, corresponds to Isolation mode "Repeatable
     * Read". If all writes to the scanned tablet are made externally consistent,
     * then this corresponds to Isolation mode "Strict-Serializable".
     *
     * Note: there currently "holes", which happen in rare edge conditions, by which writes
     * are sometimes not externally consistent even when action was taken to make them so.
     * In these cases Isolation may degenerate to mode "Read Committed". See KUDU-430.
     */
    READ_AT_SNAPSHOT(Common.ReadMode.READ_AT_SNAPSHOT);

    private Common.ReadMode pbVersion;
    private ReadMode(Common.ReadMode pbVersion) {
      this.pbVersion = pbVersion;
    }

    @InterfaceAudience.Private
    public Common.ReadMode pbVersion() {
      return this.pbVersion;
    }
  }

  //////////////////////////
  // Initial configurations.
  //////////////////////////

  private final AsyncKuduClient client;
  private final KuduTable table;
  private final Schema schema;
  private final List<Tserver.ColumnRangePredicatePB> columnRangePredicates;

  /**
   * Maximum number of bytes returned by the scanner, on each batch.
   */
  private final int batchSizeBytes;

  /**
   * The maximum number of rows to scan.
   */
  private final long limit;

  /**
   * The start partition key of the next tablet to scan.
   *
   * Each time the scan exhausts a tablet, this is updated to that tablet's end partition key.
   */
  private byte[] nextPartitionKey;

  /**
   * The end partition key of the last tablet to scan.
   */
  private final byte[] endPartitionKey;

  /**
   * Set in the builder. If it's not set by the user, it will default to EMPTY_ARRAY.
   * It is then reset to the new start primary key of each tablet we open a scanner on as the scan
   * moves from one tablet to the next.
   */
  private final byte[] startPrimaryKey;

  /**
   * Set in the builder. If it's not set by the user, it will default to EMPTY_ARRAY.
   * It's never modified after that.
   */
  private final byte[] endPrimaryKey;

  private final boolean prefetching;

  private final boolean cacheBlocks;

  private final ReadMode readMode;

  private final long htTimestamp;

  /////////////////////
  // Runtime variables.
  /////////////////////

  private boolean closed = false;

  private boolean hasMore = true;

  /**
   * The tabletSlice currently being scanned.
   * If null, we haven't started scanning.
   * If == DONE, then we're done scanning.
   * Otherwise it contains a proper tabletSlice name, and we're currently scanning.
   */
  private AsyncKuduClient.RemoteTablet tablet;

  /**
   * This is the scanner ID we got from the TabletServer.
   * It's generated randomly so any value is possible.
   */
  private byte[] scannerId;

  /**
   * The sequence ID of this call. The sequence ID should start at 0
   * with the request for a new scanner, and after each successful request,
   * the client should increment it by 1. When retrying a request, the client
   * should _not_ increment this value. If the server detects that the client
   * missed a chunk of rows from the middle of a scan, it will respond with an
   * error.
   */
  private int sequenceId;

  private Deferred<RowResultIterator> prefetcherDeferred;

  private boolean inFirstTablet = true;

  final long scanRequestTimeout;

  private static final AtomicBoolean PARTITION_PRUNE_WARN = new AtomicBoolean(true);

  AsyncKuduScanner(AsyncKuduClient client, KuduTable table, List<String> projectedNames,
                   List<Integer> projectedIndexes, ReadMode readMode, long scanRequestTimeout,
                   List<Tserver.ColumnRangePredicatePB> columnRangePredicates, long limit,
                   boolean cacheBlocks, boolean prefetching,
                   byte[] startPrimaryKey, byte[] endPrimaryKey,
                   byte[] startPartitionKey, byte[] endPartitionKey,
                   long htTimestamp, int batchSizeBytes) {
    checkArgument(batchSizeBytes > 0, "Need a strictly positive number of bytes, " +
        "got %s", batchSizeBytes);
    checkArgument(limit > 0, "Need a strictly positive number for the limit, " +
        "got %s", limit);
    if (htTimestamp != AsyncKuduClient.NO_TIMESTAMP) {
      checkArgument(htTimestamp >= 0, "Need non-negative number for the scan, " +
          " timestamp got %s", htTimestamp);
      checkArgument(readMode == ReadMode.READ_AT_SNAPSHOT, "When specifying a " +
          "HybridClock timestamp, the read mode needs to be set to READ_AT_SNAPSHOT");
    }

    this.client = client;
    this.table = table;
    this.readMode = readMode;
    this.scanRequestTimeout = scanRequestTimeout;
    this.columnRangePredicates = columnRangePredicates;
    this.limit = limit;
    this.cacheBlocks = cacheBlocks;
    this.prefetching = prefetching;
    this.startPrimaryKey = startPrimaryKey;
    this.endPrimaryKey = endPrimaryKey;
    this.htTimestamp = htTimestamp;
    this.batchSizeBytes = batchSizeBytes;

    if (!table.getPartitionSchema().isSimpleRangePartitioning() &&
        (startPrimaryKey != AsyncKuduClient.EMPTY_ARRAY ||
         endPrimaryKey != AsyncKuduClient.EMPTY_ARRAY) &&
        PARTITION_PRUNE_WARN.getAndSet(false)) {
      LOG.warn("Starting full table scan. " +
               "In the future this scan may be automatically optimized with partition pruning.");
    }

    if (table.getPartitionSchema().isSimpleRangePartitioning()) {
      // If the table is simple range partitioned, then the partition key space
      // is isomorphic to the primary key space. We can potentially reduce the
      // scan length by only scanning the intersection of the primary key range
      // and the partition key range. This is a stop-gap until real partition
      // pruning is in place that can work across any partitioning type.

      if ((endPartitionKey.length != 0 && Bytes.memcmp(startPrimaryKey, endPartitionKey) >= 0) ||
          (endPrimaryKey.length != 0 && Bytes.memcmp(startPartitionKey, endPrimaryKey) >= 0)) {
        // The primary key range and the partition key range do not intersect;
        // the scan will be empty.
        this.nextPartitionKey = startPartitionKey;
        this.endPartitionKey = endPartitionKey;
      } else {
        // Assign the scan's partition key range to the intersection of the
        // primary key and partition key ranges.
        if (Bytes.memcmp(startPartitionKey, startPrimaryKey) < 0) {
          this.nextPartitionKey = startPrimaryKey;
        } else {
          this.nextPartitionKey = startPartitionKey;
        }
        if (endPrimaryKey.length != 0 && Bytes.memcmp(endPartitionKey, endPrimaryKey) > 0) {
          this.endPartitionKey = endPrimaryKey;
        } else {
          this.endPartitionKey = endPartitionKey;
        }
      }
    } else {
      this.nextPartitionKey = startPartitionKey;
      this.endPartitionKey = endPartitionKey;
    }

    // Map the column names to actual columns in the table schema.
    // If the user set this to 'null', we scan all columns.
    if (projectedNames != null) {
      List<ColumnSchema> columns = new ArrayList<ColumnSchema>();
      for (String columnName : projectedNames) {
        ColumnSchema columnSchema = table.getSchema().getColumn(columnName);
        if (columnSchema == null) {
          throw new IllegalArgumentException("Unknown column " + columnName);
        }
        columns.add(columnSchema);
      }
      this.schema = new Schema(columns);
    } else if (projectedIndexes != null) {
      List<ColumnSchema> columns = new ArrayList<ColumnSchema>();
      for (Integer columnIndex : projectedIndexes) {
        ColumnSchema columnSchema = table.getSchema().getColumnByIndex(columnIndex);
        if (columnSchema == null) {
          throw new IllegalArgumentException("Unknown column index " + columnIndex);
        }
        columns.add(columnSchema);
      }
      this.schema = new Schema(columns);
    } else {
      this.schema = table.getSchema();
    }
  }

  /**
   * Returns the maximum number of rows that this scanner was configured to return.
   * @return a long representing the maximum number of rows that can be returned
   */
  public long getLimit() {
    return this.limit;
  }

  /**
   * Tells if the last rpc returned that there might be more rows to scan.
   * @return true if there might be more data to scan, else false
   */
  public boolean hasMoreRows() {
    return this.hasMore;
  }

  /**
   * Returns if this scanner was configured to cache data blocks or not.
   * @return true if this scanner will cache blocks, else else.
   */
  public boolean getCacheBlocks() {
    return this.cacheBlocks;
  }

  /**
   * Returns the maximum number of bytes returned by the scanner, on each batch.
   * @return a long representing the maximum number of bytes that a scanner can receive at once
   * from a tablet server
   */
  public long getBatchSizeBytes() {
    return this.batchSizeBytes;
  }

  /**
   * Returns the ReadMode for this scanner.
   * @return the configured read mode for this scanner
   */
  public ReadMode getReadMode() {
    return this.readMode;
  }

  /**
   * Returns the projection schema of this scanner. If specific columns were
   * not specified during scanner creation, the table schema is returned.
   * @return the projection schema for this scanner
   */
  public Schema getProjectionSchema() {
    return this.schema;
  }

  long getSnapshotTimestamp() {
    return this.htTimestamp;
  }

  /**
   * Scans a number of rows.
   * <p>
   * Once this method returns {@code null} once (which indicates that this
   * {@code Scanner} is done scanning), calling it again leads to an undefined
   * behavior.
   * @return a deferred list of rows.
   */
  public Deferred<RowResultIterator> nextRows() {
    if (closed) {  // We're already done scanning.
      return Deferred.fromResult(null);
    } else if (tablet == null) {

      // We need to open the scanner first.
      return client.openScanner(this).addCallbackDeferring(
          new Callback<Deferred<RowResultIterator>, AsyncKuduScanner.Response>() {
            public Deferred<RowResultIterator> call(final AsyncKuduScanner.Response resp) {
              if (!resp.more || resp.scanner_id == null) {
                scanFinished();
                return Deferred.fromResult(resp.data); // there might be data to return
              }
              scannerId = resp.scanner_id;
              sequenceId++;
              hasMore = resp.more;
              if (LOG.isDebugEnabled()) {
                LOG.debug("Scanner " + Bytes.pretty(scannerId) + " opened on " + tablet);
              }
              //LOG.info("Scan.open is returning rows: " + resp.data.getNumRows());
              return Deferred.fromResult(resp.data);
            }
            public String toString() {
              return "scanner opened";
            }
          });
    } else if (prefetching && prefetcherDeferred != null) {
      // TODO KUDU-1260 - Check if this works and add a test
      prefetcherDeferred.chain(new Deferred<RowResultIterator>().addCallback(prefetch));
      return prefetcherDeferred;
    }
    final Deferred<RowResultIterator> d =
        client.scanNextRows(this).addCallbacks(got_next_row, nextRowErrback());
    if (prefetching) {
      d.chain(new Deferred<RowResultIterator>().addCallback(prefetch));
    }
    return d;
  }

  private final Callback<RowResultIterator, RowResultIterator> prefetch =
      new Callback<RowResultIterator, RowResultIterator>() {
    @Override
    public RowResultIterator call(RowResultIterator arg) throws Exception {
      if (hasMoreRows()) {
        prefetcherDeferred = client.scanNextRows(AsyncKuduScanner.this).addCallbacks
            (got_next_row, nextRowErrback());
      }
      return null;
    }
  };

  /**
   * Singleton callback to handle responses of "next" RPCs.
   * This returns an {@code ArrayList<ArrayList<KeyValue>>} (possibly inside a
   * deferred one).
   */
  private final Callback<RowResultIterator, Response> got_next_row =
      new Callback<RowResultIterator, Response>() {
        public RowResultIterator call(final Response resp) {
          if (!resp.more) {  // We're done scanning this tablet.
            scanFinished();
            return resp.data;
          }
          sequenceId++;
          hasMore = resp.more;
          //LOG.info("Scan.next is returning rows: " + resp.data.getNumRows());
          return resp.data;
        }
        public String toString() {
          return "get nextRows response";
        }
      };

  /**
   * Creates a new errback to handle errors while trying to get more rows.
   */
  private final Callback<Exception, Exception> nextRowErrback() {
    return new Callback<Exception, Exception>() {
      public Exception call(final Exception error) {
        final AsyncKuduClient.RemoteTablet old_tablet = tablet;  // Save before invalidate().
        String message = old_tablet + " pretends to not know " + AsyncKuduScanner.this;
        LOG.warn(message, error);
        invalidate();  // If there was an error, don't assume we're still OK.
        return error;  // Let the error propagate.
      }
      public String toString() {
        return "NextRow errback";
      }
    };
  }

  void scanFinished() {
    Partition partition = tablet.getPartition();
    // Stop scanning if we have scanned until or past the end partition key.
    if (partition.isEndPartition()
        || (this.endPartitionKey != AsyncKuduClient.EMPTY_ARRAY
            && Bytes.memcmp(this.endPartitionKey, partition.getPartitionKeyEnd()) <= 0)) {
      hasMore = false;
      closed = true; // the scanner is closed on the other side at this point
      return;
    }
    if (LOG.isDebugEnabled()) {
      LOG.debug("Done scanning tablet {} for partition {} with scanner id {}",
                tablet.getTabletIdAsString(), tablet.getPartition(), Bytes.pretty(scannerId));
    }
    nextPartitionKey = partition.getPartitionKeyEnd();
    scannerId = null;
    invalidate();
  }

  /**
   * Closes this scanner (don't forget to call this when you're done with it!).
   * <p>
   * Closing a scanner already closed has no effect.  The deferred returned
   * will be called back immediately.
   * @return A deferred object that indicates the completion of the request.
   * The {@link Object} can be null, a RowResultIterator if there was data left
   * in the scanner, or an Exception.
   */
  public Deferred<RowResultIterator> close() {
    if (closed) {
      return Deferred.fromResult(null);
    }
    final Deferred<RowResultIterator> d =
       client.closeScanner(this).addCallback(closedCallback()); // TODO errBack ?
    return d;
  }

  /** Callback+Errback invoked when the TabletServer closed our scanner.  */
  private Callback<RowResultIterator, Response> closedCallback() {
    return new Callback<RowResultIterator, Response>() {
      public RowResultIterator call(Response response) {
        closed = true;
        if (LOG.isDebugEnabled()) {
          LOG.debug("Scanner " + Bytes.pretty(scannerId) + " closed on "
              + tablet);
        }
        tablet = null;
        scannerId = "client debug closed".getBytes();   // Make debugging easier.
        return response == null ? null : response.data;
      }
      public String toString() {
        return "scanner closed";
      }
    };
  }

  public String toString() {
    final String tablet = this.tablet == null ? "null" : this.tablet.getTabletIdAsString();
    final StringBuilder buf = new StringBuilder();
    buf.append("KuduScanner(table=");
    buf.append(table.getName());
    buf.append(", tablet=").append(tablet);
    buf.append(", scannerId=").append(Bytes.pretty(scannerId));
    buf.append(", scanRequestTimeout=").append(scanRequestTimeout);
    buf.append(')');
    return buf.toString();
  }

  // ---------------------- //
  // Package private stuff. //
  // ---------------------- //

  KuduTable table() {
    return table;
  }

  /**
   * Sets the name of the tabletSlice that's hosting {@code this.start_key}.
   * @param tablet The tabletSlice we're currently supposed to be scanning.
   */
  void setTablet(final AsyncKuduClient.RemoteTablet tablet) {
    this.tablet = tablet;
  }

  /**
   * Invalidates this scanner and makes it assume it's no longer opened.
   * When a TabletServer goes away while we're scanning it, or some other type
   * of access problem happens, this method should be called so that the
   * scanner will have to re-locate the TabletServer and re-open itself.
   */
  void invalidate() {
    tablet = null;
  }

  /**
   * Returns the tabletSlice currently being scanned, if any.
   */
  AsyncKuduClient.RemoteTablet currentTablet() {
    return tablet;
  }

  /**
   * Returns an RPC to open this scanner.
   */
  KuduRpc<Response> getOpenRequest() {
    checkScanningNotStarted();
    // This is the only point where we know we haven't started scanning and where the scanner
    // should be fully configured
    if (this.inFirstTablet) {
      this.inFirstTablet = false;
    }
    return new ScanRequest(table, State.OPENING);
  }

  /**
   * Returns an RPC to fetch the next rows.
   */
  KuduRpc<Response> getNextRowsRequest() {
    return new ScanRequest(table, State.NEXT);
  }

  /**
   * Returns an RPC to close this scanner.
   */
  KuduRpc<Response> getCloseRequest() {
    return new ScanRequest(table, State.CLOSING);
  }

  /**
   * Throws an exception if scanning already started.
   * @throws IllegalStateException if scanning already started.
   */
  private void checkScanningNotStarted() {
    if (tablet != null) {
      throw new IllegalStateException("scanning already started");
    }
  }

  /**
   *  Helper object that contains all the info sent by a TS afer a Scan request
   */
  static final class Response {
    /** The ID associated with the scanner that issued the request.  */
    private final byte[] scanner_id;
    /** The actual payload of the response.  */
    private final RowResultIterator data;

    /**
     * If false, the filter we use decided there was no more data to scan.
     * In this case, the server has automatically closed the scanner for us,
     * so we don't need to explicitly close it.
     */
    private final boolean more;

    Response(final byte[] scanner_id,
             final RowResultIterator data,
             final boolean more) {
      this.scanner_id = scanner_id;
      this.data = data;
      this.more = more;
    }

    public String toString() {
      return "AsyncKuduScanner$Response(scannerId=" + Bytes.pretty(scanner_id)
          + ", data=" + data + ", more=" + more +  ") ";
    }
  }

  private enum State {
    OPENING,
    NEXT,
    CLOSING
  }

  /**
   * RPC sent out to fetch the next rows from the TabletServer.
   */
  private final class ScanRequest extends KuduRpc<Response> implements KuduRpc.HasKey {

    State state;

    ScanRequest(KuduTable table, State state) {
      super(table);
      this.state = state;
      this.setTimeoutMillis(scanRequestTimeout);
    }

    @Override
    String serviceName() { return TABLET_SERVER_SERVICE_NAME; }

    @Override
    String method() {
      return "Scan";
    }

    /** Serializes this request.  */
    ChannelBuffer serialize(Message header) {
      final ScanRequestPB.Builder builder = ScanRequestPB.newBuilder();
      switch (state) {
        case OPENING:
          // Save the tablet in the AsyncKuduScanner.  This kind of a kludge but it really
          // is the easiest way.
          AsyncKuduScanner.this.tablet = super.getTablet();
          NewScanRequestPB.Builder newBuilder = NewScanRequestPB.newBuilder();
          newBuilder.setLimit(limit); // currently ignored
          newBuilder.addAllProjectedColumns(ProtobufHelper.schemaToListPb(schema));
          newBuilder.setTabletId(ZeroCopyLiteralByteString.wrap(tablet.getTabletIdAsBytes()));
          newBuilder.setReadMode(AsyncKuduScanner.this.getReadMode().pbVersion());
          newBuilder.setCacheBlocks(cacheBlocks);
          // if the last propagated timestamp is set send it with the scan
          if (table.getAsyncClient().getLastPropagatedTimestamp() != AsyncKuduClient.NO_TIMESTAMP) {
            newBuilder.setPropagatedTimestamp(table.getAsyncClient().getLastPropagatedTimestamp());
          }
          newBuilder.setReadMode(AsyncKuduScanner.this.getReadMode().pbVersion());

          // if the mode is set to read on snapshot sent the snapshot timestamp
          if (AsyncKuduScanner.this.getReadMode() == ReadMode.READ_AT_SNAPSHOT &&
            AsyncKuduScanner.this.getSnapshotTimestamp() != AsyncKuduClient.NO_TIMESTAMP) {
            newBuilder.setSnapTimestamp(AsyncKuduScanner.this.getSnapshotTimestamp());
          }

          if (AsyncKuduScanner.this.startPrimaryKey != AsyncKuduClient.EMPTY_ARRAY &&
              AsyncKuduScanner.this.startPrimaryKey.length > 0) {
            newBuilder.setStartPrimaryKey(ZeroCopyLiteralByteString.copyFrom(startPrimaryKey));
          }

          if (AsyncKuduScanner.this.endPrimaryKey != AsyncKuduClient.EMPTY_ARRAY &&
              AsyncKuduScanner.this.endPrimaryKey.length > 0) {
            newBuilder.setStopPrimaryKey(ZeroCopyLiteralByteString.copyFrom(endPrimaryKey));
          }

          if (!columnRangePredicates.isEmpty()) {
            newBuilder.addAllRangePredicates(columnRangePredicates);
          }
          builder.setNewScanRequest(newBuilder.build())
                 .setBatchSizeBytes(batchSizeBytes);
          break;
        case NEXT:
          builder.setScannerId(ZeroCopyLiteralByteString.wrap(scannerId))
                 .setCallSeqId(sequenceId)
                 .setBatchSizeBytes(batchSizeBytes);
          break;
        case CLOSING:
          builder.setScannerId(ZeroCopyLiteralByteString.wrap(scannerId))
                 .setBatchSizeBytes(0)
                 .setCloseScanner(true);
      }

      ScanRequestPB request = builder.build();
      if (LOG.isDebugEnabled()) {
        LOG.debug("Sending scan req: " + request.toString());
      }

      return toChannelBuffer(header, request);
    }

    @Override
    Pair<Response, Object> deserialize(final CallResponse callResponse,
                                       String tsUUID) throws Exception {
      ScanResponsePB.Builder builder = ScanResponsePB.newBuilder();
      readProtobuf(callResponse.getPBMessage(), builder);
      ScanResponsePB resp = builder.build();
      final byte[] id = resp.getScannerId().toByteArray();
      TabletServerErrorPB error = resp.hasError() ? resp.getError() : null;
      if (error != null && error.getCode().equals(TabletServerErrorPB.Code.TABLET_NOT_FOUND)) {
        if (state == State.OPENING) {
          // Doing this will trigger finding the new location.
          return new Pair<Response, Object>(null, error);
        } else {
          throw new NonRecoverableException("Cannot continue scanning, " +
              "the tablet has moved and this isn't a fault tolerant scan");
        }
      }
      RowResultIterator iterator = new RowResultIterator(
          deadlineTracker.getElapsedMillis(), tsUUID, schema, resp.getData(),
          callResponse);

      boolean hasMore = resp.getHasMoreResults();
      if (id.length  != 0 && scannerId != null && !Bytes.equals(scannerId, id)) {
        throw new InvalidResponseException("Scan RPC response was for scanner"
            + " ID " + Bytes.pretty(id) + " but we expected "
            + Bytes.pretty(scannerId), resp);
      }
      Response response = new Response(id, iterator, hasMore);
      if (LOG.isDebugEnabled()) {
        LOG.debug(response.toString());
      }
      return new Pair<Response, Object>(response, error);
    }

    public String toString() {
      return "ScanRequest(scannerId=" + Bytes.pretty(scannerId)
          + (tablet != null? ", tabletSlice=" + tablet.getTabletIdAsString() : "")
          + ", attempt=" + attempt + ')';
    }

    @Override
    public byte[] partitionKey() {
      // This key is used to lookup where the request needs to go
      return nextPartitionKey;
    }
  }

  /**
   * A Builder class to build {@link AsyncKuduScanner}.
   * Use {@link AsyncKuduClient#newScannerBuilder} in order to get a builder instance.
   */
  public static class AsyncKuduScannerBuilder
      extends AbstractKuduScannerBuilder<AsyncKuduScannerBuilder, AsyncKuduScanner> {

    AsyncKuduScannerBuilder(AsyncKuduClient client, KuduTable table) {
      super(client, table);
    }

    /**
     * Builds an {@link AsyncKuduScanner} using the passed configurations.
     * @return a new {@link AsyncKuduScanner}
     */
    public AsyncKuduScanner build() {
      return new AsyncKuduScanner(
          client, table, projectedColumnNames, projectedColumnIndexes, readMode,
          scanRequestTimeout, columnRangePredicates, limit, cacheBlocks,
          prefetching, lowerBoundPrimaryKey, upperBoundPrimaryKey,
          lowerBoundPartitionKey, upperBoundPartitionKey,
          htTimestamp, batchSizeBytes);
    }
  }
}
