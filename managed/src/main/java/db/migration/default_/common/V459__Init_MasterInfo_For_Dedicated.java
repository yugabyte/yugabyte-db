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

@Slf4j
public class V459__Init_MasterInfo_For_Dedicated extends BaseJavaMigration {
  private static final String DEFAULT_K8S_MASTER_DEVICE_INFO_STR =
      "{\"volumeSize\": 50, \"numVolumes\": 1}";

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

  static boolean processUniverse(JsonNode universeDetails, String universeUUID)
      throws SQLException {
    boolean shouldUpdate = false;
    if (universeDetails == null || universeDetails.isNull()) {
      return false;
    }
    JsonNode clusters = universeDetails.get("clusters");
    if (clusters == null || !clusters.isArray()) {
      return false;
    }
    for (JsonNode cluster : clusters) {
      JsonNode clusterType = cluster.get("clusterType");
      if (clusterType == null || !clusterType.asText().equals("PRIMARY")) {
        continue;
      }
      JsonNode userIntentNode = cluster.get("userIntent");
      if (userIntentNode == null
          || userIntentNode.isNull()
          || !(userIntentNode instanceof ObjectNode)) {
        log.warn("No userIntent type for {}", universeUUID);
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
      Common.CloudType providerType = Common.CloudType.valueOf(providerTypeJson.asText());
      JsonNode deviceInfo = userIntent.get("deviceInfo");
      if (deviceInfo == null || deviceInfo.isNull()) {
        log.warn("No device info for {}", universeUUID);
        return false;
      }
      JsonNode masterDeviceInfo = userIntent.get("masterDeviceInfo");
      if (masterDeviceInfo == null || masterDeviceInfo.isNull()) {
        if (providerType == Common.CloudType.kubernetes) {
          // Initializing with default values.
          masterDeviceInfo = Json.parse(DEFAULT_K8S_MASTER_DEVICE_INFO_STR);
        } else {
          // Since in our code we do fallback to deviceInfo,
          // masterDeviceInfo should be equal to deviceInfo in case of null.
          masterDeviceInfo = deviceInfo.deepCopy();
        }
        userIntent.set("masterDeviceInfo", masterDeviceInfo);
        shouldUpdate = true;
      }

      JsonNode masterInstanceType = userIntent.get("masterInstanceType");
      if (masterInstanceType == null || masterInstanceType.isNull()) {
        JsonNode instanceType = userIntent.get("instanceType");
        if (instanceType == null || instanceType.isNull()) {
          log.warn("No instance type for {}", universeUUID);
        } else {
          userIntent.set("masterInstanceType", instanceType);
          shouldUpdate = true;
        }
      }
    }
    return shouldUpdate;
  }
}
