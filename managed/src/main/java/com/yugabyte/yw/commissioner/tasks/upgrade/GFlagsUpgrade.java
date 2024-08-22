// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.TaskInfo;
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
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import play.libs.Json;

@Slf4j
@Retryable
@Abortable
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
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected void validateRerunParams(TaskInfo previousTaskInfo) {
    Universe universe = getUniverse();
    GFlagsUpgradeParams prevTaskParams =
        Json.fromJson(previousTaskInfo.getDetails(), GFlagsUpgradeParams.class);
    // Cluster with GFlags from the previous task params.
    Map<UUID, UniverseDefinitionTaskParams.Cluster> prevClusters =
        taskParams().getVersionsOfClusters(universe, prevTaskParams);
    Map<UUID, UniverseDefinitionTaskParams.Cluster> curClusters =
        universe.getUniverseDetails().clusters.stream()
            .collect(Collectors.toMap(c -> c.uuid, Function.identity()));
    // Cluster with GFlags from the current task params.
    Map<UUID, UniverseDefinitionTaskParams.Cluster> newClusters =
        taskParams().getNewVersionsOfClusters(universe);

    Set<NodeDetails> prevAffectedNodes =
        universe.getNodes().stream()
            .filter(n -> n.isMaster || n.isTserver)
            .filter(
                n -> {
                  UniverseDefinitionTaskParams.Cluster prevCluster =
                      prevClusters.get(n.placementUuid);
                  UniverseDefinitionTaskParams.Cluster curCluster =
                      curClusters.get(n.placementUuid);
                  return GFlagsUpgradeParams.nodeHasGflagsChanges(
                      n,
                      n.isMaster ? ServerType.MASTER : ServerType.TSERVER,
                      prevCluster,
                      prevClusters.values(),
                      curCluster,
                      curClusters.values());
                })
            .collect(Collectors.toSet());

    Set<NodeDetails> currAffectedNodes =
        universe.getNodes().stream()
            .filter(n -> n.isMaster || n.isTserver)
            .filter(
                n -> {
                  UniverseDefinitionTaskParams.Cluster curCluster =
                      curClusters.get(n.placementUuid);
                  UniverseDefinitionTaskParams.Cluster newCluster =
                      newClusters.get(n.placementUuid);
                  return GFlagsUpgradeParams.nodeHasGflagsChanges(
                      n,
                      n.isMaster ? ServerType.MASTER : ServerType.TSERVER,
                      curCluster,
                      curClusters.values(),
                      newCluster,
                      newClusters.values());
                })
            .collect(Collectors.toSet());

    if (CollectionUtils.isNotEmpty(prevAffectedNodes)
        && !currAffectedNodes.containsAll(prevAffectedNodes)) {
      List<String> prevAffectedNodeNames =
          prevAffectedNodes.stream().map(NodeDetails::getNodeName).collect(Collectors.toList());
      List<String> currAffectedNodeNames =
          currAffectedNodes.stream().map(NodeDetails::getNodeName).collect(Collectors.toList());
      log.error(
          "Currently affected nodes {} must contain the previously affected nodes {}, ",
          currAffectedNodeNames,
          prevAffectedNodeNames);
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Gflags upgrade rerun must affect all server types and nodes changed by the previously"
              + " failed gflags operation");
    }
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    Universe universe = getUniverse();
    List<UniverseDefinitionTaskParams.Cluster> curClusters = universe.getUniverseDetails().clusters;
    Map<UUID, UniverseDefinitionTaskParams.Cluster> newClusters =
        taskParams().getNewVersionsOfClusters(universe);

    // Fetch master and tserver nodes if there is change in gflags
    List<NodeDetails> masterNodes = fetchMasterNodes(taskParams().upgradeOption);
    List<NodeDetails> tServerNodes = fetchTServerNodes(taskParams().upgradeOption);

    for (UniverseDefinitionTaskParams.Cluster curCluster : curClusters) {
      UniverseDefinitionTaskParams.UserIntent userIntent = curCluster.userIntent;
      UniverseDefinitionTaskParams.UserIntent prevIntent = userIntent.clone();
      UniverseDefinitionTaskParams.Cluster newCluster = newClusters.get(curCluster.uuid);
      Map<String, String> masterGflags =
          GFlagsUtil.getBaseGFlags(ServerType.MASTER, newCluster, newClusters.values());
      Map<String, String> tserverGflags =
          GFlagsUtil.getBaseGFlags(ServerType.TSERVER, newCluster, newClusters.values());
      ObjectMapper mapper = new ObjectMapper();
      String redactedMasterNewFlags =
          RedactingService.filterSecretFields(
                  mapper.valueToTree(masterGflags), RedactionTarget.LOGS)
              .toString();
      String redactedMasterOldFlags =
          RedactingService.filterSecretFields(
                  mapper.valueToTree(
                      GFlagsUtil.getBaseGFlags(ServerType.MASTER, curCluster, curClusters)),
                  RedactionTarget.LOGS)
              .toString();
      log.debug(
          "Cluster {} master: new flags {} old flags {}",
          curCluster.clusterType,
          redactedMasterNewFlags,
          redactedMasterOldFlags);
      String redactedTsNewFlags =
          RedactingService.filterSecretFields(
                  mapper.valueToTree(tserverGflags), RedactionTarget.LOGS)
              .toString();
      String redactedTsOldFlags =
          RedactingService.filterSecretFields(
                  mapper.valueToTree(
                      GFlagsUtil.getBaseGFlags(ServerType.TSERVER, curCluster, curClusters)),
                  RedactionTarget.LOGS)
              .toString();
      log.debug(
          "Cluster {} tserver: new flags {} old flags {}",
          curCluster.clusterType,
          redactedTsNewFlags,
          redactedTsOldFlags);

      boolean changedByMasterFlags =
          curCluster.clusterType == UniverseDefinitionTaskParams.ClusterType.PRIMARY
              && GFlagsUtil.syncGflagsToIntent(masterGflags, userIntent);
      boolean changedByTserverFlags =
          curCluster.clusterType == UniverseDefinitionTaskParams.ClusterType.PRIMARY
              && GFlagsUtil.syncGflagsToIntent(tserverGflags, userIntent);
      log.debug(
          "Intent changed by master {} by tserver {}", changedByMasterFlags, changedByTserverFlags);

      boolean applyToAllNodes =
          (changedByMasterFlags || changedByTserverFlags)
              && verifyIntentReallyChanged(curCluster.uuid, universe, prevIntent, userIntent);

      masterNodes.removeIf(
          n ->
              n.placementUuid.equals(curCluster.uuid)
                  && !applyToAllNodes
                  && !GFlagsUpgradeParams.nodeHasGflagsChanges(
                      n,
                      ServerType.MASTER,
                      curCluster,
                      curClusters,
                      newCluster,
                      newClusters.values()));
      tServerNodes.removeIf(
          n ->
              n.placementUuid.equals(curCluster.uuid)
                  && !applyToAllNodes
                  && !GFlagsUpgradeParams.nodeHasGflagsChanges(
                      n,
                      ServerType.TSERVER,
                      curCluster,
                      curClusters,
                      newCluster,
                      newClusters.values()));
    }
    return new MastersAndTservers(masterNodes, tServerNodes);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    String softwareVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (CommonUtils.isAutoFlagSupported(softwareVersion)) {
      // Verify auto flags compatibility.
      taskParams().checkXClusterAutoFlags(universe, gFlagsValidation, xClusterUniverseService);
    }
    boolean checkForbiddenToOverride =
        !config.getBoolean("yb.cloud.enabled")
            && !confGetter.getConfForScope(universe, UniverseConfKeys.gflagsAllowUserOverride);

    MastersAndTservers mastersAndTservers = getNodesToBeRestarted();

    if (checkForbiddenToOverride) {
      List<UniverseDefinitionTaskParams.Cluster> curClusters =
          universe.getUniverseDetails().clusters;
      Map<UUID, UniverseDefinitionTaskParams.Cluster> newClusters =
          taskParams().getNewVersionsOfClusters(universe);

      for (UniverseDefinitionTaskParams.Cluster curCluster : curClusters) {
        UniverseDefinitionTaskParams.UserIntent userIntent = curCluster.userIntent;
        UniverseDefinitionTaskParams.Cluster newCluster = newClusters.get(curCluster.uuid);

        MastersAndTservers forCluster = mastersAndTservers.getForCluster(newCluster.uuid);

        forCluster.mastersList.forEach(
            node ->
                checkForbiddenToOverrideGFlags(
                    node,
                    userIntent,
                    universe,
                    ServerType.MASTER,
                    GFlagsUtil.getGFlagsForNode(
                        node, ServerType.MASTER, newCluster, newClusters.values())));
        forCluster.tserversList.forEach(
            node ->
                checkForbiddenToOverrideGFlags(
                    node,
                    userIntent,
                    universe,
                    ServerType.TSERVER,
                    GFlagsUtil.getGFlagsForNode(
                        node, ServerType.TSERVER, newCluster, newClusters.values())));
      }
    }
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          List<UniverseDefinitionTaskParams.Cluster> curClusters =
              universe.getUniverseDetails().clusters;
          Map<UUID, UniverseDefinitionTaskParams.Cluster> newClusters =
              taskParams().getNewVersionsOfClusters(universe);
          MastersAndTservers mastersAndTservers = getNodesToBeRestarted();

          for (UniverseDefinitionTaskParams.Cluster curCluster : curClusters) {
            UniverseDefinitionTaskParams.UserIntent userIntent = curCluster.userIntent;
            UniverseDefinitionTaskParams.Cluster newCluster = newClusters.get(curCluster.uuid);

            MastersAndTservers forCluster = mastersAndTservers.getForCluster(curCluster.uuid);
            // Upgrade GFlags in all nodes.
            createGFlagUpgradeTasks(
                universe,
                userIntent,
                forCluster.mastersList,
                forCluster.tserversList,
                curCluster,
                curClusters,
                newCluster,
                newClusters.values());
          }
          for (UniverseDefinitionTaskParams.Cluster curCluster : curClusters) {
            UniverseDefinitionTaskParams.Cluster newCluster = newClusters.get(curCluster.uuid);
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

  private boolean verifyIntentReallyChanged(
      UUID clusterUUID,
      Universe universe,
      UniverseDefinitionTaskParams.UserIntent oldIntent,
      UniverseDefinitionTaskParams.UserIntent newIntent) {
    // In ybm while creating universe userIntent and gflags are not synchronized.
    // So it could look like new gflags are changing userIntent, but in fact - they don't.
    Map<String, String> oldMasterGFlags =
        GFlagsUtil.getBaseGFlags(
            ServerType.MASTER,
            universe.getCluster(clusterUUID),
            universe.getUniverseDetails().clusters);
    Map<String, String> oldTserverGFlags =
        GFlagsUtil.getBaseGFlags(
            ServerType.TSERVER,
            universe.getCluster(clusterUUID),
            universe.getUniverseDetails().clusters);
    GFlagsUtil.syncGflagsToIntent(oldMasterGFlags, oldIntent);
    GFlagsUtil.syncGflagsToIntent(oldTserverGFlags, oldIntent);
    return GFlagsUtil.checkGFlagsByIntentChange(oldIntent, newIntent);
  }

  private boolean hasUpdatedGFlags(
      UniverseDefinitionTaskParams.UserIntent newIntent,
      UniverseDefinitionTaskParams.UserIntent curIntent) {
    return !Objects.equals(newIntent.specificGFlags, curIntent.specificGFlags)
        || !Objects.equals(newIntent.masterGFlags, curIntent.masterGFlags)
        || !Objects.equals(newIntent.tserverGFlags, curIntent.tserverGFlags);
  }

  private void createGFlagUpgradeTasks(
      Universe universe,
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
                createNodeSubtasks(
                    universe,
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
                createNodeSubtasks(
                    universe,
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
              createNodeSubtasks(
                  universe,
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
                      (node, params) -> {
                        params.force = true;
                        params.gflags =
                            GFlagsUtil.getGFlagsForNode(node, processType, newCluster, newClusters);
                      })
                  .setSubTaskGroupType(getTaskSubGroupType());
            },
            masterNodes,
            tServerNodes,
            DEFAULT_CONTEXT);
        break;
    }
  }

  protected void createNodeSubtasks(
      Universe universe,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> nodes,
      Set<ServerType> processTypes,
      UniverseDefinitionTaskParams.Cluster curCluster,
      Collection<UniverseDefinitionTaskParams.Cluster> curClusters,
      UniverseDefinitionTaskParams.Cluster newCluster,
      Collection<UniverseDefinitionTaskParams.Cluster> newClusters) {

    createServerConfFileUpdateTasks(
        userIntent, nodes, processTypes, curCluster, curClusters, newCluster, newClusters);

    // In case audit logs export is set up - need to re-configure otel collector.
    if (universe.getUniverseDetails().otelCollectorEnabled
        && curCluster.userIntent.auditLogConfig != null) {
      createManageOtelCollectorTasks(
          userIntent,
          nodes,
          false,
          curCluster.userIntent.auditLogConfig,
          nodeDetails ->
              GFlagsUtil.getGFlagsForNode(
                  nodeDetails, ServerType.TSERVER, newCluster, newClusters));
    }
  }
}
