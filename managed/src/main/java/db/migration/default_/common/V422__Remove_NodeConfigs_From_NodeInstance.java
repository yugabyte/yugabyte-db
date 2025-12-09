// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

/** This migration removes nodeConfigs field from node_details_json from NodeInstance. */
public class V422__Remove_NodeConfigs_From_NodeInstance extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws Exception {
    Connection connection = context.getConnection();
    String selectStmt = "SELECT node_uuid, node_details_json FROM node_instance";
    ResultSet resultSet = connection.createStatement().executeQuery(selectStmt);
    while (resultSet.next()) {
      String nodeUuid = resultSet.getString("node_uuid");
      JsonNode nodeDetails = Json.parse(resultSet.getString("node_details_json"));
      boolean updated = processNodeDetails(nodeDetails);
      if (updated) {
        String newNodeDetailsJson = Json.stringify(nodeDetails);
        PreparedStatement statement =
            connection.prepareStatement(
                "UPDATE node_instance SET node_details_json = ? WHERE node_uuid = ?::uuid");
        statement.setString(1, newNodeDetailsJson);
        statement.setString(2, nodeUuid);
        statement.execute();
      }
    }
  }

  private boolean processNodeDetails(JsonNode nodeDetailsNode) {
    if (nodeDetailsNode == null || !nodeDetailsNode.isObject()) {
      return false;
    }
    ObjectNode nodeDetails = (ObjectNode) nodeDetailsNode;
    return nodeDetails.remove("nodeConfigs") != null;
  }
}
