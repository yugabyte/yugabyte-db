package com.yugabyte.yw.models;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import junitparams.JUnitParamsRunner;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.UUID;

import static org.junit.Assert.*;

@RunWith(JUnitParamsRunner.class)
public class AsyncReplicationRelationshipTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private Universe source, target;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    source = ModelFactory.createUniverse("source", defaultCustomer.getCustomerId());
    target = ModelFactory.createUniverse("target", defaultCustomer.getCustomerId());
  }

  @After
  public void tearDown() {
    source.delete();
    target.delete();
    defaultCustomer.delete();
  }

  @Test
  public void testCreate() {
    UUID sourceTableUUID = UUID.randomUUID();
    UUID targetTableUUID = UUID.randomUUID();

    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, sourceTableUUID, target, targetTableUUID, false);

    assertNotNull(relationship.uuid);
    assertEquals(source, relationship.sourceUniverse);
    assertEquals(sourceTableUUID, relationship.sourceTableUUID);
    assertEquals(target, relationship.targetUniverse);
    assertEquals(targetTableUUID, relationship.targetTableUUID);
    assertFalse(relationship.active);
  }

  @Test
  public void testGetByUUID() {
    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, UUID.randomUUID(), target, UUID.randomUUID(), false);

    AsyncReplicationRelationship queryResult = AsyncReplicationRelationship.get(relationship.uuid);
    assertEquals(relationship, queryResult);
  }

  @Test
  public void testGetByProperties() {
    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, UUID.randomUUID(), target, UUID.randomUUID(), false);

    AsyncReplicationRelationship queryResult =
        AsyncReplicationRelationship.get(
            relationship.sourceUniverse.universeUUID, relationship.sourceTableUUID,
            relationship.targetUniverse.universeUUID, relationship.targetTableUUID);

    assertEquals(relationship, queryResult);
  }

  @Test
  public void testDeleteExistingRelationship() {
    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, UUID.randomUUID(), target, UUID.randomUUID(), false);

    assertTrue(AsyncReplicationRelationship.delete(relationship.uuid));
    assertNull(AsyncReplicationRelationship.get(relationship.uuid));
  }

  @Test
  public void testDeleteNonExistentRelationship() {
    assertFalse(AsyncReplicationRelationship.delete(UUID.randomUUID()));
  }

  @Test
  public void testUpdate() {
    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, UUID.randomUUID(), target, UUID.randomUUID(), false);

    assertFalse(relationship.active);
    relationship.update(true);
    assertTrue(relationship.active);
  }

  @Test
  public void testToJson() {
    UUID sourceTableUUID = UUID.randomUUID();
    UUID targetTableUUID = UUID.randomUUID();

    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, sourceTableUUID, target, targetTableUUID, false);

    JsonNode jsonNode = relationship.toJson();

    assertEquals(relationship.uuid.toString(), jsonNode.get("uuid").asText());
    assertEquals(
        relationship.sourceUniverse.universeUUID.toString(),
        jsonNode.get("sourceUniverseUUID").asText());
    assertEquals(relationship.sourceTableUUID.toString(), jsonNode.get("sourceTableUUID").asText());
    assertEquals(
        relationship.targetUniverse.universeUUID.toString(),
        jsonNode.get("targetUniverseUUID").asText());
    assertEquals(relationship.targetTableUUID.toString(), jsonNode.get("targetTableUUID").asText());
    assertEquals(relationship.active, jsonNode.get("active").asBoolean());
  }
}
