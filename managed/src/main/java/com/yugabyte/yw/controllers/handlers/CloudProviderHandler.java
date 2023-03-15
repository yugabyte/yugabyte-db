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
import static com.yugabyte.yw.commissioner.Common.CloudType.onprem;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.DockerInstanceTypeMetadata;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.DockerRegionMetadata;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Multimap;
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
import com.yugabyte.yw.commissioner.tasks.CloudProviderDelete;
import com.yugabyte.yw.commissioner.tasks.params.ScheduledAccessKeyRotateParams;
import com.yugabyte.yw.common.AccessKeyRotationUtil;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.EditAccessKeyRotationScheduleParams;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.RegionEditFormData;
import com.yugabyte.yw.models.AccessKey;
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
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.ProviderValidator;
import io.ebean.annotation.Transactional;
import io.fabric8.kubernetes.api.model.Container;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.utils.Serialization;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import javax.persistence.PersistenceException;
import org.apache.commons.collections.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;
import play.libs.Json;

@Singleton
public class CloudProviderHandler {
  public static final String YB_FIREWALL_TAGS = "YB_FIREWALL_TAGS";
  public static final String SKIP_KEYPAIR_VALIDATION_KEY = "yb.provider.skip_keypair_validation";
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
  @Inject private ProviderValidator providerValidator;
  @Inject private KubernetesManagerFactory kubernetesManagerFactory;
  @Inject private Config config;
  @Inject private CloudQueryHelper queryHelper;
  @Inject private AccessKeyRotationUtil accessKeyRotationUtil;
  @Inject private ProviderEditRestrictionManager providerEditRestrictionManager;
  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private AvailabilityZoneHandler availabilityZoneHandler;
  @Inject private RegionHandler regionHandler;

  @Inject private AWSInitializer awsInitializer;
  @Inject private GCPInitializer gcpInitializer;
  @Inject private AZUInitializer azuInitializer;

  public UUID delete(Customer customer, UUID providerUUID) {
    CloudProviderDelete.Params params = new CloudProviderDelete.Params();
    params.providerUUID = providerUUID;
    params.customer = customer;

    UUID taskUUID = commissioner.submit(TaskType.CloudProviderDelete, params);
    Provider provider = Provider.getOrBadRequest(customer.uuid, providerUUID);
    CustomerTask.create(
        customer,
        providerUUID,
        taskUUID,
        CustomerTask.TargetType.Provider,
        CustomerTask.TaskType.Delete,
        provider.name);

    return taskUUID;
  }

  @Transactional
  public Provider createProvider(
      Customer customer,
      Common.CloudType providerCode,
      String providerName,
      Provider reqProvider,
      boolean validate) {
    Provider existentProvider = Provider.get(customer.uuid, providerName, providerCode);
    if (existentProvider != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Provider with the name %s already exists", providerName));
    }
    // TODO: Remove this code once the validators are added for all cloud provider.
    CloudAPI cloudAPI = cloudAPIFactory.get(providerCode.toString());
    if (cloudAPI != null && !cloudAPI.isValidCreds(reqProvider, getFirstRegionCode(reqProvider))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("Invalid %s Credentials.", providerCode.toString().toUpperCase()));
    }
    if (validate) {
      providerValidator.validate(reqProvider);
    }
    Provider provider =
        Provider.create(customer.uuid, providerCode, providerName, reqProvider.details);
    maybeUpdateVPC(provider);

    Map<String, String> providerConfig = CloudInfoInterface.fetchEnvVars(provider);
    if (!providerConfig.isEmpty()) {
      // Perform for all cloud providers as it does validation.
      if (provider.getCloudCode().equals(kubernetes)) {
        updateKubeConfig(provider, providerConfig, false);
        try {
          createKubernetesInstanceTypes(customer, provider);
        } catch (PersistenceException ex) {
          // TODO: make instance types more multi-tenant friendly...
        }
      } else {
        maybeUpdateCloudProviderConfig(provider, providerConfig);
      }
    }
    provider.save();

