// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;

import org.yb.client.shaded.com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;
import com.yugabyte.yw.commissioner.tasks.params.UniverseTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.api.Play;

public class WaitForServer extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(WaitForServer.class);

  // The YB client.
  public YBClientService ybService = null;

  // Timeout for failing to respond to pings.
  private static final long TIMEOUT_SERVER_WAIT_MS = 60000;

  public static class Params extends UniverseTaskParams {
    public String nodeName;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    this.taskParams = (Params)params;
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ", " + taskParams().nodeName + ")";
  }

  @Override
  public void run() {
    boolean ret = false;
    String hostPorts = Universe.get(taskParams().universeUUID).getMasterAddresses();
    try {
      LOG.info("Running {}: hostPorts={}.", getName(), hostPorts);
      YBClient client = ybService.getClient(hostPorts);
      NodeDetails node = Universe.get(taskParams().universeUUID).getNode(taskParams().nodeName);
      HostAndPort hp = HostAndPort.fromParts(
          node.cloudInfo.private_ip, node.isMaster ? node.masterRpcPort : node.tserverRpcPort);
      ret = client.waitForServer(hp, TIMEOUT_SERVER_WAIT_MS);
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
    if (!ret) {
      throw new RuntimeException("Server did not respond to pings in the set time.");
    }
  }
}
