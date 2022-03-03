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

package org.yb.cdc;

import com.stumbleupon.async.Callback;
import com.stumbleupon.async.Deferred;
import org.apache.log4j.Logger;
import org.yb.cdc.util.Checkpoint;
import org.yb.client.*;

import java.io.IOException;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;

public class ConcurrentPoller {
  private static final Logger LOG = Logger.getLogger(ConcurrentPoller.class);
  private final String streamId;
  private final AsyncYBClient asyncYBClient;
  private final OutputClient outputClient;
  private final Semaphore requestBarrier;
  private final String format;
  private boolean stopExecution;
  private boolean enableSnapshot;

  static final AbstractMap.SimpleImmutableEntry<String, String> END_PAIR =
      new AbstractMap.SimpleImmutableEntry("", "");

  int concurrency;

  List<AbstractMap.SimpleImmutableEntry<String, String>> listTabletIdTableIdPair;
  Map<String, Checkpoint> checkPointMap;
  Map<String, YBTable> tableIdToTable;
  AtomicInteger requestNum;
  BlockingQueue<AbstractMap.SimpleImmutableEntry<String, String>> queue;
  List<Deferred<GetChangesResponse>> deferredList;

  YBClient synClient;

  public ConcurrentPoller(YBClient synClient,
                          AsyncYBClient client,
                          OutputClient outputClient,
                          String streamId,
                          Map<String, List<String>> tableIdsToTabletIds,
                          int concurrency,
                          String format,
                          boolean stopExecution,
                          boolean enableSnapshot) throws IOException {
    this.synClient = synClient;
    this.asyncYBClient = client;
    this.streamId = streamId;
    this.format = format;
    checkPointMap = new ConcurrentHashMap<>();
    tableIdToTable = new ConcurrentHashMap<>();
    requestBarrier = new Semaphore(concurrency);
    requestNum = new AtomicInteger();
    this.outputClient = outputClient;
    this.concurrency = concurrency;
    deferredList = new ArrayList<>();
    this.stopExecution = stopExecution;
    this.enableSnapshot = enableSnapshot;

    tableIdsToTabletIds.keySet().forEach(tabletId -> {
      try {
        tableIdToTable.put(tabletId, synClient.openTableByUUID(tabletId));
      } catch (Exception e) {
        e.printStackTrace();
      }
    });

    listTabletIdTableIdPair = tableIdsToTabletIds.entrySet().stream()
      .flatMap(e -> e.getValue().stream()
        .map(v -> new AbstractMap.SimpleImmutableEntry<>(v, e.getKey())))
      .collect(Collectors.toList());
    queue = new LinkedBlockingQueue();
    initOffset();
  }

  private void initOffset() {
    long term = 0;
    long index = 0;
    int writeId = 0;

    if (enableSnapshot) {
      term = -1;
      index = -1;
      writeId = -1;
    }

    long finalTerm = term;
    long finalIndex = index;
    int finalWriteId = writeId;
    listTabletIdTableIdPair.forEach(entry ->
      checkPointMap.put(entry.getKey(), new Checkpoint(finalTerm, finalIndex,
        "".getBytes(), finalWriteId, 0)));
  }

  public void poll() throws Exception {
    final List result = new ArrayList();
    queue.addAll(listTabletIdTableIdPair);
    queue.add(END_PAIR);
    while (true) {
      if (stopExecution) {
        // This signals the CDCConsoleSubscriber to stop polling further and exit.
        LOG.info("Signal received to close the CDCConsoleSubscriber, exiting...");
        System.exit(0);
      }

      requestBarrier.acquireUninterruptibly();
      final AbstractMap.SimpleImmutableEntry<String, String> entry = queue.take();
      if (entry.equals(END_PAIR)) {
        requestBarrier.release();
        break;
      }
      final Checkpoint cp = checkPointMap.get(entry.getKey());
      final YBTable table = tableIdToTable.get(entry.getValue());

      LOG.debug("Polling table: " + table + " tablet: " + entry.getKey() +
               " with checkpoint " + cp);
      Callback resCallback = new HandleResponse(table, entry.getKey(), result, requestBarrier);
      Callback errCallback = new HandleFailure(requestBarrier);

      Deferred<GetChangesResponse> response = asyncYBClient.getChangesCDCSDK(
        table, streamId, entry.getKey() /*tabletId*/,
        cp.getTerm(), cp.getIndex(), cp.getKey(), cp.getWriteId(), cp.getSnapshotTime());

      response.addCallback(resCallback);
      response.addErrback(errCallback);

      deferredList.add(response);
    }

    AtomicInteger totalException = new AtomicInteger();
    deferredList.forEach(getChangesResponseDeferred -> {
      try {
        getChangesResponseDeferred.join(120000);
      } catch (Exception e) {
        totalException.getAndIncrement();
        e.printStackTrace();
      }
    });

    if (listTabletIdTableIdPair.size() > 0) {
      if (totalException.get() == listTabletIdTableIdPair.size()) {
        LOG.error("Unable to poll further, all the nodes returned error");
        System.exit(1);
      } else {

      }
    }
  }

  final class HandleFailure implements Callback<Void, Exception> {
    private final Semaphore barrier;

    HandleFailure(Semaphore barrier) {
      this.barrier = barrier;
    }

    @Override
    public Void call(Exception e) throws Exception {
      barrier.release();
      LOG.debug("Releasing the requestbarrier" + barrier.availablePermits());

      e.printStackTrace();
      if (e instanceof CDCErrorException) {
        LOG.error("The error code is " +
                 ((CDCErrorException) e).getCDCError().getCode().getNumber());
      }
      if (e instanceof CDCErrorException &&
          ((CDCErrorException) e).getCDCError().getCode().getNumber() == 25) {
        LOG.error("Got the GetChangesResponse errback, error code is ");
      }

      // TODO: Check all the exception here and throw only non-retryable exception as
      // This is last chance for the application to retry.
      throw e;
    }
  }

  final class HandleResponse implements Callback<Void, GetChangesResponse> {
    private final List<CdcService.CDCSDKProtoRecordPB> result;
    private final YBTable table;
    private final String tabletId;
    private final Semaphore barrier;

    HandleResponse(YBTable table, String tabletId,
                   List<CdcService.CDCSDKProtoRecordPB> result,
                   Semaphore barrier) {
      this.result = result;
      this.table = table;
      this.tabletId = tabletId;
      this.barrier = barrier;
    }

    public Void call(final GetChangesResponse response) {
        return callPROTO(response);
    }

    public Void callPROTO(final GetChangesResponse response) {
      boolean noError = true;

      for (CdcService.CDCSDKProtoRecordPB record : response
        .getResp()
        .getCdcSdkProtoRecordsList()) {
        try {
          outputClient.applyChange(table, record);
          result.add(record);
        } catch (Exception e) {
          e.printStackTrace();
          noError = false;
          break;
        }
      }

      Checkpoint cp = null;
      if (noError) {
        cp = new Checkpoint(
          response.getTerm(),
          response.getIndex(),
          response.getKey(),
          response.getWriteId(),
          response.getSnapshotTime());

        checkPointMap.put(tabletId, cp);
        LOG.debug("For tablet " + this.tabletId + " got the checkpoint " + cp);
      }

      barrier.release();
      return null;
    }

    public String toString() {
      return "Handle Response";
    }
  }
}
