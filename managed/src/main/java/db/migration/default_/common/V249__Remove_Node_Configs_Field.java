// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.UUID;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

// Some enum constants are modified because of typos.
// This just deletes the nodeConfigs field it is not used.
public class V249__Remove_Node_Configs_Field extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    Connection connection = context.getConnection();
    ResultSet nodeDetailsRs =
        connection
            .createStatement()
            .executeQuery("SELECT node_uuid, node_details_json FROM node_instance");
    PreparedStatement updateNodeInstanceStmt =
        connection.prepareStatement(
            "UPDATE node_instance SET node_details_json = ? WHERE node_uuid = ?");
    while (nodeDetailsRs.next()) {
      String nodeUuid = nodeDetailsRs.getString("node_uuid");
      String nodeDetailsJson = nodeDetailsRs.getString("node_details_json");
      if (nodeDetailsJson != null) {
        JsonNode jsonNode = Json.parse(nodeDetailsJson);
        if (jsonNode != null && jsonNode.isObject()) {
          ObjectNode objectNode = (ObjectNode) jsonNode.deepCopy();
          // Just remove as it is not used.
          // It can be populated again when it is needed.
          objectNode.remove("nodeConfigs");
          updateNodeInstanceStmt.setString(1, Json.stringify(objectNode));
          updateNodeInstanceStmt.setObject(2, UUID.fromString(nodeUuid));
          updateNodeInstanceStmt.executeUpdate();
        }
      }
    }
    updateNodeInstanceStmt.close();
  }
}
