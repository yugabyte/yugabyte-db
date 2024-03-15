// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

@Slf4j
public class V315__Sync_GFlags_From_SpecificGFlags extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    migrate(context.getConnection());
  }

  static void migrate(Connection connection) throws SQLException {
    PreparedStatement updateUniverseDetailsStmt =
        connection.prepareStatement(
            "UPDATE universe SET universe_details_json = ? WHERE universe_uuid = ?");
    ResultSet universeRs =
        connection
            .createStatement()
            .executeQuery("SELECT universe_uuid, universe_details_json FROM universe");
    while (universeRs.next()) {
      UUID universeUuid = UUID.fromString(universeRs.getString("universe_uuid"));
      String universeDetailsStr = universeRs.getString("universe_details_json");
      JsonNode universeDetailsJson = Json.parse(universeDetailsStr);
      if (universeDetailsJson == null || universeDetailsJson.isNull()) {
        log.warn("Invalid universe details found for universe UUID {}", universeUuid);
        continue;
      }
      JsonNode clusters = universeDetailsJson.get("clusters");
      Map<UniverseDefinitionTaskParams.ClusterType, JsonNode> clusterByType = new HashMap<>();
      for (JsonNode cluster : clusters) {
        UniverseDefinitionTaskParams.ClusterType clusterType =
            UniverseDefinitionTaskParams.ClusterType.valueOf(cluster.get("clusterType").asText());
        clusterByType.put(clusterType, cluster);
      }
      JsonNode primaryCluster = clusterByType.get(UniverseDefinitionTaskParams.ClusterType.PRIMARY);
      if (primaryCluster == null || primaryCluster.isEmpty()) {
        log.warn("No primary cluster for universe UUID {}", universeUuid);
        continue;
      }
      SpecificGFlags primarySpecificGFlags = null;
      if (primaryCluster.has("userIntent")) {
        JsonNode userIntent = primaryCluster.get("userIntent");
        Map<String, String> masterGFlags = getGFlags(userIntent, "masterGFlags");
        Map<String, String> tserverGFlags = getGFlags(userIntent, "tserverGFlags");
        primarySpecificGFlags = getSpecificGFlags(userIntent);
        if (primarySpecificGFlags != null && primarySpecificGFlags.getPerProcessFlags() != null) {
          updateUserIntentFromSpecificGFlags(userIntent, primarySpecificGFlags);
        } else {
          primarySpecificGFlags = SpecificGFlags.construct(masterGFlags, tserverGFlags);
          ((ObjectNode) userIntent).set("specificGFlags", Json.toJson(primarySpecificGFlags));
          updateUserIntentFromSpecificGFlags(userIntent, primarySpecificGFlags);
        }
      } else {
        continue;
      }
      JsonNode asyncCluster = clusterByType.get(UniverseDefinitionTaskParams.ClusterType.ASYNC);
      if (asyncCluster != null && !asyncCluster.isEmpty() && asyncCluster.has("userIntent")) {
        JsonNode userIntent = asyncCluster.get("userIntent");
        SpecificGFlags asyncSpecificGFlags = getSpecificGFlags(userIntent);
        if (asyncSpecificGFlags == null || asyncSpecificGFlags.isInheritFromPrimary()) {
          if (asyncSpecificGFlags == null) {
            asyncSpecificGFlags = SpecificGFlags.constructInherited();
            ((ObjectNode) userIntent).set("specificGFlags", Json.toJson(asyncSpecificGFlags));
          }
          updateUserIntentFromSpecificGFlags(userIntent, primarySpecificGFlags);
        } else {
          updateUserIntentFromSpecificGFlags(userIntent, asyncSpecificGFlags);
        }
      }
      log.info("Fixing gflags for universe {}", universeUuid);
      updateUniverseDetailsStmt.setString(1, Json.stringify(universeDetailsJson));
      updateUniverseDetailsStmt.setObject(2, universeUuid);
      updateUniverseDetailsStmt.executeUpdate();
    }
    updateUniverseDetailsStmt.close();
  }

  private static SpecificGFlags getSpecificGFlags(JsonNode userIntentJson) {
    if (userIntentJson.has("specificGFlags")) {
      JsonNode specificGFlagsJson = userIntentJson.get("specificGFlags");
      if (!specificGFlagsJson.isEmpty()) {
        return Json.fromJson(specificGFlagsJson, SpecificGFlags.class);
      }
    }
    return null;
  }

  private static Map<String, String> getGFlags(JsonNode userIntentJson, String key) {
    if (userIntentJson.has(key)) {
      return Json.fromJson(userIntentJson.get(key), Map.class);
    }
    return Collections.emptyMap();
  }

  private static void updateUserIntentFromSpecificGFlags(
      JsonNode userIntent, SpecificGFlags specificGFlags) {
    Map<String, String> newMasterGFlags =
        specificGFlags.getPerProcessFlags().value.get(UniverseTaskBase.ServerType.MASTER);
    Map<String, String> newTserverGFlags =
        specificGFlags.getPerProcessFlags().value.get(UniverseTaskBase.ServerType.TSERVER);
    ((ObjectNode) userIntent).set("masterGFlags", Json.toJson(newMasterGFlags));
    ((ObjectNode) userIntent).set("tserverGFlags", Json.toJson(newTserverGFlags));
  }
}
