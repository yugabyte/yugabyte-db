// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

/**
 * New UI is erroneously setting defaultRegion to the first region.
 * From now this defaultRegion is ignored in V2 API (so universes saved through new UI will
 * have no defaultRegion). A leftover value makes the universe look like an old-style
 * geo-partitioned one.
 * Clear it from the cluster placementInfo and from every partition placement
 * of each cluster that has partitions.
 */
@Slf4j
public class V482__Clear_Default_Region_For_Partitions extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    migrate(context.getConnection());
  }

  static void migrate(Connection connection) throws SQLException {
    try (Statement select = connection.createStatement();
        ResultSet resultSet =
            select.executeQuery("SELECT universe_uuid, universe_details_json FROM universe");
        PreparedStatement update =
            connection.prepareStatement(
                "UPDATE universe SET universe_details_json = ? WHERE universe_uuid = ?::uuid")) {
      while (resultSet.next()) {
        String universeUUID = resultSet.getString("universe_uuid");
        JsonNode universeDetailsJson = Json.parse(resultSet.getString("universe_details_json"));
        if (processUniverse(universeDetailsJson, universeUUID)) {
          update.setString(1, Json.stringify(universeDetailsJson));
          update.setString(2, universeUUID);
          update.execute();
        }
      }
    }
  }

  static boolean processUniverse(JsonNode universeDetails, String universeUUID) {
    if (universeDetails == null || !universeDetails.isObject()) {
      return false;
    }
    JsonNode clusters = universeDetails.get("clusters");
    if (clusters == null || !clusters.isArray()) {
      return false;
    }
    boolean updated = false;
    for (JsonNode cluster : clusters) {
      JsonNode partitions = cluster.get("partitions");
      if (partitions == null || !partitions.isArray() || partitions.isEmpty()) {
        continue;
      }
      boolean clusterUpdated = clearDefaultRegion(cluster.get("placementInfo"));
      for (JsonNode partition : partitions) {
        clusterUpdated |= clearDefaultRegion(partition.get("placement"));
      }
      if (clusterUpdated) {
        log.info(
            "Universe {}: cleared default region for cluster {}",
            universeUUID,
            cluster.path("uuid").asText());
        updated = true;
      }
    }
    return updated;
  }

  private static boolean clearDefaultRegion(JsonNode placement) {
    if (placement == null || !placement.path("cloudList").isArray()) {
      return false;
    }
    boolean updated = false;
    for (JsonNode cloud : placement.get("cloudList")) {
      if (cloud instanceof ObjectNode && cloud.hasNonNull("defaultRegion")) {
        ((ObjectNode) cloud).remove("defaultRegion");
        updated = true;
      }
    }
    return updated;
  }
}
