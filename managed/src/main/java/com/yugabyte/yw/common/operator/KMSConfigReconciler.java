// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.kms.KMSConfigHelper;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil.AwsKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.AzuEARServiceUtil.AzuKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.CiphertrustEARServiceUtil.CipherTrustKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.GcpEARServiceUtil.GcpKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil.OciKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue.ResourceAction;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.yugabyte.operator.v1alpha1.KMSConfig;
import io.yugabyte.operator.v1alpha1.KMSConfigStatus;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

/**
 * Reconciler for the KMSConfig custom resource. Drives create/edit/delete of KMS (encryption at
 * rest) configurations in YBA.
 *
 * <p>The reconciler is task-driven: create/edit/delete are submitted through {@link
 * KMSConfigHelper} and tracked by task UUID. The CR status ({@code resourceUUID}, {@code state},
 * {@code message}, {@code taskUUID}) is written back from the reconciler once tasks complete.
 *
 * <p>The HASHICORP (Vault), AWS, GCP, AZU (Azure Key Vault), CIPHERTRUST and OCI providers are
 * supported; other providers are gated behind {@link UnsupportedOperationException} placeholders in
 * {@link OperatorUtils} and surface as an {@code Error} state on the CR.
 */
@Slf4j
public class KMSConfigReconciler extends AbstractReconciler<KMSConfig> {

  // Providers implemented by the operator (create/edit/delete). Others surface as an Error state.
  private static final Set<KeyProvider> SUPPORTED_PROVIDERS = OperatorUtils.SUPPORTED_KMS_PROVIDERS;

  private final KMSConfigHelper kmsConfigHelper;
  private final Map<String, UUID> kmsConfigTaskMap;

  public KMSConfigReconciler(
      KMSConfigHelper kmsConfigHelper,
      String namespace,
      OperatorUtils operatorUtils,
      KubernetesClient client,
      YBInformerFactory informerFactory) {
    super(client, informerFactory, KMSConfig.class, operatorUtils, namespace);
    this.kmsConfigHelper = kmsConfigHelper;
    this.kmsConfigTaskMap = new HashMap<>();
  }

  @VisibleForTesting
  UUID getKMSConfigTaskMapValue(String key) {
    return this.kmsConfigTaskMap.getOrDefault(key, null);
  }

  // Handles 3 cases
  // Case 1: KMS config is not present: adopt an existing config with the same name or create
  // Case 2: KMS config is present and requires edit: submit an edit task
  // Case 3: KMS config is present and up to date: no-op
  @Override
  protected void createActionReconcile(KMSConfig kmsConfig, Customer cust) throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(kmsConfig.getMetadata());
    String kmsConfigName = kmsConfig.getSpec().getName();
    boolean resourceExists =
        kmsConfig.getStatus() != null && kmsConfig.getStatus().getResourceUUID() != null;

