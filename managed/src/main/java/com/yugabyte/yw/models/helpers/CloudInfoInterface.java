package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.models.helpers.CommonUtils.maskConfigNew;
import static play.mvc.Http.Status.BAD_REQUEST;

import java.util.Map;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.AvailabilityZoneDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RegionDetails;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import com.yugabyte.yw.models.helpers.provider.DefaultCloudInfo;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.OnPremCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.AWSRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.AzureRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.DefaultRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.azs.DefaultAZCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;

import play.libs.Json;

public interface CloudInfoInterface {

  public final ObjectMapper mapper = Json.mapper();

  public Map<String, String> getEnvVars();

  public Map<String, String> getConfigMapForUIOnlyAPIs(Map<String, String> config);

  public void withSensitiveDataMasked();

  public static <T extends CloudInfoInterface> T get(Provider provider) {
    return get(provider, false);
  }

  public static <T extends CloudInfoInterface> T get(Region region) {
    return get(region, false);
  }

  public static <T extends CloudInfoInterface> T get(AvailabilityZone zone) {
    return get(zone, false);
  }

  public static <T extends CloudInfoInterface> T get(Provider provider, Boolean maskSensitiveData) {
    ProviderDetails providerDetails = provider.getProviderDetails();
    if (providerDetails == null) {
      providerDetails = new ProviderDetails();
    }
    ProviderDetails.CloudInfo cloudInfo = providerDetails.getCloudInfo();
    if (cloudInfo == null) {
      cloudInfo = new ProviderDetails.CloudInfo();
      providerDetails.cloudInfo = cloudInfo;
    }
    CloudType cloudType = provider.getCloudCode();
    return getCloudInfo(cloudInfo, cloudType, maskSensitiveData);
  }

  public static <T extends CloudInfoInterface> T get(Region region, Boolean maskSensitiveData) {
    RegionDetails regionDetails = region.getRegionDetails();
    if (regionDetails == null) {
      regionDetails = new RegionDetails();
    }
    RegionDetails.RegionCloudInfo cloudInfo = regionDetails.getCloudInfo();
    if (cloudInfo == null) {
      cloudInfo = new RegionDetails.RegionCloudInfo();
      regionDetails.cloudInfo = cloudInfo;
    }
    CloudType cloudType = region.provider.getCloudCode();
    return getCloudInfo(cloudInfo, cloudType, maskSensitiveData);
  }

  public static <T extends CloudInfoInterface> T get(
      AvailabilityZone zone, Boolean maskSensitiveData) {
    AvailabilityZoneDetails azDetails = zone.getAvailabilityZoneDetails();
    if (azDetails == null) {
      azDetails = new AvailabilityZoneDetails();
    }
    AvailabilityZoneDetails.AZCloudInfo cloudInfo = azDetails.getCloudInfo();
    if (cloudInfo == null) {
      cloudInfo = new AvailabilityZoneDetails.AZCloudInfo();
      azDetails.cloudInfo = cloudInfo;
    }
    CloudType cloudType = CloudType.valueOf(zone.region.provider.code);
    return getCloudInfo(cloudInfo, cloudType, maskSensitiveData);
  }

  public static <T extends CloudInfoInterface> T getCloudInfo(
      ProviderDetails.CloudInfo cloudInfo, CloudType cloudType, Boolean maskSensitiveData) {
    switch (cloudType) {
      case aws:
        AWSCloudInfo awsCloudInfo = cloudInfo.getAws();
        if (awsCloudInfo == null) {
          awsCloudInfo = new AWSCloudInfo();
          cloudInfo.setAws(awsCloudInfo);
        }
        if (awsCloudInfo != null && maskSensitiveData) {
          awsCloudInfo.withSensitiveDataMasked();
        }
        return (T) awsCloudInfo;
      case gcp:
        GCPCloudInfo gcpCloudInfo = cloudInfo.getGcp();
        if (gcpCloudInfo == null) {
          gcpCloudInfo = new GCPCloudInfo();
          cloudInfo.setGcp(gcpCloudInfo);
        }
        if (gcpCloudInfo != null && maskSensitiveData) {
          gcpCloudInfo.withSensitiveDataMasked();
        }
        return (T) gcpCloudInfo;
      case azu:
        AzureCloudInfo azuCloudInfo = cloudInfo.getAzu();
        if (azuCloudInfo == null) {
          azuCloudInfo = new AzureCloudInfo();
          cloudInfo.setAzu(azuCloudInfo);
        }
        if (azuCloudInfo != null && maskSensitiveData) {
          azuCloudInfo.withSensitiveDataMasked();
        }
        return (T) azuCloudInfo;
      case kubernetes:
        KubernetesInfo kubernetesInfo = cloudInfo.getKubernetes();
        if (kubernetesInfo == null) {
          kubernetesInfo = new KubernetesInfo();
          cloudInfo.setKubernetes(kubernetesInfo);
        }
        if (kubernetesInfo != null && maskSensitiveData) {
          kubernetesInfo.withSensitiveDataMasked();
        }
        return (T) kubernetesInfo;
      case onprem:
        OnPremCloudInfo onpremCloudInfo = cloudInfo.getOnprem();
        if (onpremCloudInfo == null) {
          onpremCloudInfo = new OnPremCloudInfo();
          cloudInfo.setOnprem(onpremCloudInfo);
        }
        if (onpremCloudInfo != null && maskSensitiveData) {
          onpremCloudInfo.withSensitiveDataMasked();
        }
        return (T) onpremCloudInfo;
      default:
        // Placeholder. Don't want consumers to receive null.
        return (T) new DefaultCloudInfo();
    }
  }

