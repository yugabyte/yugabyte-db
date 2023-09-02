package com.yugabyte.yw.common;

import static com.yugabyte.yw.commissioner.Common.CloudType.kubernetes;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.Multimap;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.gcp.GCPCloudImpl;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.AvailabilityZoneHandler;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.ProviderValidator;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import io.fabric8.kubernetes.api.model.Config;
import io.fabric8.kubernetes.api.model.Container;
import io.fabric8.kubernetes.api.model.NamedAuthInfo;
import io.fabric8.kubernetes.api.model.NamedCluster;
import io.fabric8.kubernetes.api.model.NamedContext;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.internal.KubeConfigUtils;
import java.lang.annotation.Annotation;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.MapUtils;
import org.apache.commons.lang3.StringUtils;
import play.Environment;
import play.libs.Json;

@Singleton
@Slf4j
public class CloudProviderHelper {
  public static final String YB_FIREWALL_TAGS = "YB_FIREWALL_TAGS";
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

  @Inject private AccessManager accessManager;
  @Inject private AvailabilityZoneHandler availabilityZoneHandler;
  @Inject private CloudAPI.Factory cloudAPIFactory;
  @Inject private CloudQueryHelper queryHelper;
  @Inject private ConfigHelper configHelper;
  @Inject private DnsManager dnsManager;
  @Inject private Environment environment;
  @Inject private KubernetesManagerFactory kubernetesManagerFactory;
  @Inject private ProviderValidator providerValidator;
  @Inject private RuntimeConfGetter confGetter;
  @Inject private PrometheusConfigManager prometheusConfigManager;

  public boolean editKubernetesProvider(
      Provider provider, Provider editProviderReq, Set<Region> regionsToAdd) {
    if (regionsToAdd == null || regionsToAdd.size() == 0) {
      return false;
    }
    List<Region> regions = new ArrayList<>(regionsToAdd);
    bootstrapKubernetesProvider(provider, editProviderReq, regions, true);
    return true;
  }

  public void updateAZs(
      Provider provider, Provider editProviderReq, Region region, Region currentRegion) {
    Map<String, AvailabilityZone> currentAZs =
        AvailabilityZone.getAZsForRegion(currentRegion.getUuid(), false).stream()
            .collect(Collectors.toMap(az -> az.getCode(), az -> az));
    for (AvailabilityZone zone : region.getZones()) {
      AvailabilityZone currentAZ = currentAZs.get(zone.getCode());
      if (currentAZ == null || !currentAZ.isActive()) {
        if (currentAZ != null) {
          log.debug("Hard deleting zone {}", currentAZ);
          currentAZ.delete();
        }
        log.debug("Creating zone {} in region {}", zone.getCode(), region.getCode());
        if (provider.getCloudCode().equals(kubernetes)) {
          bootstrapKubernetesProvider(
              provider, editProviderReq, region, Collections.singletonList(zone), true);
        } else {
          AvailabilityZone.createOrThrow(
              region, zone.getCode(), zone.getName(), zone.getSubnet(), zone.getSecondarySubnet());
        }
      } else if (!zone.isActive() && currentAZ.isActive()) {
        log.debug("Deleting zone {} from region {}", currentAZ.getCode(), currentRegion.getCode());
        availabilityZoneHandler.doDeleteZone(currentAZ.getUuid(), currentRegion.getUuid());
      } else if (currentAZ.isUpdateNeeded(zone) && currentAZ.isActive()) {
        log.debug("updating zone {}", zone.getCode());
        if (provider.getCloudCode().equals(kubernetes)) {
          bootstrapKubernetesProvider(
              provider, editProviderReq, currentRegion, Collections.singletonList(zone), true);
        } else {
          availabilityZoneHandler.doEditZone(
              currentAZ.getUuid(),
              currentRegion.getUuid(),
              az -> {
                az.setAvailabilityZoneDetails(zone.getAvailabilityZoneDetails());
                az.setSecondarySubnet(zone.getSecondarySubnet());
                az.setSubnet(zone.getSubnet());
              });
        }
      }
    }
  }

