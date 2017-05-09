// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import java.util.ArrayList;
import java.util.List;
import java.util.Collection;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common.HostPortPB;
import org.yb.client.ChangeLoadBalancerStateResponse;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.api.Play;

// This class runs the task that helps modify the load balancer state maintained
// on the master leader.
public class LoadBalancerStateChange extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(LoadBalancerStateChange.class);

  // The YB client.
  public YBClientService ybService = null;

  // Parameters for placement info update task.
  public static class Params extends UniverseTaskParams {
    // When true, the load balancer state is enabled on the server side, else it is disabled.
    public boolean enable;
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
    return super.getName() + "(" + taskParams().universeUUID + ", enable=" +
        taskParams().enable + ")";
  }

  @Override
  public void run() {
    ChangeLoadBalancerStateResponse resp = null;
    try {
      Universe universe = Universe.get(taskParams().universeUUID);
      String masterHostPorts = universe.getMasterAddresses();
      LOG.info("Running {}: masterHostPorts={}.", getName(), masterHostPorts);
      resp = ybService.getClient(masterHostPorts).changeLoadBalancerState(taskParams().enable);
    } catch (Exception e) {
      LOG.error("{} hit exception : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    if (resp != null && resp.hasError()) {
      String errorMsg = getName() + " failed due to error: " + resp.errorMessage();
      LOG.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }
  }
}
