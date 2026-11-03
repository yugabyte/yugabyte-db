// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;

/**
 * Array element comparator for universe definition JSON that matches {@code nodeDetailsSet} by
 * {@code nodeIdx} and {@code clusters} by {@code uuid} so that an in-place change diffs as a nested
 * {@code REPLACE} rather than {@code DELETE}+{@code ADD}. Other arrays fall back to {@code
 * hashCode} ordering.
 */
public class NodeDetailsArrayComparator implements DeltaEvaluator.ArrayElementComparator {

  @Override
  public int compare(String path, JsonNode node1, JsonNode node2) {
    if (path != null && path.endsWith("nodeDetailsSet")) {
      return nodeUuid(node1).compareTo(nodeUuid(node2));
    }
    // This covers clusters[].regions, clusters[].userIntent.regionList etc.
    return uuid(node1).compareTo(uuid(node2));
  }

  private static String nodeUuid(JsonNode node) {
    if (node.hasNonNull("nodeUuid")) {
      return node.get("nodeUuid").asText();
    }
    return node.hasNonNull("nodeName")
        ? node.get("nodeName").asText()
        : String.valueOf(node.hashCode());
  }

  private static String uuid(JsonNode node) {
    return node.hasNonNull("uuid") ? node.get("uuid").asText() : String.valueOf(node.hashCode());
  }
}
