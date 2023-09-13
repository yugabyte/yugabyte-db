// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import javax.inject.Inject;
import org.apache.commons.lang3.tuple.Pair;

/**
 * Use this task to upgrade software yugabyte DB version if universe is already on version greater
 * or equal to 2.20.x
 */
public class SoftwareUpgradeYB extends SoftwareUpgradeTaskBase {

  @Inject
  protected SoftwareUpgradeYB(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public NodeState getNodeState() {
    return NodeState.UpgradeSoftware;
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
          // We would skip ybc installation in case of manually provisioned systemd enabled
          // on-prem
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

          createCheckSoftwareVersionTask(allNodes, newVersion);
          createPromoteAutoFlagTask(
              universe.getUniverseUUID(),
              true /* ignoreErrors*/,
              AutoFlagUtil.LOCAL_VOLATILE_AUTO_FLAG_CLASS_NAME /* maxClass */);

          // Update Software version
          createUpdateSoftwareVersionTask(newVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }
}
