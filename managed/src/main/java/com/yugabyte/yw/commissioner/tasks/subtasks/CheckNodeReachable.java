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
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckNodeReachable extends NodeTaskBase {

  private NodeUniverseManager nodeUniverseManager;

  @Inject
  protected CheckNodeReachable(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
  }

  public static class Params extends NodeTaskParams {
    public NodeDetails node;
    public Universe universe;
    public long nodeReachableTimeout;
    public Set<NodeDetails> nodesReachable;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      log.info("Checking if node '{}' is reachable.", taskParams().node.getNodeName());
      boolean nodeReachable =
          nodeUniverseManager.isNodeReachable(
              taskParams().node, taskParams().universe, taskParams().nodeReachableTimeout);
      log.info("Node '{}' is reachable?: {}", taskParams().node.getNodeName(), nodeReachable);
      if (nodeReachable) {
        taskParams().nodesReachable.add(taskParams().node);
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
