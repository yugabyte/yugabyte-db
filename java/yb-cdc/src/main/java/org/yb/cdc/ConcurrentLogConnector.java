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
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.*;
import org.yb.master.MasterDdlOuterClass;
import org.yb.util.ServerInfo;

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.stream.Collectors;

public class ConcurrentLogConnector {
  private static final Logger LOG = LoggerFactory.getLogger(ConcurrentLogConnector.class);
  private static AsyncYBClient client;
  private static YBClient syncClient;
  private static String CDC_CONFIG_FILE = "";
  private String format;
  private final String PUBLIC_SCHEMA_NAME = "public";

  private final ExecutorService executor;
  private YBTable table;
  List<HostAndPort> hps = new ArrayList<>();
  private final OutputClient outputClient;
  private String streamId;
  private boolean enableSnapshot;
  private String sslCertFile;
  private String clientCertFile;
  private String clientKeyFile;

  private Set<String> tableIds;
  private String tableId = null;
  private String namespace;
  private String tableName;

  private String dbType;
  private Properties prop = new Properties();
  int concurrency = 1;

  private boolean stopExecution = false;
  private int pollingInterval;
  private boolean bootstrap;

  public ConcurrentLogConnector(CmdLineOpts opts, OutputClient opClient) throws Exception {
    InputStream input = new FileInputStream(opts.configFile);
    CDC_CONFIG_FILE = opts.configFile;
    enableSnapshot = opts.enableSnapshot;

    sslCertFile = opts.sslCertFile;
    clientCertFile = opts.clientCertFile;
    clientKeyFile = opts.clientKeyFile;

    pollingInterval = opts.pollingInterval;

    bootstrap = opts.bootstrap;

    // Load a properties file.
    prop.load(input);
    format = prop.getProperty("format");
    namespace = prop.getProperty("schema.name");
    dbType = prop.getProperty("db.type");
    tableName = prop.getProperty("table.name");
    String schemaName = PUBLIC_SCHEMA_NAME;

    if (tableName.contains(".")) {
      String[] splitVal = tableName.split("\\.");
      schemaName = splitVal[0];
      tableName = splitVal[1];
    }

    LOG.info("Schema name while setting up: " + schemaName);

    LOG.info(String.format("Creating new YB client with master address %s",
                            prop.getProperty("master.address")));

    client = new AsyncYBClient.AsyncYBClientBuilder(prop.getProperty("master.address"))
              .sslCertFile(sslCertFile)
              .sslClientCertFiles(clientCertFile, clientKeyFile)
              .numTablets(opts.maxTablets)
              .defaultAdminOperationTimeoutMs(Integer.parseInt(
                prop.getProperty("admin.operation.timeout.ms")))
              .defaultOperationTimeoutMs(Integer.parseInt(
                prop.getProperty("operation.timeout.ms")))
              .defaultSocketReadTimeoutMs(Integer.parseInt(
                prop.getProperty("socket.read.timeout.ms")))
              .build();

    syncClient = new YBClient(client);
    concurrency = Integer.parseInt(prop.getProperty("num.io.threads"));
    executor = Executors.newFixedThreadPool(concurrency,
            new ThreadFactoryBuilder().setNameFormat("connector-%d").build());

    ListTablesResponse tablesResp = syncClient.getTablesList();

    for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo : tablesResp
            .getTableInfoList()) {
      if (tableInfo.getName().equals(tableName) &&
        tableInfo.getNamespace().getName().equals(namespace) &&
        (tableInfo.getPgschemaName().isEmpty() || tableInfo.getPgschemaName().equals(schemaName))) {
        tableId = tableInfo.getId().toStringUtf8();
        // If the tableId is found, there's no point in iterating further.
        break;
      }
    }

    // If tableId is not found, it's likely that it's not present, we should not proceed
    // further in that case.
    if (tableId == null) {
      LOG.error(String.format("Could not find table with name %s.%s.%s",
                              namespace, schemaName, tableName));
      System.exit(0);
    }

    tableIds = new HashSet<>();
    tableIds.add(tableId);
    table = syncClient.openTableByUUID(tableId);
    ListTabletServersResponse serversResp = syncClient.listTabletServers();
    for (ServerInfo serverInfo : serversResp.getTabletServersList()) {
        hps.add(HostAndPort.fromParts(serverInfo.getHost(), serverInfo.getPort()));
    }
    outputClient = opClient;
    streamId = prop.getProperty("stream.id"); // Getting this from passed options (opts).
    input.close();
  }

  public void run() throws Exception {
    if (streamId.isEmpty()) {
      streamId = syncClient.createCDCStream(table, namespace, format, "IMPLICIT").getStreamId();
      LOG.debug(String.format("Created a new DB stream id: %s", streamId));

      prop.setProperty("stream.id", streamId);
      prop.store(new FileOutputStream(CDC_CONFIG_FILE), "null");
    }
    LOG.info(String.format("DB stream id is %s", streamId));

    List<LocatedTablet> tabletLocations = table.getTabletsLocations(30000);
    List<Map<String, List<String>>> tableIdsToTabletIdsMapList = new ArrayList<>(concurrency);

    for (int i = 0; i < concurrency; i++) {
      tableIdsToTabletIdsMapList.add(new HashMap<>());
    }
    int i = 0;
    for (String tableId : tableIds) {
      for (LocatedTablet tablet : tabletLocations) {
        i++;
        String tabletId = new String(tablet.getTabletId());
        tableIdsToTabletIdsMapList.get(i % concurrency).putIfAbsent(tableId,
                new ArrayList<>());
        tableIdsToTabletIdsMapList.get(i % concurrency).get(tableId).add(tabletId);
      }
    }

    List<Runnable> runnables = tableIdsToTabletIdsMapList.stream().map(
            tableIdsToTabletIds -> {
                try {
                  return new ConcurrentPoller(syncClient, client, outputClient, streamId,
                                              tableIdsToTabletIds, 2, format, stopExecution,
                                              enableSnapshot, bootstrap);
                } catch (IOException e) {
                  e.printStackTrace();
                }
                return null;
            }).filter(poller -> poller != null).map(poller -> (Runnable) () -> {
        try {
            while (true) {
              poller.poll();
              Thread.sleep(pollingInterval);
            }
        } catch (Exception e) {
          e.printStackTrace();
        }
    }).collect(Collectors.toList());

    List<Future> futures = runnables.stream()
        .map(r -> executor.submit(r)).collect(Collectors.toList());

    for (Future future : futures) {
      future.get();
    }
  }

  public void close() {
    stopExecution = true;
  }
}
