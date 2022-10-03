// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import javax.inject.Inject;

public class GFlagsKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  @Inject
  protected GFlagsKubernetesUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected GFlagsUpgradeParams taskParams() {
    return (GFlagsUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpdatingGFlags;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Cluster cluster = getUniverse().getUniverseDetails().getPrimaryCluster();
          UserIntent userIntent = cluster.userIntent;
          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse());
          // Update the list of parameter key/values in the universe with the new ones.
          updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
              .setSubTaskGroupType(getTaskSubGroupType());
          // Create Kubernetes Upgrade Task
          createUpgradeTask(
              getUniverse(),
              userIntent.ybSoftwareVersion,
              !taskParams().masterGFlags.equals(userIntent.masterGFlags),
              !taskParams().tserverGFlags.equals(userIntent.tserverGFlags));
        });
  }
}
