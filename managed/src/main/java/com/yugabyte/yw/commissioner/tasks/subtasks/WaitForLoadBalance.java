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

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class WaitForLoadBalance extends AbstractTaskBase {

  // Timeout for failing to complete load balance. Currently we do no timeout.
  // NOTE: This is similar to WaitForDataMove for blacklist removal.
  private static final long TIMEOUT_SERVER_WAIT_MS = Long.MAX_VALUE;

  // Time to sleep for before querying the loadbalancer.
  // This is done to give the loadbalancer enough time to
  // start the task of loadbalancing.
  private static final int SLEEP_TIME = 10;

  @Inject
  protected WaitForLoadBalance(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for data move wait task.
  public static class Params extends UniverseTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ")";
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    int numTservers = universe.getTServers().size();
    boolean ret = false;
    YBClient client = null;
    try {
      log.info("Running {}: hostPorts={}, numTservers={}.", getName(), hostPorts, numTservers);
      client = ybService.getClient(hostPorts, certificate);
      TimeUnit.SECONDS.sleep(getSleepMultiplier() * SLEEP_TIME);
      ret = client.waitForLoadBalance(TIMEOUT_SERVER_WAIT_MS, numTservers);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
    if (!ret) {
      throw new RuntimeException(getName() + " did not complete.");
    }
  }
}
