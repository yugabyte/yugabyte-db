// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue.ResourceAction;
import com.yugabyte.yw.common.operator.utils.ResourceAnnotationKeys;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.yugabyte.operator.v1alpha1.YBProvider;
import io.yugabyte.operator.v1alpha1.YBProviderStatus;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class YBProviderReconciler extends AbstractReconciler<YBProvider> {

  private final CloudProviderHandler cloudProviderHandler;
  private static final ObjectMapper objectMapper =
      new ObjectMapper()
          .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
          .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false)
          .configure(SerializationFeature.WRITE_NULL_MAP_VALUES, false);
  private final Map<String, UUID> providerTaskMap;
  private final Cache<String, String> kubeConfigContentCache;
  private final String defaultKubernetesPullSecretContent;
  private final String defaultKubernetesPullSecretName;

  public YBProviderReconciler(
      KubernetesClient client,
      YBInformerFactory informerFactory,
      String namespace,
      OperatorUtils operatorUtils,
      CloudProviderHandler cloudProviderHandler) {
    super(client, informerFactory, YBProvider.class, operatorUtils, namespace);
    this.cloudProviderHandler = cloudProviderHandler;
    this.providerTaskMap = new HashMap<>();
    this.kubeConfigContentCache =
        CacheBuilder.newBuilder()
            .expireAfterWrite(12, TimeUnit.HOURS) // Cache entries expire after 12 hours
            .maximumSize(100) // Maximum 100 cached kubeconfigs
            .expireAfterAccess(12, TimeUnit.HOURS) // Expire if not accessed for 12 hours
            .build();
    this.defaultKubernetesPullSecretContent =
        cloudProviderHandler.getDefaultKubernetesPullSecretYaml();
    this.defaultKubernetesPullSecretName = cloudProviderHandler.getKubernetesPullSecretName();
  }

  @VisibleForTesting
  UUID getProviderTaskMapValue(String key) {
    return this.providerTaskMap.getOrDefault(key, null);
  }

  @VisibleForTesting
  String getKubeConfigContentValue(String key) {
    return this.kubeConfigContentCache.getIfPresent(key);
  }

  @VisibleForTesting
  protected com.yugabyte.yw.common.operator.utils.OperatorWorkQueue getOperatorWorkQueue() {
    return workqueue;
  }

  private void updateProviderStatus(YBProvider provider, String message) {
    YBProviderStatus providerStatus = provider.getStatus();
    if (providerStatus == null) {
      providerStatus = new YBProviderStatus();
      providerStatus.setState(Provider.UsabilityState.UPDATING.toString());
    }
    providerStatus.setMessage(message);
    provider.setStatus(providerStatus);
    resourceClient
        .inNamespace(provider.getMetadata().getNamespace())
        .resource(provider)
        .replaceStatus();
  }

  // Delete the provider
  @Override
  protected void handleResourceDeletion(
      YBProvider provider, Customer cust, OperatorWorkQueue.ResourceAction action)
      throws Exception {
    log.info("Handling deletion of provider {} from CRD", provider.getMetadata().getName());
    String mapKey = OperatorWorkQueue.getWorkQueueKey(provider.getMetadata());
    Provider existingProvider;
    if (provider.getMetadata().getAnnotations() != null
        && provider
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      existingProvider =
          Provider.get(
              cust.getUuid(),
              UUID.fromString(
                  provider
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      existingProvider =
          Provider.get(cust.getUuid(), provider.getMetadata().getName(), CloudType.kubernetes);
    }
    if (existingProvider != null) {
      // Ensure that the provider is not in use by any universe
      if (existingProvider.getUniverseCount() != 0) {
        log.error(
            "Provider {} is in use by {} universes. Cannot delete.",
            existingProvider.getName(),
            existingProvider.getUniverseCount());
        updateProviderStatus(provider, "Provider is in use by universes. Cannot delete.");
      } else {
        TaskInfo taskInfo = getCurrentTaskInfo(provider);
        if (taskInfo == null) {
          deleteProvider(cust, provider, existingProvider.getUuid());
        }
        workqueue.requeue(
            mapKey, OperatorWorkQueue.ResourceAction.DELETE, false /* incrementRetry */);
      }
    } else {
      log.info("Removing finalizers.", provider.getMetadata().getName());
      try {
        operatorUtils.removeFinalizer(provider, resourceClient);
        workqueue.clearState(mapKey);
      } catch (Exception e) {
        log.error("Error Removing finalizer - ", e);
      }
    }
  }

  // Handles 3 cases
  // Case 1: Provider is not present: Create with latest params
  // Case 2: Provider is present but in error state: Update with latest params
  // Case 3: Provider is present and Active and requires edit: Update
  //  with latest params
  @Override
  protected void createActionReconcile(YBProvider provider, Customer cust) throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(provider.getMetadata());
    Provider existingProvider;
    if (provider.getMetadata().getAnnotations() != null
        && provider
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      existingProvider =
          Provider.get(
              cust.getUuid(),
              UUID.fromString(
                  provider
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      existingProvider =
          Provider.get(cust.getUuid(), provider.getMetadata().getName(), CloudType.kubernetes);
    }
    if (existingProvider != null) {
      log.info("Provider {} already exists in the system.", provider.getMetadata().getName());
      if (existingProvider.getUsabilityState() == Provider.UsabilityState.ERROR) {
        log.info(
            "Updating provider {} in error state with latest params", existingProvider.getName());
        editProviderFromCRD(cust, provider, existingProvider);
      } else if (isUpdateRequired(provider, existingProvider)) {
        log.info("Updating provider {} with latest params", existingProvider.getName());
        editProviderFromCRD(cust, provider, existingProvider);
      }
    } else {
      log.info("Creating new provider {} from CRD", provider.getMetadata().getName());
      createProviderFromCRD(cust, provider);
    }
  }

  // Handles 2 cases
  // Case 1: Provider is in error state: Creates edit task with latest params
  // Case 2: Provider is Active and requires edit: Create edit task with latest params
  @Override
  protected void updateActionReconcile(YBProvider provider, Customer cust) throws Exception {
    Provider existingProvider;
    if (provider.getMetadata().getAnnotations() != null
        && provider
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      existingProvider =
          Provider.get(
              cust.getUuid(),
              UUID.fromString(
                  provider
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      existingProvider =
          Provider.get(cust.getUuid(), provider.getMetadata().getName(), CloudType.kubernetes);
    }
    if (existingProvider != null) {
      long univCount = existingProvider.getUniverseCount();
      if (univCount > 0) {
        log.error(
            "Provider {} is in use by {} universes. Cannot update.",
            existingProvider.getName(),
            univCount);
        throw new Exception(
            "Provider " + existingProvider.getName() + " is in use by universes. Cannot update.");
      }
      if (existingProvider.getUsabilityState() == Provider.UsabilityState.ERROR) {
        log.info(
            "Updating provider {} in error state with latest params", existingProvider.getName());
      } else {
        log.info("Editing provider {} with latest params", existingProvider.getName());
      }
      editProviderFromCRD(cust, provider, existingProvider);

    } else {
      String message =
          "Provider "
              + provider.getMetadata().getName()
              + " not found in the system. Cannot update.";
      log.error(message);
      updateProviderStatus(provider, message);
      throw new Exception(message);
    }
  }

  // NO_OP reconcile handler
  // Case 1: Currently running task in incomplete state: NO_OP
  // Case 2: Currently running task in failed state: CREATE/UPDATE based on task type
  // Case 3: Currently running task in Success state: Reset retries and queue UPDATE if required
  // Case 4: No current task and Provider not present: Requeue CREATE
  // Case 5: No current task and Provider requires edit: Requeue UPDATE
  // Case 6: No current task and Provider in error state: Requeue CREATE
  @Override
  protected void noOpActionReconcile(YBProvider provider, Customer cust) throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(provider.getMetadata());
    String providerName = provider.getMetadata().getName();
    Provider existingProvider;
    if (provider.getMetadata().getAnnotations() != null
        && provider
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      existingProvider =
          Provider.get(
              cust.getUuid(),
              UUID.fromString(
                  provider
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      existingProvider =
          Provider.get(cust.getUuid(), provider.getMetadata().getName(), CloudType.kubernetes);
    }
    TaskInfo taskInfo = getCurrentTaskInfo(provider);
    if (taskInfo != null) {
      if (TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
        log.debug("NoOp Action: Provider {} task in progress, requeuing no-op", providerName);
        workqueue.requeue(
            mapKey, OperatorWorkQueue.ResourceAction.NO_OP, false /* incrementRetry */);
      } else if (taskInfo.getTaskState() != TaskInfo.State.Success) {
        log.debug("NoOp Action: Provider {} task failed", providerName);
        if (taskInfo.getTaskType() == TaskType.CloudBootstrap) {
          log.debug("NoOp Action: Provider {} create task failed, requeuing Create", providerName);
          workqueue.requeue(mapKey, ResourceAction.CREATE, true /* incrementRetry */);
        } else {
          log.debug("NoOp Action: Provider {} update task failed, requeuing Update", providerName);
          workqueue.requeue(mapKey, ResourceAction.UPDATE, true /* incrementRetry */);
        }
        providerTaskMap.remove(mapKey);
      } else {
        workqueue.resetRetries(mapKey);
        providerTaskMap.remove(mapKey);
      }
    } else if (existingProvider == null) {
      log.debug("NoOp Action: Provider with name {} not found, requeuing Create", providerName);
      workqueue.requeue(
          mapKey, OperatorWorkQueue.ResourceAction.CREATE, false /* incrementRetry */);
    } else {
      log.debug(
          "Provider: {} current state: {}",
          existingProvider.getName(),
          existingProvider.getUsabilityState());
      if (existingProvider.getUsabilityState() == Provider.UsabilityState.READY
          && isUpdateRequired(provider, existingProvider)) {
        workqueue.requeue(mapKey, ResourceAction.UPDATE, false /* incrementRetry */);
      } else if (existingProvider.getUsabilityState() == Provider.UsabilityState.ERROR) {
        workqueue.requeue(mapKey, ResourceAction.CREATE, false /* incrementRetry */);
      } else {
        workqueue.resetRetries(mapKey);
      }
    }
  }

  private UUID createProviderFromCRD(Customer cust, YBProvider provider) throws Exception {
    try {
      ObjectMeta objectMeta = provider.getMetadata();
      if (CollectionUtils.isEmpty(objectMeta.getFinalizers())) {
        objectMeta.setFinalizers(Collections.singletonList(OperatorUtils.YB_FINALIZER));
        resourceClient
            .inNamespace(objectMeta.getNamespace())
            .withName(objectMeta.getName())
            .patch(provider);
      }
      Provider reqProvider =
          operatorUtils.getProviderReqFromProviderDetails(getProviderPayloadFromCRD(provider));
      Provider providerEbean =
          cloudProviderHandler.createProvider(
              cust, CloudType.kubernetes, reqProvider.getName(), reqProvider, true, false);

      CloudBootstrap.Params taskParams =
          CloudBootstrap.Params.fromProvider(providerEbean, reqProvider);
      if (providerEbean.getCloudCode() == CloudType.kubernetes) {
        taskParams.reqProviderEbean = reqProvider;
      }
      KubernetesResourceDetails kubernetesResourceDetails =
          KubernetesResourceDetails.fromResource(provider);
      taskParams.kubernetesResourceDetails = kubernetesResourceDetails;
      UUID taskUuid = cloudProviderHandler.bootstrap(cust, providerEbean, taskParams);
      if (taskUuid != null) {
        providerTaskMap.put(OperatorWorkQueue.getWorkQueueKey(provider.getMetadata()), taskUuid);
      }
      return taskUuid;
    } catch (Exception e) {
      log.error("Error creating provider from CRD: {}", e.getMessage(), e);
      updateProviderStatus(provider, e.getMessage());
      throw e;
    }
  }

  @VisibleForTesting
  UUID editProviderFromCRD(Customer cust, YBProvider provider, Provider existingProvider) {
    try {
      JsonNode reqProviderJson = getProviderPayloadFromCRD(provider);
      // Copy the existing regions and zones to the request provider
      // If the region or zone is not present in the request provider, set it to inactive
      Map<String, JsonNode> reqRegionMap = new HashMap<>();
      ArrayNode regionsArray = (ArrayNode) reqProviderJson.get("regions");
      for (JsonNode regionNode : regionsArray) {
        reqRegionMap.put(regionNode.get("code").asText(), regionNode);
      }
      for (Region existingRegion : existingProvider.getRegions()) {
        String regionCode = existingRegion.getCode();
        JsonNode reqRegionNode = reqRegionMap.get(regionCode);
        if (reqRegionNode == null) {
          ObjectNode regionJson = objectMapper.valueToTree(existingRegion);
          regionJson.put("active", false);
          regionsArray.add(regionJson);
          continue;
        }
        // Region is present in existing provider. Set it to active and add the uuid.
        // Check for zones in the request provider.
        ((ObjectNode) reqRegionNode).put("active", true);
        ((ObjectNode) reqRegionNode).put("uuid", existingRegion.getUuid().toString());
        Map<String, JsonNode> reqZoneMap = new HashMap<>();
        ArrayNode zonesArray = (ArrayNode) reqRegionNode.get("zones");
        for (JsonNode zoneNode : zonesArray) {
          reqZoneMap.put(zoneNode.get("code").asText(), zoneNode);
        }

        for (AvailabilityZone existingZone : existingRegion.getZones()) {
          String zoneCode = existingZone.getCode();
          JsonNode reqZoneNode = reqZoneMap.get(zoneCode);

          if (reqZoneNode == null) {
            ObjectNode zoneJson = objectMapper.valueToTree(existingZone);
            zoneJson.put("active", false);
            zonesArray.add(zoneJson);
          } else {
            ((ObjectNode) reqZoneNode).put("active", true);
            ((ObjectNode) reqZoneNode).put("uuid", existingZone.getUuid().toString());
          }
        }
      }

      Provider reqProvider = operatorUtils.getProviderReqFromProviderDetails(reqProviderJson);
      reqProvider.setVersion(existingProvider.getVersion());

      UUID taskUuid =
          cloudProviderHandler.editProvider(cust, existingProvider, reqProvider, true, false);
      if (taskUuid != null) {
        providerTaskMap.put(OperatorWorkQueue.getWorkQueueKey(provider.getMetadata()), taskUuid);
      }
      return taskUuid;
    } catch (PlatformServiceException e) {
      log.error("Provider edit failed: {}", e.getMessage());
      updateProviderStatus(provider, e.getMessage());
      throw e;
    }
  }

  private UUID deleteProvider(Customer customer, YBProvider provider, UUID providerUUID) {
    log.info("Deleting provider {} from CRD", provider.getMetadata().getName());
    UUID taskUuid = cloudProviderHandler.delete(customer, providerUUID);
    if (taskUuid != null) {
      providerTaskMap.put(OperatorWorkQueue.getWorkQueueKey(provider.getMetadata()), taskUuid);
    }
    return taskUuid;
  }

  private JsonNode getProviderPayloadFromCRD(YBProvider provider) {
    ObjectNode payload = objectMapper.createObjectNode();
    payload.put("name", provider.getMetadata().getName());
    payload.put("code", "kubernetes");
    payload.put("kubernetesProvider", "custom");
    String providerNamespace = provider.getMetadata().getNamespace();
    addProviderLevelCloudInfoToPayload(
        payload, objectMapper.valueToTree(provider.getSpec().getCloudInfo()));
    addRegionsToPayload(
        payload, objectMapper.valueToTree(provider.getSpec().getRegions()), providerNamespace);
    return payload;
  }

  private void addRegionsToPayload(ObjectNode payload, JsonNode regions, String providerNamespace) {
    if (regions == null || !regions.isArray() || regions.isEmpty()) {
      log.warn("Regions are null, not an array, or empty. Skipping.");
      return;
    }
    ArrayNode regionsArray = payload.withArray("regions");
    for (JsonNode region : regions) {
      ObjectNode regionNode = regionsArray.addObject();
      // Set the code and name to the region code.
      regionNode.put("code", region.get("code").asText().toLowerCase());
      regionNode.put("name", region.get("code").asText().toLowerCase());
      JsonNode zones = region.get("zones");
      if (zones != null && zones.isArray()) {
        ArrayNode zonesArray = regionNode.withArray("zones");
        for (JsonNode zone : zones) {
          ObjectNode zoneNode = zonesArray.addObject();
          // Set the code and name to the zone code.
          zoneNode.put("code", zone.get("code").asText().toLowerCase());
          zoneNode.put("name", zone.get("code").asText().toLowerCase());
          JsonNode cloudInfo = zone.get("cloudInfo");
          if (cloudInfo != null && cloudInfo.isObject()) {
            ObjectNode cloudInfoNode = (ObjectNode) cloudInfo;
            if (cloudInfoNode.get("kubeNamespace") == null
                || cloudInfoNode.get("kubeNamespace").asText().isEmpty()) {
              cloudInfoNode.put("kubeNamespace", providerNamespace);
            }
            addZoneLevelCloudInfoToPayload(zoneNode, cloudInfoNode);
          }
        }
      }
    }
  }

  private void addProviderLevelCloudInfoToPayload(ObjectNode targetNode, ObjectNode cloudInfo) {
    addCloudInfoToPayload(targetNode, cloudInfo, true);
  }

  private void addZoneLevelCloudInfoToPayload(ObjectNode targetNode, ObjectNode cloudInfo) {
    addCloudInfoToPayload(targetNode, cloudInfo, false);
  }

  private void addCloudInfoToPayload(
      ObjectNode targetNode, ObjectNode cloudInfo, boolean isProviderLevel) {
    if (cloudInfo == null || cloudInfo.isEmpty()) {
      log.warn("Cloud info is null or empty, skipping.");
      return;
    }
    if (isProviderLevel) {
      cloudInfo.put("kubernetesPullSecretContent", defaultKubernetesPullSecretContent);
      cloudInfo.put("kubernetesImagePullSecretName", defaultKubernetesPullSecretName);
      cloudInfo.put("kubernetesPullSecretName", defaultKubernetesPullSecretName);
    } else {
      JsonNode kubernetesOverrides = cloudInfo.get("overrides");
      if (kubernetesOverrides != null) {
        cloudInfo.put("overrides", operatorUtils.getKubernetesOverridesString(kubernetesOverrides));
      }
    }
    cloudInfo.put("legacyK8sProvider", false);
    cloudInfo.put("isKubernetesOperatorControlled", true);
    maybeExtractKubeConfig(cloudInfo);
    ObjectNode cloudInfoNode = targetNode.withObject("details").putObject("cloudInfo");
    cloudInfoNode.set("kubernetes", cloudInfo);
  }

  @VisibleForTesting
  void maybeExtractKubeConfig(ObjectNode cloudInfo) {
    JsonNode secretRef = cloudInfo.get("kubeConfigSecret");
    if (secretRef == null || !secretRef.has("name") || !secretRef.has("namespace")) {
      return;
    }

    String secretName = secretRef.get("name").asText();
    String secretNamespace = secretRef.get("namespace").asText();
    String kubeConfigHashName = secretName + "-" + secretNamespace;
    String kubeConfigFileName = secretName + ".conf";

    String cachedKubeConfigContent = kubeConfigContentCache.getIfPresent(kubeConfigHashName);
    if (cachedKubeConfigContent != null) {
      // Cache hit
      log.debug("Using cached kubeconfig content for {}", kubeConfigFileName);
      cloudInfo.put("kubeConfigName", kubeConfigFileName);
      cloudInfo.put("kubeConfigContent", cachedKubeConfigContent);
    } else {
      // Cache miss - fetch the secret and extract content
      log.info("Kubeconfig cache miss for {}, fetching secret", kubeConfigFileName);
      Secret secret = operatorUtils.getSecret(secretName, secretNamespace);
      if (secret != null) {
        String kubeConfigContent = operatorUtils.parseSecretForKey(secret, "kubeconfig");
        cloudInfo.put("kubeConfigName", kubeConfigFileName);
        cloudInfo.put("kubeConfigContent", kubeConfigContent);
        kubeConfigContentCache.put(kubeConfigHashName, kubeConfigContent);
        log.debug("Cached kubeconfig content for {}", kubeConfigFileName);
      } else {
        log.warn(
            "Kubeconfig secret {} not found in namespace {}. Skipping.",
            secretName,
            secretNamespace);
      }
    }
    cloudInfo.remove("kubeConfigSecret");
  }

  @VisibleForTesting
  public TaskInfo getCurrentTaskInfo(YBProvider provider) {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(provider.getMetadata());
    UUID taskUUID = providerTaskMap.get(mapKey);
    if (taskUUID != null) {
      Optional<TaskInfo> optTaskInfo = TaskInfo.maybeGet(taskUUID);
      if (optTaskInfo.isPresent()) {
        return optTaskInfo.get();
      }
    }
    return null;
  }

  private boolean isUpdateRequired(YBProvider provider, Provider existingProvider) {
    log.info("Checking if update is required for provider {}", existingProvider.getName());
    Provider desiredProvider =
        operatorUtils.getProviderReqFromProviderDetails(getProviderPayloadFromCRD(provider));
    if (operatorUtils.hasKubeConfigChanged(
        existingProvider.getDetails().getCloudInfo().getKubernetes().getEnvVars(),
        desiredProvider.getDetails().getCloudInfo().getKubernetes().getEnvVars())) {
      log.info("Kubeconfig changed for provider {}", existingProvider.getName());
      return true;
    }
    if (!existingProvider
        .getDetails()
        .getCloudInfo()
        .getKubernetes()
        .getKubernetesImageRegistry()
        .equals(
            desiredProvider
                .getDetails()
                .getCloudInfo()
                .getKubernetes()
                .getKubernetesImageRegistry())) {
      log.info("Image registry changed for provider {}", existingProvider.getName());
      return true;
    }
    if (operatorUtils.hasRegionConfigChanged(
        desiredProvider.getRegions(), existingProvider.getRegions())) {
      log.info("Region config changed for provider {}", existingProvider.getName());
      return true;
    }
    return false;
  }

  @VisibleForTesting
  public boolean hasCacheEntry(String secretName, String namespace) {
    if (secretName == null || namespace == null) {
      return false;
    }
    String cacheKey = secretName + "-" + namespace;
    return kubeConfigContentCache.getIfPresent(cacheKey) != null;
  }
}
