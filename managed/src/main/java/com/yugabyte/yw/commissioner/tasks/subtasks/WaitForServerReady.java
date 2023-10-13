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
import com.google.common.base.Stopwatch;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.concurrent.CancellationException;
import java.util.concurrent.TimeUnit;
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

  private void sleepFor(long waitTimeMs) {
    waitFor(Duration.ofMillis(getSleepMultiplier() * waitTimeMs));
  }

  // Helper function to sleep for any pending amount of time in userWaitTimeMs, assuming caller
  // has timeElapsedMs of sleep.
  private void sleepRemaining(long userWaitTimeMs, long timeElapsedMs) {
    if (userWaitTimeMs > timeElapsedMs) {
      sleepFor(userWaitTimeMs - timeElapsedMs);
    }
  }

  @Override
  public void run() {
    checkParams();

    int numIters = 0;
    Duration userWaitTime = Duration.ofMillis(taskParams().waitTimeMs);
    HostAndPort hp = getHostPort();
    boolean isMasterTask = taskParams().serverType == ServerType.MASTER;
    IsServerReadyResponse response = null;
    String errorMessage = null;
    boolean shouldLog = false;

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    // Max timeout to wait for check to complete.
    Duration maxSubtaskTimeout =
        confGetter.getConfForScope(universe, UniverseConfKeys.waitForServerReadyTimeout);
    Stopwatch stopwatch = Stopwatch.createStarted();

    try (YBClient client = getClient()) {
      while (true) {
        shouldLog = (numIters % LOG_EVERY_NUM_ITERS) == 0;
        if (stopwatch.elapsed().compareTo(maxSubtaskTimeout) > 0) {
          log.info("Timing out after iters={}. error '{}'.", numIters, errorMessage);
          throw new RuntimeException(
              String.format(
                  "WaitForServerReady, timing out after retrying %d times for a duration of %dms,"
                      + " greater than max time out of %dms. Failing...",
                  numIters, stopwatch.elapsed().toMillis(), maxSubtaskTimeout.toMillis()));
        }
        errorMessage = null;

        try {
          response = client.isServerReady(hp, !isMasterTask);
          if (response.hasError()) {
            errorMessage = String.format("isServerReady rpc failed: %s", response.errorMessage());
          } else if (response.getNumNotRunningTablets() == 0) {
            log.info(
                "{} on node {} ready after iters={}.",
                taskParams().serverType,
                taskParams().nodeName,
                numIters);
            break;
          } else {
            // At least one tablet is not in running state.
            errorMessage =
                String.format(
                    "%d tablets not running out of %d.",
                    response.getNumNotRunningTablets(), response.getTotalTablets());
          }

          if (shouldLog) {
            log.info(
                "{} on node {} not ready after iters={}, error '{}'.",
                taskParams().serverType,
                taskParams().nodeName,
                numIters,
                errorMessage);
          }

        } catch (CancellationException e) {
          throw e;
        } catch (Exception e) {
          log.error("{} hit error : '{}' after {} iters", getName(), e.getMessage(), numIters);
          errorMessage = String.format("YBClient error: %s", e.getMessage());
        } finally {
          numIters++;
          sleepFor(WAIT_EACH_ATTEMPT_MS);
        }
      }
    } catch (Exception e) {
      Throwables.propagate(e);
    }

    // Sleep for the remaining portion of user specified time, if any.
    sleepRemaining(userWaitTime.toMillis(), stopwatch.elapsed(TimeUnit.MILLISECONDS));
  }
}
