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

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Range;
import com.google.common.collect.Ranges;
import com.google.common.collect.Sets;
import com.stumbleupon.async.Callback;
import com.stumbleupon.async.Deferred;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.master.Master;
import org.kududb.util.Slice;
import org.jboss.netty.util.Timeout;
import org.jboss.netty.util.TimerTask;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.annotation.concurrent.GuardedBy;
import java.util.*;

import static org.kududb.client.ExternalConsistencyMode.CLIENT_PROPAGATED;

/**
 * A AsyncKuduSession belongs to a specific AsyncKuduClient, and represents a context in
 * which all read/write data access should take place. Within a session,
 * multiple operations may be accumulated and batched together for better
 * efficiency. Settings like timeouts, priorities, and trace IDs are also set
 * per session.<p>
 *
 * AsyncKuduSession is separate from KuduTable because a given batch or transaction
 * may span multiple tables. This is particularly important in the future when
 * we add ACID support, but even in the context of batching, we may be able to
 * coalesce writes to different tables hosted on the same server into the same
 * RPC.<p>
 *
 * AsyncKuduSession is separate from AsyncKuduClient because, in a multi-threaded
 * application, different threads may need to concurrently execute
 * transactions. Similar to a JDBC "session", transaction boundaries will be
 * delineated on a per-session basis -- in between a "BeginTransaction" and
 * "Commit" call on a given session, all operations will be part of the same
 * transaction. Meanwhile another concurrent Session object can safely run
 * non-transactional work or other transactions without interfering.<p>
 *
 * Therefore, this class is <b>not</b> thread-safe.<p>
 *
 * Additionally, there is a guarantee that writes from different sessions do not
 * get batched together into the same RPCs -- this means that latency-sensitive
 * clients can run through the same AsyncKuduClient object as throughput-oriented
 * clients, perhaps by setting the latency-sensitive session's timeouts low and
 * priorities high. Without the separation of batches, a latency-sensitive
 * single-row insert might get batched along with 10MB worth of inserts from the
 * batch writer, thus delaying the response significantly.<p>
 *
 * Though we currently do not have transactional support, users will be forced
 * to use a AsyncKuduSession to instantiate reads as well as writes.  This will make
 * it more straight-forward to add RW transactions in the future without
 * significant modifications to the API.<p>
 *
 * Timeouts are handled differently depending on the flush mode.
 * With AUTO_FLUSH_SYNC, the timeout is set on each apply()'d operation.
 * With AUTO_FLUSH_BACKGROUND and MANUAL_FLUSH, the timeout is assigned to a whole batch of
 * operations upon flush()'ing. It means that in a situation with a timeout of 500ms and a flush
 * interval of 1000ms, an operation can be oustanding for up to 1500ms before being timed out.
 */
@InterfaceAudience.Public
@InterfaceStability.Unstable
public class AsyncKuduSession implements SessionConfiguration {

  public static final Logger LOG = LoggerFactory.getLogger(AsyncKuduSession.class);
  private static final Range<Float> PERCENTAGE_RANGE = Ranges.closed(0.0f, 1.0f);

  private final AsyncKuduClient client;
  private final Random randomizer = new Random();
  private final ErrorCollector errorCollector;
  private int interval = 1000;
  private int mutationBufferSpace = 1000; // TODO express this in terms of data size.
  private float mutationBufferLowWatermarkPercentage = 0.5f;
  private int mutationBufferLowWatermark;
  private FlushMode flushMode;
  private ExternalConsistencyMode consistencyMode;
  private long timeoutMs;

  // We assign a number to each operation that we batch, so that the batch can sort itself before
  // being sent to the server. We never reset this number.
  private long nextSequenceNumber = 0;

  /**
   * The following two maps are to be handled together when batching is enabled. The first one is
   * where the batching happens, the second one is where we keep track of what's been sent but
   * hasn't come back yet. A batch cannot be in both maps at the same time. A batch cannot be
   * added to the in flight map if there's already another batch for the same tablet. If this
   * happens, and the batch in the first map is full, then we fail fast and send it back to the
   * client.
   * The second map stores Deferreds because KuduRpc.callback clears out the Deferred it contains
   * (as a way to reset the RPC), so we want to store the Deferred that's with the RPC that's
   * sent out.
   */
  @GuardedBy("this")
  private final Map<Slice, Batch> operations = new HashMap<>();

