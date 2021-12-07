// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.queries;

import static play.libs.Json.toJson;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;
import play.libs.Json;

public class SlowQueryExecutor implements Callable<JsonNode> {
  public static final Logger LOG = LoggerFactory.getLogger(LiveQueryExecutor.class);

  private final ApiHelper apiHelper;
  // hostname can be either IP address or DNS
  private String hostName;
  private int port;
  private String query;
  private Universe universe;
  private String username;
  private String password;

  private final String DEFAULT_DB_USER = "yugabyte";
  private final String DEFAULT_DB_PASSWORD = "yugabyte";

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
    this.username = username == null ? DEFAULT_DB_USER : username;
    this.password = password == null ? DEFAULT_DB_PASSWORD : password;
    this.apiHelper = Play.current().injector().instanceOf(ApiHelper.class);
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
    connInfo.put("user", this.username == null ? DEFAULT_DB_USER : this.username);
    connInfo.put("password", this.password == null ? DEFAULT_DB_PASSWORD : this.password);
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    if (primaryCluster.userIntent.enableClientToNodeEncrypt) {
      connInfo.put("ssl", "true");
      connInfo.put("sslmode", "require");
    }
    try (Connection conn = DriverManager.getConnection(connectString, connInfo)) {
      if (conn == null) {
        response.put("error", "Unable to connect to DB");
      } else {
        PreparedStatement p = conn.prepareStatement(query);
        boolean hasResult = p.execute();
        if (hasResult) {
          ResultSet result = p.getResultSet();
          List<Map<String, Object>> rows = resultSetToMap(result);
          response.put("result", toJson(rows));
        }
      }
    } catch (SQLException e) {
      response.put("error", e.getMessage());
    } catch (Exception e) {
      response.put("error", e.getMessage());
    }

    return response;
  }
}
