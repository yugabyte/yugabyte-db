// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class SoftwareKubernetesUpgradeYB extends KubernetesUpgradeTaskBase {

  @Inject
  protected SoftwareKubernetesUpgradeYB(BaseTaskDependencies baseTaskDependencies) {
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
          taskParams().verifyParams(getUniverse(), isFirstTry());
          // Preliminary checks for upgrades.
          createCheckUpgradeTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Upgrading);

          // Create Kubernetes Upgrade Task
          createUpgradeTask(
              getUniverse(),
              taskParams().ybSoftwareVersion,
              true,
              true,
              taskParams().isEnableYbc(),
              taskParams().getYbcSoftwareVersion());

          createStoreAutoFlagConfigVersionTask(taskParams().getUniverseUUID());

          createPromoteAutoFlagTask(
              taskParams().getUniverseUUID(),
              true /* ignoreErrors*/,
              AutoFlagUtil.LOCAL_VOLATILE_AUTO_FLAG_CLASS_NAME /* maxClass */);

          if (taskParams().isEnableYbc()) {
            createUpdateYbcTask(taskParams().getYbcSoftwareVersion())
                .setSubTaskGroupType(getTaskSubGroupType());
          }
          // Also idempotent can be run again here.
          // Mark the final software version on the universe
          createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize);
        });
  }
}
