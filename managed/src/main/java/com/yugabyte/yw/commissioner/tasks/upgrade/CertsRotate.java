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
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeManager.CertRotateAction;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.CertsRotateParams.CertRotationType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collection;
import java.util.List;
import java.util.Set;
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
  public void run() {
    runUpgrade(
        () -> {
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodes(taskParams().upgradeOption);
          Set<NodeDetails> allNodes = toOrderedSet(nodes);
          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse());
          // For rootCA root certificate rotation, we would need to do it in three rounds
          // so that node to node communications are not disturbed during the upgrade
          // For other cases we can do it in one round by updating respective certs
          if (taskParams().upgradeOption == UpgradeOption.ROLLING_UPGRADE
              && taskParams().rootCARotationType == CertRotationType.RootCert) {
            // Update the rootCA in platform to have both old cert and new cert
            createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCert);
            // Append new root cert to the existing ca.crt
            createCertUpdateTasks(allNodes, CertRotateAction.APPEND_NEW_ROOT_CERT);
            // Do a rolling restart
            createRestartTasks(nodes, UpgradeOption.ROLLING_UPGRADE, false);
            // Copy new server certs to all nodes
            createCertUpdateTasks(allNodes, CertRotateAction.ROTATE_CERTS);
            // Do a rolling restart
            createRestartTasks(nodes, UpgradeOption.ROLLING_UPGRADE, false);
            // Remove old root cert from the ca.crt
            createCertUpdateTasks(allNodes, CertRotateAction.REMOVE_OLD_ROOT_CERT);
            // Update gflags of cert directories
            createUpdateCertDirsTask(nodes.getLeft(), ServerType.MASTER);
            createUpdateCertDirsTask(nodes.getRight(), ServerType.TSERVER);

            // Reset the old rootCA content in platform
            createUniverseUpdateRootCertTask(UpdateRootCertAction.Reset);
            // Update universe details with new cert values
            createUniverseSetTlsParamsTask(getTaskSubGroupType());
            // Do a rolling restart
            createRestartTasks(nodes, UpgradeOption.ROLLING_UPGRADE, taskParams().ybcInstalled);
          } else {
            // Update the rootCA in platform to have both old cert and new cert
            if (taskParams().rootCARotationType == CertRotationType.RootCert) {
              createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCert);
            }
            createCertUpdateTasks(
                nodes.getLeft(),
                nodes.getRight(),
                getTaskSubGroupType(),
                taskParams().rootCARotationType,
                taskParams().clientRootCARotationType);

            // Do a rolling/non-rolling restart
            createRestartTasks(nodes, taskParams().upgradeOption, taskParams().ybcInstalled);
            // Reset the old rootCA content in platform
            if (taskParams().rootCARotationType == CertRotationType.RootCert) {
              createUniverseUpdateRootCertTask(UpdateRootCertAction.Reset);
            }
            // Update universe details with new cert values
            createUniverseSetTlsParamsTask(getTaskSubGroupType());
          }
        });
  }

  private void createCertUpdateTasks(
      Collection<NodeDetails> nodes, CertRotateAction certRotateAction) {
    createCertUpdateTasks(
        nodes,
        certRotateAction,
        getTaskSubGroupType(),
        taskParams().rootCARotationType,
        taskParams().clientRootCARotationType);
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

  private void createUpdateCertDirsTask(Collection<NodeDetails> nodes, ServerType serverType) {
    createUpdateCertDirsTask(nodes, serverType, getTaskSubGroupType());
  }

  // TODO: sort out the mess with rootAndClientRootCASame silently shadowing its namesake
  // in UniverseDefinitionTaskParams
  // (referencing them through taskParams() may cause subtle bugs)
  @Override
  protected UniverseSetTlsParams.Params createSetTlsParams(SubTaskGroupType subTaskGroupType) {
    UniverseSetTlsParams.Params params = super.createSetTlsParams(subTaskGroupType);
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    return params;
  }

  @Override
  protected AnsibleConfigureServers.Params createCertUpdateParams(
      UserIntent userIntent,
      NodeDetails node,
      NodeManager.CertRotateAction certRotateAction,
      CertsRotateParams.CertRotationType rootCARotationType,
      CertsRotateParams.CertRotationType clientRootCARotationType) {
    AnsibleConfigureServers.Params params =
        super.createCertUpdateParams(
            userIntent, node, certRotateAction, rootCARotationType, clientRootCARotationType);
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    return params;
  }

  @Override
  protected AnsibleConfigureServers.Params createUpdateCertDirParams(
      UserIntent userIntent, NodeDetails node, ServerType serverType) {
    AnsibleConfigureServers.Params params =
        super.createUpdateCertDirParams(userIntent, node, serverType);
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    return params;
  }
}
