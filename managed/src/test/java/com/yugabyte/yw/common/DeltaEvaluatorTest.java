// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
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
}
