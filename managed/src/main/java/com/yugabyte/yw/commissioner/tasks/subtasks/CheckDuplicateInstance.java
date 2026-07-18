// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckDuplicateInstance extends NodeTaskBase {

  @Inject
  protected CheckDuplicateInstance(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public NodeState nodeState;
  }

  public Params taskParams() {
    return (Params) taskParams;
  }

  private boolean isNoInstanceExpected() {
    return taskParams().nodeState == NodeState.ToBeAdded
        || taskParams().nodeState == NodeState.Decommissioned;
  }

  // Basic state check. No need to cover other states in cluster ops.
  private boolean isSingleInstanceExpected() {
    return taskParams().nodeState == NodeState.Live;
  }

  @Override
  public void run() {
    // Get all instances by setting ensureSingleInstance to false.
    Optional<List<Map<String, JsonNode>>> optional =
        maybeGetInstancesDetails(taskParams(), false /* ensureSingleInstance */);
    optional.ifPresent(
        l -> {
          // Multiple instances are not allowed in any node state.
          if (l.size() > 1) {
            String msg =
                String.format(
                    "Duplicate instances found for node %s in universe %s: %s",
                    taskParams().nodeName,
                    taskParams().getUniverseUUID(),
                    Util.filterInstanceDetailsForLogging(l));
            log.error(msg);
            throw new RuntimeException(msg);
          }
        });
    int instanceCount = optional.map(List::size).orElse(0);
    // 1 or 0 instances found.
    if (isSingleInstanceExpected() && instanceCount != 1) {
      String msg =
          String.format(
              "An instance is expected for node %s in universe %s, but none found",
              taskParams().nodeName, taskParams().getUniverseUUID());
      log.error(msg);
      throw new RuntimeException(msg);
    } else if (isNoInstanceExpected() && instanceCount != 0) {
      String msg =
          String.format(
              "No instance is expected for node %s(state=%s) in universe %s, but found: %s",
              taskParams().nodeName,
              taskParams().nodeState,
              taskParams().getUniverseUUID(),
              optional.get());
      log.error(msg);
      throw new RuntimeException(msg);
    }
  }
}
