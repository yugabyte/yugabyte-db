// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UpgradeTaskBase.MastersAndTservers;
import com.yugabyte.yw.commissioner.UpgradeTaskBase.UpgradeContext;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.CertReloadTaskCreator;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateRootCert.UpdateRootCertAction;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
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
    if (taskParams().rootCARotationType == null
        || taskParams().rootCARotationType == CertsRotateParams.CertRotationType.None) {
      throw new RuntimeException("RootCA rotation type is not set");
    }
    if (CertificateHelper.checkNode2NodeCertsExpiry(getUniverse())) {
      if (taskParams().upgradeOption != UpgradeOption.NON_ROLLING_UPGRADE) {
        throw new RuntimeException(
            "Node-to-node certificates are expired, only non-rolling upgrade is supported");
      }
    } else {
      addBasicPrecheckTasks();
    }
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          taskParams().verifyParams(getUniverse(), isFirstTry());
          Universe universe = getUniverse();

          if (taskParams().rootCARotationType == CertsRotateParams.CertRotationType.RootCert) {
            createRootCertRotationTask(universe);
          } else if (taskParams().rootCARotationType
              == CertsRotateParams.CertRotationType.ServerCert) {
            createServerCertRotationTask(universe);
          } else {
            throw new RuntimeException("Invalid rootCA rotation type");
          }
        });
  }

  private void createRootCertRotationTask(Universe universe) {
    // Step 1: Create temporary multi-cert with old cert first, new cert later
    // This ensures node certs are still generated with old rootCA
    UUID temporaryRootCAUUID = CertificateHelper.getTemporaryRootCAUUID(universe);
    createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCert, temporaryRootCAUUID);

    // Step 1a: Update pods with temporary multi-cert
    createRotateCertTask(universe, temporaryRootCAUUID);

    // Step 2: Reverse the cert order - new cert first, old cert later
    // This ensures node certs are generated with new rootCA
    createUniverseUpdateRootCertTask(
        UpdateRootCertAction.MultiCertReverse, null /* temporaryRootCAUUID */);

    // Step 2a: Update pods with reversed multi-cert
    createRotateCertTask(universe, temporaryRootCAUUID);

    // Step 3: Reset to use only the new rootCA
    createUniverseUpdateRootCertTask(UpdateRootCertAction.Reset, null /* temporaryRootCAUUID */);

    // Step 3a: Update pods with final new rootCA
    createRotateCertTask(universe, taskParams().rootCA);

    // Update TLS parameters in the universe
    createUniverseSetTlsParamsTask(getTaskSubGroupType());
  }

  private void createServerCertRotationTask(Universe universe) {
    createRotateCertTask(universe, taskParams().rootCA);

    // Update TLS parameters in the universe
    createUniverseSetTlsParamsTask(getTaskSubGroupType());
  }

  private void createRotateCertTask(Universe universe, UUID rootCAUUID) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    String stableYbcVersion = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);
    if (taskParams().upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      createNonRestartUpgradeTask(universe, getUpgradeContext(rootCAUUID));
      createKubernetesCertHotReloadTask(universe, getUserTaskUUID());
    } else if (taskParams().upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      createUpgradeTask(
          getUniverse(),
          userIntent.ybSoftwareVersion,
          true /* upgradeMasters */,
          true /* upgradeTservers */,
          getUniverse().isYbcEnabled(),
          stableYbcVersion,
          getUpgradeContext(rootCAUUID));
    } else {
      createNonRollingUpgradeTask(
          universe,
          userIntent.ybSoftwareVersion,
          true /* upgradeMasters */,
          true /* upgradeTservers */,
          getUniverse().isYbcEnabled(),
          stableYbcVersion,
          getUpgradeContext(rootCAUUID));
    }
  }

  private void createKubernetesCertHotReloadTask(Universe universe, UUID userTaskUuid) {
    MastersAndTservers nodes = getNodesToBeRestarted();
    log.info(
        "Creating Kubernetes cert reload task for {} master nodes and {} tserver nodes",
        nodes.mastersList.size(),
        nodes.tserversList.size());

    CertReloadTaskCreator taskCreator =
        new CertReloadTaskCreator(
            universe.getUniverseUUID(),
            userTaskUuid,
            getRunnableTask(),
            getTaskExecutor(),
            nodes.mastersList);

    // Execute the cert reload tasks for both masters and tservers
    taskCreator.run(nodes.mastersList, Collections.singleton(ServerType.MASTER));
    taskCreator.run(nodes.tserversList, Collections.singleton(ServerType.TSERVER));

    // Handle YBC if enabled
    if (universe.isYbcEnabled()) {
      String stableYbcVersion = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);
      for (UniverseDefinitionTaskParams.Cluster cluster : universe.getUniverseDetails().clusters) {
        Set<NodeDetails> tservers =
            universe.getTserversInCluster(cluster.uuid).stream().collect(Collectors.toSet());
        if (!universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc()) {
          installYbcOnThePods(
              tservers,
              cluster.clusterType.equals(ClusterType.ASYNC),
              stableYbcVersion,
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybcFlags);
          performYbcAction(tservers, false, "stop");
        }
        createWaitForYbcServerTask(tservers);
      }
    }
  }

  private UpgradeContext getUpgradeContext(UUID rootCAUUID) {
    return UpgradeContext.builder().rootCAUUID(rootCAUUID).build();
  }
}