  @GuardedBy("this")
  private final Map<Slice, Deferred<BatchResponse>> operationsInFlight = new HashMap<>();

  /**
   * This Set is used when not in AUTO_FLUSH_SYNC mode in order to keep track of the operations
   * that are looking up their tablet, meaning that they aren't in any of the maps above. This is
   * not expected to grow a lot except when a client starts and only for a short amount of time.
   */
  @GuardedBy("this")
  private final Set<Operation> operationsInLookup = Sets.newIdentityHashSet();
  // Only populated when we're waiting to flush and there are operations in lookup
  private Deferred<Void> lookupsDone;

  /**
   * Tracks whether the session has been closed.
   */
  volatile boolean closed;

  private boolean ignoreAllDuplicateRows = false;

  /**
   * Package-private constructor meant to be used via AsyncKuduClient
   * @param client client that creates this session
   */
  AsyncKuduSession(AsyncKuduClient client) {
    this.client = client;
    this.flushMode = FlushMode.AUTO_FLUSH_SYNC;
    this.consistencyMode = CLIENT_PROPAGATED;
    this.timeoutMs = client.getDefaultOperationTimeoutMs();
    setMutationBufferLowWatermark(this.mutationBufferLowWatermarkPercentage);
    errorCollector = new ErrorCollector(mutationBufferSpace);
  }

  @Override
  public FlushMode getFlushMode() {
    return this.flushMode;
  }

  @Override
  public void setFlushMode(FlushMode flushMode) {
    if (hasPendingOperations()) {
      throw new IllegalArgumentException("Cannot change flush mode when writes are buffered");
    }
    this.flushMode = flushMode;
  }

  @Override
  public void setExternalConsistencyMode(ExternalConsistencyMode consistencyMode) {
    if (hasPendingOperations()) {
      throw new IllegalArgumentException("Cannot change consistency mode "
          + "when writes are buffered");
    }
    this.consistencyMode = consistencyMode;
  }

  @Override
  public void setMutationBufferSpace(int size) {
    if (hasPendingOperations()) {
      throw new IllegalArgumentException("Cannot change the buffer" +
          " size when operations are buffered");
    }
    this.mutationBufferSpace = size;
    // Reset the low watermark, using the same percentage as before.
    setMutationBufferLowWatermark(mutationBufferLowWatermarkPercentage);
  }

  @Override
  public void setMutationBufferLowWatermark(float mutationBufferLowWatermarkPercentage) {
    if (hasPendingOperations()) {
      throw new IllegalArgumentException("Cannot change the buffer" +
          " low watermark when operations are buffered");
    } else if (!PERCENTAGE_RANGE.contains(mutationBufferLowWatermarkPercentage)) {
      throw new IllegalArgumentException("The low watermark must be between 0 and 1 inclusively");
    }
    this.mutationBufferLowWatermarkPercentage = mutationBufferLowWatermarkPercentage;
    this.mutationBufferLowWatermark =
        (int)(this.mutationBufferLowWatermarkPercentage * mutationBufferSpace);
  }

  /**
   * Lets us set a specific seed for tests
   * @param seed
   */
  @VisibleForTesting
  void setRandomSeed(long seed) {
    this.randomizer.setSeed(seed);
  }

  @Override
  public void setFlushInterval(int interval) {
    this.interval = interval;
  }

  @Override
  public void setTimeoutMillis(long timeout) {
    this.timeoutMs = timeout;
  }

  @Override
  public long getTimeoutMillis() {
    return this.timeoutMs;
  }

  @Override
  public boolean isClosed() {
    return closed;
  }

  @Override
  public boolean isIgnoreAllDuplicateRows() {
    return ignoreAllDuplicateRows;
  }

  @Override
  public void setIgnoreAllDuplicateRows(boolean ignoreAllDuplicateRows) {
    this.ignoreAllDuplicateRows = ignoreAllDuplicateRows;
  }

  @Override
  public int countPendingErrors() {
    return errorCollector.countErrors();
  }

