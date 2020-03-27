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
import org.apache.log4j.Logger;
import org.yb.client.AsyncYBClient;
import org.yb.client.GetChangesResponse;
import org.yb.client.YBTable;

import java.util.Random;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.List;

import com.google.common.net.HostAndPort;


public class Poller {
  private static final Logger LOG = Logger.getLogger(Poller.class);

  private long term;
  private long index;
  private String streamId;
  private String tabletId;
  private ExecutorService executor;
  private YBTable table;
  private AsyncYBClient client;
  private List<HostAndPort> hps;
  private OutputClient outputClient;

  public Poller(OutputClient outputClient, List<HostAndPort> hps, YBTable table, String streamId,
                String tabletId, ExecutorService executor, AsyncYBClient client) {
    this.streamId = streamId;
    this.tabletId = tabletId;
    this.term = 0;
    this.index = 0;
    this.executor = executor;
    this.client = client;
    this.table = table;
    this.hps = hps;
    this.outputClient = outputClient;
  }

  public Void poll() {
    executor.submit(new Callable<Void>() {
      @Override
      public Void call() throws Exception {
        return doPoll();
      }
    });
    return null;
  }

  private Void doPoll() {
    Random rand = new Random();
    HostAndPort hp = hps.get(rand.nextInt(hps.size()));

    client.getChanges(hp, table, streamId, tabletId, term, index,
                      new Callback<Void, GetChangesResponse>() {
      @Override
      public Void call(GetChangesResponse getChangesResponse) throws Exception {
        return handlePoll(getChangesResponse);
      }
    });
    return null;
  }

  private Void handlePoll(GetChangesResponse getChangesResponse) {
    executor.submit(new Callable<Void>() {
      @Override
      public Void call() throws Exception {
        return doHandlePoll(getChangesResponse);
      }
    });
    return null;
  }

  private Void doHandlePoll(GetChangesResponse getChangesResponse) throws Exception {
    // Handle the poll
    if (getChangesResponse.getResp().hasError()) {
      if (getChangesResponse.getResp().getError().getCode() ==
              CdcService.CDCErrorPB.Code.INVALID_REQUEST) {
        LOG.error("Invalid Request");
        System.exit(0);
      }
      return poll();
    }

    for (org.yb.cdc.CdcService.CDCRecordPB record : getChangesResponse.getResp().getRecordsList()) {
      outputClient.applyChange(table, record);
    }

    this.term = getChangesResponse.getResp().getCheckpoint().getOpId().getTerm();
    this.index = getChangesResponse.getResp().getCheckpoint().getOpId().getIndex();

    return poll();
  }

}
