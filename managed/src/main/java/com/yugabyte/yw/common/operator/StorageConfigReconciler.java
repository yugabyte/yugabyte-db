package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.PropertyNamingStrategies;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.ResourceAnnotationKeys;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.StorageConfigStatus;
import io.yugabyte.operator.v1alpha1.storageconfigspec.AwsSecretAccessKeySecret;
import io.yugabyte.operator.v1alpha1.storageconfigspec.AzureStorageSasTokenSecret;
import io.yugabyte.operator.v1alpha1.storageconfigspec.Data;
import io.yugabyte.operator.v1alpha1.storageconfigspec.GcsCredentialsJsonSecret;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class StorageConfigReconciler implements ResourceEventHandler<StorageConfig>, Runnable {
  public static final String AWS_SECRET_ACCESS_KEY_SECRET_KEY = "awsSecretAccessKey";
  public static final String GCS_CREDENTIALS_JSON_SECRET_KEY = "gcsCredentialsJson";
  public static final String AZURE_STORAGE_SAS_TOKEN_SECRET_KEY = "azureStorageSasToken";

  private final SharedIndexInformer<StorageConfig> informer;
  private final Lister<StorageConfig> lister;
  private final MixedOperation<
          StorageConfig, KubernetesResourceList<StorageConfig>, Resource<StorageConfig>>
      resourceClient;
  private final CustomerConfigService ccs;
  private final String namespace;
  private final OperatorUtils operatorUtils;

  private final ResourceTracker resourceTracker = new ResourceTracker();

  // The current storage config resource being reconciled, used for associating secret dependencies.
  private KubernetesResourceDetails currentReconcileResource;

  public Set<KubernetesResourceDetails> getTrackedResources() {
    return resourceTracker.getTrackedResources();
  }

  public ResourceTracker getResourceTracker() {
    return resourceTracker;
  }

  public StorageConfigReconciler(
      SharedIndexInformer<StorageConfig> scInformer,
      MixedOperation<StorageConfig, KubernetesResourceList<StorageConfig>, Resource<StorageConfig>>
          resourceClient,
      CustomerConfigService ccs,
      String namespace,
      OperatorUtils operatorUtils) {
    this.resourceClient = resourceClient;
    this.informer = scInformer;
    this.lister = new Lister<>(informer.getIndexer());
    this.ccs = ccs;
    this.namespace = namespace;
    this.operatorUtils = operatorUtils;
  }

  public JsonNode getConfigPayloadFromCRD(StorageConfig sc) {
    ObjectMapper objectMapper = new ObjectMapper();
    objectMapper.configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true);

    objectMapper.configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    objectMapper.configure(SerializationFeature.WRITE_NULL_MAP_VALUES, false);
    // Convert to upper snake case for the customer config
    objectMapper.setPropertyNamingStrategy(PropertyNamingStrategies.UPPER_SNAKE_CASE);

    ObjectNode payload = objectMapper.createObjectNode();

    Data data = sc.getSpec().getData();
    JsonNode dataJson = objectMapper.valueToTree(data);
    ObjectNode object = (ObjectNode) dataJson;

    boolean useIAM = object.has("USE_IAM") && (object.get("USE_IAM").asBoolean(false) == true);
    object.remove("USE_IAM");

    String configType = sc.getSpec().getConfig_type().getValue().split("_")[1];
    if (useIAM) {
      String iamFieldName;
      if (configType.equals(CustomerConfigConsts.NAME_S3)) {
        iamFieldName = CustomerConfigConsts.USE_S3_IAM_FIELDNAME;
      } else if (configType.equals(CustomerConfigConsts.NAME_GCS)) {
        iamFieldName = CustomerConfigConsts.USE_GCP_IAM_FIELDNAME;
      } else if (configType.equals(CustomerConfigConsts.NAME_AZURE)) {
        iamFieldName = CustomerConfigConsts.USE_AZURE_IAM_FIELDNAME;
      } else {
        throw new RuntimeException(
            String.format("IAM only works with S3/GCS/AZ but %s config type used", configType));
      }
      object.put(iamFieldName, useIAM);
    }
    parseSecrets(object, sc);

    return dataJson;
  }

  private void updateStatus(StorageConfig sc, boolean success, String configUUID, String message) {
    StorageConfigStatus status = sc.getStatus();
    if (status == null) {
      status = new StorageConfigStatus();
    }
    status.setSuccess(success);
    status.setMessage(message);
    UUID currentconfigUUID = null;
    if (status.getResourceUUID() != null) {
      try {
        currentconfigUUID = UUID.fromString(status.getResourceUUID());
      } catch (Exception e) {
        // ignore invalid UUID
      }
    }
    // Don't overwrite configUUID once set.
    if (currentconfigUUID == null) {
      status.setResourceUUID(configUUID);
    }

    sc.setStatus(status);
    resourceClient.inNamespace(namespace).resource(sc).replaceStatus();
  }

  private void parseSecrets(ObjectNode configObject, StorageConfig sc) {
    AwsSecretAccessKeySecret awsSecret = sc.getSpec().getAwsSecretAccessKeySecret();
    if (awsSecret != null) {
      Secret secret = operatorUtils.getSecret(awsSecret.getName(), awsSecret.getNamespace());
      if (secret != null) {
        resourceTracker.trackDependency(currentReconcileResource, secret);
        log.trace(
            "Tracking AWS secret {} as dependency of {}",
            secret.getMetadata().getName(),
            currentReconcileResource);
        String awsSecretKey =
            operatorUtils.parseSecretForKey(secret, AWS_SECRET_ACCESS_KEY_SECRET_KEY);
        configObject.put("AWS_SECRET_ACCESS_KEY", awsSecretKey);
      } else {
        log.warn("AWS secret access key secret {} not found", awsSecret.getName());
      }
    }

    GcsCredentialsJsonSecret gcsSecret = sc.getSpec().getGcsCredentialsJsonSecret();
    if (gcsSecret != null) {
      Secret secret = operatorUtils.getSecret(gcsSecret.getName(), gcsSecret.getNamespace());
      if (secret != null) {
        resourceTracker.trackDependency(currentReconcileResource, secret);
        log.trace(
            "Tracking GCS secret {} as dependency of {}",
            secret.getMetadata().getName(),
            currentReconcileResource);
        String gcsSecretKey =
            operatorUtils.parseSecretForKey(secret, GCS_CREDENTIALS_JSON_SECRET_KEY);
        configObject.put("GCS_CREDENTIALS_JSON", gcsSecretKey);
      } else {
        log.warn("GCS credentials json secret {} not found", gcsSecret.getName());
      }
    }

    AzureStorageSasTokenSecret azureSecret = sc.getSpec().getAzureStorageSasTokenSecret();
    if (azureSecret != null) {
      Secret secret = operatorUtils.getSecret(azureSecret.getName(), azureSecret.getNamespace());
      if (secret != null) {
        resourceTracker.trackDependency(currentReconcileResource, secret);
        log.trace(
            "Tracking Azure secret {} as dependency of {}",
            secret.getMetadata().getName(),
            currentReconcileResource);
        String azureSecretKey =
            operatorUtils.parseSecretForKey(secret, AZURE_STORAGE_SAS_TOKEN_SECRET_KEY);
        configObject.put("AZURE_STORAGE_SAS_TOKEN", azureSecretKey);
      } else {
        log.warn("Azure storage sas token secret {} not found", azureSecret.getName());
      }
    }
  }

  @Override
  public void onAdd(StorageConfig sc) {
    if (sc.getStatus() != null) {
      if (sc.getStatus().getResourceUUID() != null) {
        log.info("Early return because Storage Config is already initialized");
        return;
      }
    }

    KubernetesResourceDetails resourceDetails = KubernetesResourceDetails.fromResource(sc);
    resourceTracker.trackResource(sc);
    currentReconcileResource = resourceDetails;
    log.trace("Tracking resource {}, all tracked: {}", resourceDetails, getTrackedResources());

    String cuuid;
    String value = sc.getSpec().getConfig_type().getValue();
    String name = value.split("_")[1];
    log.info("Adding a storage config {} ", name);
    try {
      cuuid = operatorUtils.getCustomerUUID();
    } catch (Exception e) {
      log.info("Failed adding storageconfig {}", sc.getMetadata().getName());
      updateStatus(sc, false, "", e.getMessage());
      return;
    }
    String configUUID;
    try {
      JsonNode payload = getConfigPayloadFromCRD(sc);
      String configName = sc.getMetadata().getName();
      if (sc.getSpec().getName() != null) {
        configName = OperatorUtils.kubernetesCompatName(sc.getSpec().getName());
      }
      if (sc.getMetadata().getAnnotations() != null
          && sc.getMetadata()
              .getAnnotations()
              .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
        if (CustomerConfig.get(
                UUID.fromString(cuuid),
                UUID.fromString(
                    sc.getMetadata().getAnnotations().get(ResourceAnnotationKeys.YBA_RESOURCE_ID)))
            != null) {
          log.info("Storage config {} is already controlled by the operator, ignoring", configName);
          updateStatus(
              sc,
              true,
              sc.getMetadata().getAnnotations().get(ResourceAnnotationKeys.YBA_RESOURCE_ID),
              "Storage Config already controlled by the operator");
          return;
        }
      }
      CustomerConfig existingConfig = CustomerConfig.get(UUID.fromString(cuuid), configName);
      if (existingConfig != null) {
        log.warn("Storage config {} already exists", configName);
        updateStatus(
            sc, true, existingConfig.getConfigUUID().toString(), "Storage Config already exists");
        return;
      }
      CustomerConfig cc =
          CustomerConfig.createStorageConfig(UUID.fromString(cuuid), name, configName, payload);

      this.ccs.create(cc);
      configUUID = Objects.toString(cc.getConfigUUID());
    } catch (Exception e) {
      log.info(
          "Failed adding storageconfig {} exception {}",
          sc.getMetadata().getName(),
          e.getMessage());
      updateStatus(sc, false, "", e.getMessage());
      return;
    }
    updateStatus(sc, true, configUUID, "Added Storage Config");
    log.info("Done adding storage config {}", configUUID);
  }

  @Override
  public void onUpdate(StorageConfig oldSc, StorageConfig newSc) {
    log.info("Updating a storage config");
    // Persist the latest resource YAML so the OperatorResource table stays current.
    resourceTracker.trackResource(newSc);
    ObjectMapper objectMapper = new ObjectMapper();
    String cuuid;
    String configUUID = null;
    if (oldSc.getStatus() != null && oldSc.getStatus().getResourceUUID() != null) {
      configUUID = oldSc.getStatus().getResourceUUID();
    }
    if (newSc.getMetadata().getAnnotations() != null
        && newSc
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      configUUID = newSc.getMetadata().getAnnotations().get(ResourceAnnotationKeys.YBA_RESOURCE_ID);
    }
    if (configUUID == null) {
      log.warn(
          "Cannot update storage config {}: no resource UUID in status or annotations",
          oldSc.getMetadata().getName());
      return;
    }

    try {
      cuuid = operatorUtils.getCustomerUUID();
    } catch (Exception e) {
      log.error("Got Error {}", e);
      log.info("Failed updating storageconfig {}, ", oldSc.getMetadata().getName());
      updateStatus(newSc, false, configUUID, e.getMessage());
      return;
    }

    try {
      JsonNode payload = getConfigPayloadFromCRD(newSc);
      CustomerConfig cc = ccs.getOrBadRequest(UUID.fromString(cuuid), UUID.fromString(configUUID));
      cc.setData((ObjectNode) payload);
      this.ccs.edit(cc);
    } catch (Exception e) {
      log.error("Failed updating storageconfig {}, ", oldSc.getMetadata().getName(), e);
      updateStatus(newSc, false, configUUID, e.getMessage());
      return;
    }
    updateStatus(newSc, true, configUUID, "Updated Storage Config");
    log.info("Done updating storage config {}", configUUID);
  }

  @Override
  public void onDelete(StorageConfig sc, boolean deletedFinalStateUnknown) {
    log.info("Deleting a storage config");
    KubernetesResourceDetails resourceDetails = KubernetesResourceDetails.fromResource(sc);
    Set<KubernetesResourceDetails> orphaned = resourceTracker.untrackResource(resourceDetails);
    log.info(
        "Untracked storage config {} and orphaned dependencies: {}", resourceDetails, orphaned);

    String cuuid;
    try {
      cuuid = operatorUtils.getCustomerUUID();
    } catch (Exception e) {
      log.info("Failed deleting storageconfig {}, ", e.getMessage());
      return;
    }
    String configUUID = null;
    if (sc.getStatus() != null && sc.getStatus().getResourceUUID() != null) {
      configUUID = sc.getStatus().getResourceUUID();
    }
    if (sc.getMetadata().getAnnotations() != null
        && sc.getMetadata().getAnnotations().containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      configUUID = sc.getMetadata().getAnnotations().get(ResourceAnnotationKeys.YBA_RESOURCE_ID);
    }
    if (configUUID == null) {
      log.warn(
          "Cannot delete storage config {}: no resource UUID in status or annotations",
          sc.getMetadata().getName());
      return;
    }
    ccs.delete(UUID.fromString(cuuid), UUID.fromString(configUUID));
    log.info("Done deleting storage config  {} {}", sc.getMetadata().getName(), configUUID);
  }

  @Override
  public void run() {
    informer.addEventHandler(this);
    informer.run();
  }
}
