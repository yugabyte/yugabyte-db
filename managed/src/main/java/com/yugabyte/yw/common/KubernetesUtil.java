// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.PlacementInfoUtil.isMultiAZ;

import com.google.common.collect.HashMultimap;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Multimap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.Quantity;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.ObjectUtils;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class KubernetesUtil {

  public static String MIN_VERSION_NON_RESTART_GFLAGS_UPGRADE_SUPPORT_PREVIEW = "2.23.0.0-b539";
  public static String MIN_VERSION_NON_RESTART_GFLAGS_UPGRADE_SUPPORT_STABLE = "2024.2.0.0-b999";
  // Kubelet secret sync time + k8s_parent template sync time.
  public static final int WAIT_FOR_GFLAG_SYNC_SECS = 90;

  public static boolean isNonRestartGflagsUpgradeSupported(String universeSoftwareVersion) {
    return Util.compareYBVersions(
            universeSoftwareVersion,
            MIN_VERSION_NON_RESTART_GFLAGS_UPGRADE_SUPPORT_STABLE,
            MIN_VERSION_NON_RESTART_GFLAGS_UPGRADE_SUPPORT_PREVIEW,
            true)
        > 0;
  }

  // ToDo: Old k8s provider needs to be fixed, so that we can get
  // rid of the old merging logic, & start treating them same as the
  // new providers.
  // https://yugabyte.atlassian.net/browse/PLAT-8492?focusedCommentId=64822

  // Get the zones with the kubeconfig for that zone.
  public static Map<UUID, Map<String, String>> getConfigPerAZ(PlacementInfo pi) {
    Map<UUID, Map<String, String>> azToConfig = new HashMap<>();
    for (PlacementCloud pc : pi.cloudList) {
      Provider provider = Provider.getOrBadRequest(pc.uuid);
      KubernetesInfo k8sInfo = CloudInfoInterface.get(provider);
      if (k8sInfo.isLegacyK8sProvider()) {
        // Merging for the legacy provider.
        Map<UUID, Map<String, String>> legacyProviderAzToConfig =
            getLegacyProviderAzToConfig(pc, provider);
        azToConfig.putAll(legacyProviderAzToConfig);
      } else {
        Map<UUID, Map<String, String>> newProviderAzToConfig =
            getNewProviderAzToConfig(pc, provider);
        azToConfig.putAll(newProviderAzToConfig);
      }
    }

    return azToConfig;
  }

  /*
   * Legacy k8s providers created before YBA version 2.18.0
   * For these providers we assume, the configs present at the level
   * as that of `KUBECONFIG` will be the complete config set to look at.
   *
   * Legacy providers will be identified via property `legacyK8sProvider` that
   * will be introduced as part of V231 migration for k8s providers.
   */
  public static Map<UUID, Map<String, String>> getLegacyProviderAzToConfig(
      PlacementCloud pc, Provider provider) {
    Map<UUID, Map<String, String>> azToConfig = new HashMap<>();
    Map<String, String> cloudConfig = CloudInfoInterface.fetchEnvVars(provider);
    for (PlacementRegion pr : pc.regionList) {
      Region region = Region.getOrBadRequest(pr.uuid);
      Map<String, String> regionConfig = CloudInfoInterface.fetchEnvVars(region);
      for (PlacementAZ pa : pr.azList) {
        AvailabilityZone az = AvailabilityZone.getOrBadRequest(pa.uuid);
        Map<String, String> zoneConfig = CloudInfoInterface.fetchEnvVars(az);
        if (cloudConfig.containsKey("KUBECONFIG")) {
          azToConfig.put(pa.uuid, cloudConfig);
        } else if (regionConfig.containsKey("KUBECONFIG")) {
          azToConfig.put(pa.uuid, regionConfig);
        } else if (zoneConfig.containsKey("KUBECONFIG")) {
          azToConfig.put(pa.uuid, zoneConfig);
        } else {
          throw new RuntimeException("No config found at any level");
        }
      }
    }

    return azToConfig;
  }

  /*
   * New k8s providers created starting YBA version 2.18.0
   * For these providers we will merge the configs(returning a combined config map)
   * giving preferences to the properties that are present at the lowest level.
   * Preference Order: Zone > Region > Provider Configs.
   *
   * New providers will be identified via property `legacyK8sProvider` that
   * be false for these.
   */
  public static Map<UUID, Map<String, String>> getNewProviderAzToConfig(
      PlacementCloud pc, Provider provider) {
    Map<UUID, Map<String, String>> azToConfig = new HashMap<>();
    for (PlacementRegion pr : pc.regionList) {
      Region region = Region.getOrBadRequest(pr.uuid);
      for (PlacementAZ pa : pr.azList) {
        AvailabilityZone az = AvailabilityZone.getOrBadRequest(pa.uuid);
        Map<String, String> cloudConfig = CloudInfoInterface.fetchEnvVars(provider);
        Map<String, String> regionConfig = CloudInfoInterface.fetchEnvVars(region);
        cloudConfig.putAll(regionConfig);
        Map<String, String> zoneConfig = CloudInfoInterface.fetchEnvVars(az);
        cloudConfig.putAll(zoneConfig);
        if (cloudConfig.containsKey("KUBECONFIG")) {
          azToConfig.put(pa.uuid, cloudConfig);
        } else {
          throw new RuntimeException("No config found at any level");
        }
      }
    }

    return azToConfig;
  }

  /**
   * For a k8s, finds all associated kubeconfig file paths related to each availability zone.
   *
   * @param provider
   * @return a map where the key is the az uuid and value is kubeConfig path.
   */
  public static Map<UUID, String> getKubeConfigPerAZ(Provider provider) {
    Map<UUID, String> azToKubeConfig = new HashMap<>();
    String providerKubeConfig = null;
    String regionKubeConfig = null;
    String azKubeConfig = null;

    if (provider.getCloudCode().equals(Common.CloudType.kubernetes)) {
      providerKubeConfig =
          CloudInfoInterface.fetchEnvVars(provider).getOrDefault("KUBECONFIG", null);
      List<Region> regions = Region.getByProvider(provider.getUuid());
      for (Region region : regions) {
        regionKubeConfig = CloudInfoInterface.fetchEnvVars(region).getOrDefault("KUBECONFIG", null);
        List<AvailabilityZone> zones = AvailabilityZone.getAZsForRegion(region.getUuid());
        for (AvailabilityZone zone : zones) {
          azKubeConfig = CloudInfoInterface.fetchEnvVars(zone).getOrDefault("KUBECONFIG", null);
          // Zone Level Config must have the highest priority.
          String kubeConfig =
              ObjectUtils.firstNonNull(azKubeConfig, regionKubeConfig, providerKubeConfig);
          if (kubeConfig != null) {
            azToKubeConfig.put(zone.getUuid(), kubeConfig);
          } else {
            throw new RuntimeException("No config found at any level");
          }
        }
      }
    }
    return azToKubeConfig;
  }

  // This function decides the value of isMultiAZ based on the value
  // of azName. In case of single AZ providers, the azName is passed
  // as null.
  public static String getKubernetesNamespace(
      String nodePrefix,
      String azName,
      Map<String, String> azConfig,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {
    boolean isMultiAZ = (azName != null);
    return getKubernetesNamespace(
        isMultiAZ, nodePrefix, azName, azConfig, newNamingStyle, isReadOnlyCluster);
  }

  /**
   * This function returns the namespace for the given AZ. If the AZ config has KUBENAMESPACE
   * defined, then it is used directly. Otherwise, the namespace is constructed with nodePrefix &
   * azName params. In case of newNamingStyle, the nodePrefix is used as it is.
   */
  public static String getKubernetesNamespace(
      boolean isMultiAZ,
      String nodePrefix,
      String azName,
      Map<String, String> azConfig,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {
    String namespace = azConfig != null ? azConfig.get("KUBENAMESPACE") : "";
    if (StringUtils.isBlank(namespace)) {
      int suffixLen = isMultiAZ ? azName.length() + 1 : 0;
      // Avoid using "-readcluster" so user has more room for
      // specifying the name.
      String readClusterSuffix = "-rr";
      if (isReadOnlyCluster) {
        suffixLen += readClusterSuffix.length();
      }
      // We don't have any suffix in case of new naming.
      suffixLen = newNamingStyle ? 0 : suffixLen;
      namespace = Util.sanitizeKubernetesNamespace(nodePrefix, suffixLen);
      if (newNamingStyle) {
        return namespace;
      }
      if (isReadOnlyCluster) {
        namespace = String.format("%s%s", namespace, readClusterSuffix);
      }
      if (isMultiAZ) {
        namespace = String.format("%s-%s", namespace, azName);
      }
    }
    return namespace;
  }

  /**
   * This method always assumes that old Helm naming style is being used.
   *
   * @deprecated Use {@link #getKubernetesConfigPerPod()} instead as it works for both new and old
   *     Helm naming styles. Read the docstrig of {@link #getKubernetesConfigPerPod()} to understand
   *     more about this deprecation.
   */
  @Deprecated
  public static Map<String, String> getConfigPerNamespace(
      PlacementInfo pi, String nodePrefix, Provider provider, boolean isReadOnlyCluster) {
    Map<String, String> namespaceToConfig = new HashMap<>();
    Map<UUID, Map<String, String>> azToConfig = getConfigPerAZ(pi);
    boolean isMultiAZ = isMultiAZ(provider);
    for (Map.Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
      String kubeconfig = entry.getValue().get("KUBECONFIG");
      if (kubeconfig == null) {
        throw new NullPointerException("Couldn't find a kubeconfig");
      }

      String azName = AvailabilityZone.get(entry.getKey()).getCode();
      String namespace =
          getKubernetesNamespace(
              isMultiAZ, nodePrefix, azName, entry.getValue(), false, isReadOnlyCluster);
      namespaceToConfig.put(namespace, kubeconfig);
      if (!isMultiAZ) {
        break;
      }
    }

    return namespaceToConfig;
  }

  // Returns the cpu core count give instance type.
  public static double getCoreCountFromInstanceType(InstanceType instanceType, boolean isMaster) {
    if (!isMaster) {
      return instanceType.getNumCores();
    }
    double cpu = 2.0; // default value from chart.
    String instanceTypeCode = instanceType.getInstanceTypeCode();
    if (instanceTypeCode.equals("dev")) {
      cpu = 0.5;
    } else if (instanceTypeCode.equals("cloud")) {
      cpu = 0.3;
    }

    return cpu;
  }

  /**
   * Returns a map of pod private_ip to configuration for all pods in the nodeDetailsSet. The
   * configuration is a map with keys "podName", "namespace", and "KUBECONFIG". This method is
   * useful for both new and old naming styles, as we are not using namespace as key.
   *
   * <p>In new naming style, all the AZ deployments are in the same namespace. These AZs can be in
   * different Kubernetes clusters, and will have same namespace name across all of them. This
   * requires different kubeconfig per cluster/pod to access them.
   */
  public static Map<String, Map<String, String>> getKubernetesConfigPerPod(
      PlacementInfo pi, Set<NodeDetails> nodeDetailsSet) {
    Map<String, Map<String, String>> podToConfig = new HashMap<>();
    Map<UUID, String> azToKubeconfig = new HashMap<>();
    Map<UUID, Map<String, String>> azToConfig = getConfigPerAZ(pi);
    for (Map.Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
      String kubeconfig = entry.getValue().get("KUBECONFIG");
      if (kubeconfig == null) {
        throw new NullPointerException("Couldn't find a kubeconfig for AZ " + entry.getKey());
      }
      azToKubeconfig.put(entry.getKey(), kubeconfig);
    }

    for (NodeDetails nd : nodeDetailsSet) {
      String kubeconfig = azToKubeconfig.get(nd.azUuid);
      if (kubeconfig == null) {
        // Ignore such a node because its corresponding AZ is removed from the PlacementInfo and the
        // node will be removed too.
        continue;
      }
      podToConfig.put(
          nd.cloudInfo.private_ip,
          ImmutableMap.of(
              "podName",
              nd.getK8sPodName(),
              "namespace",
              nd.getK8sNamespace(),
              "KUBECONFIG",
              kubeconfig));
    }
    return podToConfig;
  }

  public static Map<String, Map<String, String>> getKubernetesConfigPerPodName(
      PlacementInfo pi, Set<NodeDetails> nodeDetailsSet) {
    Map<String, Map<String, String>> podToConfig = new HashMap<>();
    Map<UUID, String> azToKubeconfig = new HashMap<>();
    Map<UUID, Map<String, String>> azToConfig = getConfigPerAZ(pi);
    for (Map.Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
      String kubeconfig = entry.getValue().get("KUBECONFIG");
      if (kubeconfig == null) {
        throw new NullPointerException("Couldn't find a kubeconfig for AZ " + entry.getKey());
      }
      azToKubeconfig.put(entry.getKey(), kubeconfig);
    }

    for (NodeDetails nd : nodeDetailsSet) {
      String kubeconfig = azToKubeconfig.get(nd.azUuid);
      if (kubeconfig == null) {
        // Ignore such a node because its corresponding AZ is removed from the PlacementInfo and the
        // node will be removed too.
        continue;
      }
      podToConfig.put(nd.nodeName, ImmutableMap.of("KUBECONFIG", kubeconfig));
    }
    return podToConfig;
  }

  /**
   * Returns a set of namespaces for the pods of the universe. Newer kubernetes universes will
   * always have a single namespace for all of a universe's pods.
   *
   * @param universe
   * @return a set of namespaces
   */
  public static Set<String> getUniverseNamespaces(Universe universe) {
    Set<String> namespaces = new HashSet<>();
    Map<String, Map<String, String>> podAddrToConfig = new HashMap<>();
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      Set<NodeDetails> nodes = universe.getUniverseDetails().getNodesInCluster(cluster.uuid);
      PlacementInfo pi = cluster.placementInfo;
      podAddrToConfig.putAll(KubernetesUtil.getKubernetesConfigPerPod(pi, nodes));
    }
    namespaces =
        podAddrToConfig.values().stream().map(e -> e.get("namespace")).collect(Collectors.toSet());
    return namespaces;
  }

  // Compute the master addresses of the pods in the deployment if multiAZ.
  public static String computeMasterAddresses(
      PlacementInfo pi,
      Map<UUID, Integer> azToNumMasters,
      String nodePrefix,
      String universeName,
      Provider provider,
      int masterRpcPort,
      boolean newNamingStyle) {
    List<String> masters = new ArrayList<>();
    Map<UUID, String> azToDomain = getDomainPerAZ(pi);
    boolean isMultiAZ = isMultiAZ(provider);
    for (Map.Entry<UUID, Integer> entry : azToNumMasters.entrySet()) {
      AvailabilityZone az = AvailabilityZone.getOrBadRequest(entry.getKey());
      Map<String, String> azConfig = CloudInfoInterface.fetchEnvVars(az);
      String namespace =
          getKubernetesNamespace(
              isMultiAZ,
              nodePrefix,
              az.getCode(),
              azConfig,
              newNamingStyle,
              false /*isReadOnlyCluster*/);
      String domain = azToDomain.get(entry.getKey());
      String helmFullName =
          getHelmFullNameWithSuffix(
              isMultiAZ, nodePrefix, universeName, az.getCode(), newNamingStyle, false);
      for (int idx = 0; idx < entry.getValue(); idx++) {
        String masterIP =
            formatPodAddress(
                azConfig.getOrDefault("KUBE_POD_ADDRESS_TEMPLATE", Util.K8S_POD_FQDN_TEMPLATE),
                String.format("%syb-master-%d", helmFullName, idx),
                helmFullName + "yb-masters",
                namespace,
                domain);
        masters.add(String.format("%s:%d", masterIP, masterRpcPort));
      }
    }

    return String.join(",", masters);
  }

  public static Map<UUID, String> getDomainPerAZ(PlacementInfo pi) {
    Map<UUID, String> azToDomain = new HashMap<>();
    for (PlacementCloud pc : pi.cloudList) {
      for (PlacementRegion pr : pc.regionList) {
        for (PlacementAZ pa : pr.azList) {
          AvailabilityZone az = AvailabilityZone.getOrBadRequest(pa.uuid);
          Map<String, String> config = CloudInfoInterface.fetchEnvVars(az);
          if (config.containsKey("KUBE_DOMAIN")) {
            azToDomain.put(pa.uuid, config.get("KUBE_DOMAIN"));
          } else {
            azToDomain.put(pa.uuid, "cluster.local");
          }
        }
      }
    }

    return azToDomain;
  }

  // This method decides the value of isMultiAZ based on the value of azName.
  // In case of single AZ providers, the azName is passed as null.
  public static String getHelmReleaseName(
      String nodePrefix,
      String universeName,
      String azName,
      boolean isReadOnlyCluster,
      boolean newNamingStyle) {
    boolean isMultiAZ = (azName != null);
    return getHelmReleaseName(
        isMultiAZ, nodePrefix, universeName, azName, isReadOnlyCluster, newNamingStyle);
  }

  public static String getHelmReleaseName(
      boolean isMultiAZ,
      String nodePrefix,
      String universeName,
      String azName,
      boolean isReadOnlyCluster,
      boolean newNamingStyle) {
    // Remove spaces in unvierse/az names.
    universeName = universeName.replaceAll("\\s", "");
    if (azName != null) azName = azName.replaceAll("\\s", "");
    // Using nodePrefix as universe names can be same from two different platforms using same k8s
    // cluster.
    // If user names are same then nodePrefix will also be same. Can we use user uuid instead?
    String tempName = isReadOnlyCluster ? nodePrefix + "-rr" : nodePrefix;
    tempName = isMultiAZ ? String.format("%s-%s", tempName, azName) : tempName;
    if (!newNamingStyle) {
      // Helm release name can't be more than 53 characters length.
      // sanitizeKubernetesNamespace limits length(tempName)+reserveSuffixLength to 63 characters.
      return Util.sanitizeKubernetesNamespace(tempName, 10);
    }

    // Need hash in relname to distinguish pods from multiple releases in same namespace.
    String hash = Util.base36hash(tempName);

    // helmReleaseName fromat: "yb" + prefix(11 chars of univ) + "-" + suffix(13 chars of AZ + rr) +
    // "-" + hash(4)
    String azRR = (isMultiAZ ? azName : "") + (isReadOnlyCluster ? "rr" : "");
    String uniqueRelName;
    if (azRR.length() == 0) {
      uniqueRelName =
          String.format(
              "%s%s-%s",
              "yb",
              universeName.toLowerCase().substring(0, Math.min(universeName.length(), 25)),
              hash);
      // 25 because 11 chars dedicated chars for univ, 1 -, 13 chars from azRR.
    } else {
      uniqueRelName =
          String.format(
              "%s%s-%s-%s",
              "yb",
              universeName.toLowerCase().substring(0, Math.min(universeName.length(), 11)),
              azRR.toLowerCase().substring(Math.max(0, azRR.length() - 13), azRR.length()),
              hash);
    }

    return uniqueRelName;
  }

  // Returns a string which is exactly the same as yugabyte chart's
  // helper template yugabyte.fullname. This is prefixed to all the
  // resource names when newNamingstyle is being used. We set
  // fullnameOverride in the Helm overrides.
  // https://git.io/yugabyte.fullname
  public static String getHelmFullNameWithSuffix(
      boolean isMultiAZ,
      String nodePrefix,
      String universeName,
      String azName,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {
    if (!newNamingStyle) {
      return "";
    }
    return getHelmReleaseName(
            isMultiAZ, nodePrefix, universeName, azName, isReadOnlyCluster, newNamingStyle)
        + "-";
  }

  /**
   * Replaces the placeholders from the template with given values and return the resultant string.
   *
   * <p>Currently supported placeholders are: {pod_name}, {service_name}, {namespace}, and
   * {cluster_domain}
   */
  public static String formatPodAddress(
      String template, String podName, String serviceName, String namespace, String clusterDomain) {
    template = template.replace("{pod_name}", podName);
    template = template.replace("{service_name}", serviceName);
    template = template.replace("{namespace}", namespace);
    template = template.replace("{cluster_domain}", clusterDomain);
    if (!Util.isValidDNSAddress(template)) {
      throw new RuntimeException(
          "Pod address template generated an invalid DNS, allowed placeholders are: {pod_name},"
              + " {service_name}, {namespace}, and {cluster_domain}");
    }
    return template;
  }

  /**
   * Returns true if MCS is enabled on the current k8s universe, which is determined by whether the
   * kubePodAddressTemplate field is set in the availabilityZone details.
   *
   * @param universe
   * @return true if MCS is enabled.
   */
  public static boolean isMCSEnabled(Universe universe) {
    UUID providerUUID =
        UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider);
    Provider provider = Provider.getOrBadRequest(providerUUID);
    if (provider.getCloudCode().equals(Common.CloudType.kubernetes)) {
      List<Region> regions = Region.getByProvider(provider.getUuid());
      for (Region region : regions) {
        List<AvailabilityZone> zones = AvailabilityZone.getAZsForRegion(region.getUuid());
        for (AvailabilityZone zone : zones) {
          if (StringUtils.isNotBlank(
              zone.getDetails().getCloudInfo().getKubernetes().getKubePodAddressTemplate())) {
            return true;
          }
        }
      }
    }
    return false;
  }

  /**
   * Gives a map of [provider|region|az]-[UUID] to KubernetesInfo objects for all the Kubernetes
   * providers. These are only for the active regions and AZs.
   *
   * <p>Example: for an AZ the key would be az-4abff05a-9b26-4d25-8a54-562669d587b0.
   */
  public static Map<String, KubernetesInfo> getAllKubernetesInfos() {
    Map<String, KubernetesInfo> k8sInfos = new HashMap<String, KubernetesInfo>();
    // Go through all the customers, providers, regions, and the
    // zones.
    for (Customer customer : Customer.getAll()) {
      for (Provider provider :
          Provider.getAll(customer.getUuid(), null, Common.CloudType.kubernetes)) {
        k8sInfos.put("provider-" + provider.getUuid().toString(), CloudInfoInterface.get(provider));
        for (Region region : provider.getAllRegions()) {
          if (!region.isActive()) {
            continue;
          }
          k8sInfos.put("region-" + region.getUuid().toString(), CloudInfoInterface.get(region));
          for (AvailabilityZone az : region.getAllZones()) {
            if (!az.isActive()) {
              continue;
            }
            k8sInfos.put("az-" + az.getUuid().toString(), CloudInfoInterface.get(az));
          }
        }
      }
    }
    return k8sInfos;
  }

  /** Returns true if actual PVC size in k8s cluster is not the same as given newDiskSizeGi. */
  public static boolean needsExpandPVC(
      String namespace,
      String helmReleaseName,
      String appName,
      boolean newNamingStyle,
      String newDiskSizeGi,
      Map<String, String> config,
      KubernetesManagerFactory kubernetesManagerFactory) {
    KubernetesManager k8s = kubernetesManagerFactory.getManager();
    List<Quantity> pvcSizeList =
        k8s.getPVCSizeList(config, namespace, helmReleaseName, appName, newNamingStyle);
    // Go through each PVCsize and check that its different from newDiskSize, if yes return true.
    Quantity newDiskSizeQty = new Quantity(newDiskSizeGi);
    for (Quantity pvcSize : pvcSizeList) {
      if (!pvcSize.equals(newDiskSizeQty)) {
        return true; // Exit the loop as soon as a difference is found
      }
    }
    return false;
  }

  // topology/failure-domain labels from the Kubernetes nodes.
  public static Multimap<String, String> computeKubernetesRegionToZoneInfo(
      Map<String, String> config, KubernetesManagerFactory kubernetesManagerFactory) {
    List<Node> nodes = kubernetesManagerFactory.getManager().getNodeInfos(config);
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
  }
}
