// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Set;

/**
 * Decides which support bundle v2 sub-components apply to a Kubernetes universe, and which pod they
 * belong on.
 *
 * <p>Everything the v2 node path needs works on Kubernetes: {@code NodeUniverseManager} routes to
 * {@code run_node_action.py k8s}, which streams scripts over {@code kubectl exec} and moves tars
 * with {@code kubectl cp}. What does not carry over is the environment inside a pod, in two
 * different ways, handled at two different times:
 *
 * <ul>
 *   <li>{@link #K8S_UNSUPPORTED_COMPONENTS} can never work in a pod regardless of which pod it is,
 *       so the API prunes them from the request and they never reach {@code BundleDetails} or the
 *       bundle's manifest.json.
 *   <li>{@link #K8S_TSERVER_ONLY_COMPONENTS} depend on the tserver process. A {@code NodeDetails}
 *       is one pod on Kubernetes, and masters run in their own pods, so this can only be decided
 *       per node at collection time.
 * </ul>
 */
public final class SupportBundleV2K8sSupport {

  /**
   * Sub-components that cannot work inside a pod at all. {@code SystemLogs} reads the host's {@code
   * /var/log} through {@code sudo}; {@code NodeAgent} collects an agent install that Kubernetes
   * universes do not have ({@code NodeAgentClient.isCloudTypeSupported(kubernetes)} is false).
   */
  public static final Set<String> K8S_UNSUPPORTED_COMPONENTS =
      ImmutableSet.of("SystemLogs", "NodeAgent");

  /**
   * Sub-components that need the tserver process, so they only run on tserver pods. {@code YbcLogs}
   * targets the ybc controller that lives with the tserver, and the other two dial the tserver's
   * RPC and HTTP ports.
   *
   * <p>The path and metadata components are deliberately absent: they list both {@code master/...}
   * and {@code tserver/...} entries and the scripts drop the ones that are missing, so each pod
   * contributes its half and the union across pods is complete.
   */
  public static final Set<String> K8S_TSERVER_ONLY_COMPONENTS =
      ImmutableSet.of("YbcLogs", "collect_cdc_state", "collect_threadz_memz_rpcz_stats");

  private SupportBundleV2K8sSupport() {}

  public static boolean isK8s(Universe universe) {
    if (universe == null || universe.getUniverseDetails() == null) {
      return false;
    }
    return CloudType.kubernetes.equals(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType);
  }

  /** False only for sub-components that cannot run in any pod. Always true off Kubernetes. */
  public static boolean supportedOnK8s(String componentLabel) {
    return componentLabel == null || !K8S_UNSUPPORTED_COMPONENTS.contains(componentLabel);
  }

  public static boolean requiresTserverPod(String componentLabel) {
    return componentLabel != null && K8S_TSERVER_ONLY_COMPONENTS.contains(componentLabel);
  }

  /**
   * True unless the node is a Kubernetes pod without the tserver process. Off Kubernetes a node
   * runs both processes, so this is always true there.
   */
  public static boolean isTserverPod(Universe universe, NodeDetails node) {
    return !isK8s(universe) || node == null || node.isTserver;
  }

  /** True when the sub-component can run on this specific node. */
  public static boolean matchesPodRole(Universe universe, NodeDetails node, String componentLabel) {
    return !requiresTserverPod(componentLabel) || isTserverPod(universe, node);
  }
}
