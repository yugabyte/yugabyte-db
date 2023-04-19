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
import java.time.Duration;
import java.util.concurrent.CancellationException;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

// Helper class to wait for a minimum number of tservers to heartbeat to the
// master leader. Currently the minimum is the same as the replication factor,
// so that next set of tasks like creating a table will not lack tserver resources.
@Slf4j
public class WaitForTServerHeartBeats extends AbstractTaskBase {

  // Timeout when minimum number of tservers have not heartbeatean to master leader.
  private static final long TIMEOUT_SERVER_WAIT_MS = 120000;

  // Time to wait (in millisec) during each iteration of check.
  private static final int WAIT_EACH_ATTEMPT_MS = 250;

  @Inject
  protected WaitForTServerHeartBeats(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for tserver heartbeat wait task.
  public static class Params extends UniverseTaskParams {}

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
    int numTservers = universe.getTServers().size();
    YBClient client = ybService.getClient(hostPorts, certificate);
    try {
      log.info("Running {}: hostPorts={}, numTservers={}.", getName(), hostPorts, numTservers);
      int numTries = 1;
      long start = System.currentTimeMillis();
      boolean timedOut = false;
      do {
        try {
          int currentNumTservers = client.listTabletServers().getTabletServersCount();

          log.info("{} tservers heartbeating to master leader.", currentNumTservers);

          if (currentNumTservers >= numTservers) {
            break;
          }

          log.info(
              "Waiting to make sure {} tservers are heartbeating to master leader; retrying "
                  + "after {}ms. Tried {} times.",
              numTservers,
              WAIT_EACH_ATTEMPT_MS,
              numTries);
          waitFor(Duration.ofMillis(getSleepMultiplier() * WAIT_EACH_ATTEMPT_MS));
          numTries++;
        } catch (CancellationException e) {
          throw e;
        } catch (Exception e) {
          log.warn("{}: ignoring error '{}'.", getName(), e.getMessage());
        }
        timedOut = System.currentTimeMillis() >= start + TIMEOUT_SERVER_WAIT_MS;
      } while (!timedOut);
      if (timedOut) {
        throw new RuntimeException(getName() + " timed out.");
      }
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }
}