  @Override
  public RowErrorsAndOverflowStatus getPendingErrors() {
    return errorCollector.getErrors();
  }

  /**
   * Flushes the buffered operations and marks this sessions as closed.
   * See the javadoc on {@link #flush()} on how to deal with exceptions coming out of this method.
   * @return a Deferred whose callback chain will be invoked when.
   * everything that was buffered at the time of the call has been flushed.
   */
  public Deferred<List<OperationResponse>> close() {
    closed = true;
    client.removeSession(this);
    return flush();
  }

  /**
   * Flushes the buffered operations.
   * @return a Deferred whose callback chain will be invoked when
   * everything that was buffered at the time of the call has been flushed.
   */
  public Deferred<List<OperationResponse>> flush() {
    LOG.trace("Flushing all tablets");
    synchronized (this) {
      if (!operationsInLookup.isEmpty()) {
        lookupsDone = new Deferred<>();
        return lookupsDone
            .addCallbackDeferring(new OperationsInLookupDoneCB())
            .addCallbackDeferring(new ConvertBatchToListOfResponsesCB());
      }
    }
    return flushAllBatches().addCallbackDeferring(new ConvertBatchToListOfResponsesCB());
  }

  class OperationsInLookupDoneCB implements
      Callback<Deferred<ArrayList<BatchResponse>>, Void> {
    @Override
    public Deferred<ArrayList<BatchResponse>>
        call(Void nothing) throws Exception {
      return flushAllBatches();
    }
  }

  /**
   * Deferring callback used to send a list of OperationResponse instead of BatchResponse since the
   * latter is an implementation detail.
   */
  class ConvertBatchToListOfResponsesCB implements
      Callback<Deferred<List<OperationResponse>>, ArrayList<BatchResponse>> {
    @Override
    public Deferred<List<OperationResponse>> call(ArrayList<BatchResponse> batchResponsesList)
        throws Exception {
      Deferred<List<OperationResponse>> deferred = new Deferred<>();
      if (batchResponsesList == null) {
        deferred.callback(null);
        return deferred;
      }

      // flushTablet() can return null when a tablet we wanted to flush was already flushed. Those
      // nulls along with BatchResponses are then put in a list by Deferred.group(). We first need
      // to filter the nulls out.
      batchResponsesList.removeAll(Collections.singleton(null));
      if (batchResponsesList.isEmpty()) {
        deferred.callback(null);
        return deferred;
      }

      // First compute the size of the union of all the lists so that we don't trigger expensive
      // list growths while adding responses to it.
      int size = 0;
      for (BatchResponse batchResponse : batchResponsesList) {
        size += batchResponse.getIndividualResponses().size();
      }

      ArrayList<OperationResponse> responsesList = new ArrayList<>(size);
      for (BatchResponse batchResponse : batchResponsesList) {
        responsesList.addAll(batchResponse.getIndividualResponses());
      }
      deferred.callback(responsesList);
      return deferred;
    }
  }

  /**
   * This will flush all the batches but not the operations that are currently in lookup.
   */
  private Deferred<ArrayList<BatchResponse>> flushAllBatches() {
    HashMap<Slice, Batch> copyOfOps;
    final ArrayList<Deferred<BatchResponse>> d = new ArrayList<>(operations.size());
    synchronized (this) {
      copyOfOps = new HashMap<>(operations);
    }
    for (Map.Entry<Slice, Batch> entry: copyOfOps.entrySet()) {
      d.add(flushTablet(entry.getKey(), entry.getValue()));
    }
    return Deferred.group(d);
  }

  @Override
  public boolean hasPendingOperations() {
    synchronized (this) {
      return !this.operations.isEmpty() || !this.operationsInFlight.isEmpty() ||
          !this.operationsInLookup.isEmpty();
    }
  }

