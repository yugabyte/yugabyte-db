// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashSet;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

/**
 * Before 2025.1, POD_INFO built every Kubernetes node by cloning one node of the cluster without
 * resetting dedicatedTo, so all pods of a dedicated universe carry the same value: MASTER or
 * TSERVER, whichever node the HashSet returned first. getInstanceTypeForNode and
 * getDeviceInfoForNode key off dedicatedTo. Once V459 set masterInstanceType, editing the tserver
 * instance type compares tserver pods against the master type, so they are not replaced and
 * universe_configure fails with an instance type mismatch.
 *
 * <p>A Kubernetes pod runs exactly one server, so dedicatedTo is derived from isTserver, as
 * PlacementInfoUtil.dedicateNodes does. A null dedicatedTo is left alone: non-dedicated universes
 * created before 2025.1 have it, and it is read as TSERVER.
 */
@Slf4j
public class V478__Fix_K8s_Node_DedicatedTo extends BaseJavaMigration {
  private static final String MASTER = "MASTER";
  private static final String TSERVER = "TSERVER";

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
    JsonNode nodes = universeDetails.get("nodeDetailsSet");
    if (clusters == null || !clusters.isArray() || nodes == null || !nodes.isArray()) {
      return false;
    }
    Set<String> k8sClusterUuids = new HashSet<>();
    for (JsonNode cluster : clusters) {
      String providerType = cluster.path("userIntent").path("providerType").asText(null);
      String clusterUuid = cluster.path("uuid").asText(null);
      if (clusterUuid != null && Common.CloudType.kubernetes.name().equals(providerType)) {
        k8sClusterUuids.add(clusterUuid);
      }
    }
    if (k8sClusterUuids.isEmpty()) {
      return false;
    }
    boolean updated = false;
    for (JsonNode node : nodes) {
      if (!(node instanceof ObjectNode)
          || !k8sClusterUuids.contains(node.path("placementUuid").asText(null))) {
        continue;
      }
      String dedicatedTo = node.path("dedicatedTo").asText(null);
      boolean isMaster = node.path("isMaster").asBoolean(false);
      boolean isTserver = node.path("isTserver").asBoolean(true);
      if (dedicatedTo == null || isMaster == isTserver) {
        continue;
      }
      String expected = isTserver ? TSERVER : MASTER;
      if (!expected.equals(dedicatedTo)) {
        log.info(
            "Universe {}: changing dedicatedTo of node {} from {} to {}",
            universeUUID,
            node.path("nodeName").asText(),
            dedicatedTo,
            expected);
        ((ObjectNode) node).put("dedicatedTo", expected);
        updated = true;
      }
    }
    return updated;
  }
}
