// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.PlacementInfoUtil.isMultiAZ;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;

public class KubernetesUtil {

  // TODO(bhavin192): there should be proper merging of the
  // configuration from all the levels. Something like storage class
  // from AZ level, kubeconfig from global level, namespace from
  // region level and so on.

  // Get the zones with the kubeconfig for that zone.
  public static Map<UUID, Map<String, String>> getConfigPerAZ(PlacementInfo pi) {
    Map<UUID, Map<String, String>> azToConfig = new HashMap<>();
    for (PlacementInfo.PlacementCloud pc : pi.cloudList) {
      Map<String, String> cloudConfig = Provider.get(pc.uuid).getUnmaskedConfig();
      for (PlacementInfo.PlacementRegion pr : pc.regionList) {
        Map<String, String> regionConfig = Region.get(pr.uuid).getUnmaskedConfig();
        for (PlacementInfo.PlacementAZ pa : pr.azList) {
          Map<String, String> zoneConfig = AvailabilityZone.get(pa.uuid).getUnmaskedConfig();
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
    }

    return azToConfig;
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
    String namespace = azConfig.get("KUBENAMESPACE");
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

      String azName = AvailabilityZone.get(entry.getKey()).code;
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

  // Compute the master addresses of the pods in the deployment if multiAZ.
  public static String computeMasterAddresses(
      PlacementInfo pi,
      Map<UUID, Integer> azToNumMasters,
      String nodePrefix,
      Provider provider,
      int masterRpcPort,
      boolean newNamingStyle) {
    List<String> masters = new ArrayList<String>();
    Map<UUID, String> azToDomain = getDomainPerAZ(pi);
    boolean isMultiAZ = isMultiAZ(provider);
    for (Map.Entry<UUID, Integer> entry : azToNumMasters.entrySet()) {
      AvailabilityZone az = AvailabilityZone.get(entry.getKey());
      Map<String, String> azConfig = az.getUnmaskedConfig();
      String namespace =
          getKubernetesNamespace(
              isMultiAZ,
              nodePrefix,
              az.code,
              azConfig,
              newNamingStyle,
              false /*isReadOnlyCluster*/);
      String domain = azToDomain.get(entry.getKey());
      String helmFullName =
          getHelmFullNameWithSuffix(isMultiAZ, nodePrefix, az.code, newNamingStyle, false);
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
    for (PlacementInfo.PlacementCloud pc : pi.cloudList) {
      for (PlacementInfo.PlacementRegion pr : pc.regionList) {
        for (PlacementInfo.PlacementAZ pa : pr.azList) {
          Map<String, String> config = AvailabilityZone.get(pa.uuid).getUnmaskedConfig();
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

  // This method decides the value of isMultiAZ based on the value of
  // azName. In case of single AZ providers, the azName is passed as
  // null.
  public static String getHelmReleaseName(
      String nodePrefix, String azName, boolean isReadOnlyCluster) {
    boolean isMultiAZ = (azName != null);
    return getHelmReleaseName(isMultiAZ, nodePrefix, azName, isReadOnlyCluster);
  }

  // TODO(bhavin192): have the release name sanitization call here,
  // TODO(bhavin192): have the release name sanitization call here,
  // instead of doing it in KubernetesManager implementations.
  public static String getHelmReleaseName(
      boolean isMultiAZ, String nodePrefix, String azName, boolean isReadOnlyCluster) {
    String helmReleaseName = isReadOnlyCluster ? nodePrefix + "-rr" : nodePrefix;
    return isMultiAZ ? String.format("%s-%s", helmReleaseName, azName) : helmReleaseName;
  }

  // Returns a string which is exactly the same as yugabyte chart's
  // helper template yugabyte.fullname. This is prefixed to all the
  // resource names when newNamingstyle is being used. We set
  // fullnameOverride in the Helm overrides.
  // https://git.io/yugabyte.fullname
  public static String getHelmFullNameWithSuffix(
      boolean isMultiAZ,
      String nodePrefix,
      String azName,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {
    if (!newNamingStyle) {
      return "";
    }
    String releaseName = getHelmReleaseName(isMultiAZ, nodePrefix, azName, isReadOnlyCluster);
    // TODO(bhavin192): remove this once we make the sanitization to
    // be 43 characters long.
    // <release name> | truncate 43
    if (releaseName.length() > 43) {
      releaseName = releaseName.substring(0, 43);
    }
    return releaseName + "-";
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
          "Pod address template generated an invalid DNS, check if placeholders are correct.");
    }
    return template;
  }
}
