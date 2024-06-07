// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import javax.inject.Inject;

@Abortable
@Retryable
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
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), getNodeState(), isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    addBasicPrecheckTasks();
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return fetchNodes(taskParams().upgradeOption);
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          // Fetch node lists
          MastersAndTservers nodes = getNodesToBeRestarted();

          if (taskParams().isYbcInstalled()) {
            createServerControlTasks(nodes.tserversList, ServerType.CONTROLLER, "stop")
                .setSubTaskGroupType(getTaskSubGroupType());
          }
          // Rolling Upgrade Systemd
          createRollingUpgradeTaskFlow(
              (nodes1, processTypes) -> createSystemdUpgradeTasks(nodes1, getSingle(processTypes)),
              nodes,
              UpgradeContext.builder()
                  .reconfigureMaster(false)
                  .runBeforeStopping(false)
                  .processInactiveMaster(false)
                  .skipStartingProcesses(true)
                  .build(),
              false);

          // Persist useSystemd changes
          createPersistSystemdUpgradeTask(true).setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private void createSystemdUpgradeTasks(List<NodeDetails> nodes, ServerType processType) {
    if (nodes.isEmpty()) {
      return;
    }

    // Needed for read replica details
    taskParams().clusters = getUniverse().getUniverseDetails().clusters;

    // Conditional Provisioning
    createSetupServerTasks(nodes, p -> p.isSystemdUpgrade = true)
        .setSubTaskGroupType(SubTaskGroupType.Provisioning);

    // Conditional Configuring
    createConfigureServerTasks(nodes, params -> params.isSystemdUpgrade = true)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Start using SystemD
    createServerControlTasks(nodes, processType, "start", params -> params.useSystemd = true);
  }
}
