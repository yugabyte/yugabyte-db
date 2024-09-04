/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.base.Stopwatch;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class WaitForServer extends ServerSubTaskBase {

  private final YsqlQueryExecutor ysqlQueryExecutor;

  @Inject
  protected WaitForServer(
      BaseTaskDependencies baseTaskDependencies, YsqlQueryExecutor ysqlQueryExecutor) {
    super(baseTaskDependencies);
    this.ysqlQueryExecutor = ysqlQueryExecutor;
  }

  public static class Params extends ServerSubTaskParams {
    // Timeout for the RPC call.
    public long serverWaitTimeoutMs;
    public UniverseDefinitionTaskParams.UserIntent userIntent;
    public CommunicationPorts customCommunicationPorts;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {

    checkParams();

    boolean ret;
    YBClient client = null;
    long startMs = System.currentTimeMillis();
    try {
      HostAndPort hp = getHostPort();
      client = getClient();
      if (taskParams().serverType == ServerType.MASTER) {
        // This first calls waitForServer followed by availability check of master UUID.
        // Check for master UUID retries until timeout.
        ret = client.waitForMaster(hp, taskParams().serverWaitTimeoutMs);
      } else if (taskParams().serverType.equals(ServerType.YSQLSERVER)) {
        Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
        NodeDetails node = universe.getNode(taskParams().nodeName);
        Provider provider =
            Provider.getOrBadRequest(
                UUID.fromString(
                    universe
                        .getUniverseDetails()
                        .getClusterByUuid(node.placementUuid)
                        .userIntent
                        .provider));
        Duration waitTimeout = Duration.ofMillis(taskParams().serverWaitTimeoutMs);
        Stopwatch stopwatch = Stopwatch.createStarted();
        Duration waitDuration =
            confGetter.getConfForScope(provider, ProviderConfKeys.waitForYQLRetryDuration);
        log.debug("Retry duration is {}", waitDuration);
        while (true) {
          log.info("Check if postgres server is healthy on node {}", node.nodeName);
          ret = checkPostgresStatus(universe);
          if (ret || stopwatch.elapsed().compareTo(waitTimeout) > 0) break;
          waitFor(waitDuration);
        }
      } else {
        ret = client.waitForServer(hp, taskParams().serverWaitTimeoutMs);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      closeClient(client);
    }
    if (!ret) {
      throw new RuntimeException(getName() + " did not respond in the set time.");
    }
    log.info(
        "Server {} responded to RPC calls in {} ms",
        (taskParams().nodeName != null) ? taskParams().nodeName : "unknown",
        (System.currentTimeMillis() - startMs));
  }

  private boolean checkPostgresStatus(Universe universe) {
    RunQueryFormData runQueryFormData = new RunQueryFormData();
    runQueryFormData.setQuery("SELECT version()");
    runQueryFormData.setDbName("system_platform");
    UniverseDefinitionTaskParams.UserIntent userIntent = taskParams().userIntent;
    if (userIntent == null) {
      userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    }
    int internalYsqlServerRpcPort;
    if (taskParams().customCommunicationPorts != null) {
      // Other flows like configure YSQL flow.
      internalYsqlServerRpcPort = taskParams().customCommunicationPorts.internalYsqlServerRpcPort;
    } else {
      // Create universe flow.
      internalYsqlServerRpcPort =
          universe.getUniverseDetails().communicationPorts.internalYsqlServerRpcPort;
    }
    NodeDetails node = universe.getNode(taskParams().nodeName);
    if (taskParams().customCommunicationPorts != null) {
      UniverseTaskParams.CommunicationPorts.setCommunicationPorts(
          taskParams().customCommunicationPorts, node);
    }
    JsonNode ysqlResponse =
        ysqlQueryExecutor.executeQueryInNodeShell(
            universe,
            runQueryFormData,
            node,
            userIntent.isYSQLAuthEnabled(),
            userIntent.enableConnectionPooling,
            internalYsqlServerRpcPort);
    return !ysqlResponse.has("error");
  }
}
