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
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Multimap;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.cloud.AZUInitializer;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.GCPInitializer;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.EditProviderRequest;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.io.IOException;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.persistence.PersistenceException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.Environment;
import play.libs.Json;

public class CloudProviderHandler {

  private static final Logger LOG = LoggerFactory.getLogger(CloudProviderHandler.class);
  private static final JsonNode KUBERNETES_CLOUD_INSTANCE_TYPE =
      Json.parse("{\"instanceTypeCode\": \"cloud\", \"numCores\": 0.5, \"memSizeGB\": 1.5}");
  private static final JsonNode KUBERNETES_DEV_INSTANCE_TYPE =
      Json.parse("{\"instanceTypeCode\": \"dev\", \"numCores\": 0.5, \"memSizeGB\": 0.5}");
  private static final JsonNode KUBERNETES_INSTANCE_TYPES =
      Json.parse(
          "["
              + "{\"instanceTypeCode\": \"xsmall\", \"numCores\": 2, \"memSizeGB\": 4},"
              + "{\"instanceTypeCode\": \"small\", \"numCores\": 4, \"memSizeGB\": 7.5},"
              + "{\"instanceTypeCode\": \"medium\", \"numCores\": 8, \"memSizeGB\": 15},"
              + "{\"instanceTypeCode\": \"large\", \"numCores\": 16, \"memSizeGB\": 15},"
              + "{\"instanceTypeCode\": \"xlarge\", \"numCores\": 32, \"memSizeGB\": 30}]");

  @Inject private Commissioner commissioner;
  @Inject private ConfigHelper configHelper;
  @Inject private AccessManager accessManager;
  @Inject private DnsManager dnsManager;
  @Inject private Environment environment;
  @Inject private CloudAPI.Factory cloudAPIFactory;
  @Inject private KubernetesManager kubernetesManager;
  @Inject private Configuration appConfig;
  @Inject private Config config;
  @Inject private CloudQueryHelper queryHelper;

  @Inject private AWSInitializer awsInitializer;
  @Inject private GCPInitializer gcpInitializer;
  @Inject private AZUInitializer azuInitializer;

  public void delete(Customer customer, Provider provider) {
    if (customer.getUniversesForProvider(provider.uuid).size() > 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot delete Provider with Universes");
    }

    // TODO: move this to task framework
    for (AccessKey accessKey : AccessKey.getAll(provider.uuid)) {
      if (!accessKey.getKeyInfo().provisionInstanceScript.isEmpty()) {
        new File(accessKey.getKeyInfo().provisionInstanceScript).delete();
      }
      accessManager.deleteKeyByProvider(provider, accessKey.getKeyCode());
      accessKey.delete();
    }
    NodeInstance.deleteByProvider(provider.uuid);
    InstanceType.deleteInstanceTypesForProvider(provider, config, configHelper);
    provider.delete();
  }

  public Provider createProvider(
      Customer customer,
      Common.CloudType providerCode,
      String providerName,
      Map<String, String> providerConfig,
      String anyProviderRegion)
      throws IOException {
    Provider existentProvider = Provider.get(customer.uuid, providerName, providerCode);
    if (existentProvider != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Provider with the name %s already exists", providerName));
    }

    Provider provider = Provider.create(customer.uuid, providerCode, providerName, providerConfig);
    if (!providerConfig.isEmpty()) {
      String hostedZoneId = provider.getHostedZoneId();
      switch (provider.code) {
        case "aws":
          // TODO: Add this validation. But there is a bad test
          //  if (anyProviderRegion == null || anyProviderRegion.isEmpty()) {
          //    throw new YWServiceException(BAD_REQUEST, "Must have at least one region");
          //  }
          CloudAPI cloudAPI = cloudAPIFactory.get(provider.code);
          if (cloudAPI != null && !cloudAPI.isValidCreds(providerConfig, anyProviderRegion)) {
            provider.delete();
            throw new PlatformServiceException(BAD_REQUEST, "Invalid AWS Credentials.");
          }
          if (hostedZoneId != null && hostedZoneId.length() != 0) {
            validateAndUpdateHostedZone(provider, hostedZoneId);
          }
          break;
        case "gcp":
          updateGCPConfig(provider, providerConfig);
          break;
        case "kubernetes":
          updateKubeConfig(provider, providerConfig, false);
          try {
            createKubernetesInstanceTypes(customer, provider);
          } catch (PersistenceException ex) {
            // TODO: make instance types more multi-tenant friendly...
          }
          break;
        case "azu":
          if (hostedZoneId != null && hostedZoneId.length() != 0) {
            validateAndUpdateHostedZone(provider, hostedZoneId);
          }
          break;
      }
    }
    return provider;
  }

