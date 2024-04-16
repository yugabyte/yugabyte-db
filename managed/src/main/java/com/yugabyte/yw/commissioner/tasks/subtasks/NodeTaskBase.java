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

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.RecoverableException;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeStatus;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public abstract class NodeTaskBase extends UniverseDefinitionTaskBase {
  @Inject
  protected NodeTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public NodeManager getNodeManager() {
    return nodeManager;
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public String getName() {
    NodeTaskParams taskParams = taskParams();
    return super.getName() + "(" + taskParams.getUniverseUUID() + ", " + taskParams.nodeName + ")";
  }

  @Override
  public JsonNode getTaskParams() {
    return Json.toJson(taskParams);
  }

  // Helper API to update the db for the current node with the given state.
  public void setNodeState(NodeDetails.NodeState state) {
    saveNodeStatus(taskParams().nodeName, NodeStatus.builder().nodeState(state).build());
  }

  public void setNodeStatus(NodeStatus nodeStatus) {
    saveNodeStatus(taskParams().nodeName, nodeStatus);
  }

  @Override
  public boolean onFailure(TaskInfo taskInfo, Throwable cause) {
    if (cause instanceof RecoverableException) {
      NodeTaskParams params = taskParams();
      // Universe UUID and node name are always set.
      Universe universe = Universe.getOrBadRequest(params.getUniverseUUID());
      CloudType cloudType = universe.getNodeDeploymentMode(universe.getNode(params.nodeName));
      if (!cloudType.isPublicCloud()) {
        log.warn("Skipping reboot of {} for non-public cloud {}", params.nodeName, cloudType);
        return false;
      }
      log.warn("Encountered a recoverable error, hard rebooting node {}", params.nodeName);
      NodeTaskParams rebootParams = new NodeTaskParams();
      rebootParams.nodeName = params.nodeName;
      rebootParams.setUniverseUUID(params.getUniverseUUID());
      rebootParams.azUuid = params.azUuid;

      HardRebootServer task = createTask(HardRebootServer.class);
      task.initialize(rebootParams);
      task.setUserTaskUUID(getUserTaskUUID());
      task.run();
      return true;
    }
    return false;
  }
}
