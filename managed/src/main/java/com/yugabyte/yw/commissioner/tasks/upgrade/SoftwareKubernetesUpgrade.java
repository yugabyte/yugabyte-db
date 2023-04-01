// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import javax.inject.Inject;

public class SoftwareKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  @Inject
  protected SoftwareKubernetesUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected SoftwareUpgradeParams taskParams() {
    return (SoftwareUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpgradingSoftware;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse());
          // Preliminary checks for upgrades.
          createCheckUpgradeTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());
          // Create Kubernetes Upgrade Task
          createUpgradeTask(
              getUniverse(),
              taskParams().ybSoftwareVersion,
              true,
              true,
              taskParams().isEnableYbc(),
              taskParams().getYbcSoftwareVersion());
          if (taskParams().upgradeSystemCatalog) {
            // Run YSQL upgrade on the universe
            createRunYsqlUpgradeTask(taskParams().ybSoftwareVersion)
                .setSubTaskGroupType(getTaskSubGroupType());
          }
          // Promote Auto flags on compatible versions.
          if (confGetter.getConfForScope(getUniverse(), UniverseConfKeys.promoteAutoFlag)
              && CommonUtils.isAutoFlagSupported(taskParams().ybSoftwareVersion)
              && !XClusterConfig.isUniverseXClusterParticipant(taskParams().getUniverseUUID())) {
            createPromoteAutoFlagTask().setSubTaskGroupType(getTaskSubGroupType());
          }

          if (taskParams().isEnableYbc()) {
            createUpdateYbcTask(taskParams().getYbcSoftwareVersion())
                .setSubTaskGroupType(getTaskSubGroupType());
          }
          // Mark the final software version on the universe
          createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }
}
