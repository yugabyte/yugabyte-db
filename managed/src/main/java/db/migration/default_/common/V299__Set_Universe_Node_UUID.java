// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

@Slf4j
public class V299__Set_Universe_Node_UUID extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    Connection connection = context.getConnection();
    PreparedStatement updateUniverseDetailsStmt =
        connection.prepareStatement(
            "UPDATE universe SET universe_details_json = ? WHERE universe_uuid = ?");
    ResultSet universeRs =
        connection
            .createStatement()
            .executeQuery("SELECT universe_uuid, universe_details_json FROM universe");
    while (universeRs.next()) {
      boolean fixNeeded = false;
      UUID universeUuid = UUID.fromString(universeRs.getString("universe_uuid"));
      String universeDetailsJson = universeRs.getString("universe_details_json");
      UniverseDefinitionTaskParams universeDetails;
      try {
        universeDetails =
            Json.mapper().readValue(universeDetailsJson, UniverseDefinitionTaskParams.class);
      } catch (JsonProcessingException e) {
        // Extra caution against invalid JSON. It must not happen.
        log.warn("Invalid universe details found for universe UUID " + universeUuid, e);
        continue;
      }
      for (Cluster cluster : universeDetails.clusters) {
        if (cluster.userIntent == null) {
          continue;
        }
        for (NodeDetails nodeDetails : universeDetails.getNodesInCluster(cluster.uuid)) {
          if (nodeDetails.nodeUuid != null || StringUtils.isBlank(nodeDetails.nodeName)) {
            // Node name can be null if the node is in ToBeAdded state. As node name and UUID are
            // set in transaction, both must be set together.
            continue;
          }
          if (cluster.userIntent.providerType == CloudType.onprem) {
            NodeInstance instance = NodeInstance.getByName(nodeDetails.nodeName);
            if (!instance.isInUse()) {
              log.warn(
                  "Onprem node {} is referenced in universe {}, but is not marked in-use",
                  nodeDetails.nodeName,
                  universeUuid);
              // This must not happen if the name is set.
              continue;
            }
            nodeDetails.nodeUuid = instance.getNodeUuid();
          } else {
            nodeDetails.nodeUuid = Util.generateNodeUUID(universeUuid, nodeDetails.nodeName);
          }
          fixNeeded = true;
        }
      }
      if (!fixNeeded) {
        continue;
      }
      log.info("Fixing node UUID for universe {}", universeUuid);
      JsonNode jsonNode = Json.toJson(universeDetails);
      updateUniverseDetailsStmt.setString(1, Json.stringify(jsonNode));
      updateUniverseDetailsStmt.setObject(2, universeUuid);
      updateUniverseDetailsStmt.executeUpdate();
    }
    updateUniverseDetailsStmt.close();
  }
}
