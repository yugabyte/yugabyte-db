// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.libs.Json.newObject;
import static play.libs.Json.toJson;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.DatabaseSecurityFormData;
import com.yugabyte.yw.forms.DatabaseUserDropFormData;
import com.yugabyte.yw.forms.DatabaseUserFormData;
import com.yugabyte.yw.forms.DatabaseUserFormData.RoleAttribute;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;

@Singleton
public class YsqlQueryExecutor {
  private static final Logger LOG = LoggerFactory.getLogger(YsqlQueryExecutor.class);
  private static final String DB_ADMIN_ROLE_NAME = Util.DEFAULT_YSQL_ADMIN_ROLE_NAME;
  private static final String PRECREATED_DB_ADMIN = "yb_db_admin";

  // This is the list of users that are created by default by the database.
  // Therefore, we don't want to allow YBA to delete them.
  private static final ImmutableSet<String> NON_DELETABLE_USERS =
      ImmutableSet.of(
          "postgres",
          "pg_monitor",
          "pg_read_all_settings",
          "pg_read_all_stats",
          "pg_stat_scan_tables",
          "pg_signal_backend",
          "pg_read_server_files",
          "pg_write_server_files",
          "pg_execute_server_program",
          "yb_extension",
          "yb_fdw",
          "yb_db_admin",
          "yugabyte",
          "yb_superuser");

  private static final String DEL_PG_ROLES_CMD_1 =
      "SET YB_NON_DDL_TXN_FOR_SYS_TABLES_ALLOWED=ON; "
          + "DELETE FROM pg_shdepend WHERE refclassid IN "
          + "(SELECT oid FROM pg_class WHERE relname='pg_authid')"
          + " AND refobjid IN (SELECT oid FROM pg_roles WHERE rolname IN "
          + "('pg_execute_server_program', 'pg_read_server_files', "
          + "'pg_write_server_files'));";
  private static final String DEL_PG_ROLES_CMD_2 =
      "DROP ROLE IF EXISTS pg_execute_server_program, pg_read_server_files, "
          + "pg_write_server_files; ";

  RuntimeConfigFactory runtimeConfigFactory;
  NodeUniverseManager nodeUniverseManager;

