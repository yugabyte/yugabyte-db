// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase.UpgradeContext;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseSetTlsParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateRootCert;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateRootCert.UpdateRootCertAction;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.util.UUID;
import javax.annotation.Nullable;
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
          String stableYbcVersion = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);

          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse(), isFirstTry());

          // Update the rootCA in platform to have both old cert and new cert
          // Here in the temporary multi-cert we will have old cert first and new cert later
          // cert key will be pointing to old root cert key itself
          // genSignedCert in helm chart will pick only the first cert present in the root chain
          // So, generated node certs will still be of old rootCA after this step
          UUID temporaryRootCAUUID = getTemporaryRootCAUUID();
          createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCert, temporaryRootCAUUID);

          // Create kubernetes upgrade task to rotate certs
          createUpgradeTask(
              getUniverse(),
              userIntent.ybSoftwareVersion,
              true /* upgradeMasters */,
              true /* upgradeTservers */,
              getUniverse().isYbcEnabled(),
              stableYbcVersion,
              getUpgradeContext(temporaryRootCAUUID));

          // Now we will change the order of certs: new cert first, followed by old root cert
          // Also cert key will be pointing to new root cert key
          // This makes sure genSignedCert in helm chart generates node certs of new root cert
          // Essentially equivalent to updating only node certs in this step
          createUniverseUpdateRootCertTask(
              UpdateRootCertAction.MultiCertReverse, null /* temporaryRootCAUUID */);
          // Create kubernetes upgrade task to rotate certs
          createUpgradeTask(
              getUniverse(),
              userIntent.ybSoftwareVersion,
              true /* upgradeMasters */,
              true /* upgradeTservers */,
              getUniverse().isYbcEnabled(),
              stableYbcVersion,
              getUpgradeContext(temporaryRootCAUUID));

          // Reset the temporary certs and update the universe to use new rootCA
          createUniverseUpdateRootCertTask(
              UpdateRootCertAction.Reset, null /* temporaryRootCAUUID */);

          createUniverseSetTlsParamsTask();
          // Create kubernetes upgrade task to rotate certs
          createUpgradeTask(
              getUniverse(),
              userIntent.ybSoftwareVersion,
              true /* upgradeMasters */,
              true /* upgradeTservers */,
              getUniverse().isYbcEnabled(),
              stableYbcVersion,
              getUpgradeContext(taskParams().rootCA));
        });
  }

  private UUID getTemporaryRootCAUUID() {
    try {
      CertificateInfo oldRootCert =
          CertificateInfo.getOrBadRequest(getUniverse().getUniverseDetails().rootCA);
      CertificateInfo temporaryCert =
          CertificateInfo.createCopy(
              oldRootCert,
              oldRootCert.getLabel() + EncryptionInTransitUtil.MULTI_ROOT_CERT_TMP_LABEL_SUFFIX,
              new File(oldRootCert.getCertificate()).getAbsolutePath());
      return temporaryCert.getUuid();
    } catch (Exception e) {
      log.error("Failed to create temporary multi cert", e);
      throw new RuntimeException("Failed to create temporary multi cert", e);
    }
  }

  private void createUniverseUpdateRootCertTask(
      UpdateRootCertAction updateAction, @Nullable UUID temporaryRootCAUUID) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UniverseUpdateRootCert");
    UniverseUpdateRootCert.Params params = new UniverseUpdateRootCert.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.rootCA = taskParams().rootCA;
    params.action = updateAction;
    params.temporaryRootCAUUID = temporaryRootCAUUID;
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

  private UpgradeContext getUpgradeContext(UUID rootCAUUID) {
    return UpgradeContext.builder().rootCAUUID(rootCAUUID).build();
  }
}
