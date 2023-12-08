// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class GFlagsUpgrade extends UpgradeTaskBase {

  private final GFlagsValidation gFlagsValidation;
  private final XClusterUniverseService xClusterUniverseService;

  @Inject
  protected GFlagsUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      GFlagsValidation gFlagsValidation,
      XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies);
    this.gFlagsValidation = gFlagsValidation;
    this.xClusterUniverseService = xClusterUniverseService;
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
  public void validateParams() {
    taskParams().verifyParams(getUniverse());
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          String softwareVersion =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
          if (CommonUtils.isAutoFlagSupported(softwareVersion)) {
            // Verify auto flags compatibility.
            taskParams()
                .checkXClusterAutoFlags(universe, gFlagsValidation, xClusterUniverseService);
          }
          boolean checkForbiddenToOverride =
              !config.getBoolean("yb.cloud.enabled")
                  && !confGetter.getConfForScope(
                      universe, UniverseConfKeys.gflagsAllowUserOverride);
          List<UniverseDefinitionTaskParams.Cluster> curClusters =
              universe.getUniverseDetails().clusters;
          Map<UUID, UniverseDefinitionTaskParams.Cluster> newClusters =
              taskParams().getNewVersionsOfClusters(universe);
          for (UniverseDefinitionTaskParams.Cluster curCluster : curClusters) {
            UniverseDefinitionTaskParams.UserIntent userIntent = curCluster.userIntent;
            UniverseDefinitionTaskParams.Cluster newCluster = newClusters.get(curCluster.uuid);
            Map<String, String> masterGflags =
                GFlagsUtil.getBaseGFlags(ServerType.MASTER, newCluster, newClusters.values());
            Map<String, String> tserverGflags =
                GFlagsUtil.getBaseGFlags(ServerType.TSERVER, newCluster, newClusters.values());
            log.debug(
                "Cluster {} master: new flags {} old flags {}",
                curCluster.clusterType,
                masterGflags,
                GFlagsUtil.getBaseGFlags(ServerType.MASTER, curCluster, curClusters));
            log.debug(
                "Cluster {} tserver: new flags {} old flags {}",
                curCluster.clusterType,
                tserverGflags,
                GFlagsUtil.getBaseGFlags(ServerType.TSERVER, curCluster, curClusters));

            boolean changedByMasterFlags =
                curCluster.clusterType == UniverseDefinitionTaskParams.ClusterType.PRIMARY
                    && GFlagsUtil.syncGflagsToIntent(masterGflags, userIntent);
            boolean changedByTserverFlags =
                curCluster.clusterType == UniverseDefinitionTaskParams.ClusterType.PRIMARY
                    && GFlagsUtil.syncGflagsToIntent(tserverGflags, userIntent);
            log.debug(
                "Intent changed by master {} by tserver {}",
                changedByMasterFlags,
                changedByTserverFlags);

            // Fetch master and tserver nodes if there is change in gflags
            List<NodeDetails> masterNodes = fetchMasterNodes(taskParams().upgradeOption);
            List<NodeDetails> tServerNodes = fetchTServerNodes(taskParams().upgradeOption);
            boolean applyToAllNodes = changedByMasterFlags || changedByTserverFlags;
            masterNodes =
                masterNodes.stream()
                    .filter(n -> n.placementUuid.equals(curCluster.uuid))
                    .filter(
                        n ->
                            applyToAllNodes
                                || GFlagsUpgradeParams.nodeHasGflagsChanges(
                                    n,
                                    ServerType.MASTER,
                                    curCluster,
                                    curClusters,
                                    newCluster,
                                    newClusters.values()))
                    .collect(Collectors.toList());
            tServerNodes =
                tServerNodes.stream()
                    .filter(n -> n.placementUuid.equals(curCluster.uuid))
                    .filter(
                        n ->
                            applyToAllNodes
                                || GFlagsUpgradeParams.nodeHasGflagsChanges(
                                    n,
                                    ServerType.TSERVER,
                                    curCluster,
                                    curClusters,
                                    newCluster,
                                    newClusters.values()))
                    .collect(Collectors.toList());

            if (checkForbiddenToOverride) {
              masterNodes.forEach(
                  node ->
                      checkForbiddenToOverrideGFlags(
                          node,
                          userIntent,
                          universe,
                          ServerType.MASTER,
                          GFlagsUtil.getGFlagsForNode(
                              node, ServerType.MASTER, newCluster, newClusters.values())));
              tServerNodes.forEach(
                  node ->
                      checkForbiddenToOverrideGFlags(
                          node,
                          userIntent,
                          universe,
                          ServerType.TSERVER,
                          GFlagsUtil.getGFlagsForNode(
                              node, ServerType.TSERVER, newCluster, newClusters.values())));
            }
            // Upgrade GFlags in all nodes
            createGFlagUpgradeTasks(
                userIntent,
                masterNodes,
                tServerNodes,
                curCluster,
                curClusters,
                newCluster,
                newClusters.values());
            // Update the list of parameter key/values in the universe with the new ones.
            if (hasUpdatedGFlags(newCluster.userIntent, curCluster.userIntent)) {
              updateGFlagsPersistTasks(
                      curCluster,
                      newCluster.userIntent.masterGFlags,
                      newCluster.userIntent.tserverGFlags,
                      newCluster.userIntent.specificGFlags)
                  .setSubTaskGroupType(getTaskSubGroupType());
            }
          }
        });
  }

  private boolean hasUpdatedGFlags(
      UniverseDefinitionTaskParams.UserIntent newIntent,
      UniverseDefinitionTaskParams.UserIntent curIntent) {
    return !Objects.equals(newIntent.specificGFlags, curIntent.specificGFlags)
        || !Objects.equals(newIntent.masterGFlags, curIntent.masterGFlags)
        || !Objects.equals(newIntent.tserverGFlags, curIntent.tserverGFlags);
  }

  private void createGFlagUpgradeTasks(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UniverseDefinitionTaskParams.Cluster curCluster,
      List<UniverseDefinitionTaskParams.Cluster> curClusters,
      UniverseDefinitionTaskParams.Cluster newCluster,
      Collection<UniverseDefinitionTaskParams.Cluster> newClusters) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(
            (nodes, processTypes) ->
                createServerConfFileUpdateTasks(
                    userIntent,
                    nodes,
                    processTypes,
                    curCluster,
                    curClusters,
                    newCluster,
                    newClusters),
            masterNodes,
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().isYbcInstalled());
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(
            (nodes, processTypes) ->
                createServerConfFileUpdateTasks(
                    userIntent,
                    nodes,
                    processTypes,
                    curCluster,
                    curClusters,
                    newCluster,
                    newClusters),
            masterNodes,
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().isYbcInstalled());
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(
            (List<NodeDetails> nodeList, Set<ServerType> processTypes) -> {
              ServerType processType = getSingle(processTypes);
              createServerConfFileUpdateTasks(
                  userIntent,
                  nodeList,
                  processTypes,
                  curCluster,
                  curClusters,
                  newCluster,
                  newClusters);
              createSetFlagInMemoryTasks(
                      nodeList,
                      processType,
                      true,
                      node ->
                          GFlagsUtil.getGFlagsForNode(node, processType, newCluster, newClusters),
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
