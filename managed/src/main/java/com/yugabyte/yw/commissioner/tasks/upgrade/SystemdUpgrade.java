// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import javax.inject.Inject;
import org.apache.commons.lang3.tuple.Pair;

public class SystemdUpgrade extends UpgradeTaskBase {

  @Inject
  protected SystemdUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected SystemdUpgradeParams taskParams() {
    return (SystemdUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.SystemdUpgrade;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.SystemdUpgrade;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          // Fetch node lists
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodes(taskParams().upgradeOption);

          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse());

          // Rolling Upgrade Systemd
          createRollingUpgradeTaskFlow(this::createSystemdUpgradeTasks, nodes, false);

          // Persist useSystemd changes
          createPersistSystemdUpgradeTask(true).setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private void createSystemdUpgradeTasks(List<NodeDetails> nodes, ServerType processType) {
    if (nodes.isEmpty()) {
      return;
    }

    // Conditional Provisioning
    createSetupServerTasks(nodes, true /* isSystemdUpgrade */)
        .setSubTaskGroupType(SubTaskGroupType.Provisioning);
    // Conditional Configuring
    createConfigureServerTasks(nodes, false, false, false, true /* isSystemdUpgrade */)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }
}
