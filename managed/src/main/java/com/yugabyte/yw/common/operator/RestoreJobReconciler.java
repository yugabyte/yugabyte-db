package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.Backup;
import io.yugabyte.operator.v1alpha1.RestoreJob;
import io.yugabyte.operator.v1alpha1.RestoreJobStatus;
import java.util.*;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RestoreJobReconciler implements ResourceEventHandler<RestoreJob>, Runnable {
  private final SharedIndexInformer<RestoreJob> informer;
  private final Lister<RestoreJob> lister;
  private final MixedOperation<RestoreJob, KubernetesResourceList<RestoreJob>, Resource<RestoreJob>>
      resourceClient;
  private final BackupHelper backupHelper;
  private final ValidatingFormFactory formFactory;
  // Need it to list Backups
  private final SharedIndexInformer<Backup> backupInformer;
  private final String namespace;
  private final OperatorUtils operatorUtils;

  public RestoreJobReconciler(
      SharedIndexInformer<RestoreJob> restoreJobInformer,
      SharedIndexInformer<Backup> backupInformer,
      MixedOperation<RestoreJob, KubernetesResourceList<RestoreJob>, Resource<RestoreJob>>
          resourceClient,
      BackupHelper backupHelper,
      ValidatingFormFactory formFactory,
      String namespace,
      OperatorUtils operatorUtils) {
    this.resourceClient = resourceClient;
    this.informer = restoreJobInformer;
    this.lister = new Lister<>(informer.getIndexer());
    this.backupHelper = backupHelper;
    this.formFactory = formFactory;
    this.namespace = namespace;
    this.backupInformer = backupInformer;
    this.operatorUtils = operatorUtils;
  }

  public UUID getBackupUUIDFromName(String backupName) {
    Lister<Backup> backupLister = new Lister<>(this.backupInformer.getIndexer());
    List<Backup> backups = backupLister.list();

    for (Backup backup : backups) {
      if (backup.getMetadata().getName().equals(backupName)) {
        return UUID.fromString(backup.getStatus().getResourceUUID());
      }
    }
    return null;
  }

  private void updateStatus(RestoreJob restoreJob, String taskUUID, String message) {
    RestoreJobStatus status = restoreJob.getStatus();
    if (status == null) {
      status = new RestoreJobStatus();
    }
    status.setMessage(message);
    status.setTaskUUID(taskUUID);
    restoreJob.setStatus(status);

    resourceClient.inNamespace(namespace).resource(restoreJob).replaceStatus();
  }

  public RestoreBackupParams getRestoreBackupParamsFromCr(RestoreJob restoreJob) throws Exception {

    ObjectMapper objectMapper = new ObjectMapper();
    JsonNode crJsonNode = objectMapper.valueToTree(restoreJob.getSpec());
    Customer cust = operatorUtils.getOperatorCustomer();

    log.info("CRSPECJSON {}", crJsonNode);

    Universe universe =
        operatorUtils.getUniverseFromNameAndNamespace(
            cust.getId(),
            restoreJob.getSpec().getUniverse(),
            restoreJob.getMetadata().getNamespace());
    if (universe == null) {
      throw new Exception("No universe found with name " + restoreJob.getSpec().getUniverse());
    }
    UUID universeUUID = universe.getUniverseUUID();
    log.info("Universe UUID {}", universeUUID);
    UUID backupUUID = getBackupUUIDFromName(restoreJob.getSpec().getBackup());
    log.info("backup UUID {}", backupUUID);
    com.yugabyte.yw.models.Backup backup =
        com.yugabyte.yw.models.Backup.getOrBadRequest(cust.getUuid(), backupUUID);
    RestoreBackupParams restoreBackupParams = new RestoreBackupParams();
    restoreBackupParams.customerUUID = cust.getUuid();
    restoreBackupParams.setUniverseUUID(universeUUID);
    restoreBackupParams.storageConfigUUID = backup.getStorageConfigUUID();
    restoreBackupParams.backupStorageInfoList = new ArrayList<BackupStorageInfo>();
    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.storageLocation = backup.getBackupInfo().backupList.get(0).storageLocation;
    storageInfo.keyspace = restoreJob.getSpec().getKeyspace();
    storageInfo.backupType = backup.getBackupInfo().backupType;

    restoreBackupParams.backupStorageInfoList.add(storageInfo);

    return restoreBackupParams;
  }

  @Override
  public void onAdd(RestoreJob restoreJob) {
    log.info("Creating restore job{} ", restoreJob);
    RestoreJobStatus status = restoreJob.getStatus();

    if (status != null) {
      log.info("Early return because we already started this restore once");
    }

    RestoreBackupParams restoreBackupParams = null;
    Customer cust;
    try {
      restoreBackupParams = getRestoreBackupParamsFromCr(restoreJob);
      cust = operatorUtils.getOperatorCustomer();
    } catch (Exception e) {
      log.error("Got Exception in converting to restore params {}", e);
      updateStatus(restoreJob, "", "Failed in scheduling restore Job" + e.getMessage());
      return;
    }

    UUID customerUUID = cust.getUuid();
    UUID taskUUID = null;
    try {
      taskUUID = backupHelper.createRestoreTask(customerUUID, restoreBackupParams);
    } catch (Exception e) {
      log.error("Got Error in launching restore Job {}", e);
      updateStatus(restoreJob, "", "Failed in scheduling restore Job" + e.getMessage());
      return;
    }
    updateStatus(restoreJob, taskUUID.toString(), "scheduled restoreJob task");
    log.info("Finished submitting a restore job with task uuid {}", taskUUID);
  }

  @Override
  public void onUpdate(RestoreJob oldRestoreJob, RestoreJob newRestoreJob) {
    log.info("updating restore job{} not supported", oldRestoreJob);
  }

  @Override
  public void onDelete(RestoreJob restoreJob, boolean deletedFinalStateUnknown) {
    log.info("Delete on restore job is not supported");
  }

  @Override
  public void run() {
    informer.addEventHandler(this);
    informer.run();
  }
}