  public Provider createKubernetes(Customer customer, KubernetesProviderFormData formData)
      throws IOException {
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
                "No Kubeconfig found at any level, in-cluster service account credentials will be used.");
          }
        }
      }
    }

    Provider provider;
    Map<String, String> config = formData.config;
    provider = Provider.create(customer.uuid, providerCode, formData.name);
    boolean isConfigInProvider = updateKubeConfig(provider, config, false);
    List<KubernetesProviderFormData.RegionData> regionList = formData.regionList;
    for (KubernetesProviderFormData.RegionData rd : regionList) {
      Map<String, String> regionConfig = rd.config;
      Region region = Region.create(provider, rd.code, rd.name, null, rd.latitude, rd.longitude);
      boolean isConfigInRegion = updateKubeConfigForRegion(provider, region, regionConfig, false);
      for (KubernetesProviderFormData.RegionData.ZoneData zd : rd.zoneList) {
        Map<String, String> zoneConfig = zd.config;
        AvailabilityZone az = AvailabilityZone.createOrThrow(region, zd.code, zd.name, null);
        boolean isConfigInZone = updateKubeConfigForZone(provider, region, az, zoneConfig, false);
        if (!(isConfigInProvider || isConfigInRegion || isConfigInZone)) {
          // Use in-cluster ServiceAccount credentials
          az.setConfig(ImmutableMap.of("KUBECONFIG", ""));
          az.save();
        }
      }
    }
    try {
      createKubernetesInstanceTypes(customer, provider);
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
  public Provider createKubernetesNew(Customer customer, Provider reqProvider) throws IOException {
    Common.CloudType providerCode = CloudType.valueOf(reqProvider.code);
    if (!providerCode.equals(kubernetes)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "API for only kubernetes provider creation: " + providerCode);
    }
    if (reqProvider.regions.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Need regions in provider");
    }
    for (Region rd : reqProvider.regions) {
      boolean hasConfig = reqProvider.getUnmaskedConfig().containsKey("KUBECONFIG_NAME");
      if (rd.getUnmaskedConfig().containsKey("KUBECONFIG_NAME")) {
        if (hasConfig) {
          throw new PlatformServiceException(BAD_REQUEST, "Kubeconfig can't be at two levels");
        } else {
          hasConfig = true;
        }
      }
      if (rd.zones.isEmpty()) {
        throw new PlatformServiceException(BAD_REQUEST, "No zone provided in region");
      }
      for (AvailabilityZone zd : rd.zones) {
        if (zd.getUnmaskedConfig().containsKey("KUBECONFIG_NAME")) {
          if (hasConfig) {
            throw new PlatformServiceException(BAD_REQUEST, "Kubeconfig can't be at two levels");
          }
        } else if (!hasConfig) {
          LOG.warn(
              "No Kubeconfig found at any level. "
                  + "In-cluster service account credentials will be used.");
        }
      }
    }

    Provider provider;
    Map<String, String> config = reqProvider.getUnmaskedConfig();
    provider = Provider.create(customer.uuid, providerCode, reqProvider.name);
    boolean isConfigInProvider = updateKubeConfig(provider, config, false);
    List<Region> regionList = reqProvider.regions;
    for (Region rd : regionList) {
      Map<String, String> regionConfig = rd.getUnmaskedConfig();
      Region region = Region.create(provider, rd.code, rd.name, null, rd.latitude, rd.longitude);
      boolean isConfigInRegion = updateKubeConfigForRegion(provider, region, regionConfig, false);
      for (AvailabilityZone zd : rd.zones) {
        Map<String, String> zoneConfig = zd.getUnmaskedConfig();
        AvailabilityZone az = AvailabilityZone.createOrThrow(region, zd.code, zd.name, null);
        boolean isConfigInZone = updateKubeConfigForZone(provider, region, az, zoneConfig, false);
        if (!(isConfigInProvider || isConfigInRegion || isConfigInZone)) {
          // Use in-cluster ServiceAccount credentials
          az.setConfig(ImmutableMap.of("KUBECONFIG", ""));
          az.save();
        }
      }
    }
    try {
      createKubernetesInstanceTypes(customer, provider);
    } catch (PersistenceException ex) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Couldn't create instance types");
      // TODO: make instance types more multi-tenant friendly...
    }
    return provider;
  }

  public boolean updateKubeConfig(Provider provider, Map<String, String> config, boolean edit)
      throws IOException {
    return updateKubeConfigForRegion(provider, null, config, edit);
  }

  public boolean updateKubeConfigForRegion(
      Provider provider, Region region, Map<String, String> config, boolean edit)
      throws IOException {
    return updateKubeConfigForZone(provider, region, null, config, edit);
  }

  public boolean updateKubeConfigForZone(
      Provider provider,
      Region region,
      AvailabilityZone zone,
      Map<String, String> config,
      boolean edit)
      throws IOException {
    String kubeConfigFile;
    String pullSecretFile = null;

    if (config == null) {
      return false;
    }

    String path = provider.uuid.toString();
    if (region != null) {
      path = path + "/" + region.uuid.toString();
      if (zone != null) {
        path = path + "/" + zone.uuid.toString();
      }
    }
    boolean hasKubeConfig = config.containsKey("KUBECONFIG_NAME");
    if (hasKubeConfig) {
      kubeConfigFile = accessManager.createKubernetesConfig(path, config, edit);

      // Remove the kubeconfig file related configs from provider config.
      config.remove("KUBECONFIG_NAME");
      config.remove("KUBECONFIG_CONTENT");

      if (kubeConfigFile != null) {
        config.put("KUBECONFIG", kubeConfigFile);
      }
    }

    if (region == null) {
      if (config.containsKey("KUBECONFIG_PULL_SECRET_NAME")) {
        if (config.get("KUBECONFIG_PULL_SECRET_NAME") != null) {
          pullSecretFile = accessManager.createPullSecret(provider.uuid, config, edit);
        }
      }
      config.remove("KUBECONFIG_PULL_SECRET_NAME");
      config.remove("KUBECONFIG_PULL_SECRET_CONTENT");
      if (pullSecretFile != null) {
        config.put("KUBECONFIG_PULL_SECRET", pullSecretFile);
      }

      provider.setConfig(config);
      provider.save();
    } else if (zone == null) {
      region.setConfig(config);
      region.save();
    } else {
      zone.updateConfig(config);
    }
    return hasKubeConfig;
  }

  public void updateGCPConfig(Provider provider, Map<String, String> config) throws IOException {
    // Remove the key to avoid generating a credentials file unnecessarily.
    config.remove("GCE_HOST_PROJECT");
    // If we were not given a config file, then no need to do anything here.
    if (config.isEmpty()) {
      return;
    }

    String gcpCredentialsFile =
        accessManager.createCredentialsFile(provider.uuid, Json.toJson(config));

    Map<String, String> newConfig = new HashMap<>();
    if (config.get("project_id") != null) {
      newConfig.put("GCE_PROJECT", config.get("project_id"));
    }
    if (config.get("client_email") != null) {
      newConfig.put("GCE_EMAIL", config.get("client_email"));
    }
    if (gcpCredentialsFile != null) {
      newConfig.put("GOOGLE_APPLICATION_CREDENTIALS", gcpCredentialsFile);
    }
    provider.setConfig(newConfig);
    provider.save();
  }

  public void createKubernetesInstanceTypes(Customer customer, Provider provider) {
    KUBERNETES_INSTANCE_TYPES.forEach(
        (instanceType -> {
          InstanceType.InstanceTypeDetails idt = new InstanceType.InstanceTypeDetails();
          idt.setVolumeDetailsList(1, 100, InstanceType.VolumeType.SSD);
          InstanceType.upsert(
              provider.uuid,
              instanceType.get("instanceTypeCode").asText(),
              instanceType.get("numCores").asDouble(),
              instanceType.get("memSizeGB").asDouble(),
              idt);
        }));
    if (environment.isDev()) {
      InstanceType.InstanceTypeDetails idt = new InstanceType.InstanceTypeDetails();
      idt.setVolumeDetailsList(1, 100, InstanceType.VolumeType.SSD);
      InstanceType.upsert(
          provider.uuid,
          KUBERNETES_DEV_INSTANCE_TYPE.get("instanceTypeCode").asText(),
          KUBERNETES_DEV_INSTANCE_TYPE.get("numCores").asDouble(),
          KUBERNETES_DEV_INSTANCE_TYPE.get("memSizeGB").asDouble(),
          idt);
    }
    if (customer.code.equals("cloud")) {
      InstanceType.InstanceTypeDetails idt = new InstanceType.InstanceTypeDetails();
      idt.setVolumeDetailsList(1, 5, InstanceType.VolumeType.SSD);
      InstanceType.upsert(
          provider.uuid,
          KUBERNETES_CLOUD_INSTANCE_TYPE.get("instanceTypeCode").asText(),
          KUBERNETES_CLOUD_INSTANCE_TYPE.get("numCores").asDouble(),
          KUBERNETES_CLOUD_INSTANCE_TYPE.get("memSizeGB").asDouble(),
          idt);
    }
  }

  public KubernetesProviderFormData suggestedKubernetesConfigs() {
    try {
      Multimap<String, String> regionToAZ = computeKubernetesRegionToZoneInfo();
      if (regionToAZ.isEmpty()) {
        LOG.info(
            "No regions and zones found, check if the region and zone labels are present on the"
                + " nodes. https://k8s.io/docs/reference/labels-annotations-taints/");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "No region and zone information found.");
      }

      String storageClass = appConfig.getString("yb.kubernetes.storageClass");
      String pullSecretName = appConfig.getString("yb.kubernetes.pullSecretName");
      if (storageClass == null || pullSecretName == null) {
        LOG.error("Required configuration keys from yb.kubernetes.* are missing.");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Required configuration is missing.");
      }
      String pullSecretContent = getKubernetesPullSecretContent(pullSecretName);

      KubernetesProviderFormData formData = new KubernetesProviderFormData();
      formData.code = kubernetes;
      if (pullSecretContent != null) {
        formData.config =
            ImmutableMap.of(
                "KUBECONFIG_IMAGE_PULL_SECRET_NAME", pullSecretName,
                "KUBECONFIG_PULL_SECRET_NAME", pullSecretName, // filename
                "KUBECONFIG_PULL_SECRET_CONTENT", pullSecretContent);
      }

      for (String region : regionToAZ.keySet()) {
        KubernetesProviderFormData.RegionData regionData =
            new KubernetesProviderFormData.RegionData();
        regionData.code = region;
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
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  } // Performs region and zone discovery based on

  // topology/failure-domain labels from the Kubernetes nodes.
  private Multimap<String, String> computeKubernetesRegionToZoneInfo() {
    JsonNode nodeInfos = kubernetesManager.getNodeInfos(null);
    Multimap<String, String> regionToAZ = HashMultimap.create();
    for (JsonNode nodeInfo : nodeInfos.path("items")) {
      JsonNode nodeLabels = nodeInfo.path("metadata").path("labels");
      // failure-domain.beta.k8s.io is deprecated as of 1.17
      String region = nodeLabels.path("topology.kubernetes.io/region").asText();
      if (region.isEmpty()) {
        region = nodeLabels.path("failure-domain.beta.kubernetes.io/region").asText();
      }
      String zone = nodeLabels.path("topology.kubernetes.io/zone").asText();
      zone =
          zone.isEmpty()
              ? nodeLabels.path("failure-domain.beta.kubernetes.io/zone").asText()
              : zone;
      if (region.isEmpty() || zone.isEmpty()) {
        LOG.debug(
            "Value of the zone or region label is empty for "
                + nodeInfo.path("metadata").path("name").asText()
                + ", skipping.");
        continue;
      }
      regionToAZ.put(region, zone);
    }
    return regionToAZ;
  } // Fetches the secret secretName from current namespace, removes

  // extra metadata and returns the secret as JSON string. Returns
  // null if the secret is not present.
  private String getKubernetesPullSecretContent(String secretName) {
    JsonNode pullSecretJson;
    try {
      pullSecretJson = kubernetesManager.getSecret(null, secretName, null);
    } catch (RuntimeException e) {
      if (e.getMessage().contains("Error from server (NotFound): secrets")) {
        LOG.debug(
            "The pull secret " + secretName + " is not present, provider won't have this field.");
        return null;
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Unable to fetch the pull secret.");
    }
    JsonNode secretMetadata = pullSecretJson.get("metadata");
    if (secretMetadata == null) {
      LOG.error(
          "metadata of the pull secret " + secretName + " is missing. This should never happen.");
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching the pull secret.");
    }
    ((ObjectNode) secretMetadata)
        .remove(
            ImmutableList.of(
                "namespace", "uid", "selfLink", "creationTimestamp", "resourceVersion"));
    JsonNode secretAnnotations = secretMetadata.get("annotations");
    if (secretAnnotations != null) {
      ((ObjectNode) secretAnnotations).remove("kubectl.kubernetes.io/last-applied-configuration");
    }
    return pullSecretJson.toString();
  }

  public Provider setupNewDockerProvider(Customer customer) {
    Provider newProvider = Provider.create(customer.uuid, Common.CloudType.docker, "Docker");
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
            InstanceType.createWithMetadata(newProvider.uuid, itCode, Json.toJson(metadata)));
    return newProvider;
  }

  public UUID bootstrap(Customer customer, Provider provider, CloudBootstrap.Params taskParams) {
    // Set the top-level provider info.
    taskParams.providerUUID = provider.uuid;
    if (taskParams.destVpcId != null && !taskParams.destVpcId.isEmpty()) {
      if (provider.code.equals("gcp")) {
        // We need to save the destVpcId into the provider config, because we'll need it during
        // instance creation. Technically, we could make it a ybcloud parameter, but we'd still need
        // to
        // store it somewhere and the config is the easiest place to put it. As such, since all the
        // config is loaded up as env vars anyway, might as well use in in devops like that...
        Map<String, String> config = provider.getUnmaskedConfig();
        config.put("CUSTOM_GCE_NETWORK", taskParams.destVpcId);
        provider.setConfig(config);
        provider.save();
      } else if (provider.code.equals("aws")) {
        taskParams.destVpcId = null;
      }
    }

    // If the regionList is still empty by here, then we need to list the regions available.
    if (taskParams.perRegionMetadata == null) {
      taskParams.perRegionMetadata = new HashMap<>();
    }
    if (taskParams.perRegionMetadata.isEmpty()) {
      List<String> regionCodes = queryHelper.getRegionCodes(provider);
      for (String regionCode : regionCodes) {
        taskParams.perRegionMetadata.put(regionCode, new CloudBootstrap.Params.PerRegionMetadata());
      }
    }

    UUID taskUUID = commissioner.submit(TaskType.CloudBootstrap, taskParams);
    CustomerTask.create(
        customer,
        provider.uuid,
        taskUUID,
        CustomerTask.TargetType.Provider,
        CustomerTask.TaskType.Create,
        provider.name);
    return taskUUID;
  }

  public void editProvider(Provider provider, EditProviderRequest editProviderReq)
      throws IOException {
    if (Provider.HostedZoneEnabledProviders.contains(provider.code)) {
      String hostedZoneId = editProviderReq.hostedZoneId;
      if (hostedZoneId == null || hostedZoneId.length() == 0) {
        throw new PlatformServiceException(BAD_REQUEST, "Required field hosted zone id");
      }
      validateAndUpdateHostedZone(provider, hostedZoneId);
    } else if (provider.code.equals(kubernetes.toString())) {
      if (editProviderReq.config != null) {
        updateKubeConfig(provider, editProviderReq.config, true);
      } else {
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Could not parse config");
      }
    } else {
      throw new PlatformServiceException(
          BAD_REQUEST, "Expected aws/k8s, but found providers with code: " + provider.code);
    }
  }

  private void validateAndUpdateHostedZone(Provider provider, String hostedZoneId) {
    // TODO: do we have a good abstraction to inspect this AND know that it's an error outside?
    ShellResponse response = dnsManager.listDnsRecord(provider.uuid, hostedZoneId);
    if (response.code != 0) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Invalid devops API response: " + response.message);
    }
    // The result returned from devops should be of the form
    // {
    //    "name": "dev.yugabyte.com."
    // }
    JsonNode hostedZoneData = Json.parse(response.message);
    hostedZoneData = hostedZoneData.get("name");
    if (hostedZoneData == null || hostedZoneData.asText().isEmpty()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Invalid devops API response: " + response.message);
    }
    provider.updateHostedZone(hostedZoneId, hostedZoneData.asText());
  }

  public void refreshPricing(UUID customerUUID, Provider provider) {
    if (provider.code.equals("gcp")) {
      gcpInitializer.initialize(customerUUID, provider.uuid);
    } else if (provider.code.equals("azu")) {
      azuInitializer.initialize(customerUUID, provider.uuid);
    } else {
      awsInitializer.initialize(customerUUID, provider.uuid);
    }
  }
}
