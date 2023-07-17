// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.operator.KubernetesOperatorStatusUpdater;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import javax.inject.Inject;

public class GFlagsKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  private final GFlagsValidation gFlagsValidation;
  private final XClusterUniverseService xClusterUniverseService;

  @Inject
  protected GFlagsKubernetesUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      GFlagsValidation gFlagsValidation,
      XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies);
    this.gFlagsValidation = gFlagsValidation;
    this.xClusterUniverseService = xClusterUniverseService;
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
          Throwable th = null;
          // TODO: support specific gflags
          Cluster cluster = getUniverse().getUniverseDetails().getPrimaryCluster();
          UserIntent userIntent = cluster.userIntent;
          Universe universe = getUniverse();
          KubernetesOperatorStatusUpdater.createYBUniverseEventStatus(
              universe, getName(), getUserTaskUUID());
          // Verify the request params and fail if invalid
          taskParams().verifyParams(universe);
          if (CommonUtils.isAutoFlagSupported(cluster.userIntent.ybSoftwareVersion)) {
            // Verify auto flags compatibility.
            taskParams()
                .checkXClusterAutoFlags(universe, gFlagsValidation, xClusterUniverseService);
          }
          // Update the list of parameter key/values in the universe with the new ones.
          updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
              .setSubTaskGroupType(getTaskSubGroupType());
          // Create Kubernetes Upgrade Task
          try {
            switch (taskParams().upgradeOption) {
              case ROLLING_UPGRADE:
                createUpgradeTask(
                    getUniverse(),
                    userIntent.ybSoftwareVersion,
                    !taskParams().masterGFlags.equals(userIntent.masterGFlags),
                    !taskParams().tserverGFlags.equals(userIntent.tserverGFlags),
                    universe.isYbcEnabled(),
                    universe.getUniverseDetails().getYbcSoftwareVersion());
                break;
              case NON_ROLLING_UPGRADE:
                createNonRollingGflagUpgradeTask(
                    getUniverse(),
                    userIntent.ybSoftwareVersion,
                    !taskParams().masterGFlags.equals(userIntent.masterGFlags),
                    !taskParams().tserverGFlags.equals(userIntent.tserverGFlags),
                    universe.isYbcEnabled(),
                    universe.getUniverseDetails().getYbcSoftwareVersion());
                break;
              case NON_RESTART_UPGRADE:
                throw new RuntimeException("Non-restart unimplemented for K8s");
            }
          } catch (Throwable t) {
            th = t;
            throw t;
          } finally {
            KubernetesOperatorStatusUpdater.updateYBUniverseStatus(
                universe, getName(), getUserTaskUUID(), th);
          }
        });
  }
}
