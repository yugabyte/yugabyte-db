// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import javax.inject.Inject;
import org.apache.commons.lang3.tuple.Pair;

public class SoftwareUpgrade extends UpgradeTaskBase {

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
  public void run() {
    runUpgrade(
        () -> {
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodes(taskParams().upgradeOption);
          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse());
          // Download software to all nodes
          createDownloadTasks(nodes.getRight());
          // Install software on nodes
          createUpgradeTaskFlow(
              (nodes1, processTypes) -> createSoftwareInstallTasks(nodes1, getSingle(processTypes)),
              taskParams().upgradeOption,
              nodes,
              DEFAULT_CONTEXT);
          // Run YSQL upgrade on the universe
          createRunYsqlUpgradeTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());
          // Mark the final software version on the universe
          createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private void createDownloadTasks(List<NodeDetails> nodes) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.DownloadingSoftware, taskParams().nodePrefix);
    SubTaskGroup downloadTaskGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      downloadTaskGroup.addTask(
          getAnsibleConfigureServerTask(node, ServerType.TSERVER, UpgradeTaskSubType.Download));
    }
    downloadTaskGroup.setSubTaskGroupType(SubTaskGroupType.DownloadingSoftware);
    subTaskGroupQueue.add(downloadTaskGroup);
  }

  private void createSoftwareInstallTasks(List<NodeDetails> nodes, ServerType processType) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }

    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.InstallingSoftware, taskParams().nodePrefix);
    SubTaskGroup taskGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      taskGroup.addTask(
          getAnsibleConfigureServerTask(node, processType, UpgradeTaskSubType.Install));
    }
    taskGroup.setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
    subTaskGroupQueue.add(taskGroup);
  }

  private AnsibleConfigureServers getAnsibleConfigureServerTask(
      NodeDetails node, ServerType processType, UpgradeTaskSubType taskSubType) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(node, processType, UpgradeTaskType.Software, taskSubType);
    params.ybSoftwareVersion = taskParams().ybSoftwareVersion;
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    return task;
  }
}
