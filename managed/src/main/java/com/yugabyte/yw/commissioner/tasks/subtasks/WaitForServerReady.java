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

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.IsServerReadyResponse;
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

    Universe universe = Universe.get(taskParams().universeUUID);
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificate();
    LOG.info("Running {} on masterAddress = {}.", getName(), masterAddresses);

    if (masterAddresses == null || masterAddresses.isEmpty()) {
      throw new IllegalArgumentException("Invalid master addresses " + masterAddresses + " for " +
          taskParams().universeUUID);
    }

    client = ybService.getClient(masterAddresses, certificate);
    NodeDetails node = universe.getNode(taskParams().nodeName);

    if (node == null) {
      throw new IllegalArgumentException("Node " + taskParams().nodeName + " not found in " +
                                         "universe " + taskParams().universeUUID);
    }

    if (taskParams().serverType != ServerType.TSERVER &&
        taskParams().serverType != ServerType.MASTER) {
      throw new IllegalArgumentException("Unexpected server type " + taskParams().serverType +
          " for universe " + taskParams().universeUUID);
    }

    boolean isTserverTask = taskParams().serverType == ServerType.TSERVER;
    if (isTserverTask && !node.isTserver) {
      throw new IllegalArgumentException("Task server type " + taskParams().serverType + " is " +
                                         "not for a node running tserver : " + node.toString());
    }

    if (!isTserverTask && !node.isMaster) {
      throw new IllegalArgumentException("Task server type " + taskParams().serverType + " is " +
                                         "not for a node running master : " + node.toString());
    }

    HostAndPort hp = HostAndPort.fromParts(node.cloudInfo.private_ip,
        isTserverTask ? node.tserverRpcPort : node.masterRpcPort);

    IsServerReadyResponse response = null;
    try {
      while (true) {
        numIters++;
        response = client.isServerReady(hp, isTserverTask);

        if (response.hasError()) {
          LOG.info("Response has error {} after iters={}.",
                   response.errorMessage(), numIters);
          break;
        }

        if (response.getNumNotRunningTablets() == 0) {
          LOG.info("{} on node {} ready after iters={}.",
                   taskParams().serverType, taskParams().nodeName, numIters);
          break;
        }

        if (numIters > (MAX_TOTAL_WAIT_MS / WAIT_EACH_ATTEMPT_MS)) {
          LOG.info("Timing out after iters={}. {} tablets not running, out of {}.",
                   numIters, response.getNumNotRunningTablets(), response.getTotalTablets());
          break;
        }

        if (numIters % LOG_EVERY_NUM_ITERS == 0) {
          LOG.info("{} on node {} not ready after iters={}, {} tablets not running out of {}.",
                   taskParams().serverType, taskParams().nodeName, numIters,
                   response.getNumNotRunningTablets(), response.getTotalTablets());
        }

        sleepFor(WAIT_EACH_ATTEMPT_MS);
      }
    } catch (Exception e) {
      // There is no generic mechanism from proto/rpc to check if an older server does not have
      // this rpc implemented. So, we just sleep for remaining time on any such error.
      LOG.info("{} hit exception '{}' after {} iters.", getName(), e.getMessage(), numIters);
    }

    // Sleep for the remaining portion of user specified time, if any.
    sleepRemaining(userWaitTimeMs, numIters);
    ybService.closeClient(client, masterAddresses);
  }
}
