package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.ResourceAnnotationKeys;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.DeleteBackupParams;
import com.yugabyte.yw.forms.DeleteBackupParams.DeleteBackupInfo;
import com.yugabyte.yw.models.Customer;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.Backup;
import io.yugabyte.operator.v1alpha1.BackupStatus;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import play.libs.Json;

@Slf4j
public class BackupReconciler implements ResourceEventHandler<Backup>, Runnable {
  private final SharedIndexInformer<Backup> informer;
  private final Lister<Backup> lister;
  private final MixedOperation<Backup, KubernetesResourceList<Backup>, Resource<Backup>>
      resourceClient;
  private final BackupHelper backupHelper;
  private final ValidatingFormFactory formFactory;
  private final String namespace;
  private final SharedIndexInformer<StorageConfig> scInformer;
  private final OperatorUtils operatorUtils;

  private final ResourceTracker resourceTracker = new ResourceTracker();

  public Set<KubernetesResourceDetails> getTrackedResources() {
    return resourceTracker.getTrackedResources();
  }

  public ResourceTracker getResourceTracker() {
    return resourceTracker;
  }

  public BackupReconciler(
      SharedIndexInformer<Backup> backupInformer,
      MixedOperation<Backup, KubernetesResourceList<Backup>, Resource<Backup>> resourceClient,
      BackupHelper backupHelper,
      ValidatingFormFactory formFactory,
      String namespace,
      SharedIndexInformer<StorageConfig> scInformer,
      OperatorUtils operatorUtils) {
    this.resourceClient = resourceClient;
    this.informer = backupInformer;
    this.lister = new Lister<>(informer.getIndexer());
    this.backupHelper = backupHelper;
    this.formFactory = formFactory;
    this.namespace = namespace;
    this.scInformer = scInformer;
    this.operatorUtils = operatorUtils;
  }

  private void updateStatus(Backup backup, String taskUUID, String backupUUID, String message) {
    BackupStatus status = backup.getStatus();
    if (status == null) {
      status = new BackupStatus();
    }
    status.setMessage(message);
    // Don't override the Backup resource and task UUID once set.
    if (status.getResourceUUID() == null) {
      status.setResourceUUID(backupUUID);
    }
    if (status.getTaskUUID() == null) {
      status.setTaskUUID(taskUUID);
    }
    backup.setStatus(status);

    resourceClient.inNamespace(namespace).resource(backup).replaceStatus();
  }

  @Override
  public void onAdd(Backup backup) {
    BackupStatus status = backup.getStatus();
    if (status != null) {
      // We don't need to do a retry because the backup state machine will take care of it.
      // Even in the case of failure, we expect customer to create a new backup CR.
      log.info("Early return because we already started this backup once");
      return;
    }

    ObjectMeta backupMeta = backup.getMetadata();
    if (MapUtils.isNotEmpty(backupMeta.getLabels())
        && backupMeta.getLabels().containsKey(OperatorUtils.IGNORE_RECONCILER_ADD_LABEL)) {
      log.debug("Backup belongs to a backup schedule, ignoring");
      return;
    }
    if (backupMeta.getAnnotations() != null
        && backupMeta.getAnnotations().containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      log.debug("backup is already controlled by the operator, ignoring");
      return;
    }

    // Track the resource only after confirming it should actually be processed.
    KubernetesResourceDetails resourceDetails = KubernetesResourceDetails.fromResource(backup);
    resourceTracker.trackResource(backup);
    log.trace("Tracking resource {}, all tracked: {}", resourceDetails, getTrackedResources());

    log.info("Creating backup {} ", backup);
    BackupRequestParams backupRequestParams = null;
    try {
      backupRequestParams = operatorUtils.getBackupRequestFromCr(backup, scInformer);
    } catch (Exception e) {
      log.error("Got Exception in converting to backup params", e);
      return;
    }

    Customer cust;
    UUID customerUUID;
    try {
      cust = operatorUtils.getOperatorCustomer();
      customerUUID = cust.getUuid();
    } catch (Exception e) {
      log.error("Got Exception in getting customer", e);
      updateStatus(backup, "", "", "Failed in adding manual backup task" + e.getMessage());
      return;
    }
    // Update owner reference to last successful incremental backup for incremental backups case
    if (backupRequestParams.baseBackupUUID != null) {
      com.yugabyte.yw.models.Backup lastSuccessfulbackup =
          com.yugabyte.yw.models.Backup.getLastSuccessfulBackupInChain(
              customerUUID, backupRequestParams.baseBackupUUID);
      try {
        backupMeta.setOwnerReferences(
            Collections.singletonList(
                operatorUtils.getResourceOwnerReference(
                    lastSuccessfulbackup.getBackupInfo().getKubernetesResourceDetails(),
                    Backup.class)));
        backupMeta.setFinalizers(Collections.singletonList(OperatorUtils.YB_FINALIZER));
        resourceClient
            .inNamespace(backup.getMetadata().getNamespace())
            .withName(backup.getMetadata().getName())
            .patch(backup);
      } catch (Exception e) {
        log.error(
            "Got error in applying owner reference to backup: {} {}",
            backup.getMetadata().getName(),
            e);
        updateStatus(backup, "", "", "Failed in adding manual backup task" + e.getMessage());
        return;
      }
    }

    log.info("BackupRequestParams {}", backupRequestParams);
    log.info("Starting backup task..");
    UUID taskUUID = null;
    try {
      taskUUID = backupHelper.createBackupTask(customerUUID, backupRequestParams);
    } catch (Exception e) {
      log.error("Got Error in launching backup", e);
      updateStatus(backup, "", "", "Failed in adding manual backup task" + e.getMessage());
      return;
    }
    if (taskUUID != null) {
      JsonNode taskParamsJson = Json.toJson(backupRequestParams);
      log.info("BackupRequestParams post launch {}", taskParamsJson.toString());
    }
    updateStatus(backup, taskUUID.toString(), "", "Manual backup task");
  }