  @Inject
  public YsqlQueryExecutor(
      RuntimeConfigFactory runtimeConfigFactory, NodeUniverseManager nodeUniverseManager) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.nodeUniverseManager = nodeUniverseManager;
  }

  private String wrapJsonAgg(String query) {
    return String.format("SELECT jsonb_agg(x) FROM (%s) as x", query);
  }

  private String getQueryType(String queryString) {
    // Ignore Set statements. E.g.: /*+Set(yb_bnl_batch_size 20)*/SELECT a.rolname, t.datname, ...
    if (queryString.startsWith("/*")) {
      String[] queryStringParts = queryString.split("\\*/", 2);
      if (queryStringParts.length < 2) {
        LOG.warn("Illegal YSQL query string: {}", queryString);
      } else {
        queryString = queryStringParts[1].trim();
      }
    }

    String[] queryParts = queryString.split(" ");
    String command = queryParts[0].toUpperCase();
    if (command.equals("TRUNCATE") || command.equals("DROP"))
      return command + " " + queryParts[1].toUpperCase();
    return command;
  }

  // TODO This is a temporary workaround until it is fixed in the server side.
  private String removeQueryFromErrorMessage(String errMsg, String queryString) {
    // An error message contains the actual query sent to the server.
    if (errMsg != null) {
      errMsg = errMsg.replace(queryString, "<Query>");
    }
    return errMsg;
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

  public JsonNode executeQuery(
      Universe universe, RunQueryFormData queryParams, String username, String password) {
    ObjectNode response = newObject();

    String ysqlEndpoints = universe.getYSQLServerAddresses();
    String connectString =
        String.format(
            "jdbc:postgresql://%s/%s", ysqlEndpoints.split(",")[0], queryParams.getDbName());
    Properties props = new Properties();
    props.put("user", username);
    props.put("password", password);
    String caCert = universe.getCertificateClientToNode();
    if (caCert != null) {
      // Using verify CA since it is possible that the CN for the server cert
      // is the loadbalancer DNS/some generic ID. Just verifying the cert
      // is good enough.
      props.put("sslmode", "verify-ca");
      props.put("sslrootcert", caCert);
    }
    try (Connection conn = DriverManager.getConnection(connectString, props)) {
      if (conn == null) {
        response.put("error", "Unable to connect to DB");
      } else {
        PreparedStatement p = conn.prepareStatement(queryParams.getQuery());
        boolean hasResult = p.execute();
        if (hasResult) {
          ResultSet result = p.getResultSet();
          List<Map<String, Object>> rows = resultSetToMap(result);
          response.set("result", toJson(rows));
        } else {
          response
              .put("queryType", getQueryType(queryParams.getQuery()))
              .put("count", p.getUpdateCount());
        }
      }
    } catch (SQLException | RuntimeException e) {
      response.put("error", removeQueryFromErrorMessage(e.getMessage(), queryParams.getQuery()));
    }
    return response;
  }

  public JsonNode executeQueryInNodeShell(
      Universe universe, RunQueryFormData queryParams, NodeDetails node) {
    return executeQueryInNodeShell(
        universe,
        queryParams,
        node,
        runtimeConfigFactory.forUniverse(universe).getLong("yb.ysql_timeout_secs"));
  }

  public JsonNode executeQueryInNodeShell(
      Universe universe, RunQueryFormData queryParams, NodeDetails node, boolean authEnabled) {
    return executeQueryInNodeShell(
        universe,
        queryParams,
        node,
        runtimeConfigFactory.forUniverse(universe).getLong("yb.ysql_timeout_secs"),
        authEnabled);
  }

  public JsonNode executeQueryInNodeShell(
      Universe universe, RunQueryFormData queryParams, NodeDetails node, long timeoutSec) {

    return executeQueryInNodeShell(
        universe,
        queryParams,
        node,
        timeoutSec,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled());
  }

  public JsonNode executeQueryInNodeShell(
      Universe universe,
      RunQueryFormData queryParams,
      NodeDetails node,
      long timeoutSec,
      boolean authEnabled) {
    ObjectNode response = newObject();
    response.put("type", "ysql");
    String queryType = getQueryType(queryParams.getQuery());
    String queryString =
        queryType.equals("SELECT") ? wrapJsonAgg(queryParams.getQuery()) : queryParams.getQuery();
    ShellResponse shellResponse = new ShellResponse();
    try {
      shellResponse =
          nodeUniverseManager
              .runYsqlCommand(
                  node, universe, queryParams.getDbName(), queryString, timeoutSec, authEnabled)
              .processErrors("Ysql Query Execution Error");
    } catch (RuntimeException e) {
      response.put("error", ShellResponse.cleanedUpErrorMessage(e.getMessage()));
      return response;
    }
    try {
      ObjectMapper objectMapper = new ObjectMapper();
      if (queryType.equals("SELECT")) {
        JsonNode jsonNode =
            objectMapper.readTree(CommonUtils.extractJsonisedSqlResponse(shellResponse));
        response.set("result", jsonNode);
      } else {
        response.put("queryType", queryType);
        response.put("result", shellResponse.message);
      }

    } catch (Exception e) {
      LOG.error(e.getMessage(), e);
      response.put("error", "Failed to parse response: " + e.getMessage());
    }
    return response;
  }

  public void dropUser(Universe universe, DatabaseUserDropFormData data) {
    Customer customer = Customer.get(universe.getCustomerId());
    boolean isCloudEnabled =
        runtimeConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled");

    if (!isCloudEnabled) {
      throw new PlatformServiceException(Http.Status.METHOD_NOT_ALLOWED, "Feature not allowed.");
    }

    // trim the username to make sure it does not contain spaces
    data.username = data.username.trim();

    LOG.info("Removing user='{}' for universe='{}'", data.username, universe.getName());
    if (NON_DELETABLE_USERS.contains(data.username)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "Cannot delete user: " + data.username + ". This is a system user.");
    }

    String query = String.format("DROP USER \"%s\"; ", data.username);

    try {
      runUserDbCommands(query, data.dbName, universe);
      LOG.info("Dropped user '{}' for universe '{}'", data.username, universe.getName());
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == Http.Status.BAD_REQUEST
          && e.getMessage().contains("does not exist")) {
        LOG.warn("User '{}' does not exist for universe '{}'", data.username, universe.getName());
      } else {
        LOG.error(
            "Error dropping user '{}' for universe '{}'", data.username, universe.getName(), e);
        throw e;
      }
    }
  }

  public void createRestrictedUser(Universe universe, DatabaseUserFormData data) {
    Customer customer = Customer.get(universe.getCustomerId());
    boolean isCloudEnabled =
        runtimeConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled");

    if (!isCloudEnabled) {
      throw new PlatformServiceException(Http.Status.METHOD_NOT_ALLOWED, "Feature not allowed.");
    }

    // trim the username to make sure it does not contain spaces
    data.username = data.username.trim();

    LOG.info("Creating restricted user='{}' for universe='{}'", data.username, universe.getName());

    StringBuilder createUserWithPrivileges = new StringBuilder();

    createUserWithPrivileges
        .append(
            String.format(
                "CREATE USER \"%s\" PASSWORD '%s'; ",
                data.username, Util.escapeSingleQuotesOnly(data.password)))
        .append(
            String.format(
                "GRANT EXECUTE ON FUNCTION pg_stat_statements_reset TO \"%1$s\"; ", data.username))
        .append(DEL_PG_ROLES_CMD_1);

    // Construct ALTER ROLE statements based on the roleAttributes specified
    if (data.dbRoleAttributes != null) {
      for (RoleAttribute roleAttribute : data.dbRoleAttributes) {
        createUserWithPrivileges.append(
            String.format(
                "ALTER ROLE \"%s\" %s;", data.username, roleAttribute.getName().toString()));
      }
    }

    try {
      runUserDbCommands(createUserWithPrivileges.toString(), data.dbName, universe);
      LOG.info("Created restricted user and deleted dependencies");
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == Http.Status.BAD_REQUEST
          && e.getMessage().contains("already exists")) {
        // User exists, we should still try and run the rest of the tasks
        // since they are idempotent.
        LOG.warn(String.format("Restricted user already exists, skipping...\n%s", e.getMessage()));
      } else {
        throw e;
      }
    }

    StringBuilder resetPgStatStatements = new StringBuilder();
    resetPgStatStatements.append(DEL_PG_ROLES_CMD_2).append(" SELECT pg_stat_statements_reset(); ");
    runUserDbCommands(resetPgStatStatements.toString(), data.dbName, universe);
    LOG.info(
        "Dropped unrequired roles and assigned permissions to the restricted user='{}' for"
            + " universe='{}'",
        data.username,
        universe.getName());
  }

  public void createUser(Universe universe, DatabaseUserFormData data) {

    Customer customer = Customer.get(universe.getCustomerId());
    boolean isCloudEnabled =
        runtimeConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled");

    StringBuilder allQueries = new StringBuilder();
    String query;

    query =
        String.format(
            "CREATE USER \"%s\" INHERIT CREATEROLE CREATEDB LOGIN BYPASSRLS PASSWORD '%s'",
            data.username, Util.escapeSingleQuotesOnly(data.password));
    allQueries.append(String.format("%s; ", query));

    if (isCloudEnabled) {
      // Create admin role
      query =
          String.format(
              "CREATE ROLE \"%s\" INHERIT CREATEROLE CREATEDB BYPASSRLS", DB_ADMIN_ROLE_NAME);
      allQueries.append(String.format("%s; ", query));
      boolean versionMatch =
          universe.getVersions().stream().allMatch(v -> Util.compareYbVersions(v, "2.6.4.0") >= 0);
      String rolesList = "pg_read_all_stats, pg_signal_backend";
      if (versionMatch) {
        // yb_extension is only supported in recent YB versions 2.6.x >= 2.6.4 and >= 2.8.0
        // not including the odd 2.7.x, 2.9.x releases
        rolesList += ", yb_extension";
      }
      boolean isYbFdwSupported =
          universe.getVersions().stream().allMatch(v -> Util.compareYbVersions(v, "2.8.1.0") >= 0);
      if (isYbFdwSupported) {
        // yb_fdw is only supported in recent YB versions >= 2.8.1
        rolesList += ", yb_fdw";
      }
      query =
          String.format(
              "GRANT %2$s TO \"%1$s\"; "
                  + "GRANT EXECUTE ON FUNCTION pg_stat_statements_reset TO \"%1$s\"; "
                  + "GRANT ALL ON DATABASE yugabyte, postgres TO \"%1$s\";",
              DB_ADMIN_ROLE_NAME, rolesList);
      allQueries.append(String.format("%s ", query));
      allQueries.append(String.format("%s ", DEL_PG_ROLES_CMD_1));

      try {
        runUserDbCommands(allQueries.toString(), data.dbName, universe);
        LOG.info("Created users and deleted dependencies");
      } catch (Exception e) {
        if (e.getMessage().contains("already exists")) {
          // User exists, we should still try and run the rest of the tasks
          // since they are idempotent.
          LOG.warn(String.format("User already exists, skipping...\n%s", e.getMessage()));
        } else {
          throw e;
        }
      }

      allQueries.setLength(0);
      allQueries.append(String.format("%s ", DEL_PG_ROLES_CMD_2));
      runUserDbCommands(allQueries.toString(), data.dbName, universe);
      LOG.info("Dropped unrequired roles");

      allQueries.setLength(0);

      versionMatch =
          universe.getVersions().stream()
              .allMatch(v -> Util.compareYbVersions(v, "2.12.2.0-b31") >= 0);
      if (versionMatch) {
        query =
            String.format(
                "GRANT \"%s\" TO \"%s\" WITH ADMIN OPTION",
                PRECREATED_DB_ADMIN, DB_ADMIN_ROLE_NAME);
        allQueries.append(String.format("%s; ", query));
      }
      query =
          String.format(
              "GRANT \"%s\" TO \"%s\" WITH ADMIN OPTION", DB_ADMIN_ROLE_NAME, data.username);
      allQueries.append(String.format("%s; ", query));
    }

    // Construct ALTER ROLE statements based on the roleAttributes specified
    if (data.dbRoleAttributes != null) {
      for (RoleAttribute roleAttribute : data.dbRoleAttributes) {
        allQueries.append(
            String.format(
                "ALTER ROLE \"%s\" %s;", data.username, roleAttribute.getName().toString()));
      }
    }

    query = "SELECT pg_stat_statements_reset();";
    allQueries.append(query);

    runUserDbCommands(allQueries.toString(), data.dbName, universe);
    LOG.info("Assigned permissions to the user");
  }

  public JsonNode runUserDbCommands(String query, String dbName, Universe universe) {
    NodeDetails nodeToUse;
    try {
      nodeToUse = CommonUtils.getServerToRunYsqlQuery(universe);
    } catch (IllegalStateException e) {
      throw new PlatformServiceException(
          Http.Status.INTERNAL_SERVER_ERROR, "DB not ready to create a user");
    }
    RunQueryFormData ysqlQuery = new RunQueryFormData();
    ysqlQuery.setQuery(query);
    ysqlQuery.setDbName(dbName);

    JsonNode ysqlResponse = executeQueryInNodeShell(universe, ysqlQuery, nodeToUse);
    if (ysqlResponse.has("error")) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, ysqlResponse.get("error").asText());
    }

    return ysqlResponse;
  }

  public void validateAdminPassword(Universe universe, DatabaseSecurityFormData data) {
    RunQueryFormData ysqlQuery = new RunQueryFormData();
    ysqlQuery.setDbName(data.dbName);
    ysqlQuery.setQuery("SELECT 1");
    JsonNode ysqlResponse =
        executeQuery(universe, ysqlQuery, data.ysqlAdminUsername, data.ysqlAdminPassword);
    if (ysqlResponse.has("error")) {
      String errMsg = ysqlResponse.get("error").asText();
      // Actual message is "FATAL: password authentication failed for user".
      // Foolproof attempt to match words in order.
      if (errMsg.matches(".*\\bpassword\\b.+\\bauthentication\\b.+\\bfailed\\b.*")) {
        throw new PlatformServiceException(Http.Status.UNAUTHORIZED, errMsg);
      }
      throw new PlatformServiceException(Http.Status.BAD_REQUEST, errMsg);
    }
  }

  public void updateAdminPassword(Universe universe, DatabaseSecurityFormData data) {
    // Update admin user password YSQL.
    StringBuilder allQueries = new StringBuilder();
    String query =
        String.format(
            "ALTER USER \"%s\" WITH PASSWORD '%s'",
            data.ysqlAdminUsername, Util.escapeSingleQuotesOnly(data.ysqlAdminPassword));
    allQueries.append(String.format("%s; ", query));
    query = "SELECT pg_stat_statements_reset();";
    allQueries.append(query);
    runUserDbCommands(allQueries.toString(), data.dbName, universe);
  }
}
