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
import org.yb.client.YBClient;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;

import play.api.Play;

// Helper class to wait for a minimum number of tservers to heartbeat to the
// master leader. Currently the minimum is the same as the replication factor,
// so that next set of tasks like creating a table will not lack tserver resources.
public class WaitForTServerHeartBeats extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(WaitForTServerHeartBeats.class);

  // The YB client to use.
  private YBClientService ybService = null;

  // Timeout when minimum number of tservers have not heartbeatean to master leader.
  private static final long TIMEOUT_SERVER_WAIT_MS = 120000;

  // Time to wait (in millisec) during each iteration of check.
  private static final int WAIT_EACH_ATTEMPT_MS = 250;

  // Parameters for tserver heartbeat wait task.
  public static class Params extends UniverseTaskParams { }

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
    return super.getName() + "(" + taskParams().universeUUID + ")";
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodeToNode();
    String[] rpcClientCertFiles = universe.getFilesForMutualTLS();
    int numTservers = universe.getTServers().size();
    YBClient client = ybService.getClient(hostPorts, certificate, rpcClientCertFiles);

    LOG.info("Running {}: hostPorts={}, numTservers={}.", getName(), hostPorts, numTservers);
    int numTries = 1;
    long start = System.currentTimeMillis();
    boolean timedOut = false;
    do {
      try {
        int currentNumTservers = client.listTabletServers().getTabletServersCount();

        LOG.info("{} tservers heartbeating to master leader.", currentNumTservers);

        if (currentNumTservers >= numTservers) {
          break;
        }

        LOG.info("Waiting to make sure {} tservers are heartbeating to master leader; retrying " +
                 "after {}ms. Tried {} times.", numTservers, WAIT_EACH_ATTEMPT_MS, numTries);

        Thread.sleep(WAIT_EACH_ATTEMPT_MS);
        numTries++;
      } catch (Exception e) {
        LOG.warn("{}: ignoring error '{}'.", getName(), e.getMessage());
      }
      timedOut = System.currentTimeMillis() >= start + TIMEOUT_SERVER_WAIT_MS;
    } while (!timedOut);

    ybService.closeClient(client, hostPorts);

    if (timedOut) {
      throw new RuntimeException(getName() + " timed out.");
    }
  }
}
