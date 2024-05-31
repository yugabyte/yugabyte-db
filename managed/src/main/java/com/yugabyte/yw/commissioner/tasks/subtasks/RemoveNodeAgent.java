// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RemoveNodeAgent extends NodeTaskBase {

  public static class Params extends NodeTaskParams {
    // Flag to be set when deletion errors should be ignored.
    public boolean isForceDelete;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Inject
  protected RemoveNodeAgent(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails nodeDetails = universe.getNode(taskParams().nodeName);
    if (nodeDetails != null) {
      log.info("Removing node agent entry with IP {}", nodeDetails.cloudInfo.private_ip);
      try {
        deleteNodeAgent(nodeDetails);
      } catch (Exception e) {
        if (!taskParams().isForceDelete) {
          throw e;
        } else {
          log.warn(
              "Ignoring error deleting node agent {} due to isForceDelete being set.",
              taskParams().nodeName,
              e);
        }
      }
    }
  }
}
