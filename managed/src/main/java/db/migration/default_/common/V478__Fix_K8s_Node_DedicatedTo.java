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
 * resetting dedicatedTo, so all pods of a universe carry the same value: MASTER, TSERVER, or null
 * when the cloned node never had one (V403 made Kubernetes clusters dedicated without setting it).
 * In a dedicated cluster getInstanceTypeForNode and evaluateDeviceInfoForNode resolve the server
 * type from dedicatedTo, reading null as TSERVER. Once V459 set masterInstanceType and
 * masterDeviceInfo, a pod with the wrong value gets the other server's instance type and device
 * info. Release branches without the Kubernetes skip in finalSanityCheckConfigure (2025.2) then
 * reject a tserver instance type edit in universe_configure.
 *
 * <p>A Kubernetes pod runs exactly one server, so dedicatedTo is set from isTserver, as POD_INFO
 * and PlacementInfoUtil.dedicateNodes do. Only dedicated clusters are touched: in a non-dedicated
 * one dedicatedTo is not used for these lookups, and MASTER on a master pod marks it for
 * replacement.
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
      JsonNode userIntent = cluster.path("userIntent");
      String providerType = userIntent.path("providerType").asText(null);
      String clusterUuid = cluster.path("uuid").asText(null);
      if (clusterUuid != null
          && Common.CloudType.kubernetes.name().equals(providerType)
          && userIntent.path("dedicatedNodes").asBoolean(false)) {
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
      if (isMaster == isTserver) {
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
