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

import static com.yugabyte.yw.commissioner.Common.CloudType.aws;
import static com.yugabyte.yw.commissioner.Common.CloudType.gcp;
import static com.yugabyte.yw.commissioner.Common.CloudType.kubernetes;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.DockerInstanceTypeMetadata;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.DockerRegionMetadata;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.base.Strings;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Multimap;
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.aws.AWSInitializer;
import com.yugabyte.yw.cloud.azu.AZUInitializer;
import com.yugabyte.yw.cloud.gcp.GCPCloudImpl;
import com.yugabyte.yw.cloud.gcp.GCPInitializer;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
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
import io.ebean.annotation.Transactional;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.Secret;
import java.io.File;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.PersistenceException;
import org.apache.commons.collections4.MapUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.Environment;
import play.libs.Json;

public class CloudProviderHandler {
  public static final String YB_FIREWALL_TAGS = "YB_FIREWALL_TAGS";

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
              + "{\"instanceTypeCode\": \"xmedium\", \"numCores\": 12, \"memSizeGB\": 15},"
              + "{\"instanceTypeCode\": \"large\", \"numCores\": 16, \"memSizeGB\": 15},"
              + "{\"instanceTypeCode\": \"xlarge\", \"numCores\": 32, \"memSizeGB\": 30}]");

  @Inject private Commissioner commissioner;
  @Inject private ConfigHelper configHelper;
  @Inject private AccessManager accessManager;
  @Inject private DnsManager dnsManager;
  @Inject private Environment environment;
  @Inject private CloudAPI.Factory cloudAPIFactory;
  @Inject private KubernetesManagerFactory kubernetesManagerFactory;
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
      accessManager.deleteKeyByProvider(
          provider, accessKey.getKeyCode(), accessKey.getKeyInfo().deleteRemote);
      accessKey.delete();
    }
    NodeInstance.deleteByProvider(provider.uuid);
    InstanceType.deleteInstanceTypesForProvider(provider, config, configHelper);
    provider.delete();
  }

  @Transactional
  public Provider createProvider(
      Customer customer,
      Common.CloudType providerCode,
      String providerName,
      Map<String, String> providerConfig,
      String anyProviderRegion) {
    Provider existentProvider = Provider.get(customer.uuid, providerName, providerCode);
    if (existentProvider != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Provider with the name %s already exists", providerName));
    }
    Provider provider = Provider.create(customer.uuid, providerCode, providerName);
    if (!providerConfig.isEmpty()) {
      // Perform for all cloud providers as it does validation.
      maybeUpdateProviderConfig(provider, providerConfig, anyProviderRegion);
      switch (provider.code) {
        case "aws": // Fall through to the common code.
        case "azu":
          // TODO: Add this validation. But there is a bad test.
          //  if (anyProviderRegion == null || anyProviderRegion.isEmpty()) {
          //    throw new YWServiceException(BAD_REQUEST, "Must have at least one region");
          //  }
          String hostedZoneId = provider.getHostedZoneId();
          if (hostedZoneId != null && hostedZoneId.length() != 0) {
            validateAndUpdateHostedZone(provider, hostedZoneId);
          }
          break;
        case "kubernetes":
          updateKubeConfig(provider, providerConfig, false);
          try {
            createKubernetesInstanceTypes(customer, provider);
          } catch (PersistenceException ex) {
            // TODO: make instance types more multi-tenant friendly...
          }
          break;
      }
    }
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
                "No Kubeconfig found at any level, in-cluster service account credentials will be used.");
          }
        }
      }
    }

    Map<String, String> config = formData.config;
    Provider provider = Provider.create(customer.uuid, providerCode, formData.name);
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
          az.updateConfig(ImmutableMap.of("KUBECONFIG", ""));
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
  public Provider createKubernetesNew(Customer customer, Provider reqProvider) {
    Common.CloudType providerCode = CloudType.valueOf(reqProvider.code);
    if (!providerCode.equals(kubernetes)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "API for only kubernetes provider creation: " + providerCode);
    }
    if (reqProvider.regions.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Need regions in provider");
    }
    boolean hasConfigInProvider = reqProvider.getUnmaskedConfig().containsKey("KUBECONFIG_NAME");
    for (Region rd : reqProvider.regions) {
      boolean hasConfig = hasConfigInProvider;
      if (rd.getUnmaskedConfig().containsKey("KUBECONFIG_NAME")) {
        if (hasConfig) {
          throw new PlatformServiceException(BAD_REQUEST, "Kubeconfig can't be at two levels");
        }
        hasConfig = true;
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
          az.updateConfig(ImmutableMap.of("KUBECONFIG", ""));
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

  public boolean updateKubeConfig(Provider provider, Map<String, String> config, boolean edit) {
    return updateKubeConfigForRegion(provider, null, config, edit);
  }

  public boolean updateKubeConfigForRegion(
      Provider provider, Region region, Map<String, String> config, boolean edit) {
    return updateKubeConfigForZone(provider, region, null, config, edit);
  }

  public boolean updateKubeConfigForZone(
      Provider provider,
      Region region,
      AvailabilityZone zone,
      Map<String, String> config,
      boolean edit) {
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

  private void updateProviderConfig(Provider provider, Map<String, String> config) {
    Map<String, String> newConfig = new HashMap<>(config);
    if ("gcp".equals(provider.code)) {
      // Remove these keys to avoid generating a credentials file unnecessarily.
      config.remove(GCPCloudImpl.GCE_HOST_PROJECT_PROPERTY);
      String ybFirewallTags = config.remove(YB_FIREWALL_TAGS);
      if (!config.isEmpty()) {
        String gcpCredentialsFile =
            accessManager.createCredentialsFile(provider.uuid, Json.toJson(config));
        String projectId = config.get(GCPCloudImpl.PROJECT_ID_PROPERTY);
        if (projectId != null) {
          newConfig.put(GCPCloudImpl.GCE_PROJECT_PROPERTY, projectId);
        }
        String clientEmail = config.get(GCPCloudImpl.CLIENT_EMAIL_PROPERTY);
        if (clientEmail != null) {
          newConfig.put(GCPCloudImpl.GCE_EMAIL_PROPERTY, clientEmail);
        }
        if (gcpCredentialsFile != null) {
          newConfig.put(GCPCloudImpl.GOOGLE_APPLICATION_CREDENTIALS_PROPERTY, gcpCredentialsFile);
        }
        if (ybFirewallTags != null) {
          newConfig.put(YB_FIREWALL_TAGS, ybFirewallTags);
        }
      }
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
    List<Node> nodes = kubernetesManagerFactory.getManager().getNodeInfos(null);
    Multimap<String, String> regionToAZ = HashMultimap.create();
    nodes.forEach(
        node -> {
          Map<String, String> labels = node.getMetadata().getLabels();
          if (labels == null) {
            return;
          }
          String region = labels.get("topology.kubernetes.io/region");
          if (region == null) {
            region = labels.get("failure-domain.beta.kubernetes.io/region");
          }
          String zone = labels.get("topology.kubernetes.io/zone");
          if (zone == null) {
            zone = labels.get("failure-domain.beta.kubernetes.io/zone");
          }
          if (region == null || zone == null) {
            LOG.debug(
                "Value of the zone or region label is empty for "
                    + node.getMetadata().getName()
                    + ", skipping.");
            return;
          }
          regionToAZ.put(region, zone);
        });
    return regionToAZ;
  } // Fetches the secret secretName from current namespace, removes

  // extra metadata and returns the secret as JSON string. Returns
  // null if the secret is not present.
  private String getKubernetesPullSecretContent(String secretName) {
    Secret pullSecret;
    try {
      pullSecret = kubernetesManagerFactory.getManager().getSecret(null, secretName, null);
    } catch (RuntimeException e) {
      if (e.getMessage().contains("Error from server (NotFound): secrets")) {
        LOG.debug(
            "The pull secret " + secretName + " is not present, provider won't have this field.");
        return null;
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Unable to fetch the pull secret.");
    }
    if (pullSecret.getMetadata() == null) {
      LOG.error(
          "metadata of the pull secret " + secretName + " is missing. This should never happen.");
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching the pull secret.");
    }

    ObjectMeta metadata = pullSecret.getMetadata();
    metadata.setNamespace(null);
    metadata.setUid(null);
    metadata.setSelfLink(null);
    metadata.setCreationTimestamp(null);
    metadata.setResourceVersion(null);

    if (metadata.getAnnotations() != null) {
      metadata.getAnnotations().remove("kubectl.kubernetes.io/last-applied-configuration");
    }
    return pullSecret.toString();
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
        config.put(GCPCloudImpl.CUSTOM_GCE_NETWORK_PROPERTY, taskParams.destVpcId);
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

  private Set<Region> checkIfRegionsToAdd(Provider editProviderReq, Provider provider) {
    Set<Region> regionsToAdd = new HashSet<>();
    if (provider.getCloudCode().canAddRegions()) {
      if (editProviderReq.regions != null && !editProviderReq.regions.isEmpty()) {
        Set<String> newRegionCodes =
            editProviderReq.regions.stream().map(region -> region.code).collect(Collectors.toSet());
        Set<String> existingRegionCodes =
            provider.regions.stream().map(region -> region.code).collect(Collectors.toSet());
        newRegionCodes.removeAll(existingRegionCodes);
        if (!newRegionCodes.isEmpty()) {
          regionsToAdd =
              editProviderReq
                  .regions
                  .stream()
                  .filter(region -> newRegionCodes.contains(region.code))
                  .collect(Collectors.toSet());
        }
      }
      if (!regionsToAdd.isEmpty()) {
        // Perform validation for necessary fields
        if (provider.getCloudCode() == gcp) {
          if (editProviderReq.destVpcId == null) {
            throw new PlatformServiceException(BAD_REQUEST, "Required field dest vpc id for GCP");
          }
        }
        // Verify access key exists. If no value provided, we use the default keycode.
        String accessKeyCode;
        if (Strings.isNullOrEmpty(editProviderReq.keyPairName)) {
          LOG.debug("Access key not specified, using default key code...");
          accessKeyCode = AccessKey.getDefaultKeyCode(provider);
        } else {
          accessKeyCode = editProviderReq.keyPairName;
        }
        AccessKey.getOrBadRequest(provider.uuid, accessKeyCode);

        regionsToAdd.forEach(
            region -> {
              if (region.getVnetName() == null && provider.getCloudCode() == aws) {
                throw new PlatformServiceException(
                    BAD_REQUEST, "Required field vnet name (VPC ID) for region: " + region.code);
              }
              if (region.zones == null || region.zones.isEmpty()) {
                throw new PlatformServiceException(
                    BAD_REQUEST, "Zone info needs to be specified for region: " + region.code);
              }
              region.zones.forEach(
                  zone -> {
                    if (zone.subnet == null) {
                      throw new PlatformServiceException(
                          BAD_REQUEST, "Required field subnet for zone: " + zone.code);
                    }
                  });
            });
      }
    }
    return regionsToAdd;
  }

  public UUID editProvider(
      Customer customer, Provider provider, Provider editProviderReq, String anyProviderRegion) {
    // Check if region edit mode.
    Set<Region> regionsToAdd = checkIfRegionsToAdd(editProviderReq, provider);
    boolean providerDataUpdated =
        updateProviderData(customer, provider, editProviderReq, anyProviderRegion, regionsToAdd);
    UUID taskUUID = maybeAddRegions(customer, editProviderReq, provider, regionsToAdd);
    if (!providerDataUpdated && taskUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No changes to be made for provider type: " + provider.code);
    }
    return taskUUID;
  }

  @Transactional
  public boolean updateProviderData(
      Customer customer,
      Provider provider,
      Provider editProviderReq,
      String anyProviderRegion,
      Set<Region> regionsToAdd) {
    Map<String, String> unmaskedConfig = editProviderReq.getUnmaskedConfig();
    boolean updatedHostedZone =
        maybeUpdateHostedZone(provider, editProviderReq.hostedZoneId, regionsToAdd);
    boolean updatedProviderConfig =
        maybeUpdateProviderConfig(provider, unmaskedConfig, anyProviderRegion);
    boolean updatedKubeConfig = maybeUpdateKubeConfig(provider, unmaskedConfig);
    return updatedHostedZone || updatedProviderConfig || updatedKubeConfig;
  }

  private UUID maybeAddRegions(
      Customer customer, Provider editProviderReq, Provider provider, Set<Region> regionsToAdd) {
    if (!regionsToAdd.isEmpty()) {
      // Validate regions to add. We only support providing custom VPCs for now.
      // So the user must have entered the VPC Info for the regions, as well as
      // the zone info.
      CloudBootstrap.Params taskParams = new CloudBootstrap.Params();
      taskParams.keyPairName =
          Strings.isNullOrEmpty(editProviderReq.keyPairName)
              ? AccessKey.getDefaultKeyCode(provider)
              : editProviderReq.keyPairName;
      taskParams.overrideKeyValidate = editProviderReq.overrideKeyValidate;
      taskParams.providerUUID = provider.uuid;
      taskParams.destVpcId = editProviderReq.destVpcId;
      taskParams.perRegionMetadata =
          regionsToAdd
              .stream()
              .collect(
                  Collectors.toMap(
                      region -> region.name, CloudBootstrap.Params.PerRegionMetadata::fromRegion));
      // Only adding new regions in the bootstrap task.
      taskParams.regionAddOnly = true;
      UUID taskUUID = commissioner.submit(TaskType.CloudBootstrap, taskParams);
      CustomerTask.create(
          customer,
          provider.uuid,
          taskUUID,
          CustomerTask.TargetType.Provider,
          CustomerTask.TaskType.Update,
          provider.name);
      return taskUUID;
    }
    return null;
  }

  private boolean maybeUpdateKubeConfig(Provider provider, Map<String, String> providerConfig) {
    if (provider.getCloudCode() == CloudType.kubernetes) {
      if (MapUtils.isEmpty(providerConfig)) {
        // This must be set for kubernetes.
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Could not parse config");
      }
      updateKubeConfig(provider, providerConfig, true);
      return true;
    }
    return false;
  }

  private boolean maybeUpdateHostedZone(
      Provider provider, String hostedZoneId, Set<Region> regionsToAdd) {
    if (provider.getCloudCode().isHostedZoneEnabled()) {
      if (Strings.isNullOrEmpty(hostedZoneId)) {
        return false;
      }
      validateAndUpdateHostedZone(provider, hostedZoneId);
      return true;
    }
    return false;
  }

  // Merges the config from a provider to another provider.
  // Only the non-existing config keys are copied.
  public void mergeProviderConfig(Provider fromProvider, Provider toProvider) {
    Map<String, String> providerConfig = toProvider.getUnmaskedConfig();
    if (MapUtils.isEmpty(providerConfig)) {
      return;
    }
    Map<String, String> existingConfig = null;
    if ("gcp".equalsIgnoreCase(fromProvider.code)) {
      // For GCP, the config in the DB is derived from the credentials file.
      existingConfig = accessManager.readCredentialsFromFile(fromProvider.uuid);
    } else {
      existingConfig = fromProvider.getUnmaskedConfig();
    }
    Set<String> unknownKeys = Sets.difference(providerConfig.keySet(), existingConfig.keySet());
    if (!unknownKeys.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST, " Unknown keys found: " + new TreeSet<>(unknownKeys));
    }
    existingConfig = new HashMap<>(existingConfig);
    existingConfig.putAll(providerConfig);
    toProvider.setConfig(existingConfig);
  }

  private boolean maybeUpdateProviderConfig(
      Provider provider, Map<String, String> providerConfig, String anyProviderRegion) {
    if (MapUtils.isEmpty(providerConfig)) {
      return false;
    }
    CloudAPI cloudAPI = cloudAPIFactory.get(provider.code);
    if (cloudAPI != null && !cloudAPI.isValidCreds(providerConfig, anyProviderRegion)) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Invalid %s Credentials.", provider.code.toUpperCase()));
    }
    updateProviderConfig(provider, providerConfig);
    return true;
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
