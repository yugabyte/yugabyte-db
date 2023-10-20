// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.Collections;
import java.util.HashSet;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class SoftwareKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  private final XClusterUniverseService xClusterUniverseService;

  @Inject
  protected SoftwareKubernetesUpgrade(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies);
    this.xClusterUniverseService = xClusterUniverseService;
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
          if (!confGetter.getConfForScope(getUniverse(), UniverseConfKeys.skipUpgradeFinalize)) {
            // Promote Auto flags on compatible versions.
            if (confGetter.getConfForScope(getUniverse(), UniverseConfKeys.promoteAutoFlag)
                && CommonUtils.isAutoFlagSupported(taskParams().ybSoftwareVersion)) {
              createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
                  Collections.singleton(taskParams().getUniverseUUID()),
                  Collections.singleton(taskParams().getUniverseUUID()),
                  xClusterUniverseService,
                  new HashSet<>(),
                  getUniverse(),
                  taskParams().ybSoftwareVersion);
            }
            if (taskParams().upgradeSystemCatalog) {
              // Run YSQL upgrade on the universe
              createRunYsqlUpgradeTask(taskParams().ybSoftwareVersion)
                  .setSubTaskGroupType(getTaskSubGroupType());
            }
          }
          if (taskParams().isEnableYbc()) {
            createUpdateYbcTask(taskParams().getYbcSoftwareVersion())
                .setSubTaskGroupType(getTaskSubGroupType());
          }
          // Also idempotent can be run again here.
          // Mark the final software version on the universe
          createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }
}
