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
import com.yugabyte.yw.models.helpers.NodeDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SetNodeState extends NodeTaskBase {

  @Inject
  protected SetNodeState(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public NodeDetails.NodeState state;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String toString() {
    return super.getName()
        + "("
        + taskParams().nodeName
        + ", "
        + taskParams().state.toString()
        + ")";
  }

  @Override
  public void run() {
    try {
      log.info(
          "Updating node {} state to {} in universe {}.",
          taskParams().nodeName,
          taskParams().state,
          taskParams().getUniverseUUID());
      setNodeState(taskParams().state);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
