// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import play.libs.Json;

/** Utility class for evaluating deltas between two JsonNode objects. */
public final class DeltaEvaluator {

  private static final ObjectMapper MAPPER = Json.mapper();

  private static final ArrayElementComparator DEFAULT_ARRAY_ELEMENT_COMPARATOR =
      (path, node1, node2) -> node1.hashCode() - node2.hashCode();

  /** Enum representing the type of delta operation. */
  enum DeltaType {
    ADD,
    DELETE,
    REPLACE,
    UNKNOWN;

    public static DeltaType fromString(String type) {
      for (DeltaType deltaType : DeltaType.values()) {
        if (deltaType.name().equalsIgnoreCase(type)) {
          return deltaType;
        }
      }
      return UNKNOWN;
    }
  }

  /** Class representing a delta node in the delta JSON tree. */
  static class DeltaNode {
    @JsonProperty("$oldValue")
    private Object oldValue;

    @JsonProperty("$newValue")
    private Object newValue;

    @JsonProperty("$deltaType")
    private DeltaType type;

    private DeltaNode(Object oldValue, Object newValue, DeltaType type) {
      this.oldValue = oldValue;
      this.newValue = newValue;
      this.type = type;
    }

    // Factory methods for creating delta node for a deleted field.
    public static JsonNode createDeleteJsonNode(Object oldValue) {
      return MAPPER.valueToTree(new DeltaNode(oldValue, null, DeltaType.DELETE));
    }

    // Factory methods for creating delta node for an added field.
    public static JsonNode createAddJsonNode(Object newValue) {
      return MAPPER.valueToTree(new DeltaNode(null, newValue, DeltaType.ADD));
    }

    // Factory methods for creating delta node for a replaced field.
    public static JsonNode createReplaceJsonNode(Object oldValue, Object newValue) {
      return MAPPER.valueToTree(new DeltaNode(oldValue, newValue, DeltaType.REPLACE));
    }

    public static DeltaType deltaNodeType(JsonNode node) {
      if (node == null || !node.isObject() || !node.has("$deltaType")) {
        return DeltaType.UNKNOWN;
      }
      return DeltaType.fromString(node.get("$deltaType").asText());
    }
  }

  /** Interface for comparing elements in JSON arrays. */
  public interface ArrayElementComparator {
    /**
     * Compares two JSON nodes at a given path.
     *
     * @param path the JSON path with dots separating the levels.
     * @param node1 the first JSON node to compare.
     * @param node2 the second JSON node to compare.
     * @return a negative integer, zero, or a positive integer as the first argument is less than,
     *     equal to, or greater than the second.
     */
    int compare(String path, JsonNode node1, JsonNode node2);
  }

  /**
   * Given the delta JSON tree, this method generates a new JSON tree that contains only the delta
   * nodes.
   *
   * @param node the delta JSON tree.
   * @return a new JSON tree containing only the delta nodes, or null if there are no delta nodes.
   */
  public static JsonNode generateOnlyDelta(JsonNode node) {
    if (node == null || node.isNull()) {
      return null;
    }
    DeltaType deltaType = DeltaNode.deltaNodeType(node);
    if (deltaType != DeltaType.UNKNOWN) {
      return node;
    }
    if (node.isArray()) {
      ArrayNode result = MAPPER.createArrayNode();
      for (JsonNode element : node) {
        JsonNode value = generateOnlyDelta(element);
        if (value != null && !value.isNull()) {
          result.add(value);
        }
      }
      return result.size() > 0 ? result : null;
    }
    if (node.isObject()) {
      ObjectNode result = MAPPER.createObjectNode();
      for (Iterator<String> iter = node.fieldNames(); iter.hasNext(); ) {
        String fieldName = iter.next();
        JsonNode value = generateOnlyDelta(node.get(fieldName));
        if (value != null && !value.isNull()) {
          result.set(fieldName, value);
        }
      }
      return result.size() > 0 ? result : null;
    }
    return null;
  }

  /**
   * Given the delta JSON tree, this method generates the old value JSON.
   *
   * @param delta the delta JSON tree.
   * @return the old value JSON tree reconstructed from the delta, or null if there is no old value.
   */
  public static JsonNode generateOldValue(JsonNode delta) {
    return generateValue(delta, true);
  }

  /**
   * Given the delta JSON tree, this method generates the new value JSON.
   *
   * @param delta the delta JSON tree.
   * @return the new value JSON tree reconstructed from the delta, or null if there is no new value.
   */
  public static JsonNode generateNewValue(JsonNode delta) {
    return generateValue(delta, false);
  }

  /**
   * Builds a delta JSON tree representing the differences between the current and new data.
   *
   * @param currentData the current data as a JsonNode or any object convertible to JsonNode.
   * @param newData the new data as a JsonNode or any object convertible to JsonNode.
   * @return a JsonNode representing the delta between currentData and newData.
   */
  public static JsonNode buildDeltaJsonTree(Object currentData, Object newData) {
    return buildDeltaJsonTree(currentData, newData, DEFAULT_ARRAY_ELEMENT_COMPARATOR);
  }

  /**
   * Builds a delta JSON tree representing the differences between the current and new data.
   *
   * @param currentData the current data as a JsonNode or or any object convertible to JsonNode
   * @param newData the new data as a JsonNode or any object convertible to JsonNode.
   * @param arrayElementComparator a comparator for comparing elements in JSON arrays.
   * @return a JsonNode representing the delta between currentData and newData.
   */
  public static JsonNode buildDeltaJsonTree(
      Object currentData, Object newData, ArrayElementComparator arrayElementComparator) {
    JsonNode currentNode =
        currentData instanceof JsonNode ? (JsonNode) currentData : MAPPER.valueToTree(currentData);
    JsonNode newNode =
        newData instanceof JsonNode ? (JsonNode) newData : MAPPER.valueToTree(newData);
    return buildDeltaJsonTree(null, currentNode, newNode, arrayElementComparator);
  }

