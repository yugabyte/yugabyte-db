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
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.forms.UpgradeParams;
import java.time.Duration;
import java.util.concurrent.CancellationException;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.YBClient;

@Slf4j
public class WaitForServerReady extends ServerSubTaskBase {

  // Time to wait (in millisec) during each iteration of server readiness check.
  private static final int WAIT_EACH_ATTEMPT_MS = 1000;

  // Log after these many iterations.
  private static final int LOG_EVERY_NUM_ITERS = 100;

  // Maximum total wait time for the rpc to return 0 not-running tablets (10min).
  private static final int MAX_TOTAL_WAIT_MS = 600000;

  @Inject
  protected WaitForServerReady(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for wait task.
  public static class Params extends ServerSubTaskParams {
    // Time to wait (as a backup) in case the server does not support is-ready check rpc.
    public int waitTimeMs;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  private void sleepFor(int waitTimeMs) {
    waitFor(Duration.ofMillis(getSleepMultiplier() * waitTimeMs));
  }

  // Helper function to sleep for any pending amount of time in userWaitTime, assuming caller
  // has completed numIters of sleep.
  private void sleepRemaining(int userWaitTimeMs, int numIters) {
    if (userWaitTimeMs > (WAIT_EACH_ATTEMPT_MS * numIters)) {
      sleepFor(userWaitTimeMs - (WAIT_EACH_ATTEMPT_MS * numIters));
    }
  }

  @Override
  public void run() {

    checkParams();

    int numIters = 0;
    int userWaitTimeMs =
        taskParams().waitTimeMs != 0
            ? taskParams().waitTimeMs
            : UpgradeParams.DEFAULT_SLEEP_AFTER_RESTART_MS;

    HostAndPort hp = getHostPort();
    boolean isMasterTask = taskParams().serverType == ServerType.MASTER;

    IsServerReadyResponse response = null;
    YBClient client = getClient();
    try {
      while (true) {
        numIters++;
        response = client.isServerReady(hp, !isMasterTask);

        if (response.hasError()) {
          log.info("Response has error {} after iters={}.", response.errorMessage(), numIters);
          break;
        }

        if (response.getNumNotRunningTablets() == 0) {
          log.info(
              "{} on node {} ready after iters={}.",
              taskParams().serverType,
              taskParams().nodeName,
              numIters);
          break;
        }

        if (numIters > (MAX_TOTAL_WAIT_MS / WAIT_EACH_ATTEMPT_MS)) {
          log.info(
              "Timing out after iters={}. {} tablets not running, out of {}.",
              numIters,
              response.getNumNotRunningTablets(),
              response.getTotalTablets());
          break;
        }

        if (numIters % LOG_EVERY_NUM_ITERS == 0) {
          log.info(
              "{} on node {} not ready after iters={}, {} tablets not running out of {}.",
              taskParams().serverType,
              taskParams().nodeName,
              numIters,
              response.getNumNotRunningTablets(),
              response.getTotalTablets());
        }

        sleepFor(WAIT_EACH_ATTEMPT_MS);
      }
    } catch (CancellationException e) {
      throw e;
    } catch (Exception e) {
      // There is no generic mechanism from proto/rpc to check if an older server does not have
      // this rpc implemented. So, we just sleep for remaining time on any such error.
      log.info("{} hit exception '{}' after {} iters.", getName(), e.getMessage(), numIters);
    } finally {
      closeClient(client);
    }

    // Sleep for the remaining portion of user specified time, if any.
    sleepRemaining(userWaitTimeMs, numIters);
  }
}
