// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.Pair;

/**
 * This task will be deprecated for upgrading universe having version greater or equal to 2.20.x,
 * please ensure any new changes here should also be made on new task SoftwareUpgradeYB.
 */
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
  public void validateParams() {
    taskParams().verifyParams(getUniverse());
  }

  protected SoftwareUpgradeParams taskParams() {
    return (SoftwareUpgradeParams) taskParams;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodes(taskParams().upgradeOption);
          Set<NodeDetails> allNodes = toOrderedSet(nodes);
          Universe universe = getUniverse();

          String newVersion = taskParams().ybSoftwareVersion;

          // Preliminary checks for upgrades.
          createCheckUpgradeTask(newVersion).setSubTaskGroupType(SubTaskGroupType.PreflightChecks);

          // PreCheck for Available Memory on tserver nodes.
          long memAvailableLimit =
              confGetter.getConfForScope(universe, UniverseConfKeys.dbMemAvailableLimit);
          // No need to run the check if the minimum allowed is 0.
          if (memAvailableLimit > 0) {
            createAvailableMemoryCheck(allNodes, Util.AVAILABLE_MEMORY, memAvailableLimit)
                .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);
          }

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

          // Download software to all nodes.
          createDownloadTasks(allNodes, newVersion);
          // Install software on nodes.
          createUpgradeTaskFlowTasks(
              nodes,
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
        });
  }
}