  /**
   * Apply the given operation.
   * The behavior of this function depends on the current flush mode. Regardless
   * of flush mode, however, Apply may begin to perform processing in the background
   * for the call (e.g looking up the tablet, etc).
   * @param operation operation to apply
   * @return a Deferred to track this operation
   */
  public Deferred<OperationResponse> apply(final Operation operation) {
    if (operation == null) {
      throw new NullPointerException("Cannot apply a null operation");
    }

    if (AsyncKuduClient.cannotRetryRequest(operation)) {
      return AsyncKuduClient.tooManyAttemptsOrTimeout(operation, null);
    }

    // This can be called multiple times but it's fine, we don't allow "thawing".
    operation.getRow().freeze();

    // If we autoflush, just send it to the TS
    if (flushMode == FlushMode.AUTO_FLUSH_SYNC) {
      if (timeoutMs != 0) {
        operation.setTimeoutMillis(timeoutMs);
      }
      operation.setExternalConsistencyMode(this.consistencyMode);
      return client.sendRpcToTablet(operation);
    }

    // We need this protection because apply() can be called multiple times for the same operations
    // due to retries, but we only want to set the sequence number once. Since a session isn't
    // thread-safe, it means that we'll always set the sequence number from the user's thread, and
    // we'll read later from other threads.
    if (operation.getSequenceNumber() == -1) {
      operation.setSequenceNumber(nextSequenceNumber++);
    }

    String tableId = operation.getTable().getTableId();
    byte[] partitionKey = operation.partitionKey();
    AsyncKuduClient.RemoteTablet tablet = client.getTablet(tableId, partitionKey);
    // We go straight to the buffer if we know the tabletSlice
    if (tablet != null) {
      operation.setTablet(tablet);
      // Handles the difference between manual and auto flush
      return addToBuffer(tablet.getTabletId(), operation);
    }

    synchronized (this) {
      operationsInLookup.add(operation);
    }
    // TODO starts looking a lot like sendRpcToTablet
    operation.attempt++;
    if (client.isTableNotServed(tableId)) {
      Callback<Deferred<OperationResponse>, Master.IsCreateTableDoneResponsePB> cb =
          new TabletLookupCB<>(operation);
      return client.delayedIsCreateTableDone(operation.getTable(), operation,
          cb, getOpInLookupErrback(operation));
    }

    Deferred<Master.GetTableLocationsResponsePB> d =
        client.locateTablet(operation.getTable(), partitionKey);
    d.addErrback(getOpInLookupErrback(operation));
    return d.addCallbackDeferring(
        new TabletLookupCB<Master.GetTableLocationsResponsePB>(operation));
  }

  /**
   * This errback is different from the one in AsyncKuduClient because we need to be able to remove
   * the operation from operationsInLookup if whatever master query we issue throws an Exception.
   * @param operation Operation to errback to.
   * @return An errback.
   */
  Callback<Exception, Exception> getOpInLookupErrback(final Operation operation) {
    return new Callback<Exception, Exception>() {
      @Override
      public Exception call(Exception e) throws Exception {
        // TODO maybe we can retry it?
        synchronized (this) {
          operationsInLookup.remove(operation);
        }
        operation.errback(e);
        return e;
      }
    };
  }

  final class TabletLookupCB<D> implements Callback<Deferred<OperationResponse>, D> {
    final Operation operation;
    TabletLookupCB(Operation operation) {
      this.operation = operation;
    }
    public Deferred<OperationResponse> call(final D arg) {
      return handleOperationInLookup(operation);
    }
    public String toString() {
      return "retry RPC after lookup";
    }
  }

  // This method takes an Object since we use it for both callback and errback.
  // The actual type doesn't matter, we just want to be called back in order to retry.
  Callback<Deferred<OperationResponse>, Object>
  getRetryOpInLookupCB(final Operation operation) {
    final class RetryOpInFlightCB implements Callback<Deferred<OperationResponse>, Object> {
      public Deferred<OperationResponse> call(final Object arg) {
        return handleOperationInLookup(operation);
      }

      public String toString() {
        return "retry RPC after PleaseThrottleException";
      }
    }
    return new RetryOpInFlightCB();
  }

  private Deferred<OperationResponse> handleOperationInLookup(Operation operation) {
    try {
      return apply(operation); // Retry the RPC.
    } catch (PleaseThrottleException pte) {
      return pte.getDeferred().addBothDeferring(getRetryOpInLookupCB(operation));
    }
  }

