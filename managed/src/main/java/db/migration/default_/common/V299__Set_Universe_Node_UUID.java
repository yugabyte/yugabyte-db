// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.Util;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

@Slf4j
public class V299__Set_Universe_Node_UUID extends BaseJavaMigration {

  private Map<String, UUID> getNodeInstanceNameToUuidMap(Connection connection)
      throws SQLException {
    Map<String, UUID> nodeInstances = new HashMap<>();
    ResultSet nodeInstanceRs =
        connection
            .createStatement()
            .executeQuery("SELECT node_uuid, node_name from node_instance where in_use = true");
    while (nodeInstanceRs.next()) {
      UUID nodeUuid = UUID.fromString(nodeInstanceRs.getString("node_uuid"));
      String nodeName = nodeInstanceRs.getString("node_name");
      if (StringUtils.isBlank(nodeName)) {
        log.warn("Node {} is being used but has no name", nodeUuid);
        continue;
      }
      nodeInstances.put(nodeName, nodeUuid);
    }
    return nodeInstances;
  }

  private Map<String, String> getClusterUuidToProviderTypeMap(JsonNode universeDetailsJson) {
    Map<String, String> clusterUuidToProviderTypeMap = new HashMap<>();
    JsonNode clusters = universeDetailsJson.get("clusters");
    Iterator<JsonNode> iter = clusters.iterator();
    while (iter.hasNext()) {
      JsonNode clusterNode = iter.next();
      String clusterUuidStr = clusterNode.get("uuid").asText();
      JsonNode userIntentNode = clusterNode.get("userIntent");
      if (userIntentNode == null || userIntentNode.isNull()) {
        log.warn("User intent is not found for cluster {}", clusterUuidStr);
        continue;
      }
      JsonNode providerTypeNode = userIntentNode.get("providerType");
      if (providerTypeNode == null || providerTypeNode.isNull()) {
        log.warn("Provider type is not set in the user intent for cluster {}", clusterUuidStr);
        continue;
      }

      clusterUuidToProviderTypeMap.put(clusterUuidStr, providerTypeNode.asText());
    }
    return clusterUuidToProviderTypeMap;
  }

  @Override
  public void migrate(Context context) throws SQLException {
    Connection connection = context.getConnection();
    Map<String, UUID> nodeInstanceNameToUuidMap = getNodeInstanceNameToUuidMap(connection);
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
      JsonNode node = Json.parse(universeDetailsJson);
      if (node == null || node.isNull()) {
        log.warn("Invalid universe details found for universe UUID {}", universeUuid);
        continue;
      }
      Map<String, String> clusterUuidToProviderTypeMap = getClusterUuidToProviderTypeMap(node);
      ObjectNode objectNode = node.deepCopy();
      JsonNode nodeDetailsSetNode = objectNode.get("nodeDetailsSet");
      if (nodeDetailsSetNode == null || nodeDetailsSetNode.isNull()) {
        log.warn("Invalid node details set for universe {}", universeUuid);
        continue;
      }
      Iterator<JsonNode> iter = nodeDetailsSetNode.elements();
      while (iter.hasNext()) {
        ObjectNode nodeDetails = (ObjectNode) iter.next();
        if (nodeDetails.has("nodeUuid") || !nodeDetails.has("nodeName")) {
          // Node which has not been added does not have a name. As node UUID and name are set in
          // transaction, both are either set or unset together.
          continue;
        }
        String nodeName = nodeDetails.get("nodeName").asText();
        JsonNode placementUuidNode = nodeDetails.get("placementUuid");
        if (placementUuidNode == null || placementUuidNode.isNull()) {
          log.warn("Placement UUID is not found for node {}", nodeName);
          continue;
        }
        String providerType = clusterUuidToProviderTypeMap.get(placementUuidNode.asText());
        if (StringUtils.isBlank(providerType)) {
          log.warn("Provider type is not found for node {}", nodeName);
          continue;
        }
        UUID nodeUuid =
            "onprem".equals(providerType)
                ? nodeInstanceNameToUuidMap.get(nodeName)
                : Util.generateNodeUUID(universeUuid, nodeName);
        if (nodeUuid == null) {
          log.warn("Node UUID is not found for node {}", nodeName);
          continue;
        }
        nodeDetails.set("nodeUuid", Json.toJson(nodeUuid));
        fixNeeded = true;
      }
      if (!fixNeeded) {
        continue;
      }
      log.info("Fixing node UUID for universe {}", universeUuid);
      updateUniverseDetailsStmt.setString(1, Json.stringify(objectNode));
      updateUniverseDetailsStmt.setObject(2, universeUuid);
      updateUniverseDetailsStmt.executeUpdate();
    }
    updateUniverseDetailsStmt.close();
  }
}