  @Override
  public void onUpdate(Backup oldBackup, Backup newBackup) {
    if (newBackup.getMetadata().getDeletionTimestamp() != null) {
      log.info("Backup has deletion timestamp set, treating as delete");
      handleDelete(newBackup);
      return;
    }
    // Persist the latest resource YAML so the OperatorResource table stays current.
    resourceTracker.trackResource(newBackup);
    log.info(
        "Got backup update {} {}, ignoring as backup does not support update.",
        oldBackup,
        newBackup);
  }

  @Override
  public void onDelete(Backup backup, boolean deletedFinalStateUnknown) {
    handleDelete(backup);
  }

  private void handleDelete(Backup backup) {
    log.info("Got backup delete {}", backup);
    KubernetesResourceDetails resourceDetails = KubernetesResourceDetails.fromResource(backup);
    Set<KubernetesResourceDetails> orphaned = resourceTracker.untrackResource(resourceDetails);
    log.info("Untracked backup {} and orphaned dependencies: {}", resourceDetails, orphaned);

    BackupStatus status = backup.getStatus();

    // Remove finalizer if no status
    if (status == null) {
      log.info("Doing nothing no task was launched");
      try {
        operatorUtils.removeFinalizer(backup, resourceClient);
      } catch (Exception ex) {
        log.error(
            "Got error removing finalizer for backup: {} {}", backup.getMetadata().getName(), ex);
      }
      return;
    }

    Customer cust;
    UUID customerUUID;
    try {
      cust = operatorUtils.getOperatorCustomer();
      customerUUID = cust.getUuid();
    } catch (Exception e) {
      log.error("Got Exception in getting customer, not deleting backup", e);
      return;
    }

    UUID taskUUID, backupUUID;
    com.yugabyte.yw.models.Backup bkp = null;
    try {
      taskUUID = UUID.fromString(status.getTaskUUID());
      backupUUID = UUID.fromString(status.getResourceUUID());
      bkp = com.yugabyte.yw.models.Backup.getOrBadRequest(customerUUID, backupUUID);
    } catch (Exception e) {
      // If the backup does not exist or wasn't created in the first place, remove the finalizer
      log.error("Got error in fetching backup: {} {}", backup.getMetadata().getName(), e);
      try {
        operatorUtils.removeFinalizer(backup, resourceClient);
      } catch (Exception ex) {
        log.error(
            "Got error removing finalizer for backup: {} {}", backup.getMetadata().getName(), ex);
      }
      return;
    }

    // Cancel backup if running
    try {
      boolean taskstatus = backupHelper.abortBackupTask(taskUUID);
      if (taskstatus) {
        log.info("cancelled ongoing task");
        BackupHelper.waitForTask(taskUUID);
      }
    } catch (Exception e) {
      log.info("Error while cancelling task {}", e.getMessage());
    }

    try {
      DeleteBackupInfo dbi = new DeleteBackupInfo();
      dbi.backupUUID = backupUUID;
      dbi.storageConfigUUID = bkp.getStorageConfigUUID();
      DeleteBackupParams dbp = new DeleteBackupParams();

      // Deleting backups by force.
      dbp.deleteBackupInfos = new ArrayList<DeleteBackupInfo>();
      dbp.deleteBackupInfos.add(dbi);
      dbp.deleteForcefully = true;

      backupHelper.createDeleteBackupTasks(customerUUID, dbp);
    } catch (Exception e) {
      log.error("Got error in deleting backup: {} {}", backup.getMetadata().getName(), e);
      return;
    }

    if (!bkp.isIncrementalBackup()) {
      // Remove finalizers for all backups in chain
      List<com.yugabyte.yw.models.Backup> backupList =
          com.yugabyte.yw.models.Backup.fetchAllBackupsByBaseBackupUUID(
              customerUUID, backupUUID, null);
      if (CollectionUtils.isNotEmpty(backupList)) {
        for (com.yugabyte.yw.models.Backup b : backupList) {
          if (b.getBackupInfo().getKubernetesResourceDetails() != null) {
            Backup crBackup =
                operatorUtils.getResource(
                    b.getBackupInfo().getKubernetesResourceDetails(), resourceClient, Backup.class);
            try {
              operatorUtils.removeFinalizer(crBackup, resourceClient);
            } catch (Exception e) {
              log.error("Got error removing finalizer", e);
            }
          }
        }
      }
    }
  }

  @Override
  public void run() {
    informer.addEventHandler(this);
    informer.run();
  }
}
