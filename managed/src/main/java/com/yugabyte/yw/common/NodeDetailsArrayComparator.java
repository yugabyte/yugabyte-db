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
      return Integer.compare(nodeIdx(node1), nodeIdx(node2));
    }
    if (path != null && path.endsWith("clusters")) {
      return uuid(node1).compareTo(uuid(node2));
    }
    return Integer.compare(node1.hashCode(), node2.hashCode());
  }

  private static int nodeIdx(JsonNode node) {
    // NodeDetails.nodeIdx defaults to -1 when unset (e.g. dedicated master stubs before
    // setNodeNames assigns an index).
    return node.hasNonNull("nodeIdx") ? node.get("nodeIdx").asInt() : -1;
  }

  private static String uuid(JsonNode node) {
    return node.hasNonNull("uuid") ? node.get("uuid").asText() : "";
  }
}
