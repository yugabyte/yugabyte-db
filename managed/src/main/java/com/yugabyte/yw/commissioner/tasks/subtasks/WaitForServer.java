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
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class WaitForServer extends ServerSubTaskBase {

  private final YsqlQueryExecutor ysqlQueryExecutor;

  private final long POSTGRES_STATUS_RETRY_SLEEP_TIME = 60000;

  @Inject
  protected WaitForServer(
      BaseTaskDependencies baseTaskDependencies, YsqlQueryExecutor ysqlQueryExecutor) {
    super(baseTaskDependencies);
    this.ysqlQueryExecutor = ysqlQueryExecutor;
  }

  public static class Params extends ServerSubTaskParams {
    // Timeout for the RPC call.
    public long serverWaitTimeoutMs;
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
        Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
        NodeDetails node = universe.getNode(taskParams().nodeName);
        ret = false;
        long retriesCount =
            (taskParams().serverWaitTimeoutMs + POSTGRES_STATUS_RETRY_SLEEP_TIME - 1)
                / POSTGRES_STATUS_RETRY_SLEEP_TIME;
        while (retriesCount > 0 && !ret) {
          ret = checkPostgresStatus(universe, node);
          retriesCount--;
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

  private boolean checkPostgresStatus(Universe universe, NodeDetails node) {
    RunQueryFormData runQueryFormData = new RunQueryFormData();
    runQueryFormData.query = "SELECT version()";
    runQueryFormData.db_name = "system_platform";
    JsonNode ysqlResponse =
        ysqlQueryExecutor.executeQueryInNodeShell(
            universe, runQueryFormData, universe.getNode(taskParams().nodeName));
    return !ysqlResponse.has("error");
  }
}
