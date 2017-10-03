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

import org.kududb.annotations.*;

import com.stumbleupon.async.Deferred;
import java.util.List;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Synchronous version of {@link AsyncKuduSession}.
 * Offers the same API but with blocking methods.<p>
 *
 * This class is <b>not</b> thread-safe.<p>
 *
 * A major difference with {@link AsyncKuduSession} is that the time spent waiting on operations is
 * defined by {@link #setTimeoutMillis(long)} which defaults to getting it from
 * {@link KuduClient#getDefaultOperationTimeoutMs()}.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class KuduSession implements SessionConfiguration {

  public static final Logger LOG = LoggerFactory.getLogger(KuduSession.class);

  private final AsyncKuduSession session;

  KuduSession(AsyncKuduSession session) {
    this.session = session;
  }

  /**
   * Blocking call with a different behavior based on the flush mode. PleaseThrottleException is
   * managed by this method and will not be thrown, unlike {@link AsyncKuduSession#apply}.
   * <p>
   * <ul>
   * <li>AUTO_FLUSH_SYNC: the call returns when the operation is persisted,
   * else it throws an exception.
   * <li>AUTO_FLUSH_BACKGROUND: the call returns when the operation has been added to the buffer.
   * This call should normally perform only fast in-memory operations but
   * it may have to wait when the buffer is full and there's another buffer being flushed. Row
   * errors can be checked by calling {@link #countPendingErrors()} and can be retrieved by calling
   * {@link #getPendingErrors()}.
   * <li>MANUAL_FLUSH: the call returns when the operation has been added to the buffer,
   * else it throws an exception such as a NonRecoverableException if the buffer is full.
   * </ul>
   *
   * @param operation operation to apply
   * @return an OperationResponse for the applied Operation
   * @throws Exception if anything went wrong
   */
  public OperationResponse apply(Operation operation) throws Exception {
    while (true) {
      try {
        Deferred<OperationResponse> d = session.apply(operation);
        if (getFlushMode() == FlushMode.AUTO_FLUSH_SYNC) {
          return d.join(getTimeoutMillis());
        }
        break;
      } catch (PleaseThrottleException ex) {
        try {
          ex.getDeferred().join(getTimeoutMillis());
        } catch (Exception e) {
          // This is the error response from the buffer that was flushing,
          // we can't do much with it at this point.
          LOG.error("Previous batch had this exception", e);
        }
      } catch (Exception e) {
        throw e;
      }
    }
    return null;
  }

  /**
   * Blocking call that force flushes this session's buffers. Data is persisted when this call
   * returns, else it will throw an exception.
   * @return a list of OperationResponse, one per operation that was flushed
   * @throws Exception if anything went wrong. If it's an issue with some or all batches,
   * it will be of type DeferredGroupException.
   */
  public List<OperationResponse> flush() throws Exception {
    return session.flush().join(getTimeoutMillis());
  }

  /**
   * Blocking call that flushes the buffers (see {@link #flush()} and closes the sessions.
   * @return List of OperationResponse, one per operation that was flushed
   * @throws Exception if anything went wrong. If it's an issue with some or all batches,
   * it will be of type DeferredGroupException.
   */
  public List<OperationResponse> close() throws Exception {
    return session.close().join(getTimeoutMillis());
  }

  @Override
  public FlushMode getFlushMode() {
    return session.getFlushMode();
  }

  @Override
  public void setFlushMode(AsyncKuduSession.FlushMode flushMode) {
    session.setFlushMode(flushMode);
  }

  @Override
  public void setMutationBufferSpace(int size) {
    session.setMutationBufferSpace(size);
  }

  @Override
  public void setMutationBufferLowWatermark(float mutationBufferLowWatermarkPercentage) {
    session.setMutationBufferLowWatermark(mutationBufferLowWatermarkPercentage);
  }

  @Override
  public void setFlushInterval(int interval) {
    session.setFlushInterval(interval);
  }

  @Override
  public long getTimeoutMillis() {
    return session.getTimeoutMillis();
  }

  @Override
  public void setTimeoutMillis(long timeout) {
    session.setTimeoutMillis(timeout);
  }

  @Override
  public boolean isClosed() {
    return session.isClosed();
  }

  @Override
  public boolean hasPendingOperations() {
    return session.hasPendingOperations();
  }

  @Override
  public void setExternalConsistencyMode(ExternalConsistencyMode consistencyMode) {
    session.setExternalConsistencyMode(consistencyMode);
  }

  @Override
  public boolean isIgnoreAllDuplicateRows() {
    return session.isIgnoreAllDuplicateRows();
  }

  @Override
  public void setIgnoreAllDuplicateRows(boolean ignoreAllDuplicateRows) {
    session.setIgnoreAllDuplicateRows(ignoreAllDuplicateRows);
  }

  @Override
  public int countPendingErrors() {
    return session.countPendingErrors();
  }

  @Override
  public RowErrorsAndOverflowStatus getPendingErrors() {
    return session.getPendingErrors();
  }
}
