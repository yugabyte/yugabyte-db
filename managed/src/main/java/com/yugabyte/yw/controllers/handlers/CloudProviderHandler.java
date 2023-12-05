/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers.handlers;

import static com.yugabyte.yw.commissioner.Common.CloudType.kubernetes;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.DockerInstanceTypeMetadata;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.DockerRegionMetadata;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Multimap;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.aws.AWSInitializer;
import com.yugabyte.yw.cloud.azu.AZUInitializer;
import com.yugabyte.yw.cloud.gcp.GCPInitializer;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudProviderDelete;
import com.yugabyte.yw.commissioner.tasks.CloudProviderEdit;
import com.yugabyte.yw.commissioner.tasks.params.ScheduledAccessKeyRotateParams;
import com.yugabyte.yw.common.AccessKeyRotationUtil;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.CloudProviderHelper;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.EditAccessKeyRotationScheduleParams;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.ProviderValidator;
import io.ebean.annotation.Transactional;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.utils.Serialization;
import jakarta.persistence.PersistenceException;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.inject.Singleton;
import org.apache.commons.lang3.RandomStringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

@Singleton
public class CloudProviderHandler {
  private static final Logger LOG = LoggerFactory.getLogger(CloudProviderHandler.class);

  @Inject private Commissioner commissioner;
  @Inject private ConfigHelper configHelper;
  @Inject private AccessManager accessManager;
  @Inject private CloudAPI.Factory cloudAPIFactory;
  @Inject private ProviderValidator providerValidator;
  @Inject private Config config;
  @Inject private CloudQueryHelper queryHelper;
  @Inject private AccessKeyRotationUtil accessKeyRotationUtil;
  @Inject private CloudProviderHelper cloudProviderHelper;

  @Inject private AWSInitializer awsInitializer;
  @Inject private GCPInitializer gcpInitializer;
  @Inject private AZUInitializer azuInitializer;

  public UUID delete(Customer customer, UUID providerUUID) {
    CloudProviderDelete.Params params = new CloudProviderDelete.Params();
    params.providerUUID = providerUUID;
    params.customer = customer;

    UUID taskUUID = commissioner.submit(TaskType.CloudProviderDelete, params);
    Provider provider = Provider.getOrBadRequest(customer.getUuid(), providerUUID);
    CustomerTask.create(
        customer,
        providerUUID,
        taskUUID,
        CustomerTask.TargetType.Provider,
        CustomerTask.TaskType.Delete,
        provider.getName());

    return taskUUID;
  }

  @Transactional
  public Provider createProvider(
      Customer customer,
      Common.CloudType providerCode,
      String providerName,
      Provider reqProvider,
      boolean validate,
      boolean ignoreValidationErrors) {
    if (providerCode.equals(Common.CloudType.gcp)) {
      cloudProviderHelper.maybeUpdateGCPProject(reqProvider);
    }

    // TODO: Remove this code once the validators are added for all cloud provider.
    CloudAPI cloudAPI = cloudAPIFactory.get(providerCode.toString());
    if (cloudAPI != null
        && !cloudAPI.isValidCreds(
            reqProvider, CloudProviderHelper.getFirstRegionCode(reqProvider))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("Invalid %s Credentials.", providerCode.toString().toUpperCase()));
    }
    JsonNode errors = null;
    if (validate) {
      try {
        providerValidator.validate(reqProvider);
      } catch (PlatformServiceException e) {
        LOG.error(
            "Received validation error,  ignoreValidationErrors=" + ignoreValidationErrors, e);
        if (!ignoreValidationErrors) {
          throw e;
        } else {
          errors = e.getContentJson();
        }
      }
    }
    if (reqProvider.getCloudCode() != kubernetes && !config.getBoolean("yb.cloud.enabled")) {
      // Always enable for non-k8s providers.
      reqProvider.getDetails().setEnableNodeAgent(true);
    }
    Provider provider =
        Provider.create(customer.getUuid(), providerCode, providerName, reqProvider.getDetails());
    cloudProviderHelper.maybeUpdateVPC(provider);

