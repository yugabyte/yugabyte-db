// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;

@Abortable
@Retryable
public class TlsToggleKubernetes extends KubernetesUpgradeTaskBase {

  @Inject
  protected TlsToggleKubernetes(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
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
    if (!(universe.getUniverseDetails().getPrimaryCluster().userIntent.enableNodeToNodeEncrypt
        && CertificateHelper.checkNode2NodeCertsExpiry(universe))) {
      addBasicPrecheckTasks();
    }
    if (taskParams().upgradeOption != UpgradeOption.NON_ROLLING_UPGRADE) {
      throw new RuntimeException("Un-supported upgrade option: " + taskParams().upgradeOption);
    }
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          updateUniverseHttpsEnabledUI(taskParams().getNodeToNodeChange(getUserIntent()));
          Universe universe = getUniverse();
          syncUniverseDetailToTaskParams();

          // Update the database with details earlier so that wait-for-server tasks
          // can utilize the node-to-node certificate for yb-client connections.
          createUniverseSetTlsParamsTask(
              taskParams().getUniverseUUID(),
              taskParams().enableNodeToNodeEncrypt,
              taskParams().enableClientToNodeEncrypt,
              taskParams().allowInsecure,
              taskParams().rootAndClientRootCASame,
              taskParams().rootCA,
              taskParams().getClientRootCA());

          createNonRollingUpgradeTask(
              universe,
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
              true,
              true,
              universe.isYbcEnabled(),
              universe.getUniverseDetails().getYbcSoftwareVersion());
        });
  }

  private void syncUniverseDetailToTaskParams() {
    super.taskParams().allowInsecure = taskParams().allowInsecure;
    taskParams()
        .clusters
        .forEach(
            cluster -> {
              cluster.userIntent.enableNodeToNodeEncrypt = taskParams().enableNodeToNodeEncrypt;
              cluster.userIntent.enableClientToNodeEncrypt = taskParams().enableClientToNodeEncrypt;
            });
  }
}