  /**
   * For manual and background flushing, this will batch the given operation
   * with the others, if any, for the specified tablet.
   * @param tablet tablet used to for batching
   * @param operation operation to batch
   * @return Defered to track the operation
   */
  private Deferred<OperationResponse> addToBuffer(Slice tablet, Operation operation) {
    boolean scheduleFlush = false;
    boolean batchIsFull = false;
    Batch batch;

    // First check if we need to flush the current batch.
    synchronized (this) {
      batch = operations.get(tablet);
      if (batch != null && batch.ops.size() + 1 > mutationBufferSpace) {
        if (flushMode == FlushMode.MANUAL_FLUSH) {
          throw new NonRecoverableException("MANUAL_FLUSH is enabled but the buffer is too big");
        }
        if (operationsInFlight.containsKey(tablet)) {
          // There's is already another batch in flight for this tablet.
          // We cannot continue here, we have to send this back to the client.
          // This is our high watermark.
          throw new PleaseThrottleException("The RPC cannot be buffered because the current " +
              "buffer is full and the previous buffer hasn't been flushed yet", null,
              operation, operationsInFlight.get(tablet));
        }
        batchIsFull = true;
      }
    }

    // We're doing this out of the synchronized block because flushTablet can take some time
    // encoding all the data.
    if (batchIsFull) {
      flushTablet(tablet, batch);
    }

    Deferred<Void> lookupsDoneCopy = null;
    synchronized (this) {
      // We need to get the batch again since we went out of the synchronized block. We can get a
      // new one, the same one, or null.
      batch = operations.get(tablet);

      if (mutationBufferLowWatermark < mutationBufferSpace && // look if it's enabled
          batch != null && // and if we have a batch
          operationsInFlight.containsKey(tablet) && // and if there's another batch outstanding
          batch.ops.size() + 1 > mutationBufferLowWatermark) { // and if we'll be over the mark

        // This is our low watermark, we throw PleaseThrottleException before hitting the high
        // mark. As we get fuller past the watermark it becomes likelier to trigger it.
        int randomWatermark = batch.ops.size() + 1 + randomizer.nextInt(mutationBufferSpace -
            mutationBufferLowWatermark);
        if (randomWatermark > mutationBufferSpace) {
          throw new PleaseThrottleException("The previous buffer hasn't been flushed and the " +
              "current one is over the low watermark, please retry later", null, operation,
              operationsInFlight.get(tablet));
        }
      }
      if (batch == null) {
        // We found a tablet that needs batching, this is the only place where
        // we schedule a flush.
        batch = new Batch(operation.getTable(), ignoreAllDuplicateRows);
        batch.setExternalConsistencyMode(this.consistencyMode);
        Batch oldBatch = operations.put(tablet, batch);
        assert (oldBatch == null);
        addBatchCallbacks(batch);
        scheduleFlush = true;
      }
      batch.ops.add(operation);
      if (!operationsInLookup.isEmpty()) {

        boolean operationWasLookingUpTablet = operationsInLookup.remove(operation);
        if (operationWasLookingUpTablet) {
          // We know that the operation we just added was in the 'operationsInLookup' list so we're
          // very likely adding it out of order from a different thread.
          // We'll need to sort the whole list later.
          batch.needsSorting = true;
        }

        if (lookupsDone != null && operationsInLookup.isEmpty()) {
          lookupsDoneCopy = lookupsDone;
          lookupsDone = null;
        }
      }
      if (flushMode == FlushMode.AUTO_FLUSH_BACKGROUND && scheduleFlush) {
        // Accumulated a first insert but we're not in manual mode,
        // schedule the flush.
        LOG.trace("Scheduling a flush");
        scheduleNextPeriodicFlush(tablet, batch);
      }
    }

    // We do this outside of the synchronized block because we might end up calling flushTablet.
    if (lookupsDoneCopy != null) {
      lookupsDoneCopy.callback(null);
    }

    // Get here if we accumulated an insert, regardless of if it scheduled
    // a flush.
    return operation.getDeferred();
  }

