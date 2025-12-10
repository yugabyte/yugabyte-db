package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.PropertyNamingStrategies;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.models.Customer;
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
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class StorageConfigReconciler implements ResourceEventHandler<StorageConfig>, Runnable {
  private final SharedIndexInformer<StorageConfig> informer;
  private final Lister<StorageConfig> lister;
  private final MixedOperation<
          StorageConfig, KubernetesResourceList<StorageConfig>, Resource<StorageConfig>>
      resourceClient;
  private final CustomerConfigService ccs;
  private final String namespace;
  private final OperatorUtils operatorUtils;

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

  public String getCustomerUUID() throws Exception {

    Customer cust = operatorUtils.getOperatorCustomer();
    return cust.getUuid().toString();
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
    UUID currentconfigUUID;
    try {
      currentconfigUUID = UUID.fromString(sc.getStatus().getResourceUUID());
    } catch (Exception e) {
      currentconfigUUID = null;
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
        String awsSecretKey = operatorUtils.parseSecretForKey(secret, "AWS_SECRET_ACCESS_KEY");
        configObject.put("AWS_SECRET_ACCESS_KEY", awsSecretKey);
      } else {
        log.warn("AWS secret access key secret {} not found", awsSecret.getName());
      }
    }

    GcsCredentialsJsonSecret gcsSecret = sc.getSpec().getGcsCredentialsJsonSecret();
    if (gcsSecret != null) {
      Secret secret = operatorUtils.getSecret(gcsSecret.getName(), gcsSecret.getNamespace());
      if (secret != null) {
        String gcsSecretKey = operatorUtils.parseSecretForKey(secret, "GCS_CREDENTIALS_JSON");
        configObject.put("GCS_CREDENTIALS_JSON", gcsSecretKey);
      } else {
        log.warn("GCS credentials json secret {} not found", gcsSecret.getName());
      }
    }

    AzureStorageSasTokenSecret azureSecret = sc.getSpec().getAzureStorageSasTokenSecret();
    if (azureSecret != null) {
      Secret secret = operatorUtils.getSecret(azureSecret.getName(), azureSecret.getNamespace());
      if (secret != null) {
        String azureSecretKey = operatorUtils.parseSecretForKey(secret, "AZURE_STORAGE_SAS_TOKEN");
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
    String cuuid;
    String value = sc.getSpec().getConfig_type().getValue();
    String name = value.split("_")[1];
    log.info("Adding a storage config {} ", name);
    try {
      cuuid = getCustomerUUID();
    } catch (Exception e) {
      log.info("Failed adding storageconfig {}", sc.getMetadata().getName());
      updateStatus(sc, false, "", e.getMessage());
      return;
    }
    String configUUID;
    try {
      JsonNode payload = getConfigPayloadFromCRD(sc);
      String configName = sc.getMetadata().getName();
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
    ObjectMapper objectMapper = new ObjectMapper();
    String cuuid;
    String configUUID = oldSc.getStatus().getResourceUUID();

    try {
      cuuid = getCustomerUUID();
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
    String cuuid;
    try {
      cuuid = getCustomerUUID();
    } catch (Exception e) {
      log.info("Failed deleting storageconfig {}, ", e.getMessage());
      return;
    }
    String configUUID = sc.getStatus().getResourceUUID();
    ccs.delete(UUID.fromString(cuuid), UUID.fromString(configUUID));
    log.info("Done deleting storage config  {} {}", sc.getMetadata().getName(), configUUID);
  }

  @Override
  public void run() {
    informer.addEventHandler(this);
    informer.run();
  }
}
