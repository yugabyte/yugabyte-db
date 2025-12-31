/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
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
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class WaitForServer extends ServerSubTaskBase {

  @Inject
  protected WaitForServer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends ServerSubTaskParams {
    // Timeout for the RPC call.
    public long serverWaitTimeoutMs;
    // During upgrades we update universe state only at the end of the task.
    // So here we can pass updated universe state that is not yet persisted.
    @Nullable public Universe currentUniverseState;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {

    checkParams();
    Universe universe =
        taskParams().currentUniverseState == null
            ? Universe.getOrBadRequest(taskParams().getUniverseUUID())
            : taskParams().currentUniverseState;
    boolean ret;
    long startMs = System.currentTimeMillis();
    try (YBClient client = getClient()) {
      HostAndPort hp = getHostPort();
      if (taskParams().serverType == ServerType.MASTER) {
        // This first calls waitForServer followed by availability check of master UUID.
        // Check for master UUID retries until timeout.
        ret = client.waitForMaster(hp, taskParams().serverWaitTimeoutMs);
      } else if (taskParams().serverType.equals(ServerType.YSQLSERVER)) {
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
      if (ret
          && (taskParams().serverType == ServerType.MASTER
              || taskParams().serverType == ServerType.TSERVER)
          && confGetter.getConfForScope(universe, UniverseConfKeys.verifyClusterUUIDOnStart)) {
        verifyUniverseUUID(hp.getHost(), taskParams().serverType, universe);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
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
    NodeDetails node = universe.getNode(taskParams().nodeName);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    JsonNode ysqlResponse =
        ysqlQueryExecutor.executeQueryInNodeShell(
            universe,
            runQueryFormData,
            node,
            userIntent.isYSQLAuthEnabled(),
            userIntent.enableConnectionPooling);
    return !ysqlResponse.has("error");
  }

  private void verifyUniverseUUID(String privateIp, ServerType serverType, Universe universe) {
    NodeDetails nodeDetails = universe.getNodeByAnyIP(privateIp);
    int port =
        serverType == ServerType.MASTER ? nodeDetails.masterHttpPort : nodeDetails.tserverHttpPort;
    JsonNode varz = nodeUIApiHelper.getRequest("http://" + privateIp + ":" + port + "/api/v1/varz");
    JsonNode errors = varz.get("error");
    if (errors != null) {
      throw new RuntimeException("Received error: " + errors.asText());
    }
    String universeUUIDStr = universe.getUniverseUUID().toString();
    if (varz.get("flags") == null) {
      throw new RuntimeException("No flags in response: " + varz);
    }
    for (JsonNode flag : varz.get("flags")) {
      if (flag.get("name").asText().equals(GFlagsUtil.CLUSTER_UUID)) {
        String flagValue = flag.get("value").asText().trim();
        if (!flagValue.equalsIgnoreCase(universeUUIDStr)) {
          throw new IllegalStateException(
              String.format(
                  "Expected '%s' for %s %s gflag, found '%s'",
                  universeUUIDStr, serverType.name(), GFlagsUtil.CLUSTER_UUID, flagValue));
        }
      }
    }
  }
}
