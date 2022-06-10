// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.queries;

import static play.libs.Json.toJson;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.Callable;

import play.libs.Json;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SlowQueryExecutor implements Callable<JsonNode> {

  // hostname can be either IP address or DNS
  private final String hostName;
  private final int port;
  private final String query;
  private final Universe universe;
  private final String username;
  private final String password;

  public SlowQueryExecutor(
      String hostName,
      int port,
      Universe universe,
      String query,
      String username,
      String password) {
    this.hostName = hostName;
    this.port = port;
    this.universe = universe;
    this.query = query;
    this.username = username == null ? "yugabyte" : username;
    this.password = password == null ? "yugabyte" : password;
  }

  private List<Map<String, Object>> resultSetToMap(ResultSet result) throws SQLException {
    List<Map<String, Object>> rows = new ArrayList<>();
    ResultSetMetaData rsmd = result.getMetaData();
    int columnCount = rsmd.getColumnCount();

    while (result.next()) {
      // Represent a row in DB. Key: Column name, Value: Column value
      Map<String, Object> row = new HashMap<>();
      for (int i = 1; i <= columnCount; i++) {
        // Note that the index is 1-based
        String colName = rsmd.getColumnName(i);
        Object colVal = result.getObject(i);
        row.put(colName, colVal);
      }
      rows.add(row);
    }
    return rows;
  }

  @Override
  public JsonNode call() {
    ObjectNode response = Json.newObject();
    String connectString = String.format("jdbc:postgresql://%s:%d/%s", hostName, port, "postgres");
    Properties connInfo = new Properties();
    connInfo.put("user", this.username);
    connInfo.put("password", this.password);
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    if (primaryCluster.userIntent.enableClientToNodeEncrypt) {
      connInfo.put("ssl", "true");
      connInfo.put("sslmode", "require");
    }
    try (Connection conn = DriverManager.getConnection(connectString, connInfo)) {
      if (conn == null) {
        response.put("error", "Unable to connect to DB");
        response.put("type", "ysql");
      } else {
        PreparedStatement p = conn.prepareStatement(query);

        if (p.execute()) {
          ResultSet result = p.getResultSet();
          List<Map<String, Object>> rows = resultSetToMap(result);
          response.set("result", toJson(rows));
        }
      }
    } catch (Exception e) {
      log.error(e.getMessage(), e);
      response.put("error", e.getMessage());
      response.put("type", "ysql");
    }

    return response;
  }
}
