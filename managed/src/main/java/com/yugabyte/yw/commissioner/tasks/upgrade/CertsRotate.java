// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseSetTlsParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateRootCert;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateRootCert.UpdateRootCertAction;
import com.yugabyte.yw.common.NodeManager.CertRotateAction;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.CertsRotateParams.CertRotationType;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
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
  public void validateParams() {
    taskParams().verifyParams(getUniverse());
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodes(taskParams().upgradeOption);
          // For rootCA root certificate rotation, we would need to do it in three rounds
          // so that node to node communications are not disturbed during the upgrade
          // For other cases we can do it in one round by updating respective certs
          if (taskParams().upgradeOption == UpgradeOption.ROLLING_UPGRADE
              && taskParams().rootCARotationType == CertRotationType.RootCert) {
            // Update the rootCA in platform to have both old cert and new cert
            createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCert);
            // Append new root cert to the existing ca.crt
            createCertUpdateTasks(nodes.getRight(), CertRotateAction.APPEND_NEW_ROOT_CERT);
            // Do a rolling restart
            createRestartTasks(nodes, UpgradeOption.ROLLING_UPGRADE);
            // Copy new server certs to all nodes
            createCertUpdateTasks(nodes.getRight(), CertRotateAction.ROTATE_CERTS);
            // Do a rolling restart
            createRestartTasks(nodes, UpgradeOption.ROLLING_UPGRADE);
            // Remove old root cert from the ca.crt
            createCertUpdateTasks(nodes.getRight(), CertRotateAction.REMOVE_OLD_ROOT_CERT);
            // Update gflags of cert directories
            createUpdateCertDirsTask(nodes.getLeft(), ServerType.MASTER);
            createUpdateCertDirsTask(nodes.getRight(), ServerType.TSERVER);
            // Reset the old rootCA content in platform
            createUniverseUpdateRootCertTask(UpdateRootCertAction.Reset);
            // Update universe details with new cert values
            createUniverseSetTlsParamsTask();
            // Do a rolling restart
            createRestartTasks(nodes, UpgradeOption.ROLLING_UPGRADE);
          } else {
            // Update the rootCA in platform to have both old cert and new cert
            if (taskParams().rootCARotationType == CertRotationType.RootCert) {
              createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCert);
            }
            // Copy new server certs to all nodes
            createCertUpdateTasks(nodes.getRight(), CertRotateAction.ROTATE_CERTS);
            // Update gflags of cert directories
            createUpdateCertDirsTask(nodes.getLeft(), ServerType.MASTER);
            createUpdateCertDirsTask(nodes.getRight(), ServerType.TSERVER);
            // Do a rolling/non-rolling restart
            createRestartTasks(nodes, taskParams().upgradeOption);
            // Reset the old rootCA content in platform
            if (taskParams().rootCARotationType == CertRotationType.RootCert) {
              createUniverseUpdateRootCertTask(UpdateRootCertAction.Reset);
            }
            // Update universe details with new cert values
            createUniverseSetTlsParamsTask();
          }
        });
  }

  private void createCertUpdateTasks(List<NodeDetails> nodes, CertRotateAction certRotateAction) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s", getTaskSubGroupType(), taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      AnsibleConfigureServers.Params params =
          getAnsibleConfigureServerParams(
              node,
              ServerType.TSERVER,
              UpgradeTaskParams.UpgradeTaskType.Certs,
              UpgradeTaskParams.UpgradeTaskSubType.None);
      params.enableNodeToNodeEncrypt = getUserIntent().enableNodeToNodeEncrypt;
      params.enableClientToNodeEncrypt = getUserIntent().enableClientToNodeEncrypt;
      params.rootCA = taskParams().rootCA;
      params.clientRootCA = taskParams().clientRootCA;
      params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
      params.rootCARotationType = taskParams().rootCARotationType;
      params.clientRootCARotationType = taskParams().clientRootCARotationType;
      params.certRotateAction = certRotateAction;
      AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      subTaskGroup.addSubTask(task);
    }
    subTaskGroup.setSubTaskGroupType(getTaskSubGroupType());
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createUniverseUpdateRootCertTask(UpdateRootCertAction updateAction) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("UniverseUpdateRootCert", executor);
    UniverseUpdateRootCert.Params params = new UniverseUpdateRootCert.Params();
    params.universeUUID = taskParams().universeUUID;
    params.rootCA = taskParams().rootCA;
    params.action = updateAction;
    UniverseUpdateRootCert task = createTask(UniverseUpdateRootCert.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    subTaskGroup.setSubTaskGroupType(getTaskSubGroupType());
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createUpdateCertDirsTask(List<NodeDetails> nodes, ServerType serverType) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s", getTaskSubGroupType(), taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      AnsibleConfigureServers.Params params =
          getAnsibleConfigureServerParams(
              node,
              serverType,
              UpgradeTaskParams.UpgradeTaskType.Certs,
              UpgradeTaskParams.UpgradeTaskSubType.None);
      params.enableNodeToNodeEncrypt = getUserIntent().enableNodeToNodeEncrypt;
      params.enableClientToNodeEncrypt = getUserIntent().enableClientToNodeEncrypt;
      params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
      params.certRotateAction = CertRotateAction.UPDATE_CERT_DIRS;
      AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      subTaskGroup.addSubTask(task);
    }
    subTaskGroup.setSubTaskGroupType(getTaskSubGroupType());
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createUniverseSetTlsParamsTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("UniverseSetTlsParams", executor);
    UniverseSetTlsParams.Params params = new UniverseSetTlsParams.Params();
    params.universeUUID = taskParams().universeUUID;
    params.enableNodeToNodeEncrypt = getUserIntent().enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = getUserIntent().enableClientToNodeEncrypt;
    params.allowInsecure = getUniverse().getUniverseDetails().allowInsecure;
    params.rootCA = taskParams().rootCA;
    params.clientRootCA = taskParams().clientRootCA;
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;

    UniverseSetTlsParams task = createTask(UniverseSetTlsParams.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    subTaskGroup.setSubTaskGroupType(getTaskSubGroupType());
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }
}