  public static <T extends CloudInfoInterface> T getCloudInfo(
      RegionDetails.RegionCloudInfo cloudInfo, CloudType cloudType, Boolean maskSensitiveData) {
    switch (cloudType) {
      case aws:
        AWSRegionCloudInfo awsRegionCloudInfo = cloudInfo.getAws();
        if (awsRegionCloudInfo == null) {
          awsRegionCloudInfo = new AWSRegionCloudInfo();
          cloudInfo.setAws(awsRegionCloudInfo);
        }
        if (awsRegionCloudInfo != null && maskSensitiveData) {
          awsRegionCloudInfo.withSensitiveDataMasked();
        }
        return (T) awsRegionCloudInfo;
      case gcp:
        GCPRegionCloudInfo gcpRegionCloudInfo = cloudInfo.getGcp();
        if (gcpRegionCloudInfo == null) {
          gcpRegionCloudInfo = new GCPRegionCloudInfo();
          cloudInfo.setGcp(gcpRegionCloudInfo);
        }
        if (gcpRegionCloudInfo != null && maskSensitiveData) {
          gcpRegionCloudInfo.withSensitiveDataMasked();
        }
        return (T) gcpRegionCloudInfo;
      case azu:
        AzureRegionCloudInfo azuRegionCloudInfo = cloudInfo.getAzu();
        if (azuRegionCloudInfo == null) {
          azuRegionCloudInfo = new AzureRegionCloudInfo();
          cloudInfo.setAzu(azuRegionCloudInfo);
        }
        if (azuRegionCloudInfo != null && maskSensitiveData) {
          azuRegionCloudInfo.withSensitiveDataMasked();
        }
        return (T) azuRegionCloudInfo;
      case kubernetes:
        KubernetesRegionInfo kubernetesInfo = cloudInfo.getKubernetes();
        if (kubernetesInfo == null) {
          kubernetesInfo = new KubernetesRegionInfo();
          cloudInfo.setKubernetes(kubernetesInfo);
        }
        if (kubernetesInfo != null && maskSensitiveData) {
          kubernetesInfo.withSensitiveDataMasked();
        }
        return (T) kubernetesInfo;
      default:
        // Placeholder. Don't want consumers to receive null.
        return (T) new DefaultRegionCloudInfo();
    }
  }

  public static <T extends CloudInfoInterface> T getCloudInfo(
      AvailabilityZoneDetails.AZCloudInfo cloudInfo,
      CloudType cloudType,
      Boolean maskSensitiveData) {
    switch (cloudType) {
      case kubernetes:
        KubernetesRegionInfo kubernetesAZInfo = cloudInfo.getKubernetes();
        if (kubernetesAZInfo == null) {
          kubernetesAZInfo = new KubernetesRegionInfo();
          cloudInfo.setKubernetes(kubernetesAZInfo);
        }
        if (kubernetesAZInfo != null && maskSensitiveData) {
          kubernetesAZInfo.withSensitiveDataMasked();
        }
        return (T) kubernetesAZInfo;
      default:
        return (T) new DefaultAZCloudInfo();
    }
  }

  public static void maskProviderDetails(Provider provider) {
    get(provider, true);
  }

  public static void maskRegionDetails(Region region) {
    get(region, true);
  }

  public static void maskAvailabilityZoneDetails(AvailabilityZone zone) {
    get(zone, true);
  }

  public static void setCloudProviderInfoFromConfig(Provider provider, Map<String, String> config) {
    ProviderDetails providerDetails = provider.getProviderDetails();
    ProviderDetails.CloudInfo cloudInfo = providerDetails.getCloudInfo();
    if (cloudInfo == null) {
      cloudInfo = new ProviderDetails.CloudInfo();
      providerDetails.setCloudInfo(cloudInfo);
    }
    CloudType cloudType = provider.getCloudCode();
    setFromConfig(cloudInfo, config, cloudType);
  }

