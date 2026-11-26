// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

/**
 * V459 backfilled a null masterDeviceInfo on dedicated Kubernetes universes with the operator
 * default (1 x 50Gi). Helm had been rendering that null as deviceInfo, which is the volume on the
 * master StatefulSet. The next helm upgrade then tries to change volumeClaimTemplates and
 * Kubernetes rejects it.
 *
 * <p>Only the exact object V459 inserts is rewritten. A master disk that was actually created at 1
 * x 50Gi is persisted through DeviceInfo and includes storageClass (empty string at minimum), so it
 * is not this object.
 */
@Slf4j
public class V469__Fix_K8s_MasterDeviceInfo_Default extends BaseJavaMigration {
  private static final int V459_DEFAULT_VOLUME_SIZE = 50;
  private static final int V459_DEFAULT_NUM_VOLUMES = 1;

  @Override
  public void migrate(Context context) throws SQLException {
    migrate(context.getConnection());
  }

  static void migrate(Connection connection) throws SQLException {
    String selectStmt = "SELECT universe_uuid, universe_details_json FROM universe";
    ResultSet resultSet = connection.createStatement().executeQuery(selectStmt);

    while (resultSet.next()) {
      String universeUUID = resultSet.getString("universe_uuid");
      JsonNode universeDetailsJson = Json.parse(resultSet.getString("universe_details_json"));
      boolean updated = processUniverse(universeDetailsJson, universeUUID);
      if (updated) {
        String newUniverseDetails = Json.stringify(universeDetailsJson);
        PreparedStatement statement =
            connection.prepareStatement(
                "UPDATE universe SET universe_details_json = ? WHERE universe_uuid = ?::uuid");
        statement.setString(1, newUniverseDetails);
        statement.setString(2, universeUUID);
        statement.execute();
      }
    }
  }

  static boolean processUniverse(JsonNode universeDetails, String universeUUID) {
    if (universeDetails == null || universeDetails.isNull()) {
      return false;
    }
    JsonNode clusters = universeDetails.get("clusters");
    if (clusters == null || !clusters.isArray()) {
      return false;
    }
    boolean shouldUpdate = false;
    for (JsonNode cluster : clusters) {
      JsonNode clusterType = cluster.get("clusterType");
      if (clusterType == null || !clusterType.asText().equals("PRIMARY")) {
        continue;
      }
      JsonNode userIntentNode = cluster.get("userIntent");
      if (userIntentNode == null
          || userIntentNode.isNull()
          || !(userIntentNode instanceof ObjectNode)) {
        log.warn("No userIntent for {}", universeUUID);
        return false;
      }
      ObjectNode userIntent = (ObjectNode) userIntentNode;
      JsonNode dedicated = userIntent.get("dedicatedNodes");
      if (dedicated == null || dedicated.isNull() || !dedicated.asBoolean()) {
        return false;
      }
      JsonNode providerTypeJson = userIntent.get("providerType");
      if (providerTypeJson == null || providerTypeJson.isNull()) {
        log.warn("No provider type for {}", universeUUID);
        return false;
      }
      if (Common.CloudType.valueOf(providerTypeJson.asText()) != Common.CloudType.kubernetes) {
        return false;
      }
      JsonNode masterDeviceInfo = userIntent.get("masterDeviceInfo");
      if (!isV459DefaultMasterDeviceInfo(masterDeviceInfo)) {
        return false;
      }
      JsonNode deviceInfo = userIntent.get("deviceInfo");
      if (deviceInfo == null || deviceInfo.isNull() || !deviceInfo.isObject()) {
        log.warn("No device info for {}", universeUUID);
        return false;
      }
      userIntent.set("masterDeviceInfo", deviceInfo.deepCopy());
      shouldUpdate = true;
    }
    return shouldUpdate;
  }

  /**
   * V459 inserts {@code {"volumeSize": 50, "numVolumes": 1}} and nothing else. A DeviceInfo saved
   * by the application also carries {@code storageClass}.
   */
  static boolean isV459DefaultMasterDeviceInfo(JsonNode masterDeviceInfo) {
    if (masterDeviceInfo == null || !masterDeviceInfo.isObject() || masterDeviceInfo.size() != 2) {
      return false;
    }
    return isInt(masterDeviceInfo.get("volumeSize"), V459_DEFAULT_VOLUME_SIZE)
        && isInt(masterDeviceInfo.get("numVolumes"), V459_DEFAULT_NUM_VOLUMES);
  }

  private static boolean isInt(JsonNode node, int expected) {
    return node != null && node.isIntegralNumber() && node.intValue() == expected;
  }
}
