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

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
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
  public static class Params extends UniverseTaskParams {
    // Default usecase is 0.
    public int numTservers;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    boolean ret = false;
    YBClient client = null;
    try {
      log.info(
          "Running {}: hostPorts={}, numTservers={}.",
          getName(),
          hostPorts,
          taskParams().numTservers);
      client = ybService.getClient(hostPorts, certificate);
      waitFor(Duration.ofSeconds(getSleepMultiplier() * SLEEP_TIME));
      // When an AZ is down and Platform is unaware(external failure) then load balancing will not
      // work if we pass in the expected number of TServers to the API as the first step for load
      // balancing is the liveness check. All those TServers will be expected to be alive at the
      // minimum. The TServer which is down becuase of an external fault will fail this liveness
      // check, so load will not be balanced. NOTE: Zero implies load distribution can be checked
      // across all servers which the master leader knows about.
      ret = client.waitForLoadBalance(TIMEOUT_SERVER_WAIT_MS, taskParams().numTservers);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
    if (!ret) {
      throw new RuntimeException(getName() + " did not complete.");
    }
    log.info(
        "Completed {}: hostPorts={}, numTservers={}.",
        getName(),
        hostPorts,
        taskParams().numTservers);
  }
}
