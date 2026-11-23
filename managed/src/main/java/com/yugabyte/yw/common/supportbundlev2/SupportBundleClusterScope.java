// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.supportbundle.SupportBundleK8sSupport;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest.Scope;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collection;
import java.util.Comparator;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;

/**
 * Runs a YSQL, YCQL or yb-admin member marked {@code scope: cluster} on one node per bundle instead
 * of on every node: it reads cluster-wide state, so the other nodes would only repeat it. The task
 * registers the chosen nodes after its reachability check; an unregistered bundle runs everything
 * everywhere, as before.
 */
@Slf4j
@Singleton
public class SupportBundleClusterScope {

  // yb-ts-cli to the node's tserver rpc port; curls of the node's tserver web pages.
  static final Set<String> LOCAL_TSERVER_MEMBERS =
      Set.of("collect_cdc_state", "collect_threadz_memz_rpcz_stats");

  private static final List<ComponentType> SCOPED_COMPONENTS =
      List.of(
          ComponentType.YSQLComponent, ComponentType.YCQLComponent, ComponentType.YbAdminComponent);

  private final SupportBundleV2ManifestLoader manifestLoader;
  private final Map<UUID, Registration> registrations = new ConcurrentHashMap<>();

  @Value
  private static class Registration {
    Set<String> clusterMembers;
    Map<ComponentType, String> chosenNodes;
  }

  @Inject
  public SupportBundleClusterScope(SupportBundleV2ManifestLoader manifestLoader) {
    this.manifestLoader = manifestLoader;
  }

  /** The members YBA's manifest marks {@code scope: cluster}. */
  public Set<String> clusterMembers() {
    return manifestLoader.load().getSpec().getComponentConfigs().entrySet().stream()
        .filter(entry -> entry.getValue() != null && entry.getValue().getScope() == Scope.CLUSTER)
        .map(Map.Entry::getKey)
        .collect(Collectors.toSet());
  }

  /**
   * YSQL and YCQL need a tserver; this is the gate the components themselves apply. A master-only
   * node, a Kubernetes master pod or a dedicated master VM, has none.
   */
  public static boolean componentRunsOn(NodeDetails node, ComponentType componentType) {
    boolean tserverOnly =
        componentType == ComponentType.YSQLComponent
            || componentType == ComponentType.YCQLComponent;
    return !tserverOnly || node == null || node.isTserver;
  }

  /**
   * False for a node-local member that talks to the node's own tserver, on a node that doesn't run
   * one (a dedicated master). On Kubernetes {@code SupportBundleK8sSupport.hasTserverIfRequired}
   * skips them on master pods; this covers VMs too.
   */
  public static boolean componentRunsOn(NodeDetails node, String componentName) {
    return node == null || node.isTserver || !LOCAL_TSERVER_MEMBERS.contains(componentName);
  }

  /**
   * False when a script component needs the tserver process and the node doesn't run one: cdc_state
   * and threadz talk to the local tserver everywhere, and on Kubernetes YbcLogs also needs the
   * tserver pod, whose sidecar YBC is. On VMs YBC runs on master nodes too.
   */
  public static boolean componentRunsOn(Universe universe, NodeDetails node, String componentName) {
    return SupportBundleK8sSupport.hasTserverIfRequired(universe, node, componentName)
        && componentRunsOn(node, componentName);
  }

  /**
   * First by name, so the estimate and the collection pick the same node: among nodes YBA has as
   * Live, and masters for yb-admin, when there are any, else among all that can run it.
   */
  public static NodeDetails chooseNode(Collection<NodeDetails> nodes, ComponentType componentType) {
    List<NodeDetails> candidates =
        nodes.stream()
            .filter(node -> componentRunsOn(node, componentType))
            .sorted(Comparator.comparing(NodeDetails::getNodeName))
            .collect(Collectors.toList());
    return candidates.stream()
        .filter(node -> isPreferred(node, componentType))
        .findFirst()
        .orElse(candidates.isEmpty() ? null : candidates.get(0));
  }

  private static boolean isPreferred(NodeDetails node, ComponentType componentType) {
    return node.state == NodeState.Live
        && (componentType != ComponentType.YbAdminComponent || node.isMaster);
  }

  public void register(UUID bundleUuid, Collection<NodeDetails> reachableNodes) {
    Map<ComponentType, String> chosenNodes = new EnumMap<>(ComponentType.class);
    for (ComponentType componentType : SCOPED_COMPONENTS) {
      NodeDetails node = chooseNode(reachableNodes, componentType);
      if (node != null) {
        chosenNodes.put(componentType, node.getNodeName());
      }
    }
    Set<String> clusterMembers;
    try {
      clusterMembers = clusterMembers();
    } catch (RuntimeException e) {
      // Collection never fails over scope: without the manifest every member runs everywhere.
      log.warn(
          "Could not read member scopes from the manifest; running every member on every node", e);
      clusterMembers = Set.of();
    }
    registrations.put(bundleUuid, new Registration(clusterMembers, chosenNodes));
  }

  /** The node this bundle's cluster-scope members of that component type run on, if registered. */
  public String chosenNode(UUID bundleUuid, ComponentType componentType) {
    Registration registration = bundleUuid == null ? null : registrations.get(bundleUuid);
    return registration == null ? null : registration.getChosenNodes().get(componentType);
  }

  /** True for node-scope members, unregistered bundles, and the chosen node of a cluster member. */
  public boolean runsOn(
      UUID bundleUuid, ComponentType componentType, String memberName, NodeDetails node) {
    Registration registration = bundleUuid == null ? null : registrations.get(bundleUuid);
    if (registration == null || !registration.getClusterMembers().contains(memberName)) {
      return true;
    }
    String chosenNode = registration.getChosenNodes().get(componentType);
    if (chosenNode == null || chosenNode.equals(node.getNodeName())) {
      return true;
    }
    log.info(
        "Skipping cluster-scope {} member '{}' on node '{}': it is collected on node '{}'",
        componentType,
        memberName,
        node.getNodeName(),
        chosenNode);
    return false;
  }

  public void forgetBundle(UUID bundleUuid) {
    if (bundleUuid != null) {
      registrations.remove(bundleUuid);
    }
  }
}
