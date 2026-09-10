// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.metrics;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.List;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Resolves a Prometheus container-metric label triple (pod_name, namespace, az_name) into the YBA
 * node name that {@link KubernetesUtil#getKubernetesNodeName} produces for the same pod (e.g.
 * {@code yb-tserver-0}, {@code yb-tserver-0_us-west1-a}, {@code yb-tserver-0_us-west1-a-readonly}).
 *
 * <p>Why not just look each pod up in {@code Universe.getNodes()}? The metric time window can span
 * points where a node has since been removed (rolling upgrade, cluster shrink, full-move edit
 * etc.), but Prometheus retains the historical samples. A {@code NodeDetails}-based map would
 * silently miss those samples and fall through to a heuristic that cannot emit the {@code
 * -readonly} suffix or distinguish primary vs. RR pods. The cluster / AZ / helm-release metadata we
 * consume here, on the other hand, is derived from {@link UniverseDefinitionTaskParams} and
 * therefore persists as long as the cluster itself exists.
 *
 * <p>The resolver is stateless once built and safe to share across the metric-query executor's
 * thread pool.
 */
public class K8sPodNodeNameResolver {

  private static final Logger LOG = LoggerFactory.getLogger(K8sPodNodeNameResolver.class);

  // Helm release / namespace descriptor for a single (cluster, AZ) tuple. Encapsulates the small
  // amount of info the resolver needs to reconstruct a node name for any pod produced by that
  // release, without having to consult live NodeDetails records. Package-private (and with a
  // package-private constructor) so tests in this package can build resolvers without needing a
  // full Universe object.
  static final class ReleaseInfo {
    final String helmReleaseName; // in new naming style this is a strict prefix of the pod name
    final String namespace;
    final String azName;
    final boolean isMultiAZ;
    final boolean isReadReplica;

    ReleaseInfo(
        String helmReleaseName,
        String namespace,
        String azName,
        boolean isMultiAZ,
        boolean isReadReplica) {
      this.helmReleaseName = helmReleaseName;
      this.namespace = namespace;
      this.azName = azName;
      this.isMultiAZ = isMultiAZ;
      this.isReadReplica = isReadReplica;
    }
  }

  private final boolean newNamingStyle;
  private final List<ReleaseInfo> releases;
  // Whether *any* cluster in the underlying universe spans multiple AZs. Derived once at build
  // time from the same {@link ReleaseInfo#isMultiAZ} flags the resolver already tracks per
  // release, so we don't have to recompute it (or thread it through the executor separately).
  // A mixed universe still counts as multi-AZ because the multi-AZ cluster's pods would carry
  // the AZ suffix in their node names; matching that here keeps the fallback naming in
  // {@link MetricQueryResponse#getInstanceNameLabelForKubernetes} consistent with
  // {@link com.yugabyte.yw.common.KubernetesUtil#getKubernetesNodeName}.
  private final boolean multiAZ;

  private K8sPodNodeNameResolver(boolean newNamingStyle, List<ReleaseInfo> releases) {
    this.newNamingStyle = newNamingStyle;
    this.releases = releases;
    this.multiAZ = releases.stream().anyMatch(r -> r.isMultiAZ);
  }

  /**
   * Whether the underlying universe should be treated as a multi-AZ Kubernetes deployment for the
   * purposes of pod-derived instance-name construction. See {@link #multiAZ} for the exact
   * semantics.
   */
  public boolean isMultiAZ() {
    return multiAZ;
  }

  /**
   * Test-only factory that builds a resolver directly from a list of release descriptors, so unit
   * tests don't have to stand up a full {@link Universe}. Returns null when {@code releases} is
   * empty, mirroring {@link #forUniverse}'s behaviour so tests can also exercise the "no resolver"
   * fallback path in {@code MetricQueryResponse}.
   */
  @VisibleForTesting
  static K8sPodNodeNameResolver ofReleases(boolean newNamingStyle, List<ReleaseInfo> releases) {
    if (releases == null || releases.isEmpty()) {
      return null;
    }
    return new K8sPodNodeNameResolver(newNamingStyle, new ArrayList<>(releases));
  }