    Map<String, String> providerConfig = CloudInfoInterface.fetchEnvVars(provider);
    if (!providerConfig.isEmpty()) {
      // Perform for all cloud providers as it does validation.
      if (provider.getCloudCode().equals(kubernetes)) {
        cloudProviderHelper.updateKubeConfig(provider, providerConfig, false);
        try {
          cloudProviderHelper.createKubernetesInstanceTypes(customer, provider);
        } catch (PersistenceException ex) {
          // TODO: make instance types more multi-tenant friendly...
        }
      } else {
        cloudProviderHelper.maybeUpdateCloudProviderConfig(provider, providerConfig);
      }
    }
    if (!providerCode.isRequiresBootstrap()) {
      provider.setUsabilityState(Provider.UsabilityState.READY);
    }
    provider.setLastValidationErrors(errors);
    provider.save();

    return provider;
  }

  public Provider createKubernetes(Customer customer, KubernetesProviderFormData formData) {
    Common.CloudType providerCode = formData.code;
    if (!providerCode.equals(kubernetes)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "API for only kubernetes provider creation: " + providerCode);
    }

    if (formData.regionList.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Need regions in provider");
    }
    for (KubernetesProviderFormData.RegionData rd : formData.regionList) {
      boolean hasConfig = formData.config.containsKey("KUBECONFIG_NAME");
      if (rd.config != null) {
        if (rd.config.containsKey("KUBECONFIG_NAME")) {
          if (hasConfig) {
            throw new PlatformServiceException(BAD_REQUEST, "Kubeconfig can't be at two levels");
          } else {
            hasConfig = true;
          }
        }
      }
      if (rd.zoneList.isEmpty()) {
        throw new PlatformServiceException(BAD_REQUEST, "No zone provided in region");
      }
      for (KubernetesProviderFormData.RegionData.ZoneData zd : rd.zoneList) {
        if (zd.config != null) {
          if (zd.config.containsKey("KUBECONFIG_NAME")) {
            if (hasConfig) {
              throw new PlatformServiceException(BAD_REQUEST, "Kubeconfig can't be at two levels");
            }
          } else if (!hasConfig) {
            LOG.warn(
                "No Kubeconfig found at any level, in-cluster service account credentials will be"
                    + " used.");
          }
        }
      }
    }

    Map<String, String> config = formData.config;
    Provider provider = Provider.create(customer.getUuid(), providerCode, formData.name, config);
    Map<String, String> providerConfig = CloudInfoInterface.fetchEnvVars(provider);

    boolean isConfigInProvider =
        cloudProviderHelper.updateKubeConfig(provider, providerConfig, false);
    List<KubernetesProviderFormData.RegionData> regionList = formData.regionList;
    for (KubernetesProviderFormData.RegionData rd : regionList) {
      Map<String, String> regionConfig = rd.config;
      String regionCode = rd.code;
      String regionName = rd.name;
      Double latitude = rd.latitude;
      Double longitude = rd.longitude;
      KubernetesInfo kubernetesCloudInfo = CloudInfoInterface.get(provider);
      ConfigHelper.ConfigType kubernetesConfigType =
          cloudProviderHelper.getKubernetesConfigType(kubernetesCloudInfo.getKubernetesProvider());
      if (kubernetesConfigType != null) {
        Map<String, Object> k8sRegionMetadata = configHelper.getConfig(kubernetesConfigType);
        if (!k8sRegionMetadata.containsKey(regionCode)) {
          throw new RuntimeException("Region " + regionCode + " metadata not found");
        }
        JsonNode metadata = Json.toJson(k8sRegionMetadata.get(regionCode));
        regionName = metadata.get("name").asText();
        latitude = metadata.get("latitude").asDouble();
        longitude = metadata.get("longitude").asDouble();
      }
      Region region = Region.create(provider, regionCode, regionName, null, latitude, longitude);
      CloudInfoInterface.setCloudProviderInfoFromConfig(region, regionConfig);
      regionConfig = CloudInfoInterface.fetchEnvVars(region);
      boolean isConfigInRegion =
          cloudProviderHelper.updateKubeConfigForRegion(provider, region, regionConfig, false);
      for (KubernetesProviderFormData.RegionData.ZoneData zd : rd.zoneList) {
        Map<String, String> zoneConfig = zd.config;
        AvailabilityZone az = AvailabilityZone.createOrThrow(region, zd.code, zd.name, null, null);
        CloudInfoInterface.setCloudProviderInfoFromConfig(az, zoneConfig);
        zoneConfig = CloudInfoInterface.fetchEnvVars(az);
        boolean isConfigInZone =
            cloudProviderHelper.updateKubeConfigForZone(provider, region, az, zoneConfig, false);
        if (!(isConfigInProvider || isConfigInRegion || isConfigInZone)) {
          // Use in-cluster ServiceAccount credentials
          KubernetesInfo k8sMetadata = CloudInfoInterface.get(az);
          k8sMetadata.setKubeConfig("");
        }
        az.save();
      }
      if (isConfigInRegion) {
        region.save();
      }
    }
    provider.save();
    try {
      cloudProviderHelper.createKubernetesInstanceTypes(customer, provider);
    } catch (PersistenceException ex) {
      provider.delete();
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Couldn't create instance types");
      // TODO: make instance types more multi-tenant friendly...
    }
    return provider;
  }

  // TODO(Shashank): For now this code is similar to createKubernetes but we can improve it.
  //  Note that we already have all the beans (i.e. regions and zones) in reqProvider.
  //  We do not need to call all the updateKubeConfig* methods. Instead just save
  //  whole thing after some validation.
  public Provider createKubernetesNew(UUID providerUUID, Provider reqProvider) {
    Provider provider = Provider.getOrBadRequest(providerUUID);
    Customer customer = Customer.getOrBadRequest(provider.getCustomerUUID());
    cloudProviderHelper.validateKubernetesProviderConfig(reqProvider);

    cloudProviderHelper.bootstrapKubernetesProvider(
        provider, reqProvider, reqProvider.getRegions(), false);
    try {
      cloudProviderHelper.createKubernetesInstanceTypes(customer, provider);
    } catch (PersistenceException ex) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Couldn't create instance types");
      // TODO: make instance types more multi-tenant friendly...
    }
    provider.setUsabilityState(Provider.UsabilityState.READY);
    provider.save();
    return provider;
  }

  public KubernetesProviderFormData suggestedKubernetesConfigs() {
    try {
      Multimap<String, String> regionToAZ = cloudProviderHelper.computeKubernetesRegionToZoneInfo();
      if (regionToAZ.isEmpty()) {
        LOG.info(
            "No regions and zones found, check if the region and zone labels are present on the"
                + " nodes. https://k8s.io/docs/reference/labels-annotations-taints/");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "No region and zone information found.");
      }
      String storageClass = config.getString("yb.kubernetes.storageClass");
      String pullSecretName = config.getString("yb.kubernetes.pullSecretName");
      if (storageClass == null || pullSecretName == null) {
        LOG.error("Required configuration keys from yb.kubernetes.* are missing.");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Required configuration is missing.");
      }
      Secret pullSecret = cloudProviderHelper.getKubernetesPullSecret(pullSecretName);

      KubernetesProviderFormData formData = new KubernetesProviderFormData();
      formData.code = kubernetes;
      formData.name =
          new StringBuilder()
              .append("k8s")
              .append(RandomStringUtils.randomAlphanumeric(5))
              .append("provider")
              .toString();
      String cloudProviderCode = cloudProviderHelper.getCloudProvider();

      if (pullSecret != null) {
        formData.config =
            ImmutableMap.of(
                "KUBECONFIG_IMAGE_PULL_SECRET_NAME",
                pullSecretName,
                "KUBECONFIG_PROVIDER",
                cloudProviderCode,
                "KUBECONFIG_PULL_SECRET_NAME",
                pullSecretName, // filename
                "KUBECONFIG_PULL_SECRET_CONTENT",
                Serialization.asYaml(pullSecret), // Yaml formatted
                "KUBECONFIG_IMAGE_REGISTRY",
                cloudProviderHelper.getKubernetesImageRepository()); // Location of the registry
      }

      for (String region : regionToAZ.keySet()) {
        KubernetesProviderFormData.RegionData regionData =
            new KubernetesProviderFormData.RegionData();
        regionData.code = region;
        String regName = cloudProviderHelper.getRegionNameFromCode(region, cloudProviderCode);
        if (regName == null) {
          regName = region;
        }
        regionData.name = regName;
        for (String az : regionToAZ.get(region)) {
          KubernetesProviderFormData.RegionData.ZoneData zoneData =
              new KubernetesProviderFormData.RegionData.ZoneData();
          zoneData.code = az;
          zoneData.name = az;
          zoneData.config = ImmutableMap.of("STORAGE_CLASS", storageClass);
          regionData.zoneList.add(zoneData);
        }
        formData.regionList.add(regionData);
      }

      return formData;
    } catch (RuntimeException e) {
      LOG.error(e.getClass() + ": " + e.getMessage(), e);
      throw e; // new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  } // Performs region and zone discovery based on

  public Provider setupNewDockerProvider(Customer customer) {
    Provider newProvider = Provider.create(customer.getUuid(), Common.CloudType.docker, "Docker");
    Map<String, Object> regionMetadata = configHelper.getConfig(DockerRegionMetadata);
    regionMetadata.forEach(
        (regionCode, metadata) -> {
          Region region = Region.createWithMetadata(newProvider, regionCode, Json.toJson(metadata));
          Arrays.asList("a", "b", "c")
              .forEach(
                  (zoneSuffix) -> {
                    String zoneName = regionCode + zoneSuffix;
                    AvailabilityZone.createOrThrow(region, zoneName, zoneName, "yugabyte-bridge");
                  });
        });
    Map<String, Object> instanceTypeMetadata = configHelper.getConfig(DockerInstanceTypeMetadata);
    instanceTypeMetadata.forEach(
        (itCode, metadata) ->
            InstanceType.createWithMetadata(newProvider.getUuid(), itCode, Json.toJson(metadata)));
    return newProvider;
  }

  public UUID bootstrap(Customer customer, Provider provider, CloudBootstrap.Params taskParams) {
    // Set the top-level provider info.
    taskParams.providerUUID = provider.getUuid();

    // If the regionList is still empty by here, then we need to list the regions available.
    if (taskParams.perRegionMetadata == null) {
      taskParams.perRegionMetadata = new HashMap<>();
    }
    if (taskParams.perRegionMetadata.isEmpty()
        && provider.getCloudCode().regionBootstrapSupported()) {
      List<String> regionCodes = queryHelper.getRegionCodes(provider);
      for (String regionCode : regionCodes) {
        taskParams.perRegionMetadata.put(regionCode, new CloudBootstrap.Params.PerRegionMetadata());
      }
    }
    cloudProviderHelper.validateInstanceTemplate(provider, taskParams);

    UUID taskUUID = commissioner.submit(TaskType.CloudBootstrap, taskParams);
    CustomerTask.create(
        customer,
        provider.getUuid(),
        taskUUID,
        CustomerTask.TargetType.Provider,
        CustomerTask.TaskType.Create,
        provider.getName());
    return taskUUID;
  }

  public UUID editProvider(
      Customer customer,
      Provider provider,
      Provider editProviderReq,
      boolean validate,
      boolean ignoreValidationErrors) {
    cloudProviderHelper.validateEditProvider(
        editProviderReq, provider, validate, ignoreValidationErrors);
    provider.setUsabilityState(Provider.UsabilityState.UPDATING);
    provider.save();
    editProviderReq.setVersion(provider.getVersion());
    CloudProviderEdit.Params taskParams = new CloudProviderEdit.Params();
    taskParams.newProviderState = editProviderReq;
    taskParams.providerUUID = provider.getUuid();
    taskParams.skipRegionBootstrap = false;
    UUID taskUUID = commissioner.submit(TaskType.CloudProviderEdit, taskParams);
    CustomerTask.create(
        customer,
        provider.getUuid(),
        taskUUID,
        CustomerTask.TargetType.Provider,
        CustomerTask.TaskType.Update,
        provider.getName());
    return taskUUID;
  }

  public void refreshPricing(UUID customerUUID, Provider provider) {
    if (provider.getCode().equals("gcp")) {
      gcpInitializer.initialize(customerUUID, provider.getUuid());
    } else if (provider.getCode().equals("azu")) {
      azuInitializer.initialize(customerUUID, provider.getUuid());
    } else {
      awsInitializer.initialize(customerUUID, provider.getUuid());
    }
  }

  public Map<UUID, UUID> rotateAccessKeys(
      UUID customerUUID, UUID providerUUID, List<UUID> universeUUIDs, String newKeyCode) {
    // fail if provider is a manually provisioned one
    accessKeyRotationUtil.failManuallyProvisioned(providerUUID, newKeyCode);
    // create access key rotation task for each of the universes
    return accessManager.rotateAccessKey(customerUUID, providerUUID, universeUUIDs, newKeyCode);
  }

  public Schedule scheduleAccessKeysRotation(
      UUID customerUUID,
      UUID providerUUID,
      List<UUID> universeUUIDs,
      int schedulingFrequencyDays,
      boolean rotateAllUniverses) {
    // fail if provider is a manually provisioned one
    accessKeyRotationUtil.failManuallyProvisioned(providerUUID, null /* newKeyCode*/);
    // fail if a universe is already in scheduled rotation, ask to edit schedule instead
    accessKeyRotationUtil.failUniverseAlreadyInRotation(customerUUID, providerUUID, universeUUIDs);
    long schedulingFrequency = accessKeyRotationUtil.convertDaysToMillis(schedulingFrequencyDays);
    TimeUnit frequencyTimeUnit = TimeUnit.DAYS;
    ScheduledAccessKeyRotateParams taskParams =
        new ScheduledAccessKeyRotateParams(
            customerUUID, providerUUID, universeUUIDs, rotateAllUniverses);
    return Schedule.create(
        customerUUID,
        providerUUID,
        taskParams,
        TaskType.CreateAndRotateAccessKey,
        schedulingFrequency,
        null,
        frequencyTimeUnit,
        null);
  }

  public Schedule editAccessKeyRotationSchedule(
      UUID customerUUID,
      UUID providerUUID,
      UUID scheduleUUID,
      EditAccessKeyRotationScheduleParams params) {
    Schedule schedule = Schedule.getOrBadRequest(customerUUID, scheduleUUID);
    if (!schedule.getOwnerUUID().equals(providerUUID)) {
      throw new PlatformServiceException(BAD_REQUEST, "Schedule is not owned by this provider");
    } else if (!schedule.getTaskType().equals(TaskType.CreateAndRotateAccessKey)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "This schedule is not for access key rotation");
    }
    if (params.status.equals(Schedule.State.Paused)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "State paused is an internal state and cannot be specified by the user");
    } else if (params.status.equals(Schedule.State.Stopped)) {
      schedule.stopSchedule();
    } else if (params.status.equals(Schedule.State.Active)) {
      if (params.schedulingFrequencyDays == 0) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Frequency cannot be null, specify frequency in days!");
      } else if (schedule.getStatus().equals(Schedule.State.Active) && schedule.isRunningState()) {
        throw new PlatformServiceException(CONFLICT, "Cannot edit schedule as it is running.");
      } else {
        ScheduledAccessKeyRotateParams taskParams =
            Json.fromJson(schedule.getTaskParams(), ScheduledAccessKeyRotateParams.class);
        // fail if a universe is already in active scheduled rotation,
        // and activating this schedule causes conflict
        accessKeyRotationUtil.failUniverseAlreadyInRotation(
            customerUUID, providerUUID, taskParams.getUniverseUUIDs());
        long schedulingFrequency =
            accessKeyRotationUtil.convertDaysToMillis(params.schedulingFrequencyDays);
        schedule.updateFrequency(schedulingFrequency);
      }
    }
    return schedule;
  }
}
