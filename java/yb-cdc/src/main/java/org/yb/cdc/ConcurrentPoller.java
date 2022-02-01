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

import com.google.common.base.Objects;
import com.stumbleupon.async.Callback;
import com.stumbleupon.async.Deferred;
import org.apache.log4j.Logger;
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
  List<AbstractMap.SimpleImmutableEntry<String, String>> listTabletIdTableIdPair;
  private final Semaphore requestBarrier;
  Map<String, CheckPoint> checkPointMap;
  YBClient synClient;
  Map<String, YBTable> tableIdToTable;
  AtomicInteger requestNum;
  int concurrency;
  private final String format;
  BlockingQueue<AbstractMap.SimpleImmutableEntry<String, String>> queue;
  static final AbstractMap.SimpleImmutableEntry<String, String> END_PAIR =
      new AbstractMap.SimpleImmutableEntry("", "");

  List<Deferred<GetChangesResponse>> deferredList;

  private boolean stopExecution;
  private boolean enableSnapshot;

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
      checkPointMap.put(entry.getKey(), new CheckPoint(finalTerm, finalIndex,
        "".getBytes(), finalWriteId, 0)));
  }

  public void poll() throws Exception {
    final List result = new ArrayList();
    queue.addAll(listTabletIdTableIdPair);
    queue.add(END_PAIR);
    while (true) {
      if (stopExecution) {
        LOG.info("Signal received to close the CDCConsoleSubscriber, exiting...");
        System.exit(0); // this signals the CDCConsoleSubscriber to go out of scope
      }

      requestBarrier.acquireUninterruptibly();
      final AbstractMap.SimpleImmutableEntry<String, String> entry = queue.take();
      if (entry.equals(END_PAIR)) {
        requestBarrier.release();
        break;
      }
      final CheckPoint cp = checkPointMap.get(entry.getKey());
      final YBTable table = tableIdToTable.get(entry.getValue());

      LOG.debug("Polling table: " + table + " tablet: " + entry.getKey() +
               " with checkpoint " + cp);
      Callback resCallback = new HandleResponse(table, entry.getKey(), result, requestBarrier);
      Callback errCallback = new HandleFailure(table, entry.getKey(), requestBarrier);

      Deferred<GetChangesResponse> response = asyncYBClient.getChangesCDCSDK(
        table, streamId, entry.getKey() /*tabletId*/,
        cp.term, cp.index, cp.key, cp.write_id, cp.time);

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

  static class CheckPoint {
    private long term;
    private long index;
    private byte[] key;
    private int write_id;
    private long time;

    public CheckPoint(long term, long index, byte[] key, int write_id,
                      long time) {
      this.term = term;
      this.index = index;
      this.key = key;
      this.write_id = write_id;
      this.time = time;
    }

    @Override
    public String toString() {
      return "CheckPoint{" +
        "term=" + term +
        ", index=" + index +
        ", key=" + Arrays.toString(key) +
        ", write_id=" + write_id +
        '}';
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) return true;
      if (o == null || getClass() != o.getClass()) return false;
      CheckPoint that = (CheckPoint) o;
      return term == that.term && index == that.index && write_id == that.write_id &&
          Objects.equal(key, that.key);
    }

    @Override
    public int hashCode() {
      return Objects.hashCode(term, index, key, write_id);
    }
  }

  final class HandleFailure implements Callback<Void, Exception> {
    private final YBTable table;
    private final String tabletId;
    private final Semaphore barrier;

    HandleFailure(YBTable table, String tabletId, Semaphore barrier) {
      this.table = table;
      this.tabletId = tabletId;
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
      // this is last chance for the application to retry.
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
      if (format.equalsIgnoreCase("PROTO")) {
        return callPROTO(response);
      }
      return callJSON(response);
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

      CheckPoint cp = null;
      if (noError) {
        cp = new CheckPoint(
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

    public Void callJSON(final GetChangesResponse response) {
      boolean noError = true;

      for (CdcService.CDCSDKRecordPB record : response
        .getResp()
        .getCdcSdkRecordsList()) {
        try {
          outputClient.applyChange(table, record);
        } catch (Exception e) {
          e.printStackTrace();
          noError = false;
          break;
        }
      }

      CheckPoint cp = null;
      if (noError) {
        cp = new CheckPoint(
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
