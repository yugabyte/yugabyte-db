package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.DeleteBackupParams;
import com.yugabyte.yw.forms.DeleteBackupParams.DeleteBackupInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.Backup;
import io.yugabyte.operator.v1alpha1.BackupStatus;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
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

  public BackupReconciler(
      SharedIndexInformer<Backup> backupInformer,
      MixedOperation<Backup, KubernetesResourceList<Backup>, Resource<Backup>> resourceClient,
      BackupHelper backupHelper,
      ValidatingFormFactory formFactory,
      String namespace,
      SharedIndexInformer<StorageConfig> scInformer) {
    this.resourceClient = resourceClient;
    this.informer = backupInformer;
    this.lister = new Lister<>(informer.getIndexer());
    this.backupHelper = backupHelper;
    this.formFactory = formFactory;
    this.namespace = namespace;
    this.scInformer = scInformer;
  }

  private void updateStatus(Backup backup, String taskUUID, String backupUUID, String message) {
    BackupStatus status = backup.getStatus();
    if (status == null) {
      status = new BackupStatus();
    }
    status.setMessage(message);
    status.setResourceUUID(backupUUID);
    status.setTaskUUID(taskUUID);
    backup.setStatus(status);

    resourceClient.inNamespace(namespace).resource(backup).replaceStatus();
  }

  public UUID getUniverseUUIDFromName(Long customerId, String universeName) {
    Optional<Universe> universe = Universe.maybeGetUniverseByName(customerId, universeName);
    UUID universeUUID;
    if (universe.isPresent()) {
      return universe.get().getUniverseUUID();
    }
    return null;
  }

  public UUID getStorageConfigUUIDFromName(String scName) {

    Lister<StorageConfig> scLister = new Lister<>(this.scInformer.getIndexer());
    List<StorageConfig> storageConfigs = scLister.list();

    for (StorageConfig storageConfig : storageConfigs) {
      if (storageConfig.getMetadata().getName().equals(scName)) {
        return UUID.fromString(storageConfig.getStatus().getResourceUUID());
      }
    }
    return null;
  }

  public BackupRequestParams getBackupTaskParamsFromCr(Backup backup) {
    // Convert the Java object to JsonNode
    ObjectMapper objectMapper = new ObjectMapper();
    JsonNode crJsonNode = objectMapper.valueToTree(backup.getSpec());
    List<Customer> custList = Customer.getAll();
    Customer cust = custList.get(0);

    log.info("CRSPECJSON {}", crJsonNode);

    UUID universeUUID = getUniverseUUIDFromName(cust.getId(), backup.getSpec().getUniverse());
    UUID storageConfigUUID = getStorageConfigUUIDFromName(backup.getSpec().getStorageConfig());

    ((ObjectNode) crJsonNode).put("universeUUID", universeUUID.toString());
    ((ObjectNode) crJsonNode).put("storageConfigUUID", storageConfigUUID.toString());

    return formFactory.getFormDataOrBadRequest(crJsonNode, BackupRequestParams.class);
  }

  @Override
  public void onAdd(Backup backup) {
    log.info("Creating backup {} ", backup);
    BackupRequestParams backupRequestParams = null;
    try {
      backupRequestParams = getBackupTaskParamsFromCr(backup);
    } catch (Exception e) {
      log.error("Got Exception in converting to backup params {}", e);
    }
    List<Customer> custList = Customer.getAll();
    Customer cust = custList.get(0);
    UUID customerUUID = cust.getUuid();
    log.info("BackupRequestParams {}", backupRequestParams);
    log.info("Starting backup task..");
    UUID taskUUID = null;
    try {
      taskUUID = backupHelper.createBackupTask(customerUUID, backupRequestParams);
    } catch (Exception e) {
      log.error("Got Error in launching backup {}", e);
      updateStatus(backup, "", "", "Failed in scheduling backup task" + e.getMessage());
      return;
    }
    if (taskUUID != null) {
      JsonNode taskParamsJson = Json.toJson(backupRequestParams);
      log.info("BackupRequestParams post launch {}", taskParamsJson.toString());
    }
    updateStatus(backup, taskUUID.toString(), "", "scheduled backup task");
  }

  @Override
  public void onUpdate(Backup oldBackup, Backup newBackup) {
    log.info("Got backup update {} {}", oldBackup, newBackup);
  }

  @Override
  public void onDelete(Backup backup, boolean deletedFinalStateUnknown) {
    log.info("Got backup delete {}", backup);
    BackupStatus status = backup.getStatus();

    if (status == null) {
      log.info("Doing nothing no task was launched");
      return;
    }

    UUID taskUUID;
    try {
      taskUUID = UUID.fromString(status.getTaskUUID());
    } catch (IllegalArgumentException e) {
      log.info("No task uuid found {} ", e);
      return;
    }

    boolean taskstatus = backupHelper.abortBackupTask(taskUUID);
    if (taskstatus == true) {
      log.info("cancelled ongoing task");
      try {
        backupHelper.waitForTask(taskUUID);
      } catch (Exception e) {
        log.info("Error while cancelling task {}", e.getMessage());
      }
    }

    List<UUID> backupUUIDList = backupHelper.getBackupUUIDList(taskUUID);
    // This should only be a single element here.
    if (backupUUIDList.isEmpty()) {
      log.info("Returing early no backups were created");
    }

    DeleteBackupInfo dbi = new DeleteBackupInfo();
    for (UUID backupUUID : backupUUIDList) {
      dbi.backupUUID = backupUUID;
      dbi.storageConfigUUID = UUID.fromString("4384bf15-c1be-448d-b78c-2411fec2b2a8");
    }
    DeleteBackupParams dbp = new DeleteBackupParams();

    // Deleting backups by force.
    dbp.deleteBackupInfos = new ArrayList<DeleteBackupInfo>();
    dbp.deleteBackupInfos.add(dbi);
    dbp.deleteForcefully = true;
    List<Customer> custList = Customer.getAll();
    Customer cust = custList.get(0);
    UUID customerUUID = cust.getUuid();
    backupHelper.createDeleteBackupTasks(customerUUID, dbp);
  }

  @Override
  public void run() {
    informer.addEventHandler(this);
    informer.run();
  }
}
