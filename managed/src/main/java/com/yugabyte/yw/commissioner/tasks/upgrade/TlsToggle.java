// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class TlsToggle extends UpgradeTaskBase {

  @Inject
  protected TlsToggle(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected TlsToggleParams taskParams() {
    return (TlsToggleParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.ToggleTls;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.ToggleTls;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);

    if (EncryptionInTransitUtil.isRootCARequired(taskParams()) && taskParams().rootCA == null) {
      throw new IllegalArgumentException("Root certificate is null");
    }

    if (EncryptionInTransitUtil.isClientRootCARequired(taskParams())
        && taskParams().getClientRootCA() == null) {
      throw new IllegalArgumentException("Client root certificate is null");
    }
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    // Skip running prechecks if Node2Node certs has expired
    if (!CertificateHelper.checkNode2NodeCertsExpiry(universe)) {
      addBasicPrecheckTasks();
    }
    if (isFirstTry()
        && (taskParams().enableNodeToNodeEncrypt || taskParams().enableClientToNodeEncrypt)) {
      createCheckCertificateConfigTask();
    }
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return fetchNodes(taskParams().upgradeOption);
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          MastersAndTservers nodes = getNodesToBeRestarted();
          Set<NodeDetails> allNodes = toOrderedSet(nodes.asPair());
          // Copy any new certs to all nodes
          createCopyCertTasks(allNodes);
          updateUniverseHttpsEnabledUI(taskParams().nodeToNodeChange);
          // Round 1 gflags upgrade
          createRound1GFlagUpdateTasks(nodes);
          // Update TLS related params in universe details
          createUniverseSetTlsParamsTask(
              taskParams().getUniverseUUID(),
              taskParams().enableNodeToNodeEncrypt,
              taskParams().enableClientToNodeEncrypt,
              taskParams().allowInsecure,
              taskParams().rootAndClientRootCASame,
              taskParams().rootCA,
              taskParams().getClientRootCA());
          // Round 2 gflags upgrade
          createRound2GFlagUpdateTasks(nodes);
        },
        u -> {
          // 1: If task is to enable node-to-node encryption.
          // -1: If task is to disable node-to-node encryption.
          // 0: If there is no change in node-to-node encryption.
          UserIntent userIntent = u.getUniverseDetails().getPrimaryCluster().userIntent;
          taskParams().nodeToNodeChange =
              userIntent.enableNodeToNodeEncrypt != taskParams().enableNodeToNodeEncrypt
                  ? (taskParams().enableNodeToNodeEncrypt ? 1 : -1)
                  : 0;
          // Persist this setting in the DB.
          updateTaskDetailsInDB(taskParams());
        });
  }

  private void createRound1GFlagUpdateTasks(MastersAndTservers nodes) {
    if (taskParams().nodeToNodeChange < 0) {
      // Skip round1 for disableTls if user selects non-rolling upgrade.
      if (taskParams().upgradeOption == UpgradeOption.NON_ROLLING_UPGRADE) {
        log.debug("Skipping round1GflagUpdate because the upgrade option is non-rolling");
        return;
      }
      // Skip running round1 if Node2Node certs have expired because the DB call will fail.
      if (CertificateHelper.checkNode2NodeCertsExpiry(getUniverse())) {
        log.debug("Skipping round 1 because the cert has expired");
        return;
      }
      // Setting allow_insecure to true can be done in non-restart way.
      createNonRestartUpgradeTaskFlow(
          (List<NodeDetails> nodeList, Set<ServerType> processTypes) -> {
            createGFlagUpdateTasks(1, nodeList, getSingle(processTypes));
            Map<String, String> gflags = new HashMap<>();
            gflags.put("allow_insecure_connections", "true");
            createSetFlagInMemoryTasks(
                    nodeList,
                    getSingle(processTypes),
                    (node, params) -> {
                      params.force = true;
                      params.gflags = gflags;
                    })
                .setSubTaskGroupType(getTaskSubGroupType());
          },
          nodes,
          DEFAULT_CONTEXT);
    } else {
      if (taskParams().upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
        createRollingUpgradeTaskFlow(
            (nodeList, processTypes) ->
                createGFlagUpdateTasks(1, nodeList, getSingle(processTypes)),
            nodes,
            RUN_BEFORE_STOPPING,
            false);
      } else if (taskParams().upgradeOption == UpgradeOption.NON_ROLLING_UPGRADE) {
        createNonRollingUpgradeTaskFlow(
            (nodeList, processTypes) ->
                createGFlagUpdateTasks(1, nodeList, getSingle(processTypes)),
            nodes,
            RUN_BEFORE_STOPPING,
            false);
      }
    }
  }

  private void createRound2GFlagUpdateTasks(MastersAndTservers nodes) {
    // Second round upgrade not needed when there is no change in node-to-node
    if (taskParams().nodeToNodeChange > 0) {
      // Setting allow_insecure can be done in non-restart way
      createNonRestartUpgradeTaskFlow(
          (List<NodeDetails> nodeList, Set<ServerType> processTypes) -> {
            ServerType processType = getSingle(processTypes);
            createGFlagUpdateTasks(2, nodeList, processType);
            Map<String, String> gflags = new HashMap<>();
            gflags.put("allow_insecure_connections", String.valueOf(taskParams().allowInsecure));
            createSetFlagInMemoryTasks(
                    nodeList,
                    processType,
                    (node, params) -> {
                      params.force = true;
                      params.gflags = gflags;
                    })
                .setSubTaskGroupType(getTaskSubGroupType());
          },
          nodes,
          DEFAULT_CONTEXT);
    } else if (taskParams().nodeToNodeChange < 0) {
      if (taskParams().upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
        createRollingUpgradeTaskFlow(
            (nodeList, processTypes) ->
                createGFlagUpdateTasks(2, nodeList, getSingle(processTypes)),
            nodes,
            RUN_BEFORE_STOPPING,
            false);
      } else if (taskParams().upgradeOption == UpgradeOption.NON_ROLLING_UPGRADE) {
        createNonRollingUpgradeTaskFlow(
            (nodeList, processTypes) ->
                createGFlagUpdateTasks(2, nodeList, getSingle(processTypes)),
            nodes,
            RUN_BEFORE_STOPPING,
            false);
      }
    }

    if (taskParams().isYbcInstalled()) {
      createStopYbControllerTasks(nodes.getAllNodes())
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      createYbcFlagsUpdateTasks(nodes.getAllNodes().stream().collect(Collectors.toList()));
      createStartYbcTasks(nodes.getAllNodes())
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      // Wait for yb-controller to be responsive on each node.
      createWaitForYbcServerTask(nodes.getAllNodes())
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
  }

  private void createGFlagUpdateTasks(int round, List<NodeDetails> nodes, ServerType processType) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }

    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s", getTaskSubGroupType(), taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(
          getAnsibleConfigureServerTaskForToggleTls(
              node,
              processType,
              round == 1
                  ? UpgradeTaskSubType.Round1GFlagsUpdate
                  : UpgradeTaskSubType.Round2GFlagsUpdate));
    }
    subTaskGroup.setSubTaskGroupType(getTaskSubGroupType());
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createYbcFlagsUpdateTasks(List<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleClusterServerCtl");
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(getAnsibleConfigureServerTaskForYbcToggleTls(node));
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createCopyCertTasks(Collection<NodeDetails> nodes) {
    // Copy cert tasks are not needed if TLS is disabled
    if (!taskParams().enableNodeToNodeEncrypt && !taskParams().enableClientToNodeEncrypt) {
      return;
    }

    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s", getTaskSubGroupType(), taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(
          getAnsibleConfigureServerTaskForToggleTls(
              node, ServerType.TSERVER, UpgradeTaskSubType.CopyCerts));
    }
    subTaskGroup.setSubTaskGroupType(getTaskSubGroupType());
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private AnsibleConfigureServers getAnsibleConfigureServerTaskForToggleTls(
      NodeDetails node, ServerType processType, UpgradeTaskSubType taskSubType) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(node, processType, UpgradeTaskType.ToggleTls, taskSubType);
    params.enableNodeToNodeEncrypt = taskParams().enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = taskParams().enableClientToNodeEncrypt;
    params.allowInsecure = taskParams().allowInsecure;
    params.rootCA = taskParams().rootCA;
    params.setClientRootCA(taskParams().getClientRootCA());
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    params.nodeToNodeChange = taskParams().nodeToNodeChange;
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    return task;
  }

  private AnsibleConfigureServers getAnsibleConfigureServerTaskForYbcToggleTls(NodeDetails node) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            node,
            ServerType.CONTROLLER,
            UpgradeTaskType.ToggleTls,
            UpgradeTaskSubType.YbcGflagsUpdate);
    params.enableNodeToNodeEncrypt = taskParams().enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = taskParams().enableClientToNodeEncrypt;
    params.allowInsecure = taskParams().allowInsecure;
    params.rootCA = taskParams().rootCA;
    params.setClientRootCA(taskParams().getClientRootCA());
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    params.nodeToNodeChange = taskParams().nodeToNodeChange;
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    return task;
  }

  public void createCheckCertificateConfigTask() {
    MastersAndTservers nodes = getNodesToBeRestarted();
    Set<NodeDetails> allNodes = toOrderedSet(nodes.asPair());
    createCheckCertificateConfigTask(
        taskParams().clusters,
        allNodes,
        taskParams().rootCA,
        taskParams().getClientRootCA(),
        taskParams().enableClientToNodeEncrypt,
        NodeManager.YUGABYTE_USER);
  }
}
