// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

/**
 * Resolves the caller supplied {@code nodeNames} list into the set of universe nodes that node
 * level support bundle components should be collected from.
 *
 * <p>Candidates are taken from {@link Universe#getNodes()} with no node state filter, matching how
 * the rest of the support bundle path treats nodes: reachability, not state, decides what is
 * collected.
 *
 * <p>Shared by the create/estimate validation handler, the collection task, and the size estimator
 * so all three agree on which nodes a request targets.
 */
@Slf4j
@Singleton
public class SupportBundleV2NodeSelector {

  /** Nodes a request resolved to, along with the requested names that matched nothing. */
  @Value
  public static class NodeSelectionResult {
    Set<NodeDetails> selectedNodes;
    List<String> unmatchedNodeNames;
  }

  /**
   * Resolve requested node names against the universe. An empty or null request selects every node,
   * which is the behaviour when the caller does not ask for specific nodes.
   */
  public NodeSelectionResult resolve(Universe universe, List<String> requestedNodeNames) {
    Collection<NodeDetails> allNodes = universe.getNodes();
    List<String> normalizedNames = normalize(requestedNodeNames);
    if (normalizedNames.isEmpty()) {
      return new NodeSelectionResult(new LinkedHashSet<>(allNodes), new ArrayList<>());
    }

    Map<String, NodeDetails> nodesByName = new LinkedHashMap<>();
    for (NodeDetails node : allNodes) {
      if (node != null && StringUtils.isNotBlank(node.nodeName)) {
        nodesByName.putIfAbsent(node.nodeName, node);
      }
    }

    Set<NodeDetails> selectedNodes = new LinkedHashSet<>();
    List<String> unmatchedNodeNames = new ArrayList<>();
    for (String nodeName : normalizedNames) {
      NodeDetails node = nodesByName.get(nodeName);
      if (node != null) {
        selectedNodes.add(node);
      } else {
        unmatchedNodeNames.add(nodeName);
      }
    }
    return new NodeSelectionResult(selectedNodes, unmatchedNodeNames);
  }

  /**
   * Resolve requested node names, rejecting the request when none of them exist in the universe. A
   * partially matching list is allowed through so that a single stale name does not block
   * collection from the nodes that do exist; the names that matched nothing are logged.
   */
  public Set<NodeDetails> resolveOrThrow(Universe universe, List<String> requestedNodeNames) {
    NodeSelectionResult result = resolve(universe, requestedNodeNames);
    if (result.getSelectedNodes().isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "None of the requested nodeNames %s exist in universe %s.",
              result.getUnmatchedNodeNames(), universe.getUniverseUUID()));
    }
    if (!result.getUnmatchedNodeNames().isEmpty()) {
      log.warn(
          "Ignoring nodeNames {} which do not exist in universe {}. Collecting from {}.",
          result.getUnmatchedNodeNames(),
          universe.getUniverseUUID(),
          nodeNamesOf(result.getSelectedNodes()));
    }
    return result.getSelectedNodes();
  }

  /** Trim, drop blanks, and de-duplicate while keeping the caller's ordering. */
  public static List<String> normalize(List<String> nodeNames) {
    if (CollectionUtils.isEmpty(nodeNames)) {
      return new ArrayList<>();
    }
    return nodeNames.stream()
        .filter(StringUtils::isNotBlank)
        .map(String::trim)
        .distinct()
        .collect(Collectors.toCollection(ArrayList::new));
  }

  public static List<String> nodeNamesOf(Collection<NodeDetails> nodes) {
    if (CollectionUtils.isEmpty(nodes)) {
      return new ArrayList<>();
    }
    return nodes.stream()
        .map(NodeDetails::getNodeName)
        .collect(Collectors.toCollection(ArrayList::new));
  }
}
