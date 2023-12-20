// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;

public class GFlagsUpgrade extends UpgradeTaskBase {

  @Inject
  protected GFlagsUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected GFlagsUpgradeParams taskParams() {
    return (GFlagsUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpdatingGFlags;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.UpdateGFlags;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    taskParams().verifyParams(getUniverse());
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          // Fetch master and tserver nodes if there is change in gflags
          List<NodeDetails> masterNodes =
              !taskParams().masterGFlags.equals(getUserIntent().masterGFlags)
                  ? fetchMasterNodes(taskParams().upgradeOption)
                  : new ArrayList<>();
          List<NodeDetails> tServerNodes =
              !taskParams().tserverGFlags.equals(getUserIntent().tserverGFlags)
                  ? fetchTServerNodes(taskParams().upgradeOption)
                  : new ArrayList<>();
          // Upgrade GFlags in all nodes
          createGFlagUpgradeTasks(masterNodes, tServerNodes);
          // Update the list of parameter key/values in the universe with the new ones.
          updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private void createGFlagUpgradeTasks(
      List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(
            this::createServerConfFileUpdateTasks, masterNodes, tServerNodes, RUN_BEFORE_STOPPING);
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(
            this::createServerConfFileUpdateTasks, masterNodes, tServerNodes, RUN_BEFORE_STOPPING);
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(
            (List<NodeDetails> nodeList, Set<ServerType> processTypes) -> {
              ServerType processType = getSingle(processTypes);
              createServerConfFileUpdateTasks(nodeList, processTypes);
              createSetFlagInMemoryTasks(
                      nodeList,
                      processType,
                      true,
                      processType == ServerType.MASTER
                          ? taskParams().masterGFlags
                          : taskParams().tserverGFlags,
                      false)
                  .setSubTaskGroupType(getTaskSubGroupType());
            },
            masterNodes,
            tServerNodes,
            DEFAULT_CONTEXT);
        break;
    }
  }

  private void createServerConfFileUpdateTasks(
      List<NodeDetails> nodes, Set<ServerType> processTypes) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }

    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.UpdatingGFlags, taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(getAnsibleConfigureServerTask(node, getSingle(processTypes)));
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private AnsibleConfigureServers getAnsibleConfigureServerTask(
      NodeDetails node, ServerType processType) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None);
    if (processType.equals(ServerType.MASTER)) {
      params.gflags = taskParams().masterGFlags;
      params.gflagsToRemove =
          Util.getDeletedGFlags(getUserIntent().masterGFlags, taskParams().masterGFlags);
    } else {
      params.gflags = taskParams().tserverGFlags;
      params.gflagsToRemove =
          Util.getDeletedGFlags(getUserIntent().tserverGFlags, taskParams().tserverGFlags);
    }
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    return task;
  }
}