  public static void setCloudProviderInfoFromConfig(Region region, Map<String, String> config) {
    CloudType cloudType = region.provider.getCloudCode();
    RegionDetails regionDetails = region.getRegionDetails();
    RegionDetails.RegionCloudInfo cloudInfo = regionDetails.getCloudInfo();
    if (cloudInfo == null) {
      cloudInfo = new RegionDetails.RegionCloudInfo();
      regionDetails.setCloudInfo(cloudInfo);
    }
    setFromConfig(cloudInfo, config, cloudType);
  }

  public static void setCloudProviderInfoFromConfig(
      AvailabilityZone az, Map<String, String> config) {
    CloudType cloudType = CloudType.valueOf(az.region.provider.code);
    AvailabilityZoneDetails azDetails = az.getAvailabilityZoneDetails();
    AvailabilityZoneDetails.AZCloudInfo cloudInfo = azDetails.getCloudInfo();
    if (cloudInfo == null) {
      cloudInfo = new AvailabilityZoneDetails.AZCloudInfo();
      azDetails.setCloudInfo(cloudInfo);
    }
    setFromConfig(cloudInfo, config, cloudType);
  }

  public static void setFromConfig(
      ProviderDetails.CloudInfo cloudInfo, Map<String, String> config, CloudType cloudType) {
    if (config == null) {
      return;
    }

    switch (cloudType) {
      case aws:
        AWSCloudInfo awsCloudInfo = mapper.convertValue(config, AWSCloudInfo.class);
        cloudInfo.setAws(awsCloudInfo);
        break;
      case gcp:
        GCPCloudInfo gcpCloudInfo = mapper.convertValue(config, GCPCloudInfo.class);
        cloudInfo.setGcp(gcpCloudInfo);
        break;
      case azu:
        AzureCloudInfo azuCloudInfo = mapper.convertValue(config, AzureCloudInfo.class);
        cloudInfo.setAzu(azuCloudInfo);
        break;
      case kubernetes:
        KubernetesInfo kubernetesInfo = mapper.convertValue(config, KubernetesInfo.class);
        cloudInfo.setKubernetes(kubernetesInfo);
        break;
      case onprem:
        OnPremCloudInfo onPremCloudInfo = mapper.convertValue(config, OnPremCloudInfo.class);
        cloudInfo.setOnprem(onPremCloudInfo);
        break;
      case local:
        // TODO: check if it used anymore? in case not, remove the local universe case
        // Import Universe case
        break;
      default:
        throw new PlatformServiceException(BAD_REQUEST, "Unsupported cloud type");
    }
  }

  public static void setFromConfig(
      RegionDetails.RegionCloudInfo cloudInfo, Map<String, String> config, CloudType cloudType) {
    if (config == null) {
      return;
    }

    switch (cloudType) {
      case aws:
        AWSRegionCloudInfo awsRegionCloudInfo =
            mapper.convertValue(config, AWSRegionCloudInfo.class);
        cloudInfo.setAws(awsRegionCloudInfo);
        break;
      case gcp:
        GCPRegionCloudInfo gcpRegionCloudInfo =
            mapper.convertValue(config, GCPRegionCloudInfo.class);
        cloudInfo.setGcp(gcpRegionCloudInfo);
        break;
      case azu:
        AzureRegionCloudInfo azuRegionCloudInfo =
            mapper.convertValue(config, AzureRegionCloudInfo.class);
        cloudInfo.setAzu(azuRegionCloudInfo);
        break;
      case kubernetes:
        KubernetesRegionInfo kubernetesRegionInfo =
            mapper.convertValue(config, KubernetesRegionInfo.class);
        cloudInfo.setKubernetes(kubernetesRegionInfo);
        break;
      default:
        break;
    }
  }

  public static void setFromConfig(
      AvailabilityZoneDetails.AZCloudInfo cloudInfo,
      Map<String, String> config,
      CloudType cloudType) {
    if (config == null) {
      return;
    }

    switch (cloudType) {
      case kubernetes:
        KubernetesRegionInfo kubernetesAZInfo =
            mapper.convertValue(config, KubernetesRegionInfo.class);
        cloudInfo.setKubernetes(kubernetesAZInfo);
        break;
      default:
        break;
    }
  }

  public static Map<String, String> fetchEnvVars(Provider provider) {
    CloudInfoInterface cloudInfo = CloudInfoInterface.get(provider);
    return cloudInfo.getEnvVars();
  }

