package com.yugabyte.yw.models;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

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
    String sourceTableID = "sourceTableID";
    String targetTableID = "targetTableID";

    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(source, sourceTableID, target, targetTableID, false);

    assertNotNull(relationship.uuid);
    assertEquals(source, relationship.sourceUniverse);
    assertEquals(sourceTableID, relationship.sourceTableID);
    assertEquals(target, relationship.targetUniverse);
    assertEquals(targetTableID, relationship.targetTableID);
    assertFalse(relationship.active);
  }

  @Test
  public void testGetByUUID() {
    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, "sourceTableID", target, "targetTableID", false);

    AsyncReplicationRelationship queryResult = AsyncReplicationRelationship.get(relationship.uuid);
    assertEquals(relationship, queryResult);
  }

  @Test
  public void testGetByProperties() {
    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, "sourceTableID", target, "targetTableID", false);

    AsyncReplicationRelationship queryResult =
        AsyncReplicationRelationship.get(
            relationship.sourceUniverse.universeUUID, relationship.sourceTableID,
            relationship.targetUniverse.universeUUID, relationship.targetTableID);

    assertEquals(relationship, queryResult);
  }

  @Test
  public void testGetBetweenUniverses() {
    AsyncReplicationRelationship relationship1 =
        AsyncReplicationRelationship.create(
            source, "sourceTableID1", target, "targetTableID1", false);

    AsyncReplicationRelationship relationship2 =
        AsyncReplicationRelationship.create(
            source, "sourceTableID2", target, "targetTableID2", false);

    List<AsyncReplicationRelationship> queryResult =
        AsyncReplicationRelationship.getBetweenUniverses(source.universeUUID, target.universeUUID);
    queryResult.sort(Comparator.comparing(r -> r.uuid));

    List<AsyncReplicationRelationship> expectedResult = Arrays.asList(relationship1, relationship2);
    expectedResult.sort(Comparator.comparing(r -> r.uuid));

    assertArrayEquals(expectedResult.toArray(), queryResult.toArray());
  }

  @Test
  public void testGetBySourceUniverseUUID() {
    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, "sourceTableID", target, "targetTableID", false);

    List<AsyncReplicationRelationship> queryResult =
        AsyncReplicationRelationship.getBySourceUniverseUUID(source.universeUUID);

    assertEquals(1, queryResult.size());
    assertEquals(relationship, queryResult.get(0));
  }

  @Test
  public void testGetByTargetUniverseUUID() {
    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, "sourceTableID", target, "targetTableID", false);

    List<AsyncReplicationRelationship> queryResult =
        AsyncReplicationRelationship.getByTargetUniverseUUID(target.universeUUID);

    assertEquals(1, queryResult.size());
    assertEquals(relationship, queryResult.get(0));
  }

  @Test
  public void testDeleteExistingRelationship() {
    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, "sourceTableID", target, "targetTableID", false);

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
            source, "sourceTableID", target, "targetTableID", false);

    assertFalse(relationship.active);
    relationship.update(true);
    assertTrue(relationship.active);
  }

  @Test
  public void testToJson() {
    String sourceTableID = "sourceTableID";
    String targetTableID = "targetTableID";

    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(source, sourceTableID, target, targetTableID, false);

    JsonNode jsonNode = relationship.toJson();

    assertEquals(relationship.uuid.toString(), jsonNode.get("uuid").asText());
    assertEquals(
        relationship.sourceUniverse.universeUUID.toString(),
        jsonNode.get("sourceUniverseUUID").asText());
    assertEquals(relationship.sourceTableID, jsonNode.get("sourceTableID").asText());
    assertEquals(
        relationship.targetUniverse.universeUUID.toString(),
        jsonNode.get("targetUniverseUUID").asText());
    assertEquals(relationship.targetTableID, jsonNode.get("targetTableID").asText());
    assertEquals(relationship.active, jsonNode.get("active").asBoolean());
  }
}
