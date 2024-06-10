// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * This task will be deprecated for upgrading universe having version greater or equal to 2.20.x,
 * please ensure any new changes here should also be made on new task SoftwareUpgradeYB.
 */
@Retryable
@Abortable
@Slf4j
public class SoftwareUpgrade extends SoftwareUpgradeTaskBase {

  private final XClusterUniverseService xClusterUniverseService;

  @Inject
  protected SoftwareUpgrade(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies);
    this.xClusterUniverseService = xClusterUniverseService;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  protected SoftwareUpgradeParams taskParams() {
    return (SoftwareUpgradeParams) taskParams;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    createPrecheckTasks(universe, taskParams().ybSoftwareVersion);
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    String newVersion = taskParams().ybSoftwareVersion;
    MastersAndTservers allNodes = fetchNodes(taskParams().upgradeOption);
    return filterOutAlreadyProcessedNodes(getUniverse(), allNodes, newVersion);
  }

  @Override
  protected String getTargetSoftwareVersion() {
    return taskParams().ybSoftwareVersion;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          MastersAndTservers nodesToApply = getNodesToBeRestarted();
          Set<NodeDetails> allNodes = toOrderedSet(fetchNodes(taskParams().upgradeOption).asPair());
          Universe universe = getUniverse();
          String newVersion = taskParams().ybSoftwareVersion;

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Upgrading);

          if (!universe
              .getUniverseDetails()
              .xClusterInfo
              .isSourceRootCertDirPathGflagConfigured()) {
            createXClusterSourceRootCertDirPathGFlagTasks();
          }

          boolean isUniverseOnPremManualProvisioned = Util.isOnPremManualProvisioning(universe);
          boolean reProvisionRequired =
              taskParams().installYbc
                  && !isUniverseOnPremManualProvisioned
                  && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;

          // Download software to nodes which does not have either master or tserver with new
          // version.
          createDownloadTasks(toOrderedSet(nodesToApply.asPair()), newVersion);

          // Install software on nodes.
          createUpgradeTaskFlowTasks(
              nodesToApply,
              newVersion,
              getUpgradeContext(taskParams().ybSoftwareVersion),
              reProvisionRequired);

          if (taskParams().installYbc) {
            createYbcInstallTask(universe, new ArrayList<>(allNodes), newVersion);
          }

          if (!confGetter.getConfForScope(universe, UniverseConfKeys.skipUpgradeFinalize)) {
            // Promote Auto flags on compatible versions.
            if (confGetter.getConfForScope(universe, UniverseConfKeys.promoteAutoFlag)
                && CommonUtils.isAutoFlagSupported(newVersion)) {
              createCheckSoftwareVersionTask(allNodes, newVersion)
                  .setSubTaskGroupType(getTaskSubGroupType());
              createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
                  Collections.singleton(universe.getUniverseUUID()),
                  Collections.singleton(universe.getUniverseUUID()),
                  xClusterUniverseService,
                  new HashSet<>(),
                  universe,
                  newVersion);
            }

            if (taskParams().upgradeSystemCatalog) {
              // Run YSQL upgrade on the universe.
              createRunYsqlUpgradeTask(newVersion).setSubTaskGroupType(getTaskSubGroupType());
            }
          }

          // Update software version in the universe metadata.
          createUpdateSoftwareVersionTask(newVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready);
        });
  }
}