    if (!resourceExists) {
      // A KMS config with this name may already exist if the status was never written back, so
      // adopt it instead of creating a duplicate.
      Optional<KmsConfig> existing = findKmsConfigByName(cust, kmsConfigName);
      if (existing.isPresent()) {
        log.info("Adopting existing KMS config {} for CR {}", kmsConfigName, kmsConfigName);
        updateStatus(kmsConfig, existing.get().getConfigUUID(), "Ready", "KMS config ready");
        return;
      }
      // Skip if a create task is already in flight for this resource.
      if (getCurrentTaskInfo(kmsConfig) != null) {
        return;
      }
      log.info("Creating new KMS config {}", kmsConfigName);
      createKMSConfigTask(kmsConfig, cust);
    } else {
      UUID configUuid = UUID.fromString(kmsConfig.getStatus().getResourceUUID());
      OperatorUtils.maybeAddYbaResourceId(kmsConfig, configUuid, resourceClient);
      KmsConfig kmsConfigModel = KmsConfig.get(configUuid);
      if (kmsConfigModel != null && requiresEdit(kmsConfig, kmsConfigModel)) {
        createEditKMSConfigTask(kmsConfig, cust, kmsConfigModel);
      }
    }
  }

  // Handles the edit case: KMS config is present and requires edit with latest params.
  @Override
  protected void updateActionReconcile(KMSConfig kmsConfig, Customer cust) throws Exception {
    boolean resourceExists =
        kmsConfig.getStatus() != null && kmsConfig.getStatus().getResourceUUID() != null;
    if (!resourceExists) {
      log.debug("KMS config does not exist yet, ignoring update");
      return;
    }
    UUID configUuid = UUID.fromString(kmsConfig.getStatus().getResourceUUID());
    KmsConfig kmsConfigModel = KmsConfig.get(configUuid);
    if (kmsConfigModel == null) {
      log.debug("KMS config model not found, ignoring update");
      return;
    }
    if (requiresEdit(kmsConfig, kmsConfigModel)) {
      createEditKMSConfigTask(kmsConfig, cust, kmsConfigModel);
    }
  }

  // NO_OP reconcile handler
  // Case 1: Currently running task in incomplete state: NO_OP
  // Case 2: Currently running task in failed state: CREATE/UPDATE based on task type
  // Case 3: Currently running task in Success state: Reset retries, write status, queue UPDATE if
  //  required
  // Case 4: No current task and config not present: adopt-by-name or requeue CREATE
  // Case 5: No current task and config requires edit: Requeue UPDATE
  @Override
  protected void noOpActionReconcile(KMSConfig kmsConfig, Customer cust) throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(kmsConfig.getMetadata());
    String kmsConfigName = kmsConfig.getSpec().getName();
    boolean resourceExists =
        kmsConfig.getStatus() != null && kmsConfig.getStatus().getResourceUUID() != null;

    TaskInfo taskInfo = getCurrentTaskInfo(kmsConfig);
    if (taskInfo != null) {
      if (TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
        log.debug("NoOp Action: KMS config {} task in progress, requeuing no-op", kmsConfigName);
        workqueue.requeue(mapKey, ResourceAction.NO_OP, false /* incrementRetry */);
      } else if (taskInfo.getTaskState() != TaskInfo.State.Success) {
        kmsConfigTaskMap.remove(mapKey);
        String taskError = taskInfo.getErrorMessage();
        if (taskInfo.getTaskType() == TaskType.CreateKMSConfig) {
          log.debug(
              "NoOp Action: KMS config {} create task failed, requeuing Create", kmsConfigName);
          updateStatus(
              kmsConfig, null, "Error", errorMessage("KMS config creation failed", taskError));
          workqueue.requeue(mapKey, ResourceAction.CREATE, true /* incrementRetry */);
        } else {
          log.debug("NoOp Action: KMS config {} edit task failed, requeuing Update", kmsConfigName);
          updateStatus(kmsConfig, null, "Error", errorMessage("KMS config edit failed", taskError));
          workqueue.requeue(mapKey, ResourceAction.UPDATE, true /* incrementRetry */);
        }
      } else {
        // Task succeeded.
        workqueue.resetRetries(mapKey);
        kmsConfigTaskMap.remove(mapKey);
        Optional<KmsConfig> optKmsConfig = findKmsConfigByName(cust, kmsConfigName);
        if (optKmsConfig.isPresent()) {
          KmsConfig kmsConfigModel = optKmsConfig.get();
          updateStatus(kmsConfig, kmsConfigModel.getConfigUUID(), "Ready", "KMS config ready");
          if (requiresEdit(kmsConfig, kmsConfigModel)) {
            workqueue.requeue(mapKey, ResourceAction.UPDATE, false /* incrementRetry */);
          }
        }
      }
    } else if (!resourceExists) {
      Optional<KmsConfig> existing = findKmsConfigByName(cust, kmsConfigName);
      if (existing.isPresent()) {
        updateStatus(kmsConfig, existing.get().getConfigUUID(), "Ready", "KMS config ready");
      } else if (isSupportedProvider(kmsConfig)) {
        log.debug("NoOp Action: KMS config {} not found, requeuing Create", kmsConfigName);
        workqueue.requeue(mapKey, ResourceAction.CREATE, false /* incrementRetry */);
      } else {
        // Unsupported provider: report an error and stop requeuing to avoid a hot loop.
        log.warn("KMS config {} provider is not supported by the operator", kmsConfigName);
        updateStatus(
            kmsConfig,
            null,
            "Error",
            "KMS provider is not yet supported via the Kubernetes operator");
      }
    } else {
      UUID configUuid = UUID.fromString(kmsConfig.getStatus().getResourceUUID());
      KmsConfig kmsConfigModel = KmsConfig.get(configUuid);
      if (kmsConfigModel == null) {
        log.debug("NoOp Action: KMS config {} model missing, requeuing Create", kmsConfigName);
        workqueue.requeue(mapKey, ResourceAction.CREATE, false /* incrementRetry */);
      } else if (requiresEdit(kmsConfig, kmsConfigModel)) {
        workqueue.requeue(mapKey, ResourceAction.UPDATE, false /* incrementRetry */);
      } else {
        workqueue.resetRetries(mapKey);
      }
    }
  }

  // Delete the KMS config
  @Override
  protected void handleResourceDeletion(
      KMSConfig kmsConfig, Customer cust, OperatorWorkQueue.ResourceAction action)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(kmsConfig.getMetadata());
    boolean resourceExists =
        kmsConfig.getStatus() != null && kmsConfig.getStatus().getResourceUUID() != null;

    boolean kmsConfigRemoved = true;
    if (resourceExists) {
      UUID configUuid = UUID.fromString(kmsConfig.getStatus().getResourceUUID());
      KmsConfig kmsConfigModel = KmsConfig.get(configUuid);
      if (kmsConfigModel != null) {
        try {
          TaskInfo taskInfo = getCurrentTaskInfo(kmsConfig);
          if (taskInfo != null) {
            if (!TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())
                && taskInfo.getTaskState() != TaskInfo.State.Success) {
              // Delete task failed, retry.
              kmsConfigTaskMap.remove(mapKey);
              workqueue.requeue(mapKey, ResourceAction.DELETE, true /* incrementRetry */);
              return;
            }
          } else if (EncryptionAtRestUtil.configInUse(configUuid)) {
            log.warn(
                "KMS config {} is in use, cannot delete; will retry", kmsConfigModel.getName());
            updateStatus(
                kmsConfig, configUuid, "InUse", "KMS config is in use and cannot be deleted");
            workqueue.requeue(mapKey, ResourceAction.DELETE, false /* incrementRetry */);
            return;
          } else {
            UUID taskUUID =
                kmsConfigHelper.deleteKMSConfig(
                    cust.getUuid(),
                    configUuid,
                    kmsConfigModel.getKeyProvider(),
                    KubernetesResourceDetails.fromResource(kmsConfig));
            if (taskUUID != null) {
              kmsConfigTaskMap.put(mapKey, taskUUID);
            }
          }
          kmsConfigRemoved = KmsConfig.get(configUuid) == null;
          if (!kmsConfigRemoved) {
            workqueue.requeue(mapKey, ResourceAction.DELETE, false /* incrementRetry */);
            return;
          }
        } catch (Exception e) {
          log.error("Failed to delete KMS config, will retry", e);
          return;
        }
      }
    }

    log.info(
        "Removing finalizer for KMSConfig {} in namespace {}",
        kmsConfig.getMetadata().getName(),
        kmsConfig.getMetadata().getNamespace());
    try {
      operatorUtils.removeFinalizer(kmsConfig, resourceClient);
      kmsConfigTaskMap.remove(mapKey);
      workqueue.clearState(mapKey);
    } catch (Exception e) {
      log.error("Removing finalizer failed, will retry", e);
    }
  }

  private UUID createKMSConfigTask(KMSConfig kmsConfig, Customer customer) throws Exception {
    try {
      ObjectMeta objectMeta = kmsConfig.getMetadata();
      if (CollectionUtils.isEmpty(objectMeta.getFinalizers())) {
        objectMeta.setFinalizers(Collections.singletonList(OperatorUtils.YB_FINALIZER));
        resourceClient
            .inNamespace(objectMeta.getNamespace())
            .withName(objectMeta.getName())
            .patch(kmsConfig);
      }
      KeyProvider provider = operatorUtils.getKMSConfigProvider(kmsConfig);
      ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);
      UUID taskUUID =
          kmsConfigHelper.createKMSConfig(
              customer.getUuid(),
              provider,
              formData,
              KubernetesResourceDetails.fromResource(kmsConfig));
      log.debug("KMS config creation triggered with task: {}", taskUUID);
      if (taskUUID != null) {
        kmsConfigTaskMap.put(OperatorWorkQueue.getWorkQueueKey(kmsConfig.getMetadata()), taskUUID);
        updateStatus(kmsConfig, null, "Creating", "KMS config creation in progress");
      }
      return taskUUID;
    } catch (UnsupportedOperationException e) {
      log.warn(
          "KMS config {} not supported by operator: {}", objectName(kmsConfig), e.getMessage());
      updateStatus(kmsConfig, null, "Error", e.getMessage());
      return null;
    } catch (Exception e) {
      log.error("Failed to create KMS config: {}", objectName(kmsConfig), e);
      throw e;
    }
  }

  private UUID createEditKMSConfigTask(KMSConfig kmsConfig, Customer customer, KmsConfig model)
      throws Exception {
    KeyProvider provider = operatorUtils.getKMSConfigProvider(kmsConfig);
    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);
    reconcileGeneratedFields(provider, formData, model);
    UUID taskUUID =
        kmsConfigHelper.editKMSConfig(
            customer.getUuid(),
            model.getConfigUUID(),
            provider,
            model.getName(),
            formData,
            KubernetesResourceDetails.fromResource(kmsConfig));
    log.debug("KMS config edit triggered with task: {}", taskUUID);
    if (taskUUID != null) {
      kmsConfigTaskMap.put(OperatorWorkQueue.getWorkQueueKey(kmsConfig.getMetadata()), taskUUID);
      updateStatus(kmsConfig, model.getConfigUUID(), "Editing", "KMS config edit in progress");
    }
    return taskUUID;
  }

  // The edit replaces the stored auth config, so anything only YBA knows about must be copied
  // over. For AWS that is cmk_id: if the CR has no cmkID, YBA created the CMK, and losing the id
  // here would make the edit create a second one. cmk_policy only matters while creating a CMK.
  private void reconcileGeneratedFields(
      KeyProvider provider, ObjectNode formData, KmsConfig model) {
    if (provider != KeyProvider.AWS) {
      return;
    }
    ObjectNode current = EncryptionAtRestUtil.getAuthConfig(model.getConfigUUID());
    if (current == null) {
      return;
    }
    String cmkIdField = AwsKmsAuthConfigField.CMK_ID.fieldName;
    if (!formData.hasNonNull(cmkIdField) && current.hasNonNull(cmkIdField)) {
      formData.set(cmkIdField, current.get(cmkIdField));
    }
    formData.remove(AwsKmsAuthConfigField.CMK_POLICY.fieldName);
  }

  // Only editable fields are compared. Unsupported providers report no edit.
  private boolean requiresEdit(KMSConfig kmsConfig, KmsConfig model) {
    KeyProvider provider = operatorUtils.getKMSConfigProvider(kmsConfig);
    if (!SUPPORTED_PROVIDERS.contains(provider)) {
      return false;
    }
    ObjectNode desired = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);
    ObjectNode current = EncryptionAtRestUtil.getAuthConfig(model.getConfigUUID());
    if (current == null) {
      return false;
    }
    switch (provider) {
      case HASHICORP:
        return fieldChanged(desired, current, HashicorpVaultConfigParams.HC_VAULT_TOKEN)
            || fieldChanged(desired, current, HashicorpVaultConfigParams.HC_VAULT_ROLE_ID)
            || fieldChanged(desired, current, HashicorpVaultConfigParams.HC_VAULT_SECRET_ID)
            || fieldChanged(desired, current, HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE);
      case AWS:
        // useIAMProfile toggles are captured via the presence/absence of the access-key fields.
        return fieldChanged(desired, current, AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName)
            || fieldChanged(desired, current, AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName)
            || fieldChanged(desired, current, AwsKmsAuthConfigField.ENDPOINT.fieldName);
      case GCP:
        // Only the service account credentials JSON is editable for GCP. It is an object node, so
        // it must be compared structurally rather than via fieldChanged (which uses asText()).
        return nodeChanged(desired, current, GcpKmsAuthConfigField.GCP_CONFIG.fieldName);
      case AZU:
        // A missing client secret means managed identity is in use; the client secret field then
        // drops out of the auth config, so a toggle in either direction is a field change.
        return fieldChanged(desired, current, AzuKmsAuthConfigField.CLIENT_ID.fieldName)
            || fieldChanged(desired, current, AzuKmsAuthConfigField.CLIENT_SECRET.fieldName)
            || fieldChanged(desired, current, AzuKmsAuthConfigField.TENANT_ID.fieldName);
      case CIPHERTRUST:
        // The auth type and its credentials are editable; the manager URL and key settings are not.
        return fieldChanged(desired, current, CipherTrustKmsAuthConfigField.AUTH_TYPE.fieldName)
            || fieldChanged(desired, current, CipherTrustKmsAuthConfigField.USERNAME.fieldName)
            || fieldChanged(desired, current, CipherTrustKmsAuthConfigField.PASSWORD.fieldName)
            || fieldChanged(
                desired, current, CipherTrustKmsAuthConfigField.REFRESH_TOKEN.fieldName);
      case OCI:
        // The API-key credentials (user/tenancy/fingerprint/private key) and compartment are
        // editable; region, vault, key name, key OCID and the auth type are not.
        return fieldChanged(desired, current, OciKmsAuthConfigField.ociUserId.fieldName)
            || fieldChanged(desired, current, OciKmsAuthConfigField.ociTenancyId.fieldName)
            || fieldChanged(desired, current, OciKmsAuthConfigField.ociFingerprint.fieldName)
            || fieldChanged(desired, current, OciKmsAuthConfigField.ociPrivateKeyContent.fieldName)
            || fieldChanged(desired, current, OciKmsAuthConfigField.ociCompartmentId.fieldName);
      default:
        return false;
    }
  }

  // Appends the underlying task error to the status message when one is available.
  private static String errorMessage(String prefix, String taskError) {
    if (taskError == null || taskError.isBlank()) {
      return prefix;
    }
    return prefix + ": " + taskError;
  }

  private static boolean fieldChanged(ObjectNode desired, ObjectNode current, String field) {
    String desiredVal = desired.hasNonNull(field) ? desired.get(field).asText() : null;
    String currentVal = current.hasNonNull(field) ? current.get(field).asText() : null;
    return !Objects.equals(desiredVal, currentVal);
  }

  // Structural comparison for object/array-valued fields (e.g. the GCP credentials JSON), where
  // fieldChanged's asText() would collapse to an empty string.
  private static boolean nodeChanged(ObjectNode desired, ObjectNode current, String field) {
    JsonNode desiredVal = desired.hasNonNull(field) ? desired.get(field) : null;
    JsonNode currentVal = current.hasNonNull(field) ? current.get(field) : null;
    return !Objects.equals(desiredVal, currentVal);
  }

  private boolean isSupportedProvider(KMSConfig kmsConfig) {
    try {
      return SUPPORTED_PROVIDERS.contains(operatorUtils.getKMSConfigProvider(kmsConfig));
    } catch (Exception e) {
      return false;
    }
  }

  private Optional<KmsConfig> findKmsConfigByName(Customer cust, String name) {
    return KmsConfig.listKMSConfigs(cust.getUuid()).stream()
        .filter(config -> config.getName().equals(name))
        .findFirst();
  }

  private TaskInfo getCurrentTaskInfo(KMSConfig kmsConfig) {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(kmsConfig.getMetadata());
    UUID taskUUID = kmsConfigTaskMap.get(mapKey);
    if (taskUUID != null) {
      Optional<TaskInfo> optTaskInfo = TaskInfo.maybeGet(taskUUID);
      if (optTaskInfo.isPresent()) {
        return optTaskInfo.get();
      }
    }
    return null;
  }

  private void updateStatus(KMSConfig kmsConfig, UUID configUUID, String state, String message) {
    try {
      KMSConfig latest =
          resourceClient
              .inNamespace(kmsConfig.getMetadata().getNamespace())
              .withName(kmsConfig.getMetadata().getName())
              .get();
      if (latest == null) {
        return;
      }
      KMSConfigStatus status = latest.getStatus();
      if (status == null) {
        status = new KMSConfigStatus();
      }
      status.setState(state);
      status.setMessage(message);
      if (configUUID != null) {
        status.setResourceUUID(configUUID.toString());
      }
      UUID taskUUID =
          kmsConfigTaskMap.get(OperatorWorkQueue.getWorkQueueKey(kmsConfig.getMetadata()));
      if (taskUUID != null) {
        status.setTaskUUID(taskUUID.toString());
      }
      latest.setStatus(status);
      resourceClient
          .inNamespace(latest.getMetadata().getNamespace())
          .resource(latest)
          .updateStatus();
      log.debug("Updated status for KMS config CR {}", latest.getMetadata().getName());
    } catch (Exception e) {
      log.error("Exception updating KMS config CR status", e);
    }
  }

  private static String objectName(KMSConfig kmsConfig) {
    return kmsConfig.getMetadata().getName();
  }
}
