// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.YBClient;

@Slf4j
public class WaitForLeaderBlacklistCompletion extends AbstractTaskBase {

  // Time to wait (in millisec) during each iteration of load move completion check.
  private static final int WAIT_EACH_ATTEMPT_MS = 1000;

  // Number of response errors to tolerate.
  private static final int MAX_ERRORS_TO_IGNORE = 128;

  // Log after these many iterations
  private static final int LOG_EVERY_NUM_ITERS = 10;

  @Inject
  protected WaitForLeaderBlacklistCompletion(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for data move wait task.
  public static class Params extends UniverseTaskParams {
    // Max time to wait for leader blacklist completion
    public int waitTimeMs;
  }

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
    double epsilon = 0.00001d;
    // Get the master addresses and certificate info.
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    log.info("Running {} on masterAddress = {}.", getName(), masterAddresses);
    long startTime = System.currentTimeMillis();
    try {

      client = ybService.getClient(masterAddresses, certificate);
      log.info("Leader Master UUID={}.", client.getLeaderMasterUUID());

      while (((double) 100 - percent) > epsilon) {
        GetLoadMovePercentResponse response = client.getLeaderBlacklistCompletion();
        log.trace(
            "percentage completed: {}, getRemaining: {}, getTotal:{}",
            response.getPercentCompleted(),
            response.getRemaining(),
            response.getTotal());

        if (response.hasError()) {
          log.warn("{} response has error {}.", getName(), response.errorMessage());
          numErrors++;
          // If there are more than the threshold of response errors, bail out.
          if (numErrors >= MAX_ERRORS_TO_IGNORE) {
            errorMsg = getName() + ": hit too many errors during leader blacklist completion wait.";
            break;
          }
          waitFor(Duration.ofMillis(WAIT_EACH_ATTEMPT_MS));
          continue;
        }

        percent = response.getPercentCompleted();
        // No need to wait if completed (as in, percent == 100).
        if (((double) 100 - percent) > epsilon) {
          waitFor(Duration.ofMillis(WAIT_EACH_ATTEMPT_MS));
        }

        numIters++;
        if (numIters % LOG_EVERY_NUM_ITERS == 0) {
          log.info("Info: iters={}, percent={}, numErrors={}.", numIters, percent, numErrors);
        }

        // If we waited more than maxWaitTimeMs for completion, continue on
        long curTime = System.currentTimeMillis();
        long timeElaped = curTime - startTime;
        if (timeElaped >= taskParams().waitTimeMs) {
          log.info(
              "Timing out after iters={}. percent={}, numErrors={}. Continuing on",
              numIters,
              percent,
              numErrors);
          break;
        }
      }
    } catch (Exception e) {
      log.error("{} hit error {}.", getName(), e.getMessage(), e);
      Throwables.propagate(e);
    } finally {
      ybService.closeClient(client, masterAddresses);
    }

    if (errorMsg != null) {
      log.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }

    long endTime = System.currentTimeMillis();
    log.debug(
        "Waiting for leader blacklist completion took {} milliseconds", (endTime - startTime));
  }
}
