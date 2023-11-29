// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateAndPersistKubernetesOverrides;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;

public class KubernetesOverridesUpgrade extends KubernetesUpgradeTaskBase {

  @Inject
  protected KubernetesOverridesUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected KubernetesOverridesUpgradeParams taskParams() {
    return (KubernetesOverridesUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpdatingKubernetesOverrides;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          Cluster cluster = universe.getUniverseDetails().getPrimaryCluster();
          // Set overrides to primary cluster so that they will be picked up in upgrade tasks.
          cluster.userIntent.universeOverrides = taskParams().universeOverrides;
          cluster.userIntent.azOverrides = taskParams().azOverrides;

          // Create Kubernetes Upgrade Task.
          createUpgradeTask(
              getUniverse(),
              cluster.userIntent.ybSoftwareVersion,
              // We don't know which overrides can affect masters or tservers so set both to true.
              /* isMasterChanged */ true,
              /* isTServerChanged */ true,
              universe.isYbcEnabled(),
              universe.getUniverseDetails().getYbcSoftwareVersion());
          // Persist new overrides in the DB.
          addPersistKubernetesOverridesTask().setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private SubTaskGroup addPersistKubernetesOverridesTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateAndPersistKubernetesOverrides");
    UpdateAndPersistKubernetesOverrides.Params params =
        new UpdateAndPersistKubernetesOverrides.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.universeOverrides = taskParams().universeOverrides;
    params.azOverrides = taskParams().azOverrides;
    UpdateAndPersistKubernetesOverrides task =
        createTask(UpdateAndPersistKubernetesOverrides.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