  public static Map<String, String> fetchEnvVars(Region region) {
    CloudInfoInterface cloudInfo = CloudInfoInterface.get(region);
    return cloudInfo.getEnvVars();
  }

  public static Map<String, String> fetchEnvVars(AvailabilityZone az) {
    CloudInfoInterface cloudInfo = CloudInfoInterface.get(az);
    return cloudInfo.getEnvVars();
  }

  public static JsonNode mayBeMassageRequest(JsonNode requestBody) {
    // For Backward Compatiblity support.
    JsonNode config = requestBody.get("config");
    ObjectNode reqBody = (ObjectNode) requestBody;
    // Confirm we had a "config" key and it was not null.
    if (config != null && !config.isNull()) {
      if (requestBody.get("code").asText().equals(CloudType.gcp.name())) {
        ObjectNode details = mapper.createObjectNode();
        ObjectNode cloudInfo = mapper.createObjectNode();
        ObjectNode gcpCloudInfo = mapper.createObjectNode();
        JsonNode configFileContent = config.get("config_file_contents");

        Boolean shouldUseHostCredentials =
            config.has("use_host_credentials") && config.get("use_host_credentials").asBoolean();

        if (config.has("host_project_id")) {
          gcpCloudInfo.set("host_project_id", config.get("host_project_id"));
        } else if (configFileContent != null && !configFileContent.isNull()) {
          gcpCloudInfo.set("host_project_id", ((ObjectNode) configFileContent).get("project_id"));
        }
        if (!shouldUseHostCredentials && configFileContent != null) {
          gcpCloudInfo.set("config_file_contents", configFileContent);
        }
        if (config.has("use_host_vpc")) {
          gcpCloudInfo.set("use_host_vpc", config.get("use_host_vpc"));
        }
        gcpCloudInfo.set("YB_FIREWALL_TAGS", config.get("YB_FIREWALL_TAGS"));

        cloudInfo.set("gcp", gcpCloudInfo);
        details.set("cloudInfo", cloudInfo);
        details.set("airGapInstall", config.get("airGapInstall"));

        reqBody.set("details", details);
        reqBody.remove("config");
      }
    }
    return reqBody;
  }

  public static Map<String, String> populateConfigMap(
      ProviderDetails.CloudInfo cloudInfo, CloudType cloudType, Map<String, String> config) {
    if (cloudInfo == null || config == null) {
      return config;
    }
    CloudInfoInterface cloudInfoInterface = null;
    switch (cloudType) {
      case aws:
        cloudInfoInterface = cloudInfo.getAws();
        break;
      case gcp:
        cloudInfoInterface = cloudInfo.getGcp();
        break;
      case azu:
        cloudInfoInterface = cloudInfo.getAzu();
        break;
      case kubernetes:
        cloudInfoInterface = cloudInfo.getKubernetes();
        break;
      case onprem:
        cloudInfoInterface = cloudInfo.getOnprem();
        break;
      case local:
        // TODO: check if it used anymore? in case not, remove the local universe case
        // Import Universe case
      default:
        break;
    }
    if (cloudInfoInterface == null) {
      return config;
    }
    return maskConfigNew(cloudInfoInterface.getConfigMapForUIOnlyAPIs(config));
  }

  public static Map<String, String> populateConfigMap(
      AvailabilityZoneDetails.AZCloudInfo cloudInfo,
      CloudType cloudType,
      Map<String, String> config) {
    if (cloudInfo == null || config == null) {
      return config;
    }
    CloudInfoInterface cloudInfoInterface = null;
    switch (cloudType) {
      case kubernetes:
        cloudInfoInterface = cloudInfo.getKubernetes();
        break;
      default:
        break;
    }
    if (cloudInfoInterface == null) {
      return config;
    }
    return maskConfigNew(cloudInfoInterface.getConfigMapForUIOnlyAPIs(config));
  }

  public static void mayBeMassageResponse(Provider p) {
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(p);
    ProviderDetails providerDetails = p.getProviderDetails();
    ProviderDetails.CloudInfo cloudInfo = providerDetails.getCloudInfo();
    CloudType cloudType = CloudType.valueOf(p.code);
    p.config = populateConfigMap(cloudInfo, cloudType, config);

    if (p.regions == null) {
      return;
    }

    for (Region region : p.regions) {
      if (region.zones == null) {
        return;
      }
      for (AvailabilityZone az : region.zones) {
        config = CloudInfoInterface.fetchEnvVars(az);
        AvailabilityZoneDetails azDetails = az.getAvailabilityZoneDetails();
        AvailabilityZoneDetails.AZCloudInfo azCloudInfo = azDetails.getCloudInfo();
        az.config = populateConfigMap(azCloudInfo, cloudType, config);
      }
    }
  }
}
