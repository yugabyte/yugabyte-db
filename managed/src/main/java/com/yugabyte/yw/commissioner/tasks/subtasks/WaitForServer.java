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

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
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
}
