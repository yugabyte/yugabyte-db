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
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.RecoverableException;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeStatus;

import javax.inject.Inject;

import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public abstract class NodeTaskBase extends UniverseDefinitionTaskBase {
  private final NodeManager nodeManager;

  @Inject
  protected NodeTaskBase(BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies);
    this.nodeManager = nodeManager;
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
    return super.getName() + "(" + taskParams.universeUUID + ", " + taskParams.nodeName + ")";
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  // Helper API to update the db for the current node with the given state.
  public void setNodeState(NodeDetails.NodeState state) {
    // Persist the desired node information into the DB.
    UniverseUpdater updater =
        nodeStateUpdater(
            taskParams().universeUUID,
            taskParams().nodeName,
            NodeStatus.builder().nodeState(state).build());
    saveUniverseDetails(updater);
  }

  public void setNodeStatus(NodeStatus nodeStatus) {
    UniverseUpdater updater =
        nodeStateUpdater(taskParams().universeUUID, taskParams().nodeName, nodeStatus);
    saveUniverseDetails(updater);
  }

  @Override
  public void onFailure(TaskInfo taskInfo, Throwable cause) {
    if (cause instanceof RecoverableException) {
      NodeTaskParams params = taskParams();

      log.warn("Encountered a recoverable error, rebooting node {}", params.nodeName);

      RebootServer.Params rebootParams = new RebootServer.Params();
      rebootParams.nodeName = params.nodeName;
      rebootParams.universeUUID = params.universeUUID;
      rebootParams.azUuid = params.azUuid;
      rebootParams.useSSH = false;

      RebootServer task = createTask(RebootServer.class);
      task.initialize(rebootParams);
      task.setUserTaskUUID(userTaskUUID);
      task.run();
    }
  }
}
