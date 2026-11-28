// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.util.UUID;
import org.junit.Test;
import play.libs.Json;

public class V482__Clear_Default_Region_For_PartitionsTest {

  private static final String REGION = UUID.randomUUID().toString();

  @Test
  public void withPartitions_clearedEverywhere() {
    ObjectNode details = details(true);

    assertTrue(V482__Clear_Default_Region_For_Partitions.processUniverse(details, "u"));
    JsonNode cluster = details.get("clusters").get(0);
    assertFalse(cloud(cluster.get("placementInfo")).has("defaultRegion"));
    for (JsonNode partition : cluster.get("partitions")) {
      assertFalse(cloud(partition.get("placement")).has("defaultRegion"));
    }
  }

  @Test
  public void withoutPartitions_noUpdate() {
    ObjectNode details = details(false);
    ObjectNode before = details.deepCopy();

    assertFalse(V482__Clear_Default_Region_For_Partitions.processUniverse(details, "u"));
    assertEquals(before, details);
  }

  @Test
  public void alreadyCleared_noUpdate() {
    ObjectNode details = details(true);
    V482__Clear_Default_Region_For_Partitions.processUniverse(details, "u");
    ObjectNode before = details.deepCopy();

    assertFalse(V482__Clear_Default_Region_For_Partitions.processUniverse(details, "u"));
    assertEquals(before, details);
  }

  @Test
  public void onlyClusterWithPartitionsTouched() {
    ObjectNode details = details(true);
    ObjectNode readReplica = cluster(false);
    ((ArrayNode) details.get("clusters")).add(readReplica);

    assertTrue(V482__Clear_Default_Region_For_Partitions.processUniverse(details, "u"));
    assertEquals(REGION, cloud(readReplica.get("placementInfo")).get("defaultRegion").asText());
  }

  private static ObjectNode details(boolean withPartitions) {
    ObjectNode details = Json.newObject();
    details.putArray("clusters").add(cluster(withPartitions));
    return details;
  }

  private static ObjectNode cluster(boolean withPartitions) {
    ObjectNode cluster = Json.newObject();
    cluster.put("uuid", UUID.randomUUID().toString());
    cluster.set("placementInfo", placement());
    if (withPartitions) {
      ArrayNode partitions = cluster.putArray("partitions");
      for (int i = 0; i < 2; i++) {
        ObjectNode partition = partitions.addObject();
        partition.put("name", "p" + i);
        partition.put("defaultPartition", i == 0);
        partition.set("placement", placement());
      }
    }
    return cluster;
  }

  private static ObjectNode placement() {
    ObjectNode placement = Json.newObject();
    ObjectNode cloud = placement.putArray("cloudList").addObject();
    cloud.put("uuid", UUID.randomUUID().toString());
    cloud.put("defaultRegion", REGION);
    cloud.putArray("regionList");
    return placement;
  }

  private static JsonNode cloud(JsonNode placement) {
    return placement.get("cloudList").get(0);
  }
}
