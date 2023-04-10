// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.migrations.V208;
import com.yugabyte.yw.models.migrations.V208.UniverseDefinitionTaskParams;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

public class V208__Universe_Details_Fill_Storage_Type extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    Connection connection = context.getConnection();
    String selectStmt = "SELECT universe_uuid, universe_details_json FROM universe";
    ResultSet resultSet = connection.createStatement().executeQuery(selectStmt);

    while (resultSet.next()) {
      String univUuid = resultSet.getString("universe_uuid");
      JsonNode univDetails = Json.parse(resultSet.getString("universe_details_json"));
      UniverseDefinitionTaskParams universeDefinition =
          Json.fromJson(univDetails, UniverseDefinitionTaskParams.class);
      boolean updated = universeDefinition.clusters.stream().anyMatch(this::processCluster);
      if (updated) {
        String newUnivDetails = Json.stringify(Json.toJson(universeDefinition));
        PreparedStatement statement =
            connection.prepareStatement(
                "UPDATE universe SET universe_details_json = ? WHERE universe_uuid = ?::uuid");
        statement.setString(1, newUnivDetails);
        statement.setString(2, univUuid);
        statement.execute();
      }
    }
  }

  private boolean processCluster(V208.Cluster cluster) {
    if (cluster.userIntent == null
        || cluster.userIntent.deviceInfo == null
        || cluster.userIntent.deviceInfo.storageType != null) {
      return false;
    }
    if (cluster.placementInfo == null || cluster.placementInfo.cloudList == null) {
      return false;
    }
    cluster.userIntent.deviceInfo.storageType =
        cluster.placementInfo.cloudList.get(0).code.equals(Common.CloudType.aws.toString())
            ? StorageType.GP2
            : StorageType.Scratch;
    return true;
  }
}