  /**
   * Build a resolver for the given universe by enumerating every (cluster, AZ) combination and
   * precomputing its helm release name and namespace. Returns {@code null} for non-Kubernetes
   * universes or when the metadata is insufficient to construct any release info; callers should
   * treat a null resolver the same as "no mapping available" and fall back to the label-only
   * heuristic.
   */
  public static K8sPodNodeNameResolver forUniverse(Universe universe) {
    if (universe == null) {
      return null;
    }
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    if (details == null || details.clusters == null || details.clusters.isEmpty()) {
      return null;
    }
    boolean newNamingStyle = details.useNewHelmNamingStyle;
    String nodePrefix = details.nodePrefix;
    List<ReleaseInfo> releases = new ArrayList<>();
    for (Cluster cluster : details.clusters) {
      Provider provider;
      try {
        provider = Util.getSingleProvider(cluster);
      } catch (Exception e) {
        // Non-K8s or misconfigured cluster; skip - we still want to build a resolver for any
        // K8s clusters that might exist alongside it.
        continue;
      }
      if (provider == null) {
        continue;
      }
      boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);
      boolean isReadReplica = cluster.clusterType == ClusterType.ASYNC;
      for (Region region : provider.getRegions()) {
        for (AvailabilityZone az : region.getZones()) {
          String azName = az.getName();
          String helmReleaseName;
          String namespace;
          try {
            helmReleaseName =
                KubernetesUtil.getHelmReleaseName(
                    isMultiAZ,
                    nodePrefix,
                    universe.getName(),
                    azName,
                    isReadReplica,
                    newNamingStyle);
            // AZ-level KUBENAMESPACE overrides are read from az config in
            // KubernetesUtil.getKubernetesNamespace; we don't have that map wired in cleanly
            // here (it would require touching several call sites for a rarely-used override),
            // so we compute the derived-name form and let the pod-prefix match (which is
            // always exact for new naming style) win when both apply.
            namespace =
                KubernetesUtil.getKubernetesNamespace(
                    isMultiAZ, nodePrefix, azName, null, newNamingStyle, isReadReplica);
          } catch (Exception e) {
            LOG.warn(
                "Failed to compute helm release / namespace for universe={} cluster={} az={}",
                universe.getName(),
                cluster.uuid,
                azName,
                e);
            continue;
          }
          releases.add(
              new ReleaseInfo(helmReleaseName, namespace, azName, isMultiAZ, isReadReplica));
        }
      }
    }
    if (releases.isEmpty()) {
      return null;
    }
    return new K8sPodNodeNameResolver(newNamingStyle, releases);
  }

  /**
   * Reconstruct the YBA node name for a Prometheus pod-based sample.
   *
   * @param podName value of the Prometheus {@code pod_name} label. For volume metrics callers
   *     should pass the pod name recovered from the {@code persistentvolumeclaim} label (i.e. with
   *     the {@code datadir<N>-} prefix already stripped) so we can prefix-match against the helm
   *     release.
   * @param namespace value of the Prometheus {@code namespace} label, or {@code null} if the label
   *     wasn't present. Used as a fallback disambiguator for old-naming-style universes where the
   *     pod name does not embed the release.
   * @param azLabel value of the Prometheus {@code az_name} label. Used as a last-resort AZ source
   *     when no release info was matched.
   * @return the YBA node name if the pod could be resolved to a release in this universe; otherwise
   *     {@code null}, in which case the caller should apply its own fallback.
   */
  public String resolveNodeName(String podName, String namespace, String azLabel) {
    if (StringUtils.isBlank(podName)) {
      return null;
    }
    // The "yb-<server>-..." tail (everything from the last "yb-" onward) is what YBA already uses
    // as the node name's core (see KubernetesUtil.getKubernetesNodeName). Both old and new
    // naming styles produce pod names that end this way; extracting it here means we don't have
    // to know the helm release name to get the core right, only the AZ / readonly suffixes.
    int ybIdx = podName.lastIndexOf("yb-");
    if (ybIdx < 0) {
      return null;
    }
    String coreName = podName.substring(ybIdx);

    ReleaseInfo match = findMatchingRelease(podName, namespace);
    if (match == null) {
      return null;
    }
    StringBuilder result = new StringBuilder(coreName);
    if (match.isMultiAZ) {
      // Prefer the AZ recorded on the release (deterministic; independent of scrape-time
      // relabelling) but fall back to the Prometheus label if the release's AZ is missing for
      // any reason.
      String az = StringUtils.isNotBlank(match.azName) ? match.azName : azLabel;
      if (StringUtils.isNotBlank(az)) {
        result.append('_').append(az);
      }
    }
    if (match.isReadReplica) {
      result.append(Universe.READONLY);
    }
    return result.toString();
  }

  private ReleaseInfo findMatchingRelease(String podName, String namespace) {
    // New naming style: the helm release name is a strict prefix of the pod name (see
    // KubernetesUtil.getHelmFullNameWithSuffix). This match is exact and always distinguishes
    // primary from RR (they get different release names via the `-rr`/`rr` suffix), so we try
    // it first regardless of which naming style the universe reports; if the universe was
    // migrated from old to new (or vice versa) both matchers still find the right entry.
    if (newNamingStyle) {
      for (ReleaseInfo info : releases) {
        if (StringUtils.isNotBlank(info.helmReleaseName)
            && podName.startsWith(info.helmReleaseName + "-yb-")) {
          return info;
        }
      }
    }
    // Old naming style / fallback: pod names are just "yb-<server>-N" and the primary vs. RR
    // distinction lives entirely on the namespace (nodePrefix vs. nodePrefix + "-rr", both
    // further suffixed with the AZ name for multi-AZ). Match on that instead.
    if (StringUtils.isNotBlank(namespace)) {
      for (ReleaseInfo info : releases) {
        if (namespace.equals(info.namespace)) {
          return info;
        }
      }
    }
    return null;
  }
}
