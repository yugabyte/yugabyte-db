package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.forms.DatabaseSecurityFormData;
import com.yugabyte.yw.forms.DatabaseUserFormData;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;

import javax.inject.Singleton;
import java.sql.*;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static play.libs.Json.newObject;
import static play.libs.Json.toJson;

@Singleton
public class YsqlQueryExecutor {
  private static final Logger LOG = LoggerFactory.getLogger(YsqlQueryExecutor.class);
  private final static String DEFAULT_DB_USER = "yugabyte";
  private final static String DEFAULT_DB_PASSWORD = "yugabyte";

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

  public void createUser(Universe universe, DatabaseUserFormData data) {
    RunQueryFormData ysqlQuery = new RunQueryFormData();
    // Create user for customer YSQL.
    ysqlQuery.query = String.format("CREATE USER \"%s\" SUPERUSER INHERIT CREATEROLE " +
        "CREATEDB LOGIN REPLICATION BYPASSRLS PASSWORD '%s'",
      data.username, Util.escapeSingleQuotesOnly(data.password));
    ysqlQuery.db_name = data.dbName;
    JsonNode ysqlResponse = executeQuery(universe, ysqlQuery,
      data.ysqlAdminUsername,
      data.ysqlAdminPassword);
    LOG.info("Creating YSQL user, result: " + ysqlResponse.toString());
    if (ysqlResponse.has("error")) {
      throw new YWServiceException(Http.Status.BAD_REQUEST, ysqlResponse.get("error").asText());
    }
  }

  public void updateAdminPassword(Universe universe, DatabaseSecurityFormData data) {
    // Update admin user password YSQL.
    RunQueryFormData ysqlQuery = new RunQueryFormData();
    ysqlQuery.query = String.format("ALTER USER \"%s\" WITH PASSWORD '%s'",
      data.ysqlAdminUsername, Util.escapeSingleQuotesOnly(data.ysqlAdminPassword));
    ysqlQuery.db_name = data.dbName;
    JsonNode ysqlResponse = executeQuery(universe, ysqlQuery,
      data.ysqlAdminUsername, data.ysqlCurrAdminPassword);
    LOG.info("Updating YSQL user, result: " + ysqlResponse.toString());
    if (ysqlResponse.has("error")) {
      throw new YWServiceException(Http.Status.BAD_REQUEST, ysqlResponse.get("error").asText());
    }
  }

}
