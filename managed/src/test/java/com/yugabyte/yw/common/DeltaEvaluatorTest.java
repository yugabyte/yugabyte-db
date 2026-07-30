// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import org.junit.Test;
import play.libs.Json;

public class DeltaEvaluatorTest {

  @Test
  public void generateOldValueNullDelta() {
    assertNull(DeltaEvaluator.generateOldValue(null));
  }

  @Test
  public void generateNewValueNullDelta() {
    assertNull(DeltaEvaluator.generateNewValue(null));
  }

  @Test
  public void generateOldValueAddDelta() {
    JsonNode delta =
        Json.parse("{\"$deltaType\":\"ADD\",\"$oldValue\":null,\"$newValue\":\"added\"}");
    assertNull(DeltaEvaluator.generateOldValue(delta));
  }

  @Test
  public void generateNewValueAddDelta() {
    JsonNode delta =
        Json.parse("{\"$deltaType\":\"ADD\",\"$oldValue\":null,\"$newValue\":\"added\"}");
    assertEquals(Json.parse("\"added\""), DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void generateOldValueDeleteDelta() {
    JsonNode delta =
        Json.parse("{\"$deltaType\":\"DELETE\",\"$oldValue\":\"removed\",\"$newValue\":null}");
    assertEquals(Json.parse("\"removed\""), DeltaEvaluator.generateOldValue(delta));
  }

  @Test
  public void generateNewValueDeleteDelta() {
    JsonNode delta =
        Json.parse("{\"$deltaType\":\"DELETE\",\"$oldValue\":\"removed\",\"$newValue\":null}");
    assertNull(DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void generateOldValueReplaceDelta() {
    JsonNode delta =
        Json.parse("{\"$deltaType\":\"REPLACE\",\"$oldValue\":\"old\",\"$newValue\":\"new\"}");
    assertEquals(Json.parse("\"old\""), DeltaEvaluator.generateOldValue(delta));
  }

  @Test
  public void generateNewValueReplaceDelta() {
    JsonNode delta =
        Json.parse("{\"$deltaType\":\"REPLACE\",\"$oldValue\":\"old\",\"$newValue\":\"new\"}");
    assertEquals(Json.parse("\"new\""), DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void generateValueNestedObjectDelta() {
    JsonNode delta =
        Json.parse(
            "{\"name\":\"alice\",\"age\":{\"$deltaType\":\"REPLACE\",\"$oldValue\":30,"
                + "\"$newValue\":31}}");
    assertEquals(
        Json.parse("{\"name\":\"alice\",\"age\":30}"), DeltaEvaluator.generateOldValue(delta));
    assertEquals(
        Json.parse("{\"name\":\"alice\",\"age\":31}"), DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void buildDeltaJsonTreeSameReference() {
    JsonNode node = Json.parse("{\"x\":1}");
    JsonNode result = DeltaEvaluator.buildDeltaJsonTree(node, node);
    assertEquals(node, result);
  }

  @Test
  public void buildDeltaJsonTreeAddScalar() {
    JsonNode newValue = Json.parse("\"added\"");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(null, newValue);
    assertEquals("ADD", delta.get("$deltaType").asText());
    assertEquals("added", delta.get("$newValue").asText());
    assertTrue(delta.get("$oldValue") == null || delta.get("$oldValue").isNull());
  }

  @Test
  public void buildDeltaJsonTreeDeleteScalar() {
    JsonNode current = Json.parse("\"removed\"");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, null);
    assertEquals("DELETE", delta.get("$deltaType").asText());
    assertEquals("removed", delta.get("$oldValue").asText());
    assertTrue(delta.get("$newValue") == null || delta.get("$newValue").isNull());
  }

  @Test
  public void buildDeltaJsonTreeReplaceScalar() {
    JsonNode current = Json.parse("\"old\"");
    JsonNode newValue = Json.parse("\"new\"");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals("REPLACE", delta.get("$deltaType").asText());
    assertEquals("old", delta.get("$oldValue").asText());
    assertEquals("new", delta.get("$newValue").asText());
  }

  @Test
  public void buildDeltaJsonTreeEqualScalars() {
    JsonNode current = Json.parse("42");
    JsonNode newValue = Json.parse("42");
    JsonNode result = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertFalse(result.has("$deltaType"));
    assertEquals(42, result.asInt());
  }

  @Test
  public void buildDeltaJsonTreeEqualObjects() {
    JsonNode current = Json.parse("{\"a\":1,\"b\":2}");
    JsonNode newValue = Json.parse("{\"a\":1,\"b\":2}");
    JsonNode result = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals(newValue, result);
  }

  @Test
  public void buildDeltaJsonTreeObjectFieldReplace() {
    JsonNode current = Json.parse("{\"name\":\"alice\",\"age\":30}");
    JsonNode newValue = Json.parse("{\"name\":\"alice\",\"age\":31}");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    JsonNode ageDelta = delta.get("age");
    assertEquals("REPLACE", ageDelta.get("$deltaType").asText());
    assertEquals(30, ageDelta.get("$oldValue").asInt());
    assertEquals(31, ageDelta.get("$newValue").asInt());
    assertEquals("alice", delta.get("name").asText());
  }

  @Test
  public void buildDeltaJsonTreeArrayAddElement() {
    JsonNode current = Json.parse("[1,2]");
    JsonNode newValue = Json.parse("[1,2,3]");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals(3, delta.size());
    assertEquals(1, delta.get(0).asInt());
    assertEquals(2, delta.get(1).asInt());
    JsonNode added = delta.get(2);
    assertEquals("ADD", added.get("$deltaType").asText());
    assertEquals(3, added.get("$newValue").asInt());
  }

  @Test
  public void buildDeltaJsonTreeArrayDeleteElement() {
    JsonNode current = Json.parse("[1,2,3]");
    JsonNode newValue = Json.parse("[1,2]");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals(3, delta.size());
    assertEquals(1, delta.get(0).asInt());
    assertEquals(2, delta.get(1).asInt());
    JsonNode deleted = delta.get(2);
    assertEquals("DELETE", deleted.get("$deltaType").asText());
    assertEquals(3, deleted.get("$oldValue").asInt());
  }

  @Test
  public void buildDeltaJsonTreeArrayDeleteAddElement() {
    JsonNode current = Json.parse("[1,2]");
    JsonNode newValue = Json.parse("[1,5]");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals(3, delta.size());
    assertEquals(1, delta.get(0).asInt());
    JsonNode delete = delta.get(1);
    assertEquals("DELETE", delete.get("$deltaType").asText());
    assertEquals(2, delete.get("$oldValue").asInt());
    JsonNode add = delta.get(2);
    assertEquals("ADD", add.get("$deltaType").asText());
    assertEquals(5, add.get("$newValue").asInt());
  }

  // Back and forth conversion from the delta tree.
  @Test
  public void buildDeltaJsonTreeRoundTripObject() {
    JsonNode current = Json.parse("{\"name\":\"alice\",\"age\":30}");
    JsonNode newValue = Json.parse("{\"name\":\"alice\",\"age\":31}");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals(current, DeltaEvaluator.generateOldValue(delta));
    assertEquals(newValue, DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void buildDeltaJsonTreeObjectFieldAdd() {
    JsonNode current = Json.parse("{\"flags\":{\"existing\":\"a\"}}");
    JsonNode newValue = Json.parse("{\"flags\":{\"existing\":\"a\",\"newFlag\":\"b\"}}");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    JsonNode addedFlag = delta.get("flags").get("newFlag");
    assertEquals("ADD", addedFlag.get("$deltaType").asText());
    assertEquals("b", addedFlag.get("$newValue").asText());
    assertEquals(current, DeltaEvaluator.generateOldValue(delta));
    assertEquals(newValue, DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void buildDeltaJsonTreeNodeDetailsSetInPlaceChangeUsesReplace() {
    JsonNode current =
        Json.parse(
            "{\"nodeDetailsSet\":[{\"nodeIdx\":1,\"nodeName\":\"n1\",\"nodeUuid\":\"uuid-1\","
                + "\"state\":\"Live\"}]}");
    JsonNode newValue =
        Json.parse(
            "{\"nodeDetailsSet\":[{\"nodeIdx\":1,\"nodeName\":\"n1\",\"nodeUuid\":\"uuid-1\","
                + "\"state\":\"ToBeAdded\"}]}");
    JsonNode delta =
        DeltaEvaluator.buildDeltaJsonTree(current, newValue, new NodeDetailsArrayComparator());
    JsonNode stateDelta = delta.get("nodeDetailsSet").get(0).get("state");
    assertEquals("REPLACE", stateDelta.get("$deltaType").asText());
    assertEquals("Live", stateDelta.get("$oldValue").asText());
    assertEquals("ToBeAdded", stateDelta.get("$newValue").asText());
    assertEquals(current, DeltaEvaluator.generateOldValue(delta));
    assertEquals(newValue, DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void buildDeltaJsonTreeNodeDetailsSetAddNode() {
    JsonNode current =
        Json.parse(
            "{\"nodeDetailsSet\":[{\"nodeIdx\":1,\"nodeName\":\"n1\",\"nodeUuid\":\"uuid-1\","
                + "\"state\":\"Live\"}]}");
    JsonNode newValue =
        Json.parse(
            "{\"nodeDetailsSet\":["
                + "{\"nodeIdx\":1,\"nodeName\":\"n1\",\"nodeUuid\":\"uuid-1\",\"state\":\"Live\"},"
                + "{\"nodeIdx\":2,\"nodeName\":\"n2\",\"nodeUuid\":\"uuid-2\","
                + "\"state\":\"ToBeAdded\"}"
                + "]}");
    JsonNode delta =
        DeltaEvaluator.buildDeltaJsonTree(current, newValue, new NodeDetailsArrayComparator());
    JsonNode nodeDetailsDelta = delta.get("nodeDetailsSet");
    assertEquals(2, nodeDetailsDelta.size());
    assertEquals("Live", nodeDetailsDelta.get(0).get("state").asText());
    JsonNode addedNode = nodeDetailsDelta.get(1);
    assertEquals("ADD", addedNode.get("$deltaType").asText());
    assertEquals("n2", addedNode.get("$newValue").get("nodeName").asText());
  }

  @Test
  public void buildDeltaJsonTreeClustersInPlaceChangeUsesReplace() {
    JsonNode current =
        Json.parse("{\"clusters\":[{\"uuid\":\"c1\",\"userIntent\":{\"numNodes\":3}}]}");
    JsonNode newValue =
        Json.parse("{\"clusters\":[{\"uuid\":\"c1\",\"userIntent\":{\"numNodes\":4}}]}");
    JsonNode delta =
        DeltaEvaluator.buildDeltaJsonTree(current, newValue, new NodeDetailsArrayComparator());
    JsonNode numNodesDelta = delta.get("clusters").get(0).get("userIntent").get("numNodes");
    assertEquals("REPLACE", numNodesDelta.get("$deltaType").asText());
    assertEquals(3, numNodesDelta.get("$oldValue").asInt());
    assertEquals(4, numNodesDelta.get("$newValue").asInt());
    assertEquals(current, DeltaEvaluator.generateOldValue(delta));
    assertEquals(newValue, DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void buildDeltaJsonTreeClustersReorderWithInPlaceChangeUsesReplace() {
    JsonNode current =
        Json.parse(
            "{\"clusters\":["
                + "{\"uuid\":\"primary\",\"userIntent\":{\"numNodes\":3}},"
                + "{\"uuid\":\"async\",\"userIntent\":{\"numNodes\":3}}"
                + "]}");
    // Reordered clusters plus an in-place edit on primary.
    JsonNode newValue =
        Json.parse(
            "{\"clusters\":["
                + "{\"uuid\":\"async\",\"userIntent\":{\"numNodes\":3}},"
                + "{\"uuid\":\"primary\",\"userIntent\":{\"numNodes\":4}}"
                + "]}");
    JsonNode delta =
        DeltaEvaluator.buildDeltaJsonTree(current, newValue, new NodeDetailsArrayComparator());
    JsonNode clustersDelta = delta.get("clusters");
    assertEquals(2, clustersDelta.size());
    // Sorted by uuid: async then primary, nested REPLACE, not whole-cluster DELETE+ADD.
    assertEquals("async", clustersDelta.get(0).get("uuid").asText());
    JsonNode numNodesDelta = clustersDelta.get(1).get("userIntent").get("numNodes");
    assertEquals("REPLACE", numNodesDelta.get("$deltaType").asText());
    assertEquals(3, numNodesDelta.get("$oldValue").asInt());
    assertEquals(4, numNodesDelta.get("$newValue").asInt());
    // Reconstruction preserves content; array order follows uuid sort, not original order.
    JsonNode reconstructedOld = DeltaEvaluator.generateOldValue(delta);
    JsonNode reconstructedNew = DeltaEvaluator.generateNewValue(delta);
    assertEquals(3, clusterNumNodes(reconstructedOld, "primary"));
    assertEquals(4, clusterNumNodes(reconstructedNew, "primary"));
    assertEquals(3, clusterNumNodes(reconstructedOld, "async"));
    assertEquals(3, clusterNumNodes(reconstructedNew, "async"));
  }

  private static int clusterNumNodes(JsonNode root, String clusterUuid) {
    for (JsonNode cluster : root.get("clusters")) {
      if (clusterUuid.equals(cluster.get("uuid").asText())) {
        return cluster.get("userIntent").get("numNodes").asInt();
      }
    }
    throw new AssertionError("cluster not found: " + clusterUuid);
  }

  // ---------------------------------------------------------------------------
  // Type transitions
  // ---------------------------------------------------------------------------

  @Test
  public void buildDeltaJsonTreeScalarToObjectIsReplace() {
    JsonNode current = Json.parse("\"x\"");
    JsonNode newValue = Json.parse("{\"k\":1}");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals("REPLACE", delta.get("$deltaType").asText());
    assertEquals(current, DeltaEvaluator.generateOldValue(delta));
    assertEquals(newValue, DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void buildDeltaJsonTreeArrayReplacedByScalarIsReplace() {
    JsonNode current = Json.parse("[1,2]");
    JsonNode newValue = Json.parse("\"x\"");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals("REPLACE", delta.get("$deltaType").asText());
    assertEquals(current, DeltaEvaluator.generateOldValue(delta));
    assertEquals(newValue, DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void buildDeltaJsonTreeBooleanReplace() {
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(Json.parse("true"), Json.parse("false"));
    assertEquals("REPLACE", delta.get("$deltaType").asText());
    assertTrue(delta.get("$oldValue").asBoolean());
    assertFalse(delta.get("$newValue").asBoolean());
  }

  // ---------------------------------------------------------------------------
  // Null-valued fields inside objects
  // ---------------------------------------------------------------------------

  @Test
  public void buildDeltaJsonTreeNullFieldValueToNonNullIsAdd() {
    JsonNode current = Json.parse("{\"a\":null}");
    JsonNode newValue = Json.parse("{\"a\":1}");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    JsonNode aDelta = delta.get("a");
    assertEquals("ADD", aDelta.get("$deltaType").asText());
    assertEquals(1, aDelta.get("$newValue").asInt());
  }

  @Test
  public void buildDeltaJsonTreeNonNullFieldValueToNullIsDelete() {
    JsonNode current = Json.parse("{\"a\":1}");
    JsonNode newValue = Json.parse("{\"a\":null}");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    JsonNode aDelta = delta.get("a");
    assertEquals("DELETE", aDelta.get("$deltaType").asText());
    assertEquals(1, aDelta.get("$oldValue").asInt());
  }

  // ---------------------------------------------------------------------------
  // Empty containers and no-op cases
  // ---------------------------------------------------------------------------

  @Test
  public void buildDeltaJsonTreeEmptyObjects() {
    JsonNode current = Json.parse("{}");
    JsonNode newValue = Json.parse("{}");
    JsonNode result = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertFalse(result.has("$deltaType"));
    assertEquals(newValue, result);
  }

  @Test
  public void buildDeltaJsonTreeEmptyArrays() {
    JsonNode current = Json.parse("[]");
    JsonNode newValue = Json.parse("[]");
    JsonNode result = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals(newValue, result);
  }

  @Test
  public void buildDeltaJsonTreeArrayReorderProducesNoDelta() {
    // With the default hashCode comparator, both sides sort to the same order, so a pure
    // reorder of the same elements yields no delta.
    JsonNode current = Json.parse("[3,1,2]");
    JsonNode newValue = Json.parse("[1,2,3]");
    JsonNode result = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertFalse(result.isObject() && result.has("$deltaType"));
    assertEquals(newValue, result);
  }

  // ---------------------------------------------------------------------------
  // Arrays of objects with a custom comparator
  // ---------------------------------------------------------------------------

  @Test
  public void buildDeltaJsonTreeArrayOfObjectsNestedReplaceWithComparator() {
    JsonNode current = Json.parse("{\"items\":[{\"id\":1,\"v\":\"a\"},{\"id\":2,\"v\":\"b\"}]}");
    JsonNode newValue = Json.parse("{\"items\":[{\"id\":1,\"v\":\"a\"},{\"id\":2,\"v\":\"c\"}]}");
    DeltaEvaluator.ArrayElementComparator byId =
        (p, n1, n2) -> {
          if (p != null && p.endsWith("items")) {
            return Integer.compare(n1.get("id").asInt(), n2.get("id").asInt());
          }
          return Integer.compare(n1.hashCode(), n2.hashCode());
        };
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue, byId);
    JsonNode vDelta = delta.get("items").get(1).get("v");
    assertEquals("REPLACE", vDelta.get("$deltaType").asText());
    assertEquals("b", vDelta.get("$oldValue").asText());
    assertEquals("c", vDelta.get("$newValue").asText());
    assertEquals(current, DeltaEvaluator.generateOldValue(delta));
    assertEquals(newValue, DeltaEvaluator.generateNewValue(delta));
  }

  // ---------------------------------------------------------------------------
  // generateOnlyDelta
  // ---------------------------------------------------------------------------

  @Test
  public void generateOnlyDeltaNull() {
    assertNull(DeltaEvaluator.generateOnlyDelta(null));
  }

  @Test
  public void generateOnlyDeltaNoChangesReturnsNull() {
    JsonNode current = Json.parse("{\"a\":1,\"b\":{\"c\":2}}");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, current.deepCopy());
    assertNull(DeltaEvaluator.generateOnlyDelta(delta));
  }

  @Test
  public void generateOnlyDeltaPrunesUnchangedSiblings() {
    JsonNode current = Json.parse("{\"a\":1,\"b\":2}");
    JsonNode newValue = Json.parse("{\"a\":1,\"b\":3}");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    JsonNode only = DeltaEvaluator.generateOnlyDelta(delta);
    assertNotNull(only);
    assertFalse(only.has("a"));
    assertEquals("REPLACE", only.get("b").get("$deltaType").asText());
  }

  @Test
  public void generateOnlyDeltaNestedKeepsOnlyChangedPath() {
    JsonNode current = Json.parse("{\"outer\":{\"a\":1,\"b\":2}}");
    JsonNode newValue = Json.parse("{\"outer\":{\"a\":1,\"b\":3}}");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    JsonNode only = DeltaEvaluator.generateOnlyDelta(delta);
    assertNotNull(only);
    JsonNode outer = only.get("outer");
    assertFalse(outer.has("a"));
    assertEquals(3, outer.get("b").get("$newValue").asInt());
  }

  @Test
  public void generateOnlyDeltaArrayKeepsOnlyDeltaElements() {
    JsonNode current = Json.parse("[1,2]");
    JsonNode newValue = Json.parse("[1,2,3]");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    JsonNode only = DeltaEvaluator.generateOnlyDelta(delta);
    assertNotNull(only);
    assertEquals(1, only.size());
    assertEquals("ADD", only.get(0).get("$deltaType").asText());
    assertEquals(3, only.get(0).get("$newValue").asInt());
  }

  // ---------------------------------------------------------------------------
  // Round-trips (old == current, new == target)
  // ---------------------------------------------------------------------------

  @Test
  public void buildDeltaJsonTreeObjectFieldDeleteRoundTrip() {
    JsonNode current = Json.parse("{\"flags\":{\"a\":\"1\",\"b\":\"2\"}}");
    JsonNode newValue = Json.parse("{\"flags\":{\"a\":\"1\"}}");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals("DELETE", delta.get("flags").get("b").get("$deltaType").asText());
    assertEquals(current, DeltaEvaluator.generateOldValue(delta));
    assertEquals(newValue, DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void buildDeltaJsonTreeArrayDeleteElementRoundTrip() {
    JsonNode current = Json.parse("[1,2,3]");
    JsonNode newValue = Json.parse("[1,2]");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals(current, DeltaEvaluator.generateOldValue(delta));
    assertEquals(newValue, DeltaEvaluator.generateNewValue(delta));
  }

  @Test
  public void buildDeltaJsonTreeMultipleFieldChangesRoundTrip() {
    JsonNode current = Json.parse("{\"a\":1,\"b\":2,\"c\":3}");
    JsonNode newValue = Json.parse("{\"a\":1,\"b\":20,\"c\":3,\"d\":4}");
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue);
    assertEquals("REPLACE", delta.get("b").get("$deltaType").asText());
    assertEquals("ADD", delta.get("d").get("$deltaType").asText());
    assertEquals(current, DeltaEvaluator.generateOldValue(delta));
    assertEquals(newValue, DeltaEvaluator.generateNewValue(delta));
    JsonNode only = DeltaEvaluator.generateOnlyDelta(delta);
    assertFalse(only.has("a"));
    assertFalse(only.has("c"));
    assertTrue(only.has("b"));
    assertTrue(only.has("d"));
  }

  @Test
  public void buildDeltaJsonTreeComplexNestedRoundTrip() {
    JsonNode current =
        Json.parse("{\"num\":5,\"tags\":{\"x\":\"1\"}," + "\"nodes\":[{\"id\":1,\"s\":\"L\"}]}");
    JsonNode newValue =
        Json.parse(
            "{\"num\":6,\"tags\":{},"
                + "\"nodes\":[{\"id\":1,\"s\":\"L\"},{\"id\":2,\"s\":\"A\"}]}");
    DeltaEvaluator.ArrayElementComparator byId =
        (p, n1, n2) -> {
          if (p != null && p.endsWith("nodes")) {
            return Integer.compare(n1.get("id").asInt(), n2.get("id").asInt());
          }
          return Integer.compare(n1.hashCode(), n2.hashCode());
        };
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(current, newValue, byId);
    assertEquals(current, DeltaEvaluator.generateOldValue(delta));
    assertEquals(newValue, DeltaEvaluator.generateNewValue(delta));
  }
}
