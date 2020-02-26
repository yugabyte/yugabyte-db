package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Universe;

import javax.inject.Singleton;
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

import static play.libs.Json.*;

@Singleton
public class YsqlQueryExecutor {
  private final String DEFAULT_DB_USER = "yugabyte";
  private final String DEFAULT_DB_PASSWORD = "yugabyte";

  private String getQueryType(String queryString) {
    String[] queryParts = queryString.split(" ");
    String command = queryParts[0].toUpperCase();
    if (command.equals("TRUNCATE") || command.equals("DROP"))
      return  command + " " + queryParts[1].toUpperCase();
    return command;
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

  public JsonNode executeQuery(Universe universe, RunQueryFormData queryParams) {
    return executeQuery(universe, queryParams, DEFAULT_DB_USER, DEFAULT_DB_PASSWORD);
  }

  public JsonNode executeQuery(Universe universe, RunQueryFormData queryParams, String username,
                               String password) {
    ObjectNode response = newObject();

    // TODO: implement execute query for CQL
    String ysqlEndpoints = universe.getYSQLServerAddresses();
    String connectString =  String.format("jdbc:postgresql://%s/%s",
        ysqlEndpoints.split(",")[0], queryParams.db_name);
    try (Connection conn = DriverManager.getConnection(
        connectString, username, password)) {
      if (conn == null) {
        response.put("error", "Unable to connect to DB");
      } else {
        PreparedStatement p = conn.prepareStatement(queryParams.query);
        boolean hasResult = p.execute();
        if (hasResult) {
          ResultSet result = p.getResultSet();
          List<Map<String, Object>> rows = resultSetToMap(result);
          response.put("result", toJson(rows));
        } else {
          response.put("queryType", getQueryType(queryParams.query))
                  .put("count", p.getUpdateCount());
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
