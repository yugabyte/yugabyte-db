// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.queries;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.SlowQueriesParams;
import com.yugabyte.yw.models.MetricConfig;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.api.Play;
import play.libs.Json;

import javax.inject.Singleton;
import java.sql.*;
import java.util.*;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

import static play.libs.Json.newObject;
import static play.libs.Json.toJson;

public class SlowQueryExecutor implements Callable<JsonNode> {
  public static final Logger LOG = LoggerFactory.getLogger(LiveQueryExecutor.class);

  private final ApiHelper apiHelper;
  // hostname can be either IP address or DNS
  private String hostName;
  private int port;
  private String query;

  private final String DEFAULT_DB_USER = "yugabyte";
  private final String DEFAULT_DB_PASSWORD = "yugabyte";

  public SlowQueryExecutor(String hostName, int port, String query) {
    this.hostName = hostName;
    this.port = port;
    this.query = query;
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
    String connectString =  String.format("jdbc:postgresql://%s:%d/%s",
      hostName, port, "postgres");
    try (Connection conn = DriverManager.getConnection(
      connectString, DEFAULT_DB_USER, DEFAULT_DB_PASSWORD)) {
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
