// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseSetTlsParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateRootCert;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateRootCert.UpdateRootCertAction;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CertsRotateKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  @Inject
  protected CertsRotateKubernetesUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
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
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Cluster cluster = getUniverse().getUniverseDetails().getPrimaryCluster();
          UserIntent userIntent = cluster.userIntent;

          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse(), isFirstTry());

          // Update the rootCA in platform to have both old cert and new cert
          // Here in the temporary multi-cert we will have old cert first and new cert later
          // cert key will be pointing to old root cert key itself
          // genSignedCert in helm chart will pick only the first cert present in the root chain
          // So, generated node certs will still be of old rootCA after this step
          createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCert);
          // Create kubernetes upgrade task to rotate certs
          createUpgradeTask(
              getUniverse(),
              userIntent.ybSoftwareVersion,
              true /* isMasterChanged */,
              true /* isTserverChanged */,
              getUniverse().isYbcEnabled(),
              getUniverse().getUniverseDetails().getYbcSoftwareVersion());

          // Now we will change the order of certs: new cert first, followed by old root cert
          // Also cert key will be pointing to new root cert key
          // This makes sure genSignedCert in helm chart generates node certs of new root cert
          // Essentially equivalent to updating only node certs in this step
          createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCertReverse);
          // Create kubernetes upgrade task to rotate certs
          createUpgradeTask(
              getUniverse(),
              userIntent.ybSoftwareVersion,
              true /* isMasterChanged */,
              true /* isTserverChanged */,
              getUniverse().isYbcEnabled(),
              getUniverse().getUniverseDetails().getYbcSoftwareVersion());

          // Reset the temporary certs and update the universe to use new rootCA
          createUniverseUpdateRootCertTask(UpdateRootCertAction.Reset);
          createUniverseSetTlsParamsTask();
          // Create kubernetes upgrade task to rotate certs
          createUpgradeTask(
              getUniverse(),
              userIntent.ybSoftwareVersion,
              true /* isMasterChanged */,
              true /* isTserverChanged */,
              getUniverse().isYbcEnabled(),
              getUniverse().getUniverseDetails().getYbcSoftwareVersion());
        });
  }

  private void createUniverseUpdateRootCertTask(UpdateRootCertAction updateAction) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UniverseUpdateRootCert");
    UniverseUpdateRootCert.Params params = new UniverseUpdateRootCert.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.rootCA = taskParams().rootCA;
    params.action = updateAction;
    UniverseUpdateRootCert task = createTask(UniverseUpdateRootCert.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    subTaskGroup.setSubTaskGroupType(getTaskSubGroupType());
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createUniverseSetTlsParamsTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UniverseSetTlsParams");
    UniverseSetTlsParams.Params params = new UniverseSetTlsParams.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.enableNodeToNodeEncrypt = getUserIntent().enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = getUserIntent().enableClientToNodeEncrypt;
    params.allowInsecure = getUniverse().getUniverseDetails().allowInsecure;
    params.rootCA = taskParams().rootCA;
    params.clientRootCA = getUniverse().getUniverseDetails().getClientRootCA();
    params.rootAndClientRootCASame = getUniverse().getUniverseDetails().rootAndClientRootCASame;
    UniverseSetTlsParams task = createTask(UniverseSetTlsParams.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    subTaskGroup.setSubTaskGroupType(getTaskSubGroupType());
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }
}
