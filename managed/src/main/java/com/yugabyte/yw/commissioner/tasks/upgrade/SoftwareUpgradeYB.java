// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
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

          createCheckSoftwareVersionTask(allNodes, newVersion);

          createStoreAutoFlagConfigVersionTask(taskParams().getUniverseUUID());

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
