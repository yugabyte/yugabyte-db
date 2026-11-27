// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

/**
 * The V1 create and edit APIs used to accept userIntent.useYbdbInbuiltYbc on VM universes, though
 * inbuilt YBC exists only in the Kubernetes DB image. On a VM the flag blocks rollback to a version
 * without inbuilt YBC, and the only API that clears it is Kubernetes-only. It also makes backups
 * and restores skip the YBC upgrade and restore-overwrite validation. Those APIs now reject it, so
 * this clears it on the primary cluster, the only cluster it is read from.
 *
 * <p>A cluster whose provider type is kubernetes, unknown or missing is left alone: clearing the
 * flag on a Kubernetes universe would make YBA manage a YBC that ships in the DB image.
 */
@Slf4j
public class V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s extends BaseJavaMigration {
  private static final String FLAG = "useYbdbInbuiltYbc";

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
    if (universeDetails == null || !universeDetails.path("clusters").isArray()) {
      return false;
    }
    boolean updated = false;
    for (JsonNode cluster : universeDetails.get("clusters")) {
      JsonNode userIntent = cluster.path("userIntent");
      if ("PRIMARY".equals(cluster.path("clusterType").asText(null))
          && userIntent instanceof ObjectNode
          && userIntent.path(FLAG).asBoolean(false)
          && isKnownNonK8s(userIntent)) {
        log.info("Universe {}: clearing {} on non-Kubernetes cluster", universeUUID, FLAG);
        ((ObjectNode) userIntent).put(FLAG, false);
        updated = true;
      }
    }
    return updated;
  }

  // Mirrors UserIntent.getAllCloudTypes: multicloud clusters keep a type per provider spec.
  private static boolean isKnownNonK8s(JsonNode userIntent) {
    List<String> types = new ArrayList<>();
    JsonNode specs = userIntent.path("providerSpecifications");
    if (specs.isArray() && specs.size() > 0) {
      specs.forEach(spec -> types.add(spec.path("providerType").asText(null)));
    } else {
      types.add(userIntent.path("providerType").asText(null));
    }
    return types.stream()
        .allMatch(
            t ->
                t != null
                    && !CloudType.kubernetes.name().equals(t)
                    && !CloudType.unknown.name().equals(t));
  }
}
