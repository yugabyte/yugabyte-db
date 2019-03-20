// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.IsTabletServerReadyResponse;
import org.yb.client.YBClient;
import org.yb.tserver.Tserver.TabletServerErrorPB;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.RollingRestartParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.api.Play;

public class WaitForServerReady extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(WaitForServerReady.class);

  // The YB client to use.
  private YBClientService ybService;

  // Time to wait (in millisec) during each iteration of server readiness check.
  private static final int WAIT_EACH_ATTEMPT_MS = 1000;

  // Log after these many iterations.
  private static final int LOG_EVERY_NUM_ITERS = 100;

  // Maximum total wait time for the rpc to return 0 not-running tablets (10min).
  private static final int MAX_TOTAL_WAIT_MS = 600000;

  // Parameters for wait task.
  public static class Params extends UniverseTaskParams {
    // The name of the node which contains the server process.
    public String nodeName;

    // Server type running on the above node for which we will wait.
    public ServerType serverType;

    // Time to wait (as a backup) in case the server does not support is-ready check rpc.
    public int waitTimeMs;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ", " + taskParams().nodeName +
        ", type=" + taskParams().serverType + ")";
  }

  private void sleepFor(int waitTimeMs) {
    try {
      Thread.sleep(waitTimeMs);
    } catch (InterruptedException ie) {
      // Do nothing
    }
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
    YBClient client = null;
    int numIters = 0;
    int userWaitTimeMs = taskParams().waitTimeMs != 0 ? taskParams().waitTimeMs :
                             RollingRestartParams.DEFAULT_SLEEP_AFTER_RESTART_MS;

    // Need not perform any rpc for MASTER.
    if (taskParams().serverType == ServerType.MASTER) {
      LOG.info("Waiting for master on " + taskParams().nodeName);
      sleepFor(userWaitTimeMs);
      return;
    }

    String masterAddresses = Universe.get(taskParams().universeUUID).getMasterAddresses();
    LOG.info("Running {} on masterAddress = {}.", getName(), masterAddresses);

    if (masterAddresses == null || masterAddresses.isEmpty()) {
      throw new IllegalArgumentException("Invalid master addresses " + masterAddresses + " for " +
          taskParams().universeUUID);
    }

    client = ybService.getClient(masterAddresses);
    NodeDetails node = Universe.get(taskParams().universeUUID).getNode(taskParams().nodeName);

    if (node == null) {
      throw new IllegalArgumentException("Node " + taskParams().nodeName + " not found in " +
                                         "universe " + taskParams().universeUUID);
    }

    if (taskParams().serverType == ServerType.TSERVER && !node.isTserver) {
      throw new IllegalArgumentException("Task server type " + taskParams().serverType + " is " +
                                         "not for a node running tserver : " + node.toString());
    }

    HostAndPort hp = HostAndPort.fromParts(node.cloudInfo.private_ip, node.tserverRpcPort);

    IsTabletServerReadyResponse response = null;
    while (true) {
      numIters++;
      try {
        response = client.isTServerReady(hp);
      } catch (Exception e) {
        String excepMsg = getName() + " hit error " + e.getMessage();
        // There is no generic mechanism from proto/rpc to check if an older server does not have
        // this rpc implemented. So, we just sleep for remaining time on any such error.
        if (e.getMessage().contains("invalid method name: IsTabletServerReady")) {
          LOG.info(excepMsg);
          break;
        }

        if (numIters > (MAX_TOTAL_WAIT_MS / WAIT_EACH_ATTEMPT_MS)) {
          LOG.info("Timing out after iters={}, message={}, numNotRunning={}, excepMsg={}.",
                   numIters, response != null ? response.errorMessage() : "",
                   response != null ? response.getNumNotRunningTablets() : 0, excepMsg);
          break;
        }

        if (numIters % LOG_EVERY_NUM_ITERS == 0) {
          LOG.info("Iters={}, message={}, numNotRunning={}, excepMsg={}.",
                   numIters, response != null ? response.errorMessage() : "",
                   response != null ? response.getNumNotRunningTablets() : 0, excepMsg);
        }

        sleepFor(WAIT_EACH_ATTEMPT_MS);

        continue;
      }

      LOG.info("{} on node {} ready after iters={}, numNotRunning={}.",
               taskParams().serverType, taskParams().nodeName, numIters,
               response.getNumNotRunningTablets());
      break;
    }

    // Sleep for the remaining portion of user specified time, if any.
    sleepRemaining(userWaitTimeMs, numIters);
    ybService.closeClient(client, masterAddresses);
  }
}
