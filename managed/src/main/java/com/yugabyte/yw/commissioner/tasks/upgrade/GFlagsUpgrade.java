// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
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
  public void run() {
    runUpgrade(
        () -> {
          UniverseDefinitionTaskParams.UserIntent userIntent = getUserIntent();

          boolean changedByMasterFlags =
              GFlagsUtil.syncGflagsToIntent(taskParams().masterGFlags, userIntent);
          boolean changedByTserverFlags =
              GFlagsUtil.syncGflagsToIntent(taskParams().tserverGFlags, userIntent);
          log.debug(
              "Intent changed by master {} by tserver {}",
              changedByMasterFlags,
              changedByTserverFlags);

          // Fetch master and tserver nodes if there is change in gflags
          List<NodeDetails> masterNodes =
              (changedByMasterFlags || changedByTserverFlags)
                      || !taskParams().masterGFlags.equals(getUserIntent().masterGFlags)
                  ? fetchMasterNodes(taskParams().upgradeOption)
                  : new ArrayList<>();

          List<NodeDetails> tServerNodes =
              (changedByMasterFlags || changedByTserverFlags)
                      || !taskParams().tserverGFlags.equals(getUserIntent().tserverGFlags)
                  ? fetchTServerNodes(taskParams().upgradeOption)
                  : new ArrayList<>();

          Universe universe = getUniverse();
          Config runtimeConfig = runtimeConfigFactory.forUniverse(universe);

          if (!config.getBoolean("yb.cloud.enabled")
              && !runtimeConfig.getBoolean("yb.gflags.allow_user_override")) {
            masterNodes.forEach(
                node ->
                    checkForbiddenToOverrideGFlags(
                        node,
                        userIntent,
                        universe,
                        ServerType.MASTER,
                        taskParams().masterGFlags,
                        config));
            tServerNodes.forEach(
                node ->
                    checkForbiddenToOverrideGFlags(
                        node,
                        userIntent,
                        universe,
                        ServerType.TSERVER,
                        taskParams().tserverGFlags,
                        config));
          }

          // Verify the request params and fail if invalid
          taskParams().verifyParams(universe);
          // Upgrade GFlags in all nodes
          createGFlagUpgradeTasks(userIntent, masterNodes, tServerNodes);
          // Update the list of parameter key/values in the universe with the new ones.
          updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private void checkForbiddenToOverrideGFlags(
      NodeDetails node,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      Universe universe,
      ServerType processType,
      Map<String, String> newGFlags,
      Config config) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            userIntent, node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None);

    String errorMsg =
        GFlagsUtil.checkForbiddenToOverride(node, params, userIntent, universe, newGFlags, config);
    if (errorMsg != null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          errorMsg
              + ". It is not advised to set these internal gflags. If you want to do it"
              + " forcefully - set runtime config value for "
              + "'yb.gflags.allow_user_override' to 'true'");
    }
  }

  private void createGFlagUpgradeTasks(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(
            (nodes, processTypes) ->
                createServerConfFileUpdateTasks(userIntent, nodes, processTypes),
            masterNodes,
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().ybcInstalled);
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(
            (nodes, processTypes) ->
                createServerConfFileUpdateTasks(userIntent, nodes, processTypes),
            masterNodes,
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().ybcInstalled);
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(
            (List<NodeDetails> nodeList, Set<ServerType> processTypes) -> {
              ServerType processType = getSingle(processTypes);
              createServerConfFileUpdateTasks(userIntent, nodeList, processTypes);
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
            DEFAULT_CONTEXT,
            taskParams().ybcInstalled);
        break;
    }
  }

  private void createServerConfFileUpdateTasks(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> nodes,
      Set<ServerType> processTypes) {
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
      subTaskGroup.addSubTask(
          getAnsibleConfigureServerTask(userIntent, node, getSingle(processTypes)));
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private AnsibleConfigureServers getAnsibleConfigureServerTask(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      NodeDetails node,
      ServerType processType) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            userIntent, node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None);
    if (processType.equals(ServerType.MASTER)) {
      params.gflags = taskParams().masterGFlags;
      params.gflagsToRemove =
          getUserIntent()
              .masterGFlags
              .keySet()
              .stream()
              .filter(flag -> !taskParams().masterGFlags.containsKey(flag))
              .collect(Collectors.toSet());
    } else {
      params.gflags = taskParams().tserverGFlags;
      params.gflagsToRemove =
          getUserIntent()
              .tserverGFlags
              .keySet()
              .stream()
              .filter(flag -> !taskParams().tserverGFlags.containsKey(flag))
              .collect(Collectors.toSet());
    }
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    return task;
  }
}
