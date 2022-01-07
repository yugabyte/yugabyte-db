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

import com.google.common.net.HostAndPort;
import org.yb.client.YBTable;
import org.yb.client.AsyncYBClient;
import org.yb.client.YBClient;
import org.yb.client.ListTablesResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.LocatedTablet;

import java.util.ArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.*;
import org.apache.log4j.Logger;
import org.yb.master.MasterDdlOuterClass;
import org.yb.util.ServerInfo;

class LogConnector {
  private static final Logger LOG = Logger.getLogger(LogConnector.class);

  private static AsyncYBClient client;
  private static YBClient syncClient;

  private ExecutorService executor;

  private static final int DEFAULT_TIMEOUT = 30000;
  private static final int DEFAULT_NUM_THREADS = 8;

  private YBTable table;

  private List<Poller> pollers = new ArrayList<>();

  List<HostAndPort> hps = new ArrayList<>();

  private OutputClient outputClient;

  private String streamId;

  public LogConnector(CmdLineOpts opts) throws Exception {
    LOG.info("Creating new YB client...");
    client = new AsyncYBClient.AsyncYBClientBuilder(opts.masterAddrs)
            .defaultAdminOperationTimeoutMs(DEFAULT_TIMEOUT)
            .defaultOperationTimeoutMs(DEFAULT_TIMEOUT)
            .defaultSocketReadTimeoutMs(DEFAULT_TIMEOUT)
            .build();

    syncClient = new YBClient(client);

    executor = Executors.newFixedThreadPool(DEFAULT_NUM_THREADS);

    String tableId = null;
    ListTablesResponse tablesResp = syncClient.getTablesList();
    for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo :
            tablesResp.getTableInfoList()) {
      if (tableInfo.getName().equals(opts.tableName) &&
          tableInfo.getNamespace().getName().equals(opts.namespaceName)) {
        tableId =  tableInfo.getId().toStringUtf8();
      }
    }

    if (tableId == null) {
      LOG.error(String.format("Could not find a table with name %s.%s",
                              opts.namespaceName, opts.tableName));
      System.exit(0);
    }

    table = syncClient.openTableByUUID(tableId);

    ListTabletServersResponse serversResp = syncClient.listTabletServers();
    for (ServerInfo serverInfo : serversResp.getTabletServersList()) {
      hps.add(HostAndPort.fromParts(serverInfo.getHost(), serverInfo.getPort()));
    }

    outputClient = new LogClient();

    streamId = opts.streamId;
  }

  public void run() throws Exception {
    Random rand = new Random();

    HostAndPort hp = hps.get(rand.nextInt(hps.size()));
    if (streamId.isEmpty()) {
      streamId = syncClient.createCDCStream(hp, table.getTableId()).getStreamId();
      LOG.info(String.format("Created new stream with id %s", streamId));
    }

    List<LocatedTablet> tabletLocations = table.getTabletsLocations(DEFAULT_TIMEOUT);

    for (LocatedTablet tablet : tabletLocations) {
      String tabletId = new String(tablet.getTabletId());
      LOG.info(String.format("Polling for new tablet %s", tabletId));
      Poller poller = new Poller(outputClient, hps, table, streamId, tabletId, executor, client);
      pollers.add(poller);
      poller.poll();
    }
  }
}