    return provider;
  }

  private void validateInstanceTemplate(Provider provider, CloudBootstrap.Params taskParams) {
    // Validate instance template, if provided. Only supported for GCP currently.
    taskParams.perRegionMetadata.forEach(
        (region, metadata) -> {
          if (metadata.instanceTemplate != null) {
            CloudAPI cloudAPI = cloudAPIFactory.get(provider.code);
            cloudAPI.validateInstanceTemplate(provider, metadata.instanceTemplate);
          }
        });
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
    Provider provider = Provider.create(customer.uuid, providerCode, formData.name, config);
    Map<String, String> providerConfig = CloudInfoInterface.fetchEnvVars(provider);

    boolean isConfigInProvider = updateKubeConfig(provider, providerConfig, false);
    List<KubernetesProviderFormData.RegionData> regionList = formData.regionList;
    for (KubernetesProviderFormData.RegionData rd : regionList) {
      Map<String, String> regionConfig = rd.config;
      Region region = Region.create(provider, rd.code, rd.name, null, rd.latitude, rd.longitude);
      CloudInfoInterface.setCloudProviderInfoFromConfig(region, regionConfig);
      regionConfig = CloudInfoInterface.fetchEnvVars(region);
      boolean isConfigInRegion = updateKubeConfigForRegion(provider, region, regionConfig, false);
      for (KubernetesProviderFormData.RegionData.ZoneData zd : rd.zoneList) {
        Map<String, String> zoneConfig = zd.config;
        AvailabilityZone az = AvailabilityZone.createOrThrow(region, zd.code, zd.name, null, null);
        CloudInfoInterface.setCloudProviderInfoFromConfig(az, zoneConfig);
        zoneConfig = CloudInfoInterface.fetchEnvVars(az);
        boolean isConfigInZone = updateKubeConfigForZone(provider, region, az, zoneConfig, false);
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
    Map<String, String> providerConfig = CloudInfoInterface.fetchEnvVars(reqProvider);

    boolean hasConfigInProvider = providerConfig.containsKey("KUBECONFIG_NAME");
    for (Region rd : reqProvider.regions) {
      boolean hasConfig = hasConfigInProvider;
      Map<String, String> regionConfig = CloudInfoInterface.fetchEnvVars(rd);
      if (regionConfig.containsKey("KUBECONFIG_NAME")) {
        if (hasConfig) {
          throw new PlatformServiceException(BAD_REQUEST, "Kubeconfig can't be at two levels");
        }
        hasConfig = true;
      }
      if (rd.zones.isEmpty()) {
        throw new PlatformServiceException(BAD_REQUEST, "No zone provided in region");
      }
      for (AvailabilityZone zd : rd.zones) {
        Map<String, String> zoneConfig = CloudInfoInterface.fetchEnvVars(zd);
        if (zoneConfig.containsKey("KUBECONFIG_NAME")) {
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

    Provider provider =
        Provider.create(customer.uuid, providerCode, reqProvider.name, reqProvider.details);

    boolean isConfigInProvider = updateKubeConfig(provider, providerConfig, false);
    List<Region> regionList = reqProvider.regions;
    for (Region rd : regionList) {
      Map<String, String> regionConfig = CloudInfoInterface.fetchEnvVars(rd);
      Region region =
          Region.create(provider, rd.code, rd.name, null, rd.latitude, rd.longitude, rd.details);
      boolean isConfigInRegion = updateKubeConfigForRegion(provider, region, regionConfig, false);
      for (AvailabilityZone zd : rd.zones) {
        Map<String, String> zoneConfig = CloudInfoInterface.fetchEnvVars(zd);
        AvailabilityZone az =
            AvailabilityZone.createOrThrow(region, zd.code, zd.name, null, null, zd.details);
        boolean isConfigInZone = updateKubeConfigForZone(provider, region, az, zoneConfig, false);
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
    return providerEditRestrictionManager.tryEditProvider(
        provider.uuid, () -> doUpdateKubeConfigForZone(provider, region, zone, config, edit));
  }

  public static String generateRandomString(int length, String prefix, String suffix) {
    String chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    StringBuilder sb = new StringBuilder(length);
    sb.append(prefix);
    Random random = new Random();
    for (int i = 0; i < length; i++) {
      int index = random.nextInt(chars.length());
      sb.append(chars.charAt(index));
    }
    sb.append(suffix);
    return sb.toString();
  }

  private boolean doUpdateKubeConfigForZone(
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

    KubernetesInfo k8sMetadata = null;
    if (region == null) {
      k8sMetadata = CloudInfoInterface.get(provider);
    } else if (zone == null) {
      k8sMetadata = CloudInfoInterface.get(region);
    } else {
      k8sMetadata = CloudInfoInterface.get(zone);
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

      if (kubeConfigFile != null) {
        k8sMetadata.setKubeConfig(kubeConfigFile);
        k8sMetadata.setKubeConfigContent(null);
        k8sMetadata.setKubeConfigName(null);
      }
    }

    if (config.containsKey("STORAGE_CLASS") && k8sMetadata != null) {
      k8sMetadata.setKubernetesStorageClass(config.get("STORAGE_CLASS"));
    }
    if (region == null) {
      if (config.containsKey("KUBECONFIG_PULL_SECRET_NAME")) {
        if (config.get("KUBECONFIG_PULL_SECRET_NAME") != null) {
          pullSecretFile = accessManager.createPullSecret(provider.uuid, config, edit);
        }
      }
      if (pullSecretFile != null && k8sMetadata != null) {
        k8sMetadata.setKubernetesPullSecret(pullSecretFile);
        k8sMetadata.setKubernetesPullSecretName(null);
        k8sMetadata.setKubernetesPullSecretContent(null);
      }
    }
    return hasKubeConfig;
  }

  private void updateGCPProviderConfig(Provider provider, Map<String, String> config) {
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
    JsonNode gcpCredentials = gcpCloudInfo.getGceApplicationCredentials();
    if (gcpCredentials != null) {
      String gcpCredentialsFile =
          accessManager.createGCPCredentialsFile(provider.uuid, gcpCredentials);
      if (gcpCredentialsFile != null) {
        gcpCloudInfo.setGceApplicationCredentialsPath(gcpCredentialsFile);
      }
    }
    if (!config.isEmpty()) {
      if (config.containsKey(GCPCloudImpl.GCE_PROJECT_PROPERTY)) {
        gcpCloudInfo.setGceProject(config.get(GCPCloudImpl.GCE_PROJECT_PROPERTY));
      }
      if (config.containsKey(YB_FIREWALL_TAGS)) {
        gcpCloudInfo.setYbFirewallTags(config.get(YB_FIREWALL_TAGS));
      }
    }
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
      String storageClass = config.getString("yb.kubernetes.storageClass");
      String pullSecretName = config.getString("yb.kubernetes.pullSecretName");
      if (storageClass == null || pullSecretName == null) {
        LOG.error("Required configuration keys from yb.kubernetes.* are missing.");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Required configuration is missing.");
      }
      Secret pullSecret = getKubernetesPullSecret(pullSecretName);

      KubernetesProviderFormData formData = new KubernetesProviderFormData();
      formData.code = kubernetes;
      formData.name = generateRandomString(5, "k8s", "provider");

      if (pullSecret != null) {
        formData.config =
            ImmutableMap.of(
                "KUBECONFIG_IMAGE_PULL_SECRET_NAME",
                pullSecretName,
                "KUBECONFIG_PROVIDER",
                getCloudProvider(),
                "KUBECONFIG_PULL_SECRET_NAME",
                pullSecretName, // filename
                "KUBECONFIG_PULL_SECRET_CONTENT",
                Serialization.asYaml(pullSecret), // Yaml formatted
                "KUBECONFIG_IMAGE_REGISTRY",
                getKubernetesImageRepository()); // Location of the registry
      }

      for (String region : regionToAZ.keySet()) {
        KubernetesProviderFormData.RegionData regionData =
            new KubernetesProviderFormData.RegionData();
        regionData.code = region;
        String regName = getRegionNameFromCode(region);
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
      LOG.error(
          e.getClass()
              + ": "
              + e.getMessage()
              + ": "
              + e.getCause()
              + "\n"
              + e.getStackTrace().toString());
      throw e; // new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
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

  // Extra metadata and returns the secret object.
  // Returns null if the secret is not present.
  private Secret getKubernetesPullSecret(String secretName) {
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
    metadata.setManagedFields(null);

    if (metadata.getAnnotations() != null) {
      metadata.getAnnotations().remove("kubectl.kubernetes.io/last-applied-configuration");
    }
    return pullSecret;
  }

  public String getCloudProvider() {
    String cloudProvider = kubernetesManagerFactory.getManager().getCloudProvider(null);
    if (StringUtils.isEmpty(cloudProvider)) {
      return "CUSTOM";
    }
    String retVal;
    switch (cloudProvider) {
      case "gce":
        retVal = "GKE";
        break;
      case "aws":
        retVal = "EKS";
        break;
      case "azure":
        retVal = "AKS";
        break;
      default:
        retVal = "CUSTOM";
        break;
    }
    return retVal;
  }

  public String getKubernetesImageRepository() {
    String podName = System.getenv("HOSTNAME");
    if (podName == null) {
      podName = "yugaware-0";
    }

    String containerName = System.getenv("container");
    if (containerName == null) {
      containerName = "yugaware";
    }
    // Container Name can change between yugaware and yugaware-docker.
    containerName = containerName.split("-")[0];

    Pod podObject = kubernetesManagerFactory.getManager().getPodObject(null, null, podName);
    if (podObject == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching pod details for yugaware");
    }
    String imageName = null;
    List<Container> containers = podObject.getSpec().getContainers();
    for (Container c : containers) {
      if (containerName.equals(c.getName())) {
        imageName = c.getImage();
      }
    }
    if (imageName == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching image details for yugaware");
    }
    String[] parsed = imageName.split("/");
    /* Algorithm Used
    Take last element, split it with ":", take the 0th element of that list.
    in that element replace string yugaware with yugabyte.
    gcr.io/yugabyte/dev-ci-yugaware:2.17.2.0-b9999480 -> gcr.io/yugabyte/dev-ci-yugabyte */
    parsed[parsed.length - 1] = parsed[parsed.length - 1].split(":")[0];
    parsed[parsed.length - 1] = parsed[parsed.length - 1].replace("yugaware", "yugabyte");
    return String.join("/", parsed);
  }

  public String getRegionNameFromCode(String code) {
    LOG.info("Code is:", code);
    String regionFile = "k8s_regions.json";
    InputStream inputStream = environment.resourceAsStream(regionFile);
    JsonNode jsonNode = Json.parse(inputStream);
    JsonNode nameNode = jsonNode.get(code);
    if (nameNode == null || nameNode.isMissingNode()) {
      LOG.info("Could not find code in file, sending it back as name");
      return code;
    }
    return jsonNode.get(code).asText();
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
    taskParams.skipKeyPairValidate =
        runtimeConfigFactory.forProvider(provider).getBoolean(SKIP_KEYPAIR_VALIDATION_KEY);

    // If the regionList is still empty by here, then we need to list the regions available.
    if (taskParams.perRegionMetadata == null) {
      taskParams.perRegionMetadata = new HashMap<>();
    }
    if (taskParams.perRegionMetadata.isEmpty()
        && !provider.getCloudCode().equals(Common.CloudType.onprem)) {
      List<String> regionCodes = queryHelper.getRegionCodes(provider);
      for (String regionCode : regionCodes) {
        taskParams.perRegionMetadata.put(regionCode, new CloudBootstrap.Params.PerRegionMetadata());
      }
    }
    validateInstanceTemplate(provider, taskParams);

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
        Map<String, Region> newRegions =
            editProviderReq.regions.stream().collect(Collectors.toMap(r -> r.code, r -> r));
        Set<String> existingRegionCodes =
            provider.regions.stream().map(region -> region.code).collect(Collectors.toSet());
        newRegions.keySet().removeAll(existingRegionCodes);
        if (!newRegions.isEmpty()) {
          regionsToAdd = new HashSet<>(newRegions.values());
        }
      }
    }
    return regionsToAdd;
  }

  private boolean removeAndUpdateRegions(Provider editProviderReq, Provider provider) {
    Map<String, Region> existingRegions =
        provider.regions.stream().collect(Collectors.toMap(r -> r.code, r -> r));
    boolean result = false;
    for (Region region : editProviderReq.regions) {
      Region oldRegion = existingRegions.get(region.code);
      if (oldRegion != null && oldRegion.isUpdateNeeded(region)) {
        LOG.debug("Editing region {}", region.code);
        regionHandler.editRegion(
            provider.customerUUID,
            provider.uuid,
            oldRegion.uuid,
            RegionEditFormData.fromRegion(region));
        result = true;
      } else if (oldRegion != null && !region.isActive() && oldRegion.isActive()) {
        LOG.debug("Deleting region {}", region.code);
        regionHandler.deleteRegion(provider.customerUUID, provider.uuid, region.uuid);
        result = true;
      }
    }
    return result;
  }

  public UUID editProvider(
      Customer customer, Provider provider, Provider editProviderReq, boolean validate) {
    return providerEditRestrictionManager.tryEditProvider(
        provider.uuid, () -> doEditProvider(customer, provider, editProviderReq, validate));
  }

  private UUID doEditProvider(
      Customer customer, Provider provider, Provider editProviderReq, boolean validate) {
    provider.setVersion(editProviderReq.getVersion());
    // We cannot change the provider type for the given provider.
    if (!provider.getCloudCode().equals(editProviderReq.getCloudCode())) {
      throw new PlatformServiceException(BAD_REQUEST, "Changing provider type is not supported!");
    }
    CloudInfoInterface.mergeSensitiveFields(provider, editProviderReq);
    // Check if region edit mode.
    Set<Region> regionsToAdd = checkIfRegionsToAdd(editProviderReq, provider);
    UUID taskUUID = null;
    if (!regionsToAdd.isEmpty()) {
      // TODO: PLAT-7258 allow adding region for auto-creating VPC case
      taskUUID = addRegions(customer, provider, regionsToAdd, true);
    }
    boolean providerModified =
        addOrRemoveAZs(editProviderReq, provider)
            | removeAndUpdateRegions(editProviderReq, provider)
            | updateProviderData(customer, provider, editProviderReq, validate);

    if (!providerModified && taskUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No changes to be made for provider type: " + provider.code);
    }
    return taskUUID;
  }

  @Transactional
  private boolean updateProviderData(
      Customer customer, Provider provider, Provider editProviderReq, boolean validate) {
    Map<String, String> providerConfig = CloudInfoInterface.fetchEnvVars(editProviderReq);
    Map<String, String> existingConfigMap = CloudInfoInterface.fetchEnvVars(provider);
    boolean updatedProviderDetails = false;
    boolean updatedProviderConfig = false;
    // TODO: Remove this code once the validators are added for all cloud provider.
    CloudAPI cloudAPI = cloudAPIFactory.get(provider.code);
    if (cloudAPI != null
        && !cloudAPI.isValidCreds(editProviderReq, getFirstRegionCode(editProviderReq))) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Invalid %s Credentials.", provider.code.toUpperCase()));
    }
    if (validate) {
      providerValidator.validate(editProviderReq);
    }
    if (!provider.details.equals(editProviderReq.details)) {
      updatedProviderDetails = true;
      provider.details = editProviderReq.details;
    }
    if (!existingConfigMap.equals(providerConfig)) {
      provider.details.cloudInfo = editProviderReq.details.cloudInfo;
      updatedProviderConfig = maybeUpdateCloudProviderConfig(provider, providerConfig);
    }
    boolean updatedKubeConfig = maybeUpdateKubeConfig(provider, providerConfig);
    boolean providerDataUpdated =
        updatedProviderConfig || updatedKubeConfig || updatedProviderDetails;
    if (providerDataUpdated) {
      // Should not increment the version number in case of no change.
      provider.save();
    }
    return providerDataUpdated;
  }

  public UUID addRegions(
      Customer customer, Provider provider, Set<Region> regionsToAdd, boolean skipBootstrap) {
    // Perform validation for necessary fields
    if (provider.getCloudCode() == gcp) {
      // TODO: Remove once we allow vpc creation for added regions
      if (skipBootstrap && provider.destVpcId == null) {
        throw new PlatformServiceException(BAD_REQUEST, "Required field dest vpc id for GCP");
      }
    }
    regionsToAdd.forEach(
        region -> {
          // TODO: Remove once we allow vpc creation for added regions
          if (skipBootstrap && region.getVnetName() == null && provider.getCloudCode() == aws) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Required field vnet name (VPC ID) for region: " + region.code);
          }
          if (region.zones == null || region.zones.isEmpty()) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Zone info needs to be specified for region: " + region.code);
          }
          region.zones.forEach(
              zone -> {
                if (zone.subnet == null && provider.getCloudCode() != onprem) {
                  throw new PlatformServiceException(
                      BAD_REQUEST, "Required field subnet for zone: " + zone.code);
                }
              });
        });

    // Validate regions to add. We only support providing custom VPCs for now.
    // So the user must have entered the VPC Info for the regions, as well as
    // the zone info.
    CloudBootstrap.Params taskParams = new CloudBootstrap.Params();
    // Assuming that at that point we already have at least one AccessKey.
    // And we can use actual one.
    taskParams.keyPairName = AccessKey.getLatestKey(provider.uuid).getKeyCode();
    taskParams.skipKeyPairValidate =
        runtimeConfigFactory.forProvider(provider).getBoolean(SKIP_KEYPAIR_VALIDATION_KEY);
    taskParams.providerUUID = provider.uuid;
    taskParams.destVpcId = provider.destVpcId;
    List<Region> allRegions = new ArrayList<>(provider.regions);
    allRegions.addAll(regionsToAdd);
    taskParams.perRegionMetadata =
        allRegions
            .stream()
            .collect(
                Collectors.toMap(
                    region -> region.name, CloudBootstrap.Params.PerRegionMetadata::fromRegion));
    taskParams.addedRegionCodes =
        regionsToAdd.stream().map(r -> r.code).collect(Collectors.toSet());
    taskParams.skipBootstrapRegion = skipBootstrap;
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

  private boolean addOrRemoveAZs(Provider editProviderReq, Provider provider) {
    boolean result = false;
    Map<String, Region> currentRegionMap =
        provider.regions.stream().collect(Collectors.toMap(r -> r.code, r -> r));

    for (Region region : editProviderReq.regions) {
      Region currentState = currentRegionMap.get(region.code);
      if (currentState != null) {
        Map<String, AvailabilityZone> currentAZs =
            currentState.zones.stream().collect(Collectors.toMap(az -> az.code, az -> az));
        for (AvailabilityZone zone : region.zones) {
          AvailabilityZone currentAZ = currentAZs.get(zone.code);
          if (currentAZ == null) {
            if (!zone.isActive()) {
              LOG.warn("Zone {} is added but not active - ignoring", zone.code);
              continue;
            }
            result = true;
            LOG.debug("Creating zone {} in region {}", zone.code, region.code);
            AvailabilityZone.createOrThrow(
                region, zone.code, zone.name, zone.subnet, zone.secondarySubnet);
          } else if (!zone.isActive() && currentAZ.isActive()) {
            LOG.debug("Deleting zone {} from region {}", zone.code, region.code);
            availabilityZoneHandler.deleteZone(zone.uuid, region.uuid);
            result = true;
          } else if (currentAZ.shouldBeUpdated(zone)) {
            LOG.debug("updating zone {}", zone.code);
            availabilityZoneHandler.editZone(
                zone.uuid,
                region.uuid,
                az -> {
                  az.setAvailabilityZoneDetails(zone.getAvailabilityZoneDetails());
                  az.secondarySubnet = zone.secondarySubnet;
                  az.subnet = zone.subnet;
                });
            result = true;
          }
        }
      }
    }
    return result;
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

  private void maybeUpdateVPC(Provider provider) {
    switch (provider.getCloudCode()) {
      case gcp:
        GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
        if (gcpCloudInfo == null) {
          return;
        }

        if (StringUtils.isBlank(gcpCloudInfo.getGceProject())) {
          /**
           * Preferences for GCP Project. 1. User provided project name. 2. `project_id` present in
           * gcp credentials user provided. 3. Metadata query to fetch the same.
           */
          ObjectNode credentialJSON = (ObjectNode) gcpCloudInfo.getGceApplicationCredentials();
          if (credentialJSON != null && credentialJSON.has("project_id")) {
            gcpCloudInfo.setGceProject(credentialJSON.get("project_id").asText());
          }
        }

        if (gcpCloudInfo.getUseHostCredentials() != null
            && gcpCloudInfo.getUseHostCredentials()
            && gcpCloudInfo.getUseHostVPC() != null
            && gcpCloudInfo.getUseHostVPC()) {
          JsonNode currentHostInfo = queryHelper.getCurrentHostInfo(provider.getCloudCode());
          if (!hasHostInfo(currentHostInfo)) {
            throw new IllegalStateException("Cannot use host vpc as there is no vpc");
          }
          String network = currentHostInfo.get("network").asText();
          gcpCloudInfo.setHostVpcId(network);
          // If destination VPC network is not specified, then we will use the
          // host VPC as for both hostVpcId and destVpcId.
          if (gcpCloudInfo.getDestVpcId() == null) {
            gcpCloudInfo.setDestVpcId(network);
          }
          if (StringUtils.isBlank(gcpCloudInfo.getGceProject())) {
            gcpCloudInfo.setGceProject(currentHostInfo.get("host_project").asText());
          }
        }
        break;
      case aws:
        JsonNode currentHostInfo = queryHelper.getCurrentHostInfo(provider.getCloudCode());
        if (hasHostInfo(currentHostInfo)) {
          AWSCloudInfo awsCloudInfo = CloudInfoInterface.get(provider);
          awsCloudInfo.setHostVpcRegion(currentHostInfo.get("region").asText());
          awsCloudInfo.setHostVpcId(currentHostInfo.get("vpc-id").asText());
        }
        break;
      default:
    }
  }

  private boolean hasHostInfo(JsonNode hostInfo) {
    return (hostInfo != null && !hostInfo.isEmpty() && !hostInfo.has("error"));
  }

  private boolean maybeUpdateCloudProviderConfig(
      Provider provider, Map<String, String> providerConfig) {
    if (MapUtils.isEmpty(providerConfig)) {
      return false;
    }
    switch (provider.getCloudCode()) {
      case aws:
      case azu: // Fall through to the common code.
        // TODO: Add this validation. But there is a bad test.
        //  if (anyProviderRegion == null || anyProviderRegion.isEmpty()) {
        //    throw new YWServiceException(BAD_REQUEST, "Must have at least one region");
        //  }
        String hostedZoneId = providerConfig.get("HOSTED_ZONE_ID");
        if (hostedZoneId != null && hostedZoneId.length() != 0) {
          validateAndUpdateHostedZone(provider, hostedZoneId);
        }
        break;
      case gcp:
        updateGCPProviderConfig(provider, providerConfig);
        break;
    }
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

    if (provider.getCloudCode().equals(CloudType.aws)) {
      AWSCloudInfo awsCloudInfo = CloudInfoInterface.get(provider);
      awsCloudInfo.setAwsHostedZoneId(hostedZoneId);
      awsCloudInfo.setAwsHostedZoneName(hostedZoneData.asText());
    } else if (provider.getCloudCode().equals(CloudType.azu)) {
      AzureCloudInfo azuCloudInfo = CloudInfoInterface.get(provider);
      azuCloudInfo.setAzuHostedZoneId(hostedZoneId);
      azuCloudInfo.setAzuHostedZoneName(hostedZoneData.asText());
    }
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
      } else if (schedule.getStatus().equals(Schedule.State.Active) && schedule.getRunningState()) {
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

  private static String getFirstRegionCode(Provider provider) {
    for (Region r : provider.regions) {
      return r.code;
    }
    return null;
  }
}
