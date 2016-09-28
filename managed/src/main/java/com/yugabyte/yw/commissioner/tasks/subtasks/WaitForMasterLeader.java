// Copyright (c) YugaByte, Inc.

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

public class WaitForMasterLeader extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(WaitForMasterLeader.class);

  // The YB client.
  public YBClientService ybService = null;

  // Timeout for failing to respond to pings.
  private static final long TIMEOUT_SERVER_WAIT_MS = 60000;

  public static class Params extends UniverseTaskParams {
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    this.taskParams = params;
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ")";
  }

  @Override
  public void run() {
    String hostPorts = Universe.get(taskParams().universeUUID).getMasterAddresses();
    try {
      LOG.info("Running {}: hostPorts={}.", getName(), hostPorts);
      YBClient client = ybService.getClient(hostPorts);
      client.waitForMasterLeader(TIMEOUT_SERVER_WAIT_MS);
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
  }
}
