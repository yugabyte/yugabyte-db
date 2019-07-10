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

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.api.Play;

public class WaitForServer extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(WaitForServer.class);

  // The YB client.
  public YBClientService ybService = null;

  // Timeout for failing to respond to pings.
  private static final long TIMEOUT_SERVER_WAIT_MS = 120000;

  public static class Params extends UniverseTaskParams {
    // The name of the node which contains the server process.
    public String nodeName;

    // Server type running on the above node for which we will wait. 
    public ServerType serverType;

    // Timeout for the RPC call.
    public long serverWaitTimeoutMs = TIMEOUT_SERVER_WAIT_MS;
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

  @Override
  public void run() {
    boolean ret = false;
    if (taskParams().serverType != ServerType.MASTER && taskParams().serverType != ServerType.TSERVER) {
      throw new IllegalArgumentException("Unexpected server type " + taskParams().serverType);
    }
    String hostPorts = Universe.get(taskParams().universeUUID).getMasterAddresses();
    YBClient client = null;
    try {
      LOG.info("Running {}: hostPorts={}.", getName(), hostPorts);
      NodeDetails node = Universe.get(taskParams().universeUUID).getNode(taskParams().nodeName);

      if (taskParams().serverType == ServerType.MASTER && !node.isMaster) {
        throw new IllegalArgumentException("Task server type " + taskParams().serverType + " is not for a " +
                                           "node running master : " + node.toString());
      }

      if (taskParams().serverType == ServerType.TSERVER && !node.isTserver) {
        throw new IllegalArgumentException("Task server type " + taskParams().serverType + " is not for a " +
                                           "node running tserver : " + node.toString());
      }

      HostAndPort hp = HostAndPort.fromParts(
          node.cloudInfo.private_ip,
          taskParams().serverType == ServerType.MASTER ? node.masterRpcPort : node.tserverRpcPort);
      client = ybService.getClient(hostPorts);
      ret = client.waitForServer(hp, taskParams().serverWaitTimeoutMs);
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
    if (!ret) {
      throw new RuntimeException(getName() + " did not respond to pings in the set time.");
    }
  }
}