  /**
   * Creates callbacks to handle a multi-put and adds them to the request.
   * @param request the request for which we must handle the response
   */
  private void addBatchCallbacks(final Batch request) {
    final class BatchCallback implements
        Callback<BatchResponse, BatchResponse> {
      public BatchResponse call(final BatchResponse response) {
        LOG.trace("Got a Batch response for " + request.ops.size() + " rows");
        if (response.getWriteTimestamp() != 0) {
          AsyncKuduSession.this.client.updateLastPropagatedTimestamp(response.getWriteTimestamp());
        }

        // Send individualized responses to all the operations in this batch.
        for (OperationResponse operationResponse : response.getIndividualResponses()) {
          operationResponse.getOperation().callback(operationResponse);
          if (flushMode == FlushMode.AUTO_FLUSH_BACKGROUND && operationResponse.hasRowError()) {
            errorCollector.addError(operationResponse.getRowError());
          }
        }

        return response;
      }

      @Override
      public String toString() {
        return "apply batch response";
      }
    }

    final class BatchErrCallback implements Callback<Exception, Exception> {
      @Override
      public Exception call(Exception e) throws Exception {
        // Send the same exception to all the operations.
        for (int i = 0; i < request.ops.size(); i++) {
          request.ops.get(i).errback(e);
        }
        return e;
      }

      @Override
      public String toString() {
        return "apply batch error response";
      }
    }

    request.getDeferred().addCallbacks(new BatchCallback(), new BatchErrCallback());
  }

  /**
   * Schedules the next periodic flush of buffered edits.
   */
  private void scheduleNextPeriodicFlush(Slice tablet, Batch batch) {
    client.newTimeout(new FlusherTask(tablet, batch), interval);
  }

  /**
   * Flushes the edits for the given tablet. It will also check that the Batch we're flushing is
   * the one that was requested. This is mostly done so that the FlusherTask doesn't trigger
   * lots of small flushes under a write-heavy scenario where we're able to fill a Batch multiple
   * times per interval.
   *
   * Also, if there's already a Batch in flight for the given tablet,
   * the flush will be delayed and the returned Deferred will be chained to it.
   *
   * This method should not be called within a synchronized block because we can spend a lot of
   * time encoding the batch.
   */
  private Deferred<BatchResponse> flushTablet(Slice tablet, Batch expectedBatch) {
    assert (expectedBatch != null);
    assert (!Thread.holdsLock(this));
    Batch batch;
    synchronized (this) {
      // Check this first, no need to wait after anyone if the batch we were supposed to flush
      // was already flushed.
      if (operations.get(tablet) != expectedBatch) {
        LOG.trace("Had to flush a tablet but it was already flushed: " + Bytes.getString(tablet));
        // It is OK to return null here, since we currently do not use the returned value
        // when doing background flush or auto flushing when buffer is full.
        // The returned value is used when doing manual flush, but it will not run into this
        // condition, or there is a bug.
        return Deferred.fromResult(null);
      }

      if (operationsInFlight.containsKey(tablet)) {
        if (LOG.isTraceEnabled()) {
          LOG.trace("Tablet " + Bytes.getString(tablet)
              + " is already in flight, attaching a callback to retry "
              + expectedBatch.toDebugString() + " later.");
        }
        // No matter previous batch get error or not, we still have to flush this batch.
        FlushRetryCallback retryCallback = new FlushRetryCallback(tablet, operations.get(tablet));
        FlushRetryErrback retryErrback = new FlushRetryErrback(tablet, operations.get(tablet));
        // Note that if we do manual flushing multiple times when previous batch is still inflight,
        // we may add the same callback multiple times, later retry of flushTablet will return null
        // immediately. Since it is an illegal use case, we do not handle this currently.
        operationsInFlight.get(tablet).addCallbacks(retryCallback, retryErrback);
        return expectedBatch.getDeferred();
      }

      batch = operations.remove(tablet);
      if (batch == null) {
        LOG.trace("Had to flush a tablet but there was nothing to flush: " +
            Bytes.getString(tablet));
        return Deferred.fromResult(null);
      }
      Deferred<BatchResponse> batchDeferred = batch.getDeferred();
      batchDeferred.addCallbacks(getOpInFlightCallback(tablet), getOpInFlightErrback(tablet));
      Deferred<BatchResponse> oldBatch = operationsInFlight.put(tablet, batchDeferred);
      assert (oldBatch == null);
      if (timeoutMs != 0) {
        batch.deadlineTracker.reset();
        batch.setTimeoutMillis(timeoutMs);
      }
    }
    return client.sendRpcToTablet(batch);
  }


