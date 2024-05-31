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

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.ChangeLoadBalancerStateResponse;
import org.yb.client.YBClient;

// This class runs the task that helps modify the load balancer state maintained
// on the master leader.
@Slf4j
public class LoadBalancerStateChange extends UniverseTaskBase {

  @Inject
  protected LoadBalancerStateChange(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for placement info update task.
  public static class Params extends UniverseTaskParams {
    // When true, the load balancer state is enabled on the server side, else it is disabled.
    public boolean enable;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().getUniverseUUID()
        + ", enable="
        + taskParams().enable
        + ")";
  }

  @Override
  public void run() {
    ChangeLoadBalancerStateResponse resp;
    YBClient client = null;
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    try {
      log.info("Running {}: masterHostPorts={}.", getName(), masterHostPorts);

      client = ybService.getClient(masterHostPorts, certificate);
      resp = client.changeLoadBalancerState(taskParams().enable);
    } catch (Exception e) {
      log.error("{} hit exception : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, masterHostPorts);
    }

    if (resp != null && resp.hasError()) {
      String errorMsg = getName() + " failed due to error: " + resp.errorMessage();
      log.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }
  }
}
