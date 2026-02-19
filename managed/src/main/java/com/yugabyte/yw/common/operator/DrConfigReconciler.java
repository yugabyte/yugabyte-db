package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.dr.DrConfigHelper;
import com.yugabyte.yw.common.dr.DrConfigHelper.DrConfigTaskResult;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.models.Customer;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.yugabyte.operator.v1alpha1.DrConfig;
import io.yugabyte.operator.v1alpha1.DrConfigStatus;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class DrConfigReconciler implements ResourceEventHandler<DrConfig>, Runnable {
  private final SharedIndexInformer<DrConfig> informer;
  private final MixedOperation<DrConfig, KubernetesResourceList<DrConfig>, Resource<DrConfig>>
      resourceClient;
  private final DrConfigHelper drConfigHelper;
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

  public DrConfigReconciler(
      SharedIndexInformer<DrConfig> drConfigInformer,
      MixedOperation<DrConfig, KubernetesResourceList<DrConfig>, Resource<DrConfig>> resourceClient,
      DrConfigHelper drConfigHelper,
      String namespace,
      SharedIndexInformer<StorageConfig> scInformer,
      OperatorUtils operatorUtils) {
    this.resourceClient = resourceClient;
    this.informer = drConfigInformer;
    this.namespace = namespace;
    this.drConfigHelper = drConfigHelper;
    this.scInformer = scInformer;
    this.operatorUtils = operatorUtils;
  }

  private void updateStatus(
      DrConfig drConfig, String taskUUID, String drConfigUUID, String message) {
    DrConfigStatus status = drConfig.getStatus();
    if (status == null) {
      status = new DrConfigStatus();
    }
    status.setMessage(message);
    // Don't override the Dr Config resource and task UUID once set.
    if (status.getResourceUUID() == null) {
      status.setResourceUUID(drConfigUUID);
    }
    if (status.getTaskUUID() == null) {
      status.setTaskUUID(taskUUID);
    }
    drConfig.setStatus(status);

    resourceClient.inNamespace(namespace).resource(drConfig).updateStatus();
  }

  @Override
  public void onAdd(DrConfig drConfig) {
    DrConfigStatus status = drConfig.getStatus();
    if (status != null && status.getResourceUUID() != null) {
      log.info("Early return because DR Config is already initialized");
      return;
    }

    KubernetesResourceDetails resourceDetails = KubernetesResourceDetails.fromResource(drConfig);
    resourceTracker.trackResource(drConfig);
    log.trace("Tracking resource {}, all tracked: {}", resourceDetails, getTrackedResources());

    log.debug("Creating DR config: {}", drConfig);

    try {
      DrConfigCreateForm drConfigCreateForm =
          operatorUtils.getDrConfigCreateFormFromCr(drConfig, scInformer);

      Customer cust = operatorUtils.getOperatorCustomer();
      UUID customerUUID = cust.getUuid();

      log.debug("DrConfigCreateForm: {}", drConfigCreateForm);
      log.info("Starting DR config create task");

      DrConfigTaskResult result =
          drConfigHelper.createDrConfigTask(customerUUID, drConfigCreateForm);
      UUID taskUUID = result.taskUuid();

      JsonNode drConfigCreateJson = Json.toJson(drConfigCreateForm);
      log.info("drConfigCreateJson post launch: {}", drConfigCreateJson.toString());
      updateStatus(drConfig, taskUUID.toString(), "", "Create DR config task");

    } catch (Exception e) {
      log.error("Failed to create DR config task", e);
      updateStatus(drConfig, "", "", "Failed to create DR config task: " + e.getMessage());
    }
  }

  @Override
  public void onUpdate(DrConfig oldDrConfig, DrConfig newDrConfig) {
    if (newDrConfig.getMetadata().getDeletionTimestamp() != null) {
      log.info("DR config has deletion timestamp set, treating as delete");
      handleDelete(newDrConfig);
      return;
    }
    // Persist the latest resource YAML so the OperatorResource table stays current.
    resourceTracker.trackResource(newDrConfig);

    List<String> oldDbs =
        oldDrConfig.getSpec() != null ? oldDrConfig.getSpec().getDatabases() : null;
    List<String> newDbs =
        newDrConfig.getSpec() != null ? newDrConfig.getSpec().getDatabases() : null;
    if (Objects.equals(oldDbs, newDbs)) {
      log.info("The list of databases didn't change, there is nothing to update");
      return;
    }

    log.info("Editing DR config: {}", newDrConfig);

    DrConfigStatus status = newDrConfig.getStatus();

    // Remove finalizer if no status
    if (status == null) {
      log.info("Doing nothing, no task was launched");
      try {
        operatorUtils.removeFinalizer(newDrConfig, resourceClient);
      } catch (Exception e) {
        log.error(
            "Failed to remove finalizer for DR config: {}", newDrConfig.getSpec().getName(), e);
      }
      return;
    }

    try {
      UUID drConfigUUID = UUID.fromString(status.getResourceUUID());

      DrConfigSetDatabasesForm drConfigSetDatabasesForm =
          operatorUtils.getDrConfigSetDatabasesFormFromCr(newDrConfig, scInformer);

      Customer cust = operatorUtils.getOperatorCustomer();
      UUID customerUUID = cust.getUuid();

      log.info("DrConfigSetDatabasesForm: {}", drConfigSetDatabasesForm);
      log.info("Starting DR config set databases task");

      UUID taskUUID =
          drConfigHelper.setDatabasesTask(customerUUID, drConfigUUID, drConfigSetDatabasesForm);

      JsonNode drConfigSetDatabasesJson = Json.toJson(drConfigSetDatabasesForm);
      log.info("drConfigSetDatabasesJson post launch: {}", drConfigSetDatabasesJson.toString());
      updateStatus(newDrConfig, taskUUID.toString(), "", "Set databases DR config task");

    } catch (Exception e) {
      log.error("Failed to update DR config databases", e);
      updateStatus(newDrConfig, "", "", "Failed to update DR config databases: " + e.getMessage());
    }
  }

  @Override
  public void onDelete(DrConfig drConfig, boolean deletedFinalStateUnknown) {
    handleDelete(drConfig);
  }

  private void handleDelete(DrConfig drConfig) {
    log.info("Got DR config delete: {}", drConfig);
    KubernetesResourceDetails resourceDetails = KubernetesResourceDetails.fromResource(drConfig);
    Set<KubernetesResourceDetails> orphaned = resourceTracker.untrackResource(resourceDetails);
    log.info("Untracked DR config {} and orphaned dependencies: {}", resourceDetails, orphaned);

    DrConfigStatus status = drConfig.getStatus();

    // Remove finalizer if no status
    if (status == null) {
      log.info("Doing nothing, no task was launched");
      try {
        operatorUtils.removeFinalizer(drConfig, resourceClient);
      } catch (Exception e) {
        log.error("Failed to remove finalizer for DR config: {}", drConfig.getSpec().getName(), e);
      }
      return;
    }

    try {
      Customer cust = operatorUtils.getOperatorCustomer();
      UUID customerUUID = cust.getUuid();

      UUID taskUUID = UUID.fromString(status.getTaskUUID());
      UUID drConfigUUID = UUID.fromString(status.getResourceUUID());
      com.yugabyte.yw.models.DrConfig drc =
          com.yugabyte.yw.models.DrConfig.getOrBadRequest(drConfigUUID);

      // Cancel DR config task if running
      try {
        boolean taskStatus = drConfigHelper.abortDrConfigTask(taskUUID);
        if (taskStatus) {
          log.info("Cancelled ongoing task");
          drConfigHelper.waitForTask(taskUUID);
        }
      } catch (Exception e) {
        log.warn("Error while cancelling task: {}", e.getMessage());
      }

      drConfigHelper.deleteDrConfigTask(customerUUID, drConfigUUID, true /* forceDelete */);

      if (drc.getKubernetesResourceDetails() != null) {
        DrConfig crDrConfig =
            operatorUtils.getResource(
                drc.getKubernetesResourceDetails(), resourceClient, DrConfig.class);
        try {
          operatorUtils.removeFinalizer(crDrConfig, resourceClient);
        } catch (Exception e) {
          log.error("Failed to remove finalizer", e);
        }
      }

    } catch (Exception e) {
      log.error("Failed to delete DR config", e);
      try {
        operatorUtils.removeFinalizer(drConfig, resourceClient);
      } catch (Exception ex) {
        log.error("Failed to remove finalizer for DR config: {}", drConfig.getSpec().getName(), ex);
      }
    }
  }

  @Override
  public void run() {
    informer.addEventHandler(this);
    informer.run();
  }
}