  public Provider bootstrapKubernetesProvider(
      Provider provider, Provider reqProvider, List<Region> regionList, boolean edit) {
    if (regionList == null) {
      regionList = reqProvider.getRegions();
    }

    Map<String, String> providerConfig = CloudInfoInterface.fetchEnvVars(reqProvider);
    boolean isConfigInProvider = updateKubeConfig(provider, providerConfig, edit);
    // We will update the pull secret related information for the provider.
    Map<String, String> updatedProviderConfig = CloudInfoInterface.fetchEnvVars(provider);

    for (Region region : regionList) {
      bootstrapKubernetesProvider(provider, reqProvider, region, region.getZones(), edit);
    }
    if (isConfigInProvider || !providerConfig.equals(updatedProviderConfig)) {
      // Top level provider properties are handled in `updateProviderData` with other provider
      // types.
      provider.save();
    }
    return provider;
  }

  public Provider bootstrapKubernetesProvider(
      Provider provider,
      Provider reqProvider,
      Region rd,
      List<AvailabilityZone> azList,
      boolean edit) {
    if (azList == null) {
      azList = rd.getZones();
    }

    Map<String, String> regionConfig = CloudInfoInterface.fetchEnvVars(rd);
    String regionCode = rd.getCode();
    Region region =
        provider.getRegions().stream()
            .filter(r -> r.getCode().equals(rd.getCode()))
            .findFirst()
            .orElse(null);
    if (region == null) {
      log.info("Region {} does not exists. Creating one...", rd.getName());
      String regionName = rd.getName();
      Double latitude = rd.getLatitude();
      Double longitude = rd.getLongitude();
      KubernetesInfo kubernetesCloudInfo = CloudInfoInterface.get(provider);
      ConfigHelper.ConfigType kubernetesConfigType =
          getKubernetesConfigType(kubernetesCloudInfo.getKubernetesProvider());
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
      region =
          Region.create(
              provider, regionCode, regionName, null, latitude, longitude, rd.getDetails());
    }
    boolean regionUpdateNeeded = region.isUpdateNeeded(rd);
    if (regionUpdateNeeded) {
      // Update the k8s region config.
      region.setDetails(rd.getDetails());
    }
    boolean isConfigInRegion = updateKubeConfigForRegion(provider, region, regionConfig, edit);
    for (AvailabilityZone zone : azList) {
      Map<String, String> zoneConfig = CloudInfoInterface.fetchEnvVars(zone);
      AvailabilityZone az = null;
      try {
        az = AvailabilityZone.getByCode(provider, zone.getCode());
      } catch (RuntimeException e) {
        log.info("Availability Zone {} does not exists. Creating one...", zone.getName());
        az =
            AvailabilityZone.createOrThrow(
                region, zone.getCode(), zone.getName(), null, null, zone.getDetails());
      }
      boolean zoneUpdateNeeded = az.isUpdateNeeded(zone);
      if (zoneUpdateNeeded) {
        // Update the k8s zone config.
        az.setDetails(zone.getDetails());
      }
      boolean isConfigInZone = updateKubeConfigForZone(provider, region, az, zoneConfig, edit);
      KubernetesInfo k8sProviderInfo = CloudInfoInterface.get(provider);
      boolean isConfigInProvider = k8sProviderInfo.getKubeConfig() != null;
      boolean useInClusterServiceAccount =
          !(isConfigInProvider || isConfigInRegion || isConfigInZone) && !edit;
      if (useInClusterServiceAccount) {
        // Use in-cluster ServiceAccount credentials
        KubernetesInfo k8sMetadata = CloudInfoInterface.get(az);
        k8sMetadata.setKubeConfig("");
      }
      if (zoneUpdateNeeded || isConfigInZone || useInClusterServiceAccount) {
        az.save();
      }
    }
    if (regionUpdateNeeded || isConfigInRegion) {
      region.save();
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

    KubernetesInfo k8sMetadata = null;
    if (region == null) {
      k8sMetadata = CloudInfoInterface.get(provider);
    } else if (zone == null) {
      k8sMetadata = CloudInfoInterface.get(region);
    } else {
      k8sMetadata = CloudInfoInterface.get(zone);
    }

    String path = provider.getUuid().toString();
    if (region != null) {
      path = path + "/" + region.getUuid().toString();
      if (zone != null) {
        path = path + "/" + zone.getUuid().toString();
      }
    }
    if (edit && k8sMetadata.getKubeConfigContent() != null) {
      String kubeConfigPath = k8sMetadata.getKubeConfig();
      if (kubeConfigPath != null) {
        String[] paths = kubeConfigPath.split("/");
        config.putIfAbsent("KUBECONFIG_NAME", paths[paths.length - 1]);
      }
    }
    boolean hasKubeConfig = config.containsKey("KUBECONFIG_NAME");
    if (hasKubeConfig) {
      kubeConfigFile = accessManager.createKubernetesConfig(path, config, edit);
      if (kubeConfigFile != null) {
        k8sMetadata.setKubeConfig(kubeConfigFile);
        try {
          saveKubeConfigAuthData(k8sMetadata, path, edit);
        } catch (Exception e) {
          log.warn(
              "Failed to save authentication data from the kubeconfig. "
                  + "Metrics from this Kubernetes cluster won't be available "
                  + "if it is an external cluster: {}",
              e.getMessage());
        }
        k8sMetadata.setKubeConfigContent(null);
        k8sMetadata.setKubeConfigName(null);
      }
    }

    if (region == null) {
      if (edit && k8sMetadata.getKubernetesPullSecretContent() != null) {
        String pullSecretPath = k8sMetadata.getKubernetesPullSecret();
        if (pullSecretPath != null) {
          String[] paths = pullSecretPath.split("/");
          config.putIfAbsent("KUBECONFIG_PULL_SECRET_NAME", paths[paths.length - 1]);
        }
      }
      if (config.containsKey("KUBECONFIG_PULL_SECRET_NAME")) {
        if (config.get("KUBECONFIG_PULL_SECRET_NAME") != null) {
          pullSecretFile = accessManager.createPullSecret(provider.getUuid(), config, edit);
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

  /**
   * Parses the given kubeconfig content and saves the details requried by Prometheus to
   * authenticate with the API server to given k8sInfo.
   */
  private KubernetesInfo saveKubeConfigAuthData(KubernetesInfo k8sInfo, String path, boolean edit) {
    String kubeConfigContent = k8sInfo.getKubeConfigContent();
    String kubeConfigName = k8sInfo.getKubeConfigName();
    if (edit && (kubeConfigContent == null || kubeConfigName == null)) {
      return null;
    }
    if (kubeConfigContent == null) {
      throw new RuntimeException("Missing kubeconfig content data in the kubernetesinfo");
    } else if (kubeConfigName == null) {
      throw new RuntimeException("Missing kubeconfig name in the kubernetesinfo");
    }

    Config kubeconfig = null;
    kubeconfig = KubeConfigUtils.parseConfigFromString(kubeConfigContent);

    String contextName = kubeconfig.getCurrentContext();
    NamedContext currentContext =
        kubeconfig.getContexts().stream()
            .filter(ctx -> ctx.getName().equals(contextName))
            .findFirst()
            .orElse(null);
    if (currentContext == null) {
      throw new RuntimeException("Context set as current context is not present in the kubeconfig");
    }
    String clusterName = currentContext.getContext().getCluster();
    String userName = currentContext.getContext().getUser();
    NamedCluster cluster =
        kubeconfig.getClusters().stream()
            .filter(c -> c.getName().equals(clusterName))
            .findFirst()
            .orElse(null);
    if (cluster == null) {
      throw new RuntimeException("Cluster from current context is not present in the kubeconfig");
    }
    NamedAuthInfo authInfo =
        kubeconfig.getUsers().stream()
            .filter(u -> u.getName().equals(userName))
            .findFirst()
            .orElse(null);
    if (authInfo == null) {
      throw new RuntimeException("User from current context is not present in the kubeconfig");
    }

    k8sInfo.setApiServerEndpoint(cluster.getCluster().getServer());

    String certBase64 = cluster.getCluster().getCertificateAuthorityData();
    if (StringUtils.isBlank(certBase64)) {
      throw new RuntimeException("Certificate authority data is missing in the kubeconfig");
    }
    k8sInfo.setKubeConfigCAFile(
        accessManager.createKubernetesAuthDataFile(
            path,
            kubeConfigName + "-ca.crt",
            new String(Base64.getDecoder().decode(certBase64)),
            edit));

    String token = authInfo.getUser().getToken();
    if (StringUtils.isBlank(token)) {
      throw new RuntimeException("Token is missing in the kubeconfig");
    }
    k8sInfo.setKubeConfigTokenFile(
        accessManager.createKubernetesAuthDataFile(
            path, kubeConfigName + "-token.txt", token, edit));
    return k8sInfo;
  }

  public boolean maybeUpdateCloudProviderConfig(
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
    ShellResponse response = dnsManager.listDnsRecord(provider.getUuid(), hostedZoneId);
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

  private void updateGCPProviderConfig(Provider provider, Map<String, String> config) {
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
    JsonNode gcpCredentials = gcpCloudInfo.getGceApplicationCredentials();
    if (gcpCredentials != null) {
      String gcpCredentialsFile =
          accessManager.createGCPCredentialsFile(provider.getUuid(), gcpCredentials);
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

  public void maybeUpdateVPC(Provider provider) {
    switch (provider.getCloudCode()) {
      case gcp:
        GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
        if (gcpCloudInfo == null) {
          return;
        }

        if (gcpCloudInfo.getUseHostVPC() != null && !gcpCloudInfo.getUseHostVPC()) {
          gcpCloudInfo.setVpcType(CloudInfoInterface.VPCType.NEW);
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
            gcpCloudInfo.setVpcType(CloudInfoInterface.VPCType.HOSTVPC);
          }
          if (StringUtils.isBlank(gcpCloudInfo.getGceProject())) {
            gcpCloudInfo.setGceProject(currentHostInfo.get("project").asText());
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

  public void maybeUpdateGCPProject(Provider provider) {
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);

    if (StringUtils.isBlank(gcpCloudInfo.getGceProject())) {
      /**
       * Preferences for GCP Project. 1. User provided project name. 2. `project_id` present in gcp
       * credentials user provided. 3. Metadata query to fetch the same.
       */
      ObjectNode credentialJSON = (ObjectNode) gcpCloudInfo.getGceApplicationCredentials();
      if (credentialJSON != null && credentialJSON.has("project_id")) {
        gcpCloudInfo.setGceProject(credentialJSON.get("project_id").asText());
      }
    }
  }

  public void validateKubernetesProviderConfig(Provider reqProvider) {
    CloudType providerCode = CloudType.valueOf(reqProvider.getCode());
    if (!providerCode.equals(kubernetes)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "API for only kubernetes provider creation: " + providerCode);
    }
    if (reqProvider.getRegions().isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Need regions in provider");
    }
    Map<String, String> providerConfig = CloudInfoInterface.fetchEnvVars(reqProvider);
    KubernetesInfo kubernetesInfo = CloudInfoInterface.get(reqProvider);

    boolean hasConfigInProvider = providerConfig.containsKey("KUBECONFIG_NAME");
    if (kubernetesInfo.getKubeConfig() != null) {
      hasConfigInProvider = true;
    }
    for (Region rd : reqProvider.getRegions()) {
      boolean hasConfig = hasConfigInProvider;
      KubernetesRegionInfo k8sRegionInfo = CloudInfoInterface.get(rd);
      Map<String, String> regionConfig = CloudInfoInterface.fetchEnvVars(rd);
      if (regionConfig.containsKey("KUBECONFIG_NAME") || k8sRegionInfo.getKubeConfig() != null) {
        if (hasConfig) {
          throw new PlatformServiceException(BAD_REQUEST, "Kubeconfig can't be at two levels");
        }
        hasConfig = true;
      }
      if (rd.getZones().isEmpty()) {
        throw new PlatformServiceException(BAD_REQUEST, "No zone provided in region");
      }
      for (AvailabilityZone zd : rd.getZones()) {
        Map<String, String> zoneConfig = CloudInfoInterface.fetchEnvVars(zd);
        k8sRegionInfo = CloudInfoInterface.get(zd);
        if (zoneConfig.containsKey("KUBECONFIG_NAME") || k8sRegionInfo.getKubeConfig() != null) {
          if (hasConfig) {
            throw new PlatformServiceException(BAD_REQUEST, "Kubeconfig can't be at two levels");
          }
        } else if (!hasConfig) {
          log.warn(
              "No Kubeconfig found at any level. "
                  + "In-cluster service account credentials will be used.");
        }
      }
    }
  }

  public void validateInstanceTemplate(Provider provider, CloudBootstrap.Params taskParams) {
    // Validate instance template, if provided. Only supported for GCP currently.
    taskParams.perRegionMetadata.forEach(
        (region, metadata) -> {
          if (metadata.instanceTemplate != null) {
            CloudAPI cloudAPI = cloudAPIFactory.get(provider.getCode());
            cloudAPI.validateInstanceTemplate(provider, metadata.instanceTemplate);
          }
        });
  }

  public void createKubernetesInstanceTypes(Customer customer, Provider provider) {
    KUBERNETES_INSTANCE_TYPES.forEach(
        (instanceType -> {
          InstanceType.InstanceTypeDetails idt = new InstanceType.InstanceTypeDetails();
          idt.setVolumeDetailsList(1, 100, InstanceType.VolumeType.SSD);
          InstanceType.upsert(
              provider.getUuid(),
              instanceType.get("instanceTypeCode").asText(),
              instanceType.get("numCores").asDouble(),
              instanceType.get("memSizeGB").asDouble(),
              idt);
        }));
    if (environment.isDev()) {
      InstanceType.InstanceTypeDetails idt = new InstanceType.InstanceTypeDetails();
      idt.setVolumeDetailsList(1, 100, InstanceType.VolumeType.SSD);
      InstanceType.upsert(
          provider.getUuid(),
          KUBERNETES_DEV_INSTANCE_TYPE.get("instanceTypeCode").asText(),
          KUBERNETES_DEV_INSTANCE_TYPE.get("numCores").asDouble(),
          KUBERNETES_DEV_INSTANCE_TYPE.get("memSizeGB").asDouble(),
          idt);
    }
    if (customer.getCode().equals("cloud")) {
      InstanceType.InstanceTypeDetails idt = new InstanceType.InstanceTypeDetails();
      idt.setVolumeDetailsList(1, 5, InstanceType.VolumeType.SSD);
      InstanceType.upsert(
          provider.getUuid(),
          KUBERNETES_CLOUD_INSTANCE_TYPE.get("instanceTypeCode").asText(),
          KUBERNETES_CLOUD_INSTANCE_TYPE.get("numCores").asDouble(),
          KUBERNETES_CLOUD_INSTANCE_TYPE.get("memSizeGB").asDouble(),
          idt);
    }
  }

  // topology/failure-domain labels from the Kubernetes nodes.
  public Multimap<String, String> computeKubernetesRegionToZoneInfo() {
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
            log.debug(
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
  public Secret getKubernetesPullSecret(String secretName) {
    Secret pullSecret;
    try {
      pullSecret = kubernetesManagerFactory.getManager().getSecret(null, secretName, null);
    } catch (RuntimeException e) {
      if (e.getMessage().contains("Error from server (NotFound): secrets")) {
        log.debug(
            "The pull secret " + secretName + " is not present, provider won't have this field.");
        return null;
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Unable to fetch the pull secret.");
    }
    if (pullSecret.getMetadata() == null) {
      log.error(
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

  public String getRegionNameFromCode(String code, String cloudProviderCode) {
    log.info("Code is: {}", code);
    ConfigHelper.ConfigType kubernetesConfigType = getKubernetesConfigType(cloudProviderCode);
    if (kubernetesConfigType == null) {
      return code;
    }
    Map<String, Object> k8sRegionMetadata = configHelper.getConfig(kubernetesConfigType);
    if (!k8sRegionMetadata.containsKey(code)) {
      log.info("Could not find code in file, sending it back as name");
      return code;
    }

    JsonNode metadata = Json.toJson(k8sRegionMetadata.get(code));
    return metadata.get("name").asText();
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

  public Set<Region> checkIfRegionsToAdd(Provider editProviderReq, Provider provider) {
    Set<Region> regionsToAdd = new HashSet<>();
    if (provider.getCloudCode().canAddRegions()) {
      if (editProviderReq.getRegions() != null && !editProviderReq.getRegions().isEmpty()) {
        Map<String, Region> newRegions =
            editProviderReq.getRegions().stream()
                .collect(Collectors.toMap(r -> r.getCode(), r -> r));
        Map<String, Region> existingRegions =
            Region.getByProvider(provider.getUuid(), false).stream()
                .collect(Collectors.toMap(r -> r.getCode(), r -> r));
        Set<String> activeRegionCodes =
            existingRegions.values().stream()
                .filter(r -> r.isActive())
                .map(r -> r.getCode())
                .collect(Collectors.toSet());

        newRegions.keySet().removeAll(activeRegionCodes);
        regionsToAdd = new HashSet<>(newRegions.values());
        regionsToAdd.forEach(
            reg -> {
              Region inactive = existingRegions.get(reg.getCode());
              if (inactive != null) {
                log.debug("Hard deleting region {}", inactive);
                inactive.delete();
              }
            });
      }
    }
    return regionsToAdd;
  }

  private void validateProviderEditPayload(Provider provider, Provider editProviderReq) {
    /*
     * For the providers associated with the running universes, we will only allow properties
     * that does not impact the running universes.
     * Things that can be modified.
     * 1. Region/Availablity Zone addition to the provider.
     * 2. Removal of region/availability zone from the provider, in case they are not used.
     * 3. Addition of new access keys, we will not allow removal of the existing ones.
     * 4. Addition of new image bundles/ removal of unused image bundles.
     */

    // Check if provider details are being modified
    if (!provider.getDetails().equals(editProviderReq.getDetails())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Modifying provider details is not allowed for providers in use.");
    }

    CloudInfoInterface providerCloudInfo = CloudInfoInterface.get(provider);
    CloudInfoInterface editProviderCloudInfo = CloudInfoInterface.get(editProviderReq);
    checkCloudInfoFieldsInUseProvider(providerCloudInfo, editProviderCloudInfo);

    // Collect existing and current regions into maps
    Map<String, Region> existingRegions =
        provider.getRegions().stream().collect(Collectors.toMap(r -> r.getCode(), r -> r));
    Map<String, Region> currentRegions =
        editProviderReq.getRegions().stream().collect(Collectors.toMap(r -> r.getCode(), r -> r));

    // Compute regions to be deleted
    Map<String, Region> regionsToBeDeleted =
        existingRegions.entrySet().stream()
            .filter(entry -> !currentRegions.containsKey(entry.getKey()))
            .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue));

    // Check if any regions to be deleted are associated with universes
    regionsToBeDeleted.forEach(
        (code, region) -> {
          if (region.getNodeCount() > 0) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Cannot delete region %s as it is associated with running universes.", code));
          }
        });

    // Iterate through current regions
    for (Region currentRegion : editProviderReq.getRegions()) {
      String regionCode = currentRegion.getCode();

      // Check if the region exists in the existing regions
      if (existingRegions.containsKey(regionCode)) {
        Region existingRegion = existingRegions.get(regionCode);

        // Check if region details are being modified & the region is in use by universe.
        if (existingRegion.isUpdateNeeded(currentRegion) && existingRegion.getNodeCount() > 0) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              String.format(
                  "Modifying region %s details is not allowed for providers in use.", regionCode));
        }

        // Collect existing and current availability zones into maps
        Map<String, AvailabilityZone> existingAZs =
            existingRegion.getZones().stream()
                .collect(Collectors.toMap(az -> az.getCode(), az -> az));
        Map<String, AvailabilityZone> currentAZs =
            currentRegion.getZones().stream()
                .collect(Collectors.toMap(az -> az.getCode(), az -> az));

        // Compute availability zones to be deleted
        Map<String, AvailabilityZone> azsToBeDeleted =
            existingAZs.entrySet().stream()
                .filter(entry -> !currentAZs.containsKey(entry.getKey()))
                .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue));

        // Check if any availability zones to be deleted are associated with universes
        azsToBeDeleted.forEach(
            (azCode, az) -> {
              if (az.getNodeCount() > 0) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    String.format(
                        "Cannot delete zone %s as it is associated with running universes.",
                        azCode));
              }
            });

        // Iterate through current availability zones
        for (AvailabilityZone currentZone : currentRegion.getZones()) {
          String zoneCode = currentZone.getCode();

          // Check if the availability zone exists in the existing availability zones
          if (existingAZs.containsKey(zoneCode)) {
            AvailabilityZone existingZone = existingAZs.get(zoneCode);

            // Check if availability zone details are being modified & the az is in use by universe.
            if (existingZone.isUpdateNeeded(currentZone) && existingZone.getNodeCount() > 0) {
              throw new PlatformServiceException(
                  BAD_REQUEST,
                  String.format(
                      "Modifying zone %s details is not allowed for providers in use.", zoneCode));
            }
          }
        }
      }
    }

    // Validate the imageBundles, deletion of in-use imageBundles is not allowed
    Map<UUID, ImageBundle> existingImageBundles =
        provider.getImageBundles().stream().collect(Collectors.toMap(iB -> iB.getUuid(), iB -> iB));
    Map<UUID, ImageBundle> currentImageBundles =
        editProviderReq.getImageBundles().stream()
            .filter(iB -> iB.getUuid() != null)
            .collect(Collectors.toMap(iB -> iB.getUuid(), iB -> iB));

    existingImageBundles.forEach(
        (uuid, imageBundle) -> {
          if (!currentImageBundles.containsKey(uuid) && imageBundle.getUniverseCount() > 0) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Image Bundle %s is associated with some universes. Cannot delete!",
                    imageBundle.getName()));
          }
          ImageBundle currentImageBundle = currentImageBundles.get(uuid);
          if (imageBundle.getUniverseCount() > 0
              && currentImageBundle.isUpdateNeeded(imageBundle)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Image Bundle %s is associated with some universes. Cannot modify!",
                    imageBundle.getName()));
          }
        });
  }

  public void validateEditProvider(
      Provider editProviderReq,
      Provider provider,
      boolean cloudValidate,
      boolean ignoreCloudValidationErrors) {
    if (editProviderReq.getVersion() < provider.getVersion()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Provider has changed, please refresh and try again");
    }
    if (!provider.getCloudCode().equals(editProviderReq.getCloudCode())) {
      throw new PlatformServiceException(BAD_REQUEST, "Changing provider type is not supported!");
    }
    if (!provider.getName().equals(editProviderReq.getName())) {
      List<Provider> providers =
          Provider.getAll(
              provider.getCustomerUUID(), editProviderReq.getName(), provider.getCloudCode());
      if (providers.size() > 0) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format("Provider with name %s already exists.", editProviderReq.getName()));
      }
    }

    CloudInfoInterface.mergeSensitiveFields(provider, editProviderReq);
    // Validate the provider request so as to ensure we only allow editing of fields
    // that does not impact the existing running universes.
    long universeCount = provider.getUniverseCount();
    if (!confGetter.getGlobalConf(GlobalConfKeys.allowUsedProviderEdit) && universeCount > 0) {
      validateProviderEditPayload(provider, editProviderReq);
    }
    Set<Region> regionsToAdd = checkIfRegionsToAdd(editProviderReq, provider);
    // Validate regions to add. We only support providing custom VPCs for now.
    // So the user must have entered the VPC Info for the regions, as well as
    // the zone info.
    if (!regionsToAdd.isEmpty()) {
      for (Region region : regionsToAdd) {
        if (region.getZones() == null || region.getZones().isEmpty()) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Zone info needs to be specified for region: " + region.getCode());
        }
        region
            .getZones()
            .forEach(
                zone -> {
                  if (zone.getSubnet() == null
                      && provider.getCloudCode() != CloudType.onprem
                      && provider.getCloudCode() != CloudType.kubernetes) {
                    throw new PlatformServiceException(
                        BAD_REQUEST, "Required field subnet for zone: " + zone.getCode());
                  }
                });
      }
    }
    // TODO: Remove this code once the validators are added for all cloud provider.
    CloudAPI cloudAPI = cloudAPIFactory.get(provider.getCode());
    if (cloudAPI != null
        && !cloudAPI.isValidCreds(editProviderReq, getFirstRegionCode(editProviderReq))) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Invalid %s Credentials.", provider.getCode().toUpperCase()));
    }
    if (provider.getCloudCode().equals(CloudType.kubernetes)) {
      validateKubernetesProviderConfig(editProviderReq);
    }
    JsonNode newErrors = null;
    if (cloudValidate) {
      try {
        providerValidator.validate(editProviderReq);
      } catch (PlatformServiceException e) {
        log.error(
            "Received validation error,  ignoreValidationErrors=" + ignoreCloudValidationErrors, e);
        newErrors = e.getContentJson();
        if (!ignoreCloudValidationErrors) {
          throw e;
        }
      }
    }
    provider.setLastValidationErrors(newErrors);
  }

  public static String getFirstRegionCode(Provider provider) {
    for (Region r : provider.getRegions()) {
      return r.getCode();
    }
    return null;
  }

  private void checkCloudInfoFieldsInUseProvider(
      CloudInfoInterface cloudInfo, CloudInfoInterface reqCloudInfo) {
    Field[] fields = reqCloudInfo.getClass().getDeclaredFields();
    // Iterate over each field
    for (Field field : fields) {
      // Get the annotations for the field
      Annotation[] annotations = field.getDeclaredAnnotations();
      // Iterate over each annotation
      for (Annotation annotation : annotations) {
        // Check if it's your custom annotation
        if (annotation instanceof EditableInUseProvider) {
          EditableInUseProvider editableInUseProviderAnnotation =
              (EditableInUseProvider) annotation;
          boolean editAllowed = editableInUseProviderAnnotation.allowed();
          if (!editAllowed) {
            field.setAccessible(true);
            try {
              Object existingValue = field.get(reqCloudInfo);
              Object updatedValue = field.get(cloudInfo);

              if (!Objects.equals(existingValue, updatedValue)) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    String.format(
                        "%s cannot be modified for in-use providers.",
                        editableInUseProviderAnnotation.name()));
              }
            } catch (IllegalAccessException e) {
              log.debug(String.format("%s does not exist in cloudInfo, skipping", field.getName()));
            }
          }
        }
      }
    }
  }

  @Retention(RetentionPolicy.RUNTIME)
  @Target({ElementType.METHOD, ElementType.FIELD})
  public @interface EditableInUseProvider {
    public String name();

    public boolean allowed() default true;
  }

  public ConfigHelper.ConfigType getKubernetesConfigType(String cloudProviderCode) {
    if (cloudProviderCode == null) {
      return null;
    }

    ConfigHelper.ConfigType kubernetesConfigType = null;
    switch (cloudProviderCode.toLowerCase()) {
      case "gke":
        kubernetesConfigType = ConfigHelper.ConfigType.GKEKubernetesRegionMetadata;
        break;
      case "eks":
        kubernetesConfigType = ConfigHelper.ConfigType.EKSKubernetesRegionMetadata;
        break;
      case "aks":
        kubernetesConfigType = ConfigHelper.ConfigType.AKSKubernetesRegionMetadata;
        break;
      default:
        // Defaulting to EKS for now.
        kubernetesConfigType = ConfigHelper.ConfigType.EKSKubernetesRegionMetadata;
        break;
    }

    return kubernetesConfigType;
  }

  // Triggers the Prometheus scrape config update
  public void updatePrometheusConfig(Provider p) {
    if (p.getCloudCode() == CloudType.kubernetes) {
      prometheusConfigManager.updateK8sScrapeConfigs();
    }
  }
}
