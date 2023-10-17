// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import javax.inject.Inject;
import org.apache.commons.lang3.tuple.Pair;

public class SoftwareUpgrade extends UpgradeTaskBase {

  private static final UpgradeContext SOFTWARE_UPGRADE_CONTEXT =
      UpgradeContext.builder()
          .reconfigureMaster(false)
          .runBeforeStopping(false)
          .processInactiveMaster(true)
          .build();

  @Inject
  protected SoftwareUpgrade(BaseTaskDependencies baseTaskDependencies) {
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
  public NodeState getNodeState() {
    return NodeState.UpgradeSoftware;
  }

  @Override
  public void validateParams() {
    taskParams().verifyParams(getUniverse());
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodes(taskParams().upgradeOption);
          Universe universe = getUniverse();
          // Precheck for Available Memory on tserver nodes.
          long memAvailableLimit =
              runtimeConfigFactory
                  .forUniverse(universe)
                  .getLong("yb.dbmem.checks.mem_available_limit_kb");
          createAvailabeMemoryCheck(nodes.getRight(), Util.AVAILABLE_MEMORY, memAvailableLimit)
              .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);

          String newVersion = taskParams().ybSoftwareVersion;

          // Download software to all nodes.
          createDownloadTasks(nodes.getRight(), newVersion);
          // Install software on nodes.
          createUpgradeTaskFlow(
              (nodes1, processTypes) ->
                  createSoftwareInstallTasks(
                      nodes1, getSingle(processTypes), newVersion, getTaskSubGroupType()),
              nodes,
              SOFTWARE_UPGRADE_CONTEXT);
          // Run YSQL upgrade on the universe.
          createRunYsqlUpgradeTask(newVersion).setSubTaskGroupType(getTaskSubGroupType());
          // Update software version in the universe metadata.
          createUpdateSoftwareVersionTask(newVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private void createDownloadTasks(List<NodeDetails> nodes, String softwareVersion) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.DownloadingSoftware, taskParams().nodePrefix);

    SubTaskGroup downloadTaskGroup =
        getTaskExecutor().createSubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      downloadTaskGroup.addSubTask(
          getAnsibleConfigureServerTask(
              node, ServerType.TSERVER, UpgradeTaskSubType.Download, softwareVersion));
    }
    downloadTaskGroup.setSubTaskGroupType(SubTaskGroupType.DownloadingSoftware);
    getRunnableTask().addSubTaskGroup(downloadTaskGroup);
  }
}
