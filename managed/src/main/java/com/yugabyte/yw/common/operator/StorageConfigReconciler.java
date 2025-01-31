package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.StorageConfigStatus;
import io.yugabyte.operator.v1alpha1.storageconfigspec.Data;
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

  public static JsonNode getConfigPayloadFromCRD(StorageConfig sc) {
    ObjectMapper objectMapper = new ObjectMapper();
    objectMapper.configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true);

    objectMapper.configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    objectMapper.configure(SerializationFeature.WRITE_NULL_MAP_VALUES, false);

    ObjectNode payload = objectMapper.createObjectNode();

    Data data = sc.getSpec().getData();
    JsonNode dataJson = objectMapper.valueToTree(data);
    ObjectNode object = (ObjectNode) dataJson;
    // TODO: find a better way to do cleanup
    object.remove("aws_ACCESS_KEY_ID");
    object.remove("aws_SECRET_ACCESS_KEY");
    object.remove("backup_LOCATION");
    object.remove("gcs_CREDENTIALS_JSON");
    object.remove("azure_STORAGE_SAS_TOKEN");
    object.remove("use_IAM");

    boolean useIAM = object.has("USE_IAM") && (object.get("USE_IAM").asBoolean(false) == true);
    object.remove("USE_IAM");

    String configType = sc.getSpec().getConfig_type().getValue().split("_")[1];
    if (useIAM) {
      String iamFieldName;
      if (configType.equals(CustomerConfigConsts.NAME_S3)) {
        iamFieldName = CustomerConfigConsts.USE_S3_IAM_FIELDNAME;
      } else if (configType.equals(CustomerConfigConsts.NAME_GCS)) {
        iamFieldName = CustomerConfigConsts.USE_GCP_IAM_FIELDNAME;
      } else {
        throw new RuntimeException(
            String.format("IAM only works with S3/GCS but %s config type used", configType));
      }
      object.put(iamFieldName, useIAM);
    }

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
      log.error("Got Error {}", e);
      log.info("Failed updating storageconfig {}, ", oldSc.getMetadata().getName());
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
