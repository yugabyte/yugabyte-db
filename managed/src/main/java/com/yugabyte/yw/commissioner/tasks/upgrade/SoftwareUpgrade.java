// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
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
  public void run() {
    runUpgrade(
        () -> {
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodes(taskParams().upgradeOption);
          Set<NodeDetails> allNodes = toOrderedSet(nodes);
          Universe universe = getUniverse();
          // Verify the request params and fail if invalid.
          taskParams().verifyParams(universe);

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

          // Re-provisioning the nodes if ybc needs to be installed and systemd is already enabled
          // to register newly introduced ybc service if it is missing in case old universes.
          // We would skip ybc installation in case of manually provisioned systemd enabled on-prem
          // universes as we may not have sudo permissions.
          if (taskParams().installYbc
              && !isUniverseOnPremManualProvisioned
              && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd) {
            createSetupServerTasks(nodes.getRight(), param -> param.isSystemdUpgrade = true);
          }

          // Download software to all nodes.
          createDownloadTasks(allNodes, newVersion);
          // Install software on nodes.
          createUpgradeTaskFlow(
              (nodes1, processTypes) ->
                  createSoftwareInstallTasks(
                      nodes1, getSingle(processTypes), newVersion, getTaskSubGroupType()),
              nodes,
              getUpgradeContext(),
              false);

          if (taskParams().installYbc) {
            createYbcSoftwareInstallTasks(nodes.getRight(), newVersion, getTaskSubGroupType());
            // Start yb-controller process and wait for it to get responsive.
            createStartYbcProcessTasks(
                new HashSet<>(nodes.getRight()),
                universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd);
            createUpdateYbcTask(taskParams().getYbcSoftwareVersion())
                .setSubTaskGroupType(getTaskSubGroupType());
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
