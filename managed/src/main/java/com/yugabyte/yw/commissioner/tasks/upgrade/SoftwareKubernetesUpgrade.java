// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.models.Universe;
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
      BaseTaskDependencies baseTaskDependencies,
      XClusterUniverseService xClusterUniverseService,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
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
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected String getTargetSoftwareVersion() {
    return taskParams().ybSoftwareVersion;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    createSoftwareUpgradePrecheckTasks(taskParams().ybSoftwareVersion);
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          String stableYbcVersion = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);

          // Create Kubernetes Upgrade Task
          createUpgradeTask(
              getUniverse(),
              taskParams().ybSoftwareVersion,
              true,
              true,
              taskParams().isEnableYbc(),
              stableYbcVersion);
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
