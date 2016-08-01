// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.GetLoadMovePercentResponse;

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
    try {
      // Get the master addresses.
      String masterAddresses = Universe.get(taskParams().universeUUID).getMasterAddresses();
      LOG.info("Running {} on masterAddress = {}", getName(), masterAddresses);
      if (masterAddresses == null || masterAddresses.isEmpty()) {
        throw new IllegalStateException("No master host/ports for a load move wait op in " +
            taskParams().universeUUID);
      }

      int num_errors = 0;
      double percent = 0;
      // TODO: Have a mechanism to send this percent to the parent task completion.
      while (percent <= 100) {
        GetLoadMovePercentResponse response =
          ybService.getClient(masterAddresses).getLoadMoveCompletion();

        if (response.hasError()) {
          // If there are more than the threshold of response errors, bail out. 
          if (num_errors >= MAX_ERRORS_TO_IGNORE) {
            // Logging happens in catch block.
            throw new RuntimeException("Hit too many errors during tablet load move completion check."); 
          }

          LOG.warn("{} response has error {}.", getName(), response.errorMessage());
          num_errors++;
        }

        percent = response.getPercentCompleted();
        // No need to wait if completed (as in, percent == 100).
        if (percent < 100) {
          Thread.sleep(WAIT_EACH_ATTEMPT_MS);
        }

        // For now, we wait until load moves out fully. TODO: Add an overall timeout as needed.
      }
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(getName() + " hit error: " , e);
    }
  }
}
