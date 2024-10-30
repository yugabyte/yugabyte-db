// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstallThirdPartySoftwareK8s;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
@Abortable
public class GFlagsKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  private final GFlagsValidation gFlagsValidation;
  private final XClusterUniverseService xClusterUniverseService;

  @Inject
  protected GFlagsKubernetesUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      GFlagsValidation gFlagsValidation,
      XClusterUniverseService xClusterUniverseService,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory,
      KubernetesManagerFactory kubernetesManagerFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory, kubernetesManagerFactory);
    this.gFlagsValidation = gFlagsValidation;
    this.xClusterUniverseService = xClusterUniverseService;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    if (isFirstTry) {
      // Verify the task params.
      validateGflagsTaskParams();
    }
  }

  @Override
  protected KubernetesGFlagsUpgradeParams taskParams() {
    return (KubernetesGFlagsUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpdatingGFlags;
  }

  public SpecificGFlags getPrimaryClusterSpecificGFlags() {
    for (Cluster incomingCluster : taskParams().clusters) {
      if (incomingCluster.clusterType == ClusterType.PRIMARY) {
        UserIntent incomingUserIntent = incomingCluster.userIntent;
        return incomingUserIntent.specificGFlags;
      }
    }
    return null;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    String softwareVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (CommonUtils.isAutoFlagSupported(softwareVersion)) {
      // Verify auto flags compatibility.
      taskParams().checkXClusterAutoFlags(universe, gFlagsValidation, xClusterUniverseService);
    }
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Cluster cluster = getUniverse().getUniverseDetails().getPrimaryCluster();
          UserIntent userIntent = cluster.userIntent;
          Universe universe = getUniverse();
          // Verify the request params and fail if invalid only if its the first time we are
          // invoked.
          if (isFirstTry()) {
            taskParams().verifyParams(universe, isFirstTry());
          }
          if (CommonUtils.isAutoFlagSupported(cluster.userIntent.ybSoftwareVersion)) {
            // Verify auto flags compatibility.
            taskParams()
                .checkXClusterAutoFlags(universe, gFlagsValidation, xClusterUniverseService);
          }

          // Always update both master and tserver,
          // Helm update will finish without any restarts if there are no updates
          boolean updateMaster = true;
          boolean updateTserver = true;

          switch (taskParams().upgradeOption) {
            case ROLLING_UPGRADE:
              createUpgradeTask(
                  getUniverse(),
                  userIntent.ybSoftwareVersion,
                  updateMaster,
                  updateTserver,
                  universe.isYbcEnabled(),
                  universe.getUniverseDetails().getYbcSoftwareVersion());
              break;
            case NON_ROLLING_UPGRADE:
              createNonRollingGflagUpgradeTask(
                  getUniverse(),
                  userIntent.ybSoftwareVersion,
                  updateMaster,
                  updateTserver,
                  universe.isYbcEnabled(),
                  universe.getUniverseDetails().getYbcSoftwareVersion());
              break;
            case NON_RESTART_UPGRADE:
              createNonRestartGflagsUpgradeTask(getUniverse());
              break;
            default:
              throw new RuntimeException("Invalid Upgrade type!");
          }
          installThirdPartyPackagesTaskK8s(
              universe, InstallThirdPartySoftwareK8s.SoftwareUpgradeType.JWT_JWKS);
          // task to persist changed GFlags to universe in DB
          updateGFlagsPersistTasks(
                  cluster,
                  taskParams().masterGFlags,
                  taskParams().tserverGFlags,
                  getPrimaryClusterSpecificGFlags())
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private void validateGflagsTaskParams() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (taskParams().upgradeOption == UpgradeOption.NON_RESTART_UPGRADE
        && !KubernetesUtil.isNonRestartGflagsUpgradeSupported(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion)) {
      throw new RuntimeException("Universe does not support Non-restart gflags upgrade");
    }
  }
}
