// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteRootVolumes extends NodeTaskBase {

  @Inject
  protected DeleteRootVolumes(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    // Flag to be set if errors will be ignored.
    public boolean isForceDelete;
    // Specific volume IDs to be deleted.
    public Set<String> volumeIds;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    UserIntent userIntent =
        u.getUniverseDetails()
            .getClusterByUuid(u.getNode(taskParams().nodeName).placementUuid)
            .userIntent;

    if (!userIntent.providerType.equals(Common.CloudType.onprem)) {
      try {
        getNodeManager()
            .nodeCommand(NodeManager.NodeCommandType.Delete_Root_Volumes, taskParams())
            .processErrors();
      } catch (Exception e) {
        if (!taskParams().isForceDelete) {
          throw e;
        } else {
          log.debug(
              "Ignoring error deleting volumes for {} due to isForceDelete being set.",
              taskParams().nodeName,
              e);
        }
      }
    }
  }
}
