// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import javax.inject.Inject;
import org.apache.commons.lang3.tuple.Pair;

public class CertsRotate extends UpgradeTaskBase {

  @Inject
  protected CertsRotate(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected CertsRotateParams taskParams() {
    return (CertsRotateParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.RotatingCert;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.UpdateCert;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodes(taskParams().upgradeOption);
          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse());
          // Update certs in all nodes
          createCertUpdateTasks(nodes.getRight());
          // Restart all nodes
          createRestartTasks(nodes, taskParams().upgradeOption);
          // Update certificate details in universe details
          createUnivSetCertTask(taskParams().certUUID).setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private void createCertUpdateTasks(List<NodeDetails> nodes) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s", getTaskSubGroupType(), taskParams().nodePrefix);
    SubTaskGroup rotateCertGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      AnsibleConfigureServers.Params params =
          getAnsibleConfigureServerParams(
              node,
              ServerType.TSERVER,
              UpgradeTaskParams.UpgradeTaskType.Certs,
              UpgradeTaskParams.UpgradeTaskSubType.None);
      params.rootCA = taskParams().certUUID;
      AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      rotateCertGroup.addTask(task);
    }
    rotateCertGroup.setSubTaskGroupType(SubTaskGroupType.RotatingCert);
    subTaskGroupQueue.add(rotateCertGroup);
  }
}
