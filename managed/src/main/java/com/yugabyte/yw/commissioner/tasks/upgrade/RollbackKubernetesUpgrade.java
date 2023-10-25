// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.ITask.Abortable;
import static com.yugabyte.yw.commissioner.ITask.Retryable;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class RollbackKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  @Inject
  protected RollbackKubernetesUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected RollbackUpgradeParams taskParams() {
    return (RollbackUpgradeParams) taskParams;
  }

  @Override
  public UserTaskDetails.SubTaskGroupType getTaskSubGroupType() {
    return UserTaskDetails.SubTaskGroupType.UpgradingSoftware;
  }

  public NodeDetails.NodeState getNodeState() {
    return NodeDetails.NodeState.RollbackUpgrade;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();

          UniverseDefinitionTaskParams.PrevYBSoftwareConfig prevYBSoftwareConfig =
              universe.getUniverseDetails().prevYBSoftwareConfig;
          String newVersion =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
          // Skip auto flags restore incase upgrade did not take place or succeed.
          if (prevYBSoftwareConfig != null
              && !newVersion.equals(prevYBSoftwareConfig.getSoftwareVersion())) {
            newVersion = prevYBSoftwareConfig.getSoftwareVersion();
            int autoFlagConfigVersion = prevYBSoftwareConfig.getAutoFlagConfigVersion();
            // Restore old auto flag Config
            createRollbackAutoFlagTask(taskParams().getUniverseUUID(), autoFlagConfigVersion);
          }

          // Create Kubernetes Upgrade Task
          createUpgradeTask(
              getUniverse(),
              newVersion,
              true,
              true,
              taskParams().isEnableYbc(),
              taskParams().getYbcSoftwareVersion());

          // Update Software version
          createUpdateSoftwareVersionTask(newVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }
}
