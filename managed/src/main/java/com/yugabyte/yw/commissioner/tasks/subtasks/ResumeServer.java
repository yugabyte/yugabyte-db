/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ResumeServer extends NodeTaskBase {

  @Inject
  protected ResumeServer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    // IP of node to be resumed.
    public String nodeIP = null;
    public String capacityReservation;
  }

  @Override
  protected ResumeServer.Params taskParams() {
    return (ResumeServer.Params) taskParams;
  }

  private void resumeUniverse(final String nodeName) {
    Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (u.getNode(nodeName) == null) {
      log.error("No node in universe with name " + nodeName);
      return;
    }
    log.info("Resumed the node " + nodeName + " from universe " + taskParams().getUniverseUUID());
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails nodeDetails = universe.getNode(taskParams().nodeName);
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
    taskParams().capacityReservation =
        CapacityReservationUtil.getReservationIfPresent(
            getTaskCache(), provider, taskParams().nodeName);
    getNodeManager().nodeCommand(NodeManager.NodeCommandType.Resume, taskParams()).processErrors();
    resumeUniverse(taskParams().nodeName);
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }

  @Override
  public boolean onFailure(TaskInfo taskInfo, Throwable cause) {
    // reboot unless this is an InsufficientInstanceCapacity error from AWS
    if (cause.getMessage() != null
        && !cause.getMessage().contains("InsufficientInstanceCapacity")) {
      return super.onFailure(taskInfo, cause);
    }

    return false;
  }
}
