// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UpgradeTaskBase.UpgradeContext;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
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
  protected RollbackKubernetesUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
  }

  @Override
  protected RollbackUpgradeParams taskParams() {
    return (RollbackUpgradeParams) taskParams;
  }

  @Override
  public UserTaskDetails.SubTaskGroupType getTaskSubGroupType() {
    return UserTaskDetails.SubTaskGroupType.RollingBackSoftware;
  }

  public NodeDetails.NodeState getNodeState() {
    return NodeDetails.NodeState.RollbackUpgrade;
  }

  @Override
  protected String getTargetSoftwareVersion() {
    Universe universe = getUniverse();
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig prevYBSoftwareConfig =
        universe.getUniverseDetails().prevYBSoftwareConfig;
    String newVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (prevYBSoftwareConfig != null
        && !newVersion.equals(prevYBSoftwareConfig.getSoftwareVersion())) {
      newVersion = prevYBSoftwareConfig.getSoftwareVersion();
    }
    return newVersion;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.RollingBack);

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

          String stableYbcVersion = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);

          // Create Kubernetes Upgrade Task
          createUpgradeTask(
              getUniverse(),
              newVersion,
              true,
              true,
              taskParams().isEnableYbc(),
              stableYbcVersion,
              getRollbackUpgradeContext(newVersion));

          // Update Software version
          createUpdateSoftwareVersionTask(newVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
              false /* isSoftwareRollbackAllowed */);
        });
  }

  private UpgradeContext getRollbackUpgradeContext(String targetSoftwareVersion) {
    return UpgradeContext.builder()
        .reconfigureMaster(false)
        .runBeforeStopping(false)
        .processInactiveMaster(true)
        .processTServersFirst(true)
        .targetSoftwareVersion(targetSoftwareVersion)
        .build();
  }
}
