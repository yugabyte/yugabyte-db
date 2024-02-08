// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.Common;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Iterator;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

@Slf4j
public class V208__Universe_Details_Fill_Storage_Type extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    Connection connection = context.getConnection();
    String selectStmt = "SELECT universe_uuid, universe_details_json FROM universe";
    ResultSet resultSet = connection.createStatement().executeQuery(selectStmt);

    while (resultSet.next()) {
      String univUuid = resultSet.getString("universe_uuid");
      JsonNode univDetails = Json.parse(resultSet.getString("universe_details_json"));
      boolean updated = processUniverse(univDetails, univUuid);
      if (updated) {
        String newUnivDetails = Json.stringify(univDetails);
        PreparedStatement statement =
            connection.prepareStatement(
                "UPDATE universe SET universe_details_json = ? WHERE universe_uuid = ?::uuid");
        statement.setString(1, newUnivDetails);
        statement.setString(2, univUuid);
        statement.execute();
      }
    }
  }

  private boolean processUniverse(JsonNode universeDetailsJson, String univUuid) {
    JsonNode clusters = universeDetailsJson.get("clusters");
    Iterator<JsonNode> iter = clusters.iterator();
    boolean updated = false;
    while (iter.hasNext()) {
      JsonNode clusterNode = iter.next();

      if (!clusterNode.has("userIntent")) {
        continue;
      }
      JsonNode userIntentNode = clusterNode.get("userIntent");

      if (!userIntentNode.has("deviceInfo")) {
        continue;
      }
      JsonNode deviceInfoNode = userIntentNode.get("deviceInfo");

      if (deviceInfoNode.has("storageType") && deviceInfoNode.get("storageType").asText() != null) {
        continue;
      }

      if (!clusterNode.has("placementInfo")) {
        continue;
      }
      JsonNode placementInfoNode = clusterNode.get("placementInfo");

      if (!placementInfoNode.has("cloudList")) {
        continue;
      }
      JsonNode cloudListNode = placementInfoNode.get("cloudList");

      ArrayNode cloudList = (ArrayNode) cloudListNode;
      JsonNode cloudData = cloudList.get(0);
      String providerCode = cloudData.get("code").asText();

      if (!providerCode.equals(Common.CloudType.aws.toString())
          && !providerCode.equals(Common.CloudType.gcp.toString())) {
        continue;
      }
      StorageType storageType =
          providerCode.equals(Common.CloudType.aws.toString())
              ? StorageType.GP2
              : StorageType.Scratch;
      ObjectNode deviceInfo = (ObjectNode) deviceInfoNode;
      deviceInfo.put("storageType", Json.toJson(storageType.name()));
      updated = true;
      log.warn("setting storagetype to {} for universe {}", storageType.name(), univUuid);
    }
    return updated;
  }
}
