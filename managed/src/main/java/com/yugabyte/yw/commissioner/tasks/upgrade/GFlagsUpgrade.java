// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
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

          if (!config.getBoolean("yb.cloud.enabled")
              && !confGetter.getConfForScope(universe, UniverseConfKeys.gflagsAllowUserOverride)) {
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

  private void createGFlagUpgradeTasks(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(
            (nodes, processTypes) ->
                createServerConfFileUpdateTasks(
                    userIntent,
                    nodes,
                    processTypes,
                    taskParams().masterGFlags,
                    taskParams().tserverGFlags),
            masterNodes,
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().ybcInstalled);
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(
            (nodes, processTypes) ->
                createServerConfFileUpdateTasks(
                    userIntent,
                    nodes,
                    processTypes,
                    taskParams().masterGFlags,
                    taskParams().tserverGFlags),
            masterNodes,
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().ybcInstalled);
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(
            (List<NodeDetails> nodeList, Set<ServerType> processTypes) -> {
              ServerType processType = getSingle(processTypes);
              createServerConfFileUpdateTasks(
                  userIntent,
                  nodeList,
                  processTypes,
                  taskParams().masterGFlags,
                  taskParams().tserverGFlags);
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
}
