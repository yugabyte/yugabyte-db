// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes;
import com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateNodeDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public class VMImageUpgrade extends UpgradeTaskBase {

  private final Map<UUID, List<String>> replacementRootVolumes = new ConcurrentHashMap<>();

  @Inject
  protected VMImageUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected VMImageUpgradeParams taskParams() {
    return (VMImageUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.Invalid;
  }

  @Override
  public NodeState getNodeState() {
    return null;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodes(taskParams().upgradeOption);
          Set<NodeDetails> nodeSet = new LinkedHashSet<>();
          nodeSet.addAll(nodes.getLeft());
          nodeSet.addAll(nodes.getRight());
          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse());
          // Create task sequence for VM Image upgrade
          createVMImageUpgradeTasks(nodeSet);
        });
  }

  private void createVMImageUpgradeTasks(Set<NodeDetails> nodes) {
    createRootVolumeCreationTasks(new ArrayList<>(nodes))
        .setSubTaskGroupType(getTaskSubGroupType());

    for (NodeDetails node : nodes) {
      UUID region = taskParams().nodeToRegion.get(node.nodeUuid);
      String machineImage = taskParams().machineImages.get(region);

      if (!taskParams().forceVMImageUpgrade && machineImage.equals(node.machineImage)) {
        log.info(
            "Skipping node {} as it's already running on {} and force flag is not set",
            node.nodeName,
            machineImage);
        continue;
      }

      List<UniverseDefinitionTaskBase.ServerType> processTypes = new ArrayList<>();
      if (node.isMaster) processTypes.add(ServerType.MASTER);
      if (node.isTserver) processTypes.add(ServerType.TSERVER);

      processTypes.forEach(
          processType ->
              createServerControlTask(node, processType, "stop")
                  .setSubTaskGroupType(getTaskSubGroupType()));

      createRootVolumeReplacementTask(node).setSubTaskGroupType(getTaskSubGroupType());

      List<NodeDetails> nodeList = Collections.singletonList(node);
      createSetupServerTasks(nodeList).setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      UniverseDefinitionTaskParams universeDetails = getUniverse().getUniverseDetails();
      taskParams().rootCA = universeDetails.rootCA;
      taskParams().clientRootCA = universeDetails.clientRootCA;
      taskParams().rootAndClientRootCASame = universeDetails.rootAndClientRootCASame;
      taskParams().allowInsecure = universeDetails.allowInsecure;
      taskParams().setTxnTableWaitCountFlag = universeDetails.setTxnTableWaitCountFlag;
      createConfigureServerTasks(nodeList, false, false, false)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      processTypes.forEach(
          processType -> {
            createGFlagsOverrideTasks(nodeList, processType);
            createServerControlTask(node, processType, "start")
                .setSubTaskGroupType(getTaskSubGroupType());
            createWaitForServersTasks(new HashSet<>(nodeList), processType);
            createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
                .setSubTaskGroupType(getTaskSubGroupType());
          });

      createWaitForKeyInMemoryTask(node);

      node.machineImage = machineImage;
      createNodeDetailsUpdateTask(node).setSubTaskGroupType(getTaskSubGroupType());
    }
  }

  private SubTaskGroup createRootVolumeCreationTasks(List<NodeDetails> nodes) {
    Map<UUID, List<NodeDetails>> rootVolumesPerAZ =
        nodes.stream().collect(Collectors.groupingBy(n -> n.azUuid));
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("CreateRootVolumes", executor);

    rootVolumesPerAZ.forEach(
        (key, value) -> {
          NodeDetails node = value.get(0);
          UUID region = taskParams().nodeToRegion.get(node.nodeUuid);
          String machineImage = taskParams().machineImages.get(region);
          int numVolumes = value.size();

          if (!taskParams().forceVMImageUpgrade) {
            numVolumes =
                (int) value.stream().filter(n -> !machineImage.equals(n.machineImage)).count();
          }

          if (numVolumes == 0) {
            log.info("Nothing to upgrade in AZ {}", node.cloudInfo.az);
            return;
          }

          CreateRootVolumes.Params params = new CreateRootVolumes.Params();
          Cluster cluster = taskParams().getClusterByUuid(node.placementUuid);
          if (cluster == null) {
            throw new IllegalArgumentException(
                "No cluster available with UUID: " + node.placementUuid);
          }
          UserIntent userIntent = cluster.userIntent;
          fillCreateParamsForNode(params, userIntent, node);
          params.numVolumes = numVolumes;
          params.machineImage = machineImage;
          params.bootDisksPerZone = this.replacementRootVolumes;

          log.info(
              "Creating {} root volumes using {} in AZ {}",
              params.numVolumes,
              params.machineImage,
              node.cloudInfo.az);

          CreateRootVolumes task = createTask(CreateRootVolumes.class);
          task.initialize(params);
          subTaskGroup.addSubTask(task);
        });

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private SubTaskGroup createRootVolumeReplacementTask(NodeDetails node) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("ReplaceRootVolume", executor);
    ReplaceRootVolume.Params replaceParams = new ReplaceRootVolume.Params();
    replaceParams.nodeName = node.nodeName;
    replaceParams.azUuid = node.azUuid;
    replaceParams.universeUUID = taskParams().universeUUID;
    replaceParams.bootDisksPerZone = this.replacementRootVolumes;

    ReplaceRootVolume replaceDiskTask = createTask(ReplaceRootVolume.class);
    replaceDiskTask.initialize(replaceParams);
    subTaskGroup.addSubTask(replaceDiskTask);

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private SubTaskGroup createNodeDetailsUpdateTask(NodeDetails node) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("UpdateNodeDetails", executor);
    UpdateNodeDetails.Params updateNodeDetailsParams = new UpdateNodeDetails.Params();
    updateNodeDetailsParams.universeUUID = taskParams().universeUUID;
    updateNodeDetailsParams.azUuid = node.azUuid;
    updateNodeDetailsParams.nodeName = node.nodeName;
    updateNodeDetailsParams.details = node;

    UpdateNodeDetails updateNodeTask = createTask(UpdateNodeDetails.class);
    updateNodeTask.initialize(updateNodeDetailsParams);
    updateNodeTask.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(updateNodeTask);

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
