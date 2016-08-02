// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;
import com.yugabyte.yw.commissioner.tasks.params.UniverseTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;

import play.api.Play;

public class WaitForMasterLeader extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(WaitForMasterLeader.class);

  // The YB client.
  public YBClientService ybService = null;

  public static class Params extends UniverseTaskParams {
  }

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
    return super.getName() + "(" + taskParams().universeUUID + ")";
  }

  @Override
  public void run() {
    String hostPorts = Universe.get(taskParams().universeUUID).getMasterAddresses();
    boolean ret = false;
    try {
      LOG.info("Running {}: hostPorts={}.", getName(), hostPorts);
      YBClient client = ybService.getClient(hostPorts);
      ret = client.waitForMasterLeader(client.getDefaultAdminOperationTimeoutMs());
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
    if (!ret) {
      throw new RuntimeException("Could not get Leader Master UUID");
    }
  }
}
