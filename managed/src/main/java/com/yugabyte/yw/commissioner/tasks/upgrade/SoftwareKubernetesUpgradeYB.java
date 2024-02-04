// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import java.io.IOException;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Status;

@Slf4j
@Abortable
@Retryable
public class SoftwareKubernetesUpgradeYB extends KubernetesUpgradeTaskBase {

  private final AutoFlagUtil autoFlagUtil;

  @Inject
  protected SoftwareKubernetesUpgradeYB(
      BaseTaskDependencies baseTaskDependencies, AutoFlagUtil autoFlagUtil) {
    super(baseTaskDependencies);
    this.autoFlagUtil = autoFlagUtil;
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
          String newVersion = taskParams().ybSoftwareVersion;
          String currentVersion =
              getUniverse().getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;

          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse(), isFirstTry());
          // Preliminary checks for upgrades.
          createCheckUpgradeTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Upgrading,
              true /* isSoftwareRollbackAllowed */);

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

          boolean upgradeRequireFinalize;
          try {
            upgradeRequireFinalize =
                autoFlagUtil.upgradeRequireFinalize(currentVersion, newVersion);
          } catch (IOException e) {
            log.error("Error: ", e);
            throw new PlatformServiceException(
                Status.INTERNAL_SERVER_ERROR, "Error while checking auto-finalize for upgrade");
          }
          if (upgradeRequireFinalize) {
            createUpdateUniverseSoftwareUpgradeStateTask(
                UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize,
                true /* isSoftwareRollbackAllowed */);
          } else {
            createUpdateUniverseSoftwareUpgradeStateTask(
                UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
                true /* isSoftwareRollbackAllowed */);
          }
        });
  }
}
