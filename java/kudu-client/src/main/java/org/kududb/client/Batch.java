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
import com.google.protobuf.Message;
import com.google.protobuf.ZeroCopyLiteralByteString;

import org.kududb.WireProtocol;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.tserver.Tserver;
import org.kududb.tserver.Tserver.TabletServerErrorPB;
import org.kududb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/**
 * Used internally to batch Operations together before sending to the cluster
 */
@InterfaceAudience.Private
class Batch extends KuduRpc<BatchResponse> implements KuduRpc.HasKey {

  private static final OperationsComparatorBySequenceNumber SEQUENCE_NUMBER_COMPARATOR =
      new OperationsComparatorBySequenceNumber();

  final List<Operation> ops;

  // Operations can be added out of order to 'ops' if the tablet had to be looked up. We can detect
  // this situation in AsyncKuduSession and set this to true.
  boolean needsSorting = false;

  /** See {@link SessionConfiguration#setIgnoreAllDuplicateRows(boolean)} */
  final boolean ignoreAllDuplicateRows;

  Batch(KuduTable table, boolean ignoreAllDuplicateRows) {
    this(table, ignoreAllDuplicateRows, 1000);
  }

  Batch(KuduTable table, boolean ignoreAllDuplicateRows, int estimatedBatchSize) {
    super(table);
    this.ops = new ArrayList<Operation>(estimatedBatchSize);
    this.ignoreAllDuplicateRows = ignoreAllDuplicateRows;
  }

  @Override
  ChannelBuffer serialize(Message header) {

    // This should only happen if at least one operation triggered a tablet lookup, which is rare
    // on a long-running client.
    if (needsSorting) {
      Collections.sort(ops, SEQUENCE_NUMBER_COMPARATOR);
    }

    final Tserver.WriteRequestPB.Builder builder =
        Operation.createAndFillWriteRequestPB(ops.toArray(new Operation[ops.size()]));
    builder.setTabletId(ZeroCopyLiteralByteString.wrap(getTablet().getTabletIdAsBytes()));
    builder.setExternalConsistencyMode(this.externalConsistencyMode.pbVersion());
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return TABLET_SERVER_SERVICE_NAME; }

  @Override
  String method() {
    return Operation.METHOD;
  }

  @Override
  Pair<BatchResponse, Object> deserialize(final CallResponse callResponse,
                                              String tsUUID) throws Exception {
    Tserver.WriteResponsePB.Builder builder = Tserver.WriteResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);

    List<Tserver.WriteResponsePB.PerRowErrorPB> errorsPB = builder.getPerRowErrorsList();
    if (ignoreAllDuplicateRows) {
      boolean allAlreadyPresent = true;
      for (Tserver.WriteResponsePB.PerRowErrorPB errorPB : errorsPB) {
        if (errorPB.getError().getCode() != WireProtocol.AppStatusPB.ErrorCode.ALREADY_PRESENT) {
          allAlreadyPresent = false;
          break;
        }
      }
      if (allAlreadyPresent) {
        errorsPB = Collections.emptyList();
      }
    }

    BatchResponse response = new BatchResponse(deadlineTracker.getElapsedMillis(), tsUUID,
        builder.getTimestamp(), errorsPB, ops);

    if (injectedError != null) {
      if (injectedlatencyMs > 0) {
        try {
          Thread.sleep(injectedlatencyMs);
        } catch (InterruptedException e) {
        }
      }
      return new Pair<BatchResponse, Object>(response, injectedError);
    }

    return new Pair<BatchResponse, Object>(response, builder.hasError() ? builder.getError() : null);
  }

  @Override
  public byte[] partitionKey() {
    assert this.ops.size() > 0;
    return this.ops.get(0).partitionKey();
  }

  public String toDebugString() {
    return "Batch(" + ops.size() + " ops)@" + Integer.toHexString(hashCode());
  }

  /**
   * Sorts the Operations by their sequence number.
   */
  private static class OperationsComparatorBySequenceNumber implements Comparator<Operation> {
    @Override
    public int compare(Operation o1, Operation o2) {
      return Long.compare(o1.getSequenceNumber(), o2.getSequenceNumber());
    }
  }

  private static TabletServerErrorPB injectedError;
  private static int injectedlatencyMs;

  /**
   * Inject tablet server side error for Batch rpc related tests.
   * @param error error response from tablet server
   * @param latencyMs blocks response handling thread for some time to simulate
   * write latency
   */
  @VisibleForTesting
  static void injectTabletServerErrorAndLatency(TabletServerErrorPB error, int latencyMs) {
    injectedError = error;
    injectedlatencyMs = latencyMs;
  }
}
