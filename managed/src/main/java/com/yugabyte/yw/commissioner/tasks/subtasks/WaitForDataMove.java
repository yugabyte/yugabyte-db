// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.YBClient;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;
import com.yugabyte.yw.commissioner.tasks.params.UniverseTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;

import play.api.Play;

public class WaitForDataMove extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(WaitForDataMove.class);

  // The YB client to use.
  private YBClientService ybService;

  // Time to wait (in millisec) during each iteration of load move completion check.
  private static final int WAIT_EACH_ATTEMPT_MS = 100;

  // Number of response errors to tolerate.
  private static final int MAX_ERRORS_TO_IGNORE = 128;

  // Log after these many iterations
  private static final int LOG_EVERY_NUM_ITERS = 100;

  // Parameters for data move wait task.
  public static class Params extends UniverseTaskParams { }

  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public void run() {
    String errorMsg = null;
    try {
      // Get the master addresses.
      String masterAddresses = Universe.get(taskParams().universeUUID).getMasterAddresses();
      LOG.info("Running {} on masterAddress = {}", getName(), masterAddresses);

      int numErrors = 0;
      double percent = 0;
      int numIters = 0;
      YBClient client = ybService.getClient(masterAddresses);

      LOG.info("Leader Master UUID={}, HostPort={}.",
               client.getLeaderMasterUUID(), client.getLeaderMasterHostAndPort().toString());

      // TODO: Have a mechanism to send this percent to the parent task completion.
      while (percent < (double)100) {
        GetLoadMovePercentResponse response = client.getLoadMoveCompletion();

        if (response.hasError()) {
          LOG.warn("{} response has error {}.", getName(), response.errorMessage());
          numErrors++;
          // If there are more than the threshold of response errors, bail out. 
          if (numErrors >= MAX_ERRORS_TO_IGNORE) {
            errorMsg = getName() + ": hit too many errors during data move completion wait.";
            break;
          }
        }

        percent = response.getPercentCompleted();
        // No need to wait if completed (as in, percent == 100).
        if (percent < (double)100) {
          Thread.sleep(WAIT_EACH_ATTEMPT_MS);
        }

        numIters++;
        if (numIters % LOG_EVERY_NUM_ITERS == 0) {
          LOG.info("Info: iters={}, percent={}, numErrors={}.", numIters, percent, numErrors);
        }
        // For now, we wait until load moves out fully. TODO: Add an overall timeout as needed.
      }
    } catch (Exception e) {
      LOG.error("{} hit error {}.", getName(), e.getMessage(), e);
      throw new RuntimeException(getName() + " hit error: " , e);
    }

    if (errorMsg != null) {
      LOG.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }
  }
}