  /** Reconstructs the old or new JsonNode from a delta produced by {@link #buildDeltaJsonTree}. */
  private static JsonNode generateValue(JsonNode node, boolean oldValue) {
    if (node == null || node.isNull()) {
      return null;
    }
    DeltaType deltaType = DeltaNode.deltaNodeType(node);
    if (deltaType != DeltaType.UNKNOWN) {
      if (DeltaType.ADD == deltaType) {
        return oldValue ? null : node.get("$newValue");
      }
      if (DeltaType.DELETE == deltaType) {
        return oldValue ? node.get("$oldValue") : null;
      }
      if (DeltaType.REPLACE == deltaType) {
        return oldValue ? node.get("$oldValue") : node.get("$newValue");
      }
      return node;
    }
    if (node.isArray()) {
      ArrayNode result = MAPPER.createArrayNode();
      for (JsonNode element : node) {
        JsonNode value = generateValue(element, oldValue);
        if (value != null && !value.isNull()) {
          result.add(value);
        }
      }
      return result;
    }
    if (node.isObject()) {
      ObjectNode result = MAPPER.createObjectNode();
      for (Iterator<String> iter = node.fieldNames(); iter.hasNext(); ) {
        String fieldName = iter.next();
        JsonNode value = generateValue(node.get(fieldName), oldValue);
        if (value != null && !value.isNull()) {
          result.set(fieldName, value);
        }
      }
      return result;
    }
    return node;
  }

  // This method recursively builds a delta JSON tree representing the differences between the
  // current and new data. The comparator allows deeper check into the array elements. Without it,
  // the whole element is generated as 'REPLACE'.
  private static JsonNode buildDeltaJsonTree(
      String path,
      JsonNode currentData,
      JsonNode newData,
      ArrayElementComparator arrayElementComparator) {
    if (currentData == newData) {
      // Short circuit if they are the same reference.
      return newData;
    }
    if (newData == null || newData.isNull()) {
      return DeltaNode.createDeleteJsonNode(currentData);
    }
    if (currentData == null || currentData.isNull()) {
      return DeltaNode.createAddJsonNode(newData);
    }
    if (currentData.isArray()) {
      if (currentData.equals(newData)) {
        return newData;
      }
      if (!newData.isArray()) {
        return DeltaNode.createReplaceJsonNode(currentData, newData);
      }

      List<JsonNode> currentList = new ArrayList<>();
      for (JsonNode element : currentData) {
        currentList.add(element);
      }
      Collections.sort(currentList, (a, b) -> arrayElementComparator.compare(path, a, b));
      List<JsonNode> newList = new ArrayList<>();
      for (JsonNode element : newData) {
        newList.add(element);
      }
      Collections.sort(newList, (a, b) -> arrayElementComparator.compare(path, a, b));

      ArrayNode deltaArray = MAPPER.createArrayNode();
      boolean hasChanges = false;
      int i = 0;
      int j = 0;
      while (i < currentList.size() && j < newList.size()) {
        int cmp = arrayElementComparator.compare(path, currentList.get(i), newList.get(j));
        if (cmp < 0) {
          deltaArray.add(MAPPER.valueToTree(DeltaNode.createDeleteJsonNode(currentList.get(i))));
          hasChanges = true;
          i++;
        } else if (cmp > 0) {
          deltaArray.add(MAPPER.valueToTree(DeltaNode.createAddJsonNode(newList.get(j))));
          hasChanges = true;
          j++;
        } else {
          JsonNode oldElement = currentList.get(i);
          JsonNode newElement = newList.get(j);
          if (!oldElement.equals(newElement)) {
            JsonNode result =
                buildDeltaJsonTree(path, oldElement, newElement, arrayElementComparator);

            deltaArray.add(result);
            hasChanges = true;
          } else {
            deltaArray.add(newElement);
          }
          i++;
          j++;
        }
      }
      // Process the remaining elements in both.
      while (i < currentList.size()) {
        deltaArray.add(MAPPER.valueToTree(DeltaNode.createDeleteJsonNode(currentList.get(i))));
        hasChanges = true;
        i++;
      }
      while (j < newList.size()) {
        deltaArray.add(MAPPER.valueToTree(DeltaNode.createAddJsonNode(newList.get(j))));
        hasChanges = true;
        j++;
      }
      return hasChanges ? deltaArray : newData;
    }
    if (currentData.isObject()) {
      if (currentData.equals(newData)) {
        return newData;
      }
      ObjectNode deltaObject = MAPPER.createObjectNode();
      // Iterate the union of both sides' field names. Iterating only currentData would miss keys
      // present solely in newData (e.g. a newly added gflag or instanceTags entry), dropping them
      // from the delta so they could not be reconstructed by generateNewValue.
      Set<String> fieldNames = new HashSet<>();
      currentData.fieldNames().forEachRemaining(fieldNames::add);
      newData.fieldNames().forEachRemaining(fieldNames::add);
      for (String fieldName : fieldNames) {
        String nextPath = path == null ? fieldName : path + "." + fieldName;
        JsonNode currentFieldValue = currentData.get(fieldName);
        JsonNode newFieldValue = newData.get(fieldName);
        JsonNode result =
            buildDeltaJsonTree(nextPath, currentFieldValue, newFieldValue, arrayElementComparator);
        if (result != null) {
          deltaObject.set(fieldName, result);
        }
      }
      return deltaObject;
    }
    if (currentData.equals(newData)) {
      return newData;
    }
    return DeltaNode.createReplaceJsonNode(currentData, newData);
  }
}