  /**
   * Simple callback so that we try to flush this tablet again if we were waiting on the previous
   * Batch to finish.
   */
  class FlushRetryCallback implements Callback<BatchResponse, BatchResponse> {
    private final Slice tablet;
    private final Batch expectedBatch;
    public FlushRetryCallback(Slice tablet, Batch expectedBatch) {
      this.tablet = tablet;
      this.expectedBatch = expectedBatch;
    }

    @Override
    public BatchResponse call(BatchResponse o) throws Exception {
      if (LOG.isTraceEnabled()) {
        LOG.trace("Previous batch in flight is done. " + toString());
      }
      flushTablet(tablet, expectedBatch);
      return o;
    }

    @Override
    public String toString() {
      return String.format("FlushRetryCallback: retry flush tablet %s %s", Bytes.getString(tablet),
          expectedBatch.toDebugString());
    }
  }

  /**
   * Same callback as above FlushRetryCallback, for the case that previous batch has error.
   */
  class FlushRetryErrback implements Callback<Exception, Exception> {
    private final Slice tablet;
    private final Batch expectedBatch;
    public FlushRetryErrback(Slice tablet, Batch expectedBatch) {
      this.tablet = tablet;
      this.expectedBatch = expectedBatch;
    }

    @Override
    public Exception call(Exception e) throws Exception {
      if (LOG.isTraceEnabled()) {
        LOG.trace("Previous batch ended with an error. " + toString());
      }
      flushTablet(tablet, expectedBatch);
      return e;
    }

    @Override
    public String toString() {
      return String.format("FlushRetryErrback: retry flush tablet %s %s", Bytes.getString(tablet),
          expectedBatch.toDebugString());
    }
  }

  /**
   * Simple callback that removes the tablet from the in flight operations map once it completed.
   */
  private Callback<BatchResponse, BatchResponse>
      getOpInFlightCallback(final Slice tablet) {
    return new Callback<BatchResponse, BatchResponse>() {
      @Override
      public BatchResponse call(BatchResponse o) throws Exception {
        tabletInFlightDone(tablet);
        return o;
      }

      @Override
      public String toString() {
        return "callback: mark tablet " + Bytes.getString(tablet) + " inflight done";
      }
    };
  }

  /**
   * We need a separate callback for errors since the generics are different. We still remove the
   * tablet from the in flight operations since there's nothing we can do about it,
   * and by returning the Exception we will bubble it up to the user.
   */
  private Callback<Exception, Exception> getOpInFlightErrback(final Slice tablet) {
    return new Callback<Exception, Exception>() {
      @Override
      public Exception call(Exception e) throws Exception {
        tabletInFlightDone(tablet);
        return e;
      }

      @Override
      public String toString() {
        return "errback: mark tablet " + Bytes.getString(tablet) + " inflight done";
      }
    };
  }

  private void tabletInFlightDone(Slice tablet) {
    synchronized (AsyncKuduSession.this) {
      LOG.trace("Unmarking this tablet as in flight: " + Bytes.getString(tablet));
      operationsInFlight.remove(tablet);
    }
  }

  /**
   * A FlusherTask is created for each scheduled flush per tabletSlice.
   */
  class FlusherTask implements TimerTask {
    final Slice tabletSlice;
    final Batch expectedBatch;

    FlusherTask(Slice tabletSlice, Batch expectedBatch) {
      this.tabletSlice = tabletSlice;
      this.expectedBatch = expectedBatch;
    }

    public void run(final Timeout timeout) {
      if (isClosed()) {
        return; // we ran too late, no-op
      }
      LOG.trace("Timed flushing: " + Bytes.getString(tabletSlice));
      flushTablet(this.tabletSlice, this.expectedBatch);
    }
    public String toString() {
      return "flush commits of session " + AsyncKuduSession.this +
          " for tabletSlice " + Bytes.getString(tabletSlice);
    }
  };
}
