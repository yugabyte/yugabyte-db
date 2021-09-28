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
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.YBClient;

@Slf4j
public class WaitForDataMove extends AbstractTaskBase {

  // Time to wait (in millisec) during each iteration of load move completion check.
  private static final int WAIT_EACH_ATTEMPT_MS = 100;

  // Number of response errors to tolerate.
  private static final int MAX_ERRORS_TO_IGNORE = 128;

  // Log after these many iterations
  private static final int LOG_EVERY_NUM_ITERS = 100;

  @Inject
  protected WaitForDataMove(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for data move wait task.
  public static class Params extends UniverseTaskParams {}

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    String errorMsg = null;
    YBClient client = null;
    int numErrors = 0;
    double percent = 0;
    int numIters = 0;
    // Get the master addresses and certificate info.
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    log.info("Running {} on masterAddress = {}.", getName(), masterAddresses);

    try {

      client = ybService.getClient(masterAddresses, certificate);
      log.info("Leader Master UUID={}.", client.getLeaderMasterUUID());

      // TODO: Have a mechanism to send this percent to the parent task completion.
      while (percent < 100) {
        GetLoadMovePercentResponse response = client.getLoadMoveCompletion();

        if (response.hasError()) {
          log.warn("{} response has error {}.", getName(), response.errorMessage());
          numErrors++;
          // If there are more than the threshold of response errors, bail out.
          if (numErrors >= MAX_ERRORS_TO_IGNORE) {
            errorMsg = getName() + ": hit too many errors during data move completion wait.";
            break;
          }
          Thread.sleep(getSleepMultiplier() * WAIT_EACH_ATTEMPT_MS);
          continue;
        }

        percent = response.getPercentCompleted();
        // No need to wait if completed (as in, percent == 100).
        if (percent < 100) {
          Thread.sleep(getSleepMultiplier() * WAIT_EACH_ATTEMPT_MS);
        }

        numIters++;
        if (numIters % LOG_EVERY_NUM_ITERS == 0) {
          log.info("Info: iters={}, percent={}, numErrors={}.", numIters, percent, numErrors);
        }
        // For now, we wait until load moves out fully. TODO: Add an overall timeout as needed.
      }
    } catch (Exception e) {
      log.error("{} hit error {}.", getName(), e.getMessage(), e);
      throw new RuntimeException(getName() + " hit error: ", e);
    } finally {
      ybService.closeClient(client, masterAddresses);
    }

    if (errorMsg != null) {
      log.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }
  }
}
