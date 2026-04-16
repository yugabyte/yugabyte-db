// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.RetryTaskUntilCondition;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig.TableType;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Rule;
import org.junit.rules.Timeout;
import org.yb.CommonTypes;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class XClusterLocalTestBase extends LocalProviderUniverseTestBase {
  public static Map<String, String> DEFAULT_TABLE_COLUMNS = Map.of("id", "int", "name", "text");

  // Set a higher timeout for xCluster tests to deflake the utest failures.
  @Rule public Timeout globalTimeout = Timeout.seconds(2400);

  public static class Db {
    public String name;
    public boolean colocation;
    public TableType tableType = TableType.YSQL;

    public static Db create(String name, boolean colocation) {
      Db db = new Db();
      db.name = name;
      db.colocation = colocation;
      return db;
    }

    public static Db create(String name, boolean colocation, TableType tableType) {
      Db db = new Db();
      db.name = name;
      db.colocation = colocation;
      db.tableType = tableType;
      return db;
    }
  }

  public static class Table {
    public String name;
    // columnName -> column type.
    public Map<String, String> columns;
    public Boolean colocation;
    public Db db;
    public int colocationId;

    // Must be between "16384" and "4294967295".
    public static int colocationIdCounter = 20000;

    public static Table create(String name, Map<String, String> columns, Db db) {
      Table table = new Table();
      table.name = name;
      table.columns = columns;
      table.db = db;
      if (db.colocation) {
        table.colocationId = Table.colocationIdCounter++;
        table.colocation = true;
      } else {
        table.colocation = false;
      }
      return table;
    }

    public static Table create(
        String name, Map<String, String> columns, Db db, boolean escapeColocation) {
      Table table = new Table();
      table.name = name;
      table.columns = columns;
      table.db = db;
      table.colocation = !escapeColocation;
      return table;
    }
  }

  public static class IndexTable {
    public String name;
    // For simplicity, create on single column.
    public String columnName;
    public Db db;
    public Table table;

    public static IndexTable create(String name, String columnName, Db db, Table table) {
      IndexTable index = new IndexTable();
      index.name = name;
      index.columnName = columnName;
      index.db = db;
      index.table = table;
      return index;
    }
  }

  public void createTestSet(Universe universe, List<Db> dbs, List<Table> tables) {
    for (Db db : dbs) {
      createDatabase(universe, db);
    }

    for (Table t : tables) {
      createTable(universe, t);
    }
  }

  public void insertRow(Universe universe, Table table, Map<String, String> columnValues) {
    List<String> columnList = new ArrayList<>();
    List<String> valueList = new ArrayList<>();
    for (String column : columnValues.keySet()) {
      columnList.add(column);
      valueList.add(columnValues.get(column));
    }
    String query =
        String.format(
            "insert into %s (%s) values (%s)",
            table.name, String.join(", ", columnList), String.join(", ", valueList));

    log.debug("Universe: {}, database: {}, query: {}", universe.getName(), table.db.name, query);

    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            getLiveNode(universe), universe, table.db.name, query, 10);
    assertTrue(ysqlResponse.isSuccess());
  }

  public int getRowCount(Universe universe, Table table) {
    String query = String.format("select count(*) from %s", table.name);
    log.debug("Universe: {}, database: {}, Query: {}", universe.getName(), table.db.name, query);
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            getLiveNode(universe), universe, table.db.name, query, 10);
    if (!ysqlResponse.isSuccess()) {
      throw new RuntimeException(
          String.format(
              "Failed to get row count for universe: %s and table: %s",
              universe.getName(), table.name));
    }
    return Integer.parseInt(CommonUtils.extractJsonisedSqlResponse(ysqlResponse).trim());
  }

  public void validateRowCount(Universe universe, Table table, int expectedRows) {
    assertNotEquals(-1, expectedRows);
    RetryTaskUntilCondition<Integer> condition =
        new RetryTaskUntilCondition<>(
            () -> {
              try {
                int rowCount = getRowCount(universe, table);
                log.debug("row count {}", rowCount);
                return rowCount;
              } catch (Exception e) {
                log.error(e.getMessage());
                return -1;
              }
            },
            rowCount -> rowCount == expectedRows);
    boolean success = condition.retryUntilCond(1, TimeUnit.MINUTES.toSeconds(1));
    if (!success) {
      throw new RuntimeException(
          String.format(
              "Failed to get expected number of rows: %d for table %s.", expectedRows, table.name));
    }
  }

  // Validates that the number of rows never becomes notExpectedRows.
  public void validateNotExpectedRowCount(Universe universe, Table table, int notExpectedRows) {
    assertNotEquals(-1, notExpectedRows);
    RetryTaskUntilCondition<Integer> condition =
        new RetryTaskUntilCondition<>(
            () -> {
              try {
                int rowCount = getRowCount(universe, table);
                log.debug("row count: {}", rowCount);
                return rowCount;
              } catch (Exception e) {
                log.error(e.getMessage());
                return -1;
              }
            },
            rowCount -> rowCount == notExpectedRows);
    boolean success = condition.retryUntilCond(1 /* delayBetweenRetrySecs */, 10 /* timeoutSecs */);
    if (success) {
      throw new RuntimeException(
          String.format(
              "Got number of rows we did not expect: %d for table %s.",
              notExpectedRows, table.name));
    }
  }

  public void createDatabase(Universe universe, Db db) {
    if (db.tableType == TableType.YSQL) {
      createYsqlDatabase(universe, db);
    } else {
      createYcqlDatabase(universe, db);
    }
  }

  public void createYsqlDatabase(Universe universe, Db db) {
    NodeDetails node = getLiveNode(universe);
    String query = String.format("create database %s with colocation = %b", db.name, db.colocation);
    log.debug("Universe: {}, Query: {}", universe.getName(), query);
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(node, universe, YUGABYTE_DB, query, 20);
    assertTrue(ysqlResponse.isSuccess());
  }

  public void createYcqlDatabase(Universe universe, Db db) {
    RunQueryFormData queryParams = new RunQueryFormData();
    queryParams.setTableType(CommonTypes.TableType.YQL_TABLE_TYPE);
    queryParams.setQuery("CREATE KEYSPACE " + db.name);
    JsonNode response = ycqlQueryExecutor.executeQuery(universe, queryParams, false);
    assertFalse(response.has("error"));
  }

  public void createTable(Universe universe, Table table) {
    if (table.db.tableType == TableType.YSQL) {
      createYsqlTable(universe, table);
    } else {
      createYcqlTable(universe, table);
    }
  }

  public void createYsqlTable(Universe universe, Table table) {
    NodeDetails node = getLiveNode(universe);
    String columns =
        table.columns.entrySet().stream()
            .map(e -> (e.getKey() + " " + e.getValue()))
            .collect(Collectors.joining(", "));
    String query = String.format("create table %s (%s)", table.name, columns);
    if (!table.colocation && table.db.colocation) {
      query = query + " with (colocation = false)";
    }
    if (table.colocation) {
      query = query + " with (COLOCATION_ID = " + table.colocationId + ")";
    }
    log.debug("Universe: {}, database: {}, Query: {}", universe.getName(), table.db.name, query);
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(node, universe, table.db.name, query, 10);
    assertTrue(ysqlResponse.isSuccess());
  }

  public void createYcqlTable(Universe universe, Table table) {
    RunQueryFormData queryParams = new RunQueryFormData();
    queryParams.setTableType(CommonTypes.TableType.YQL_TABLE_TYPE);
    queryParams.setQuery(
        "CREATE TABLE "
            + table.db.name
            + "."
            + table.name
            + " ("
            + table.columns.entrySet().stream()
                .map(
                    e ->
                        e.getKey()
                            + " "
                            + e.getValue()
                            + (e.getKey() == "id" ? "" : " PRIMARY KEY"))
                .collect(Collectors.joining(", "))
            + ")"
            + "WITH TRANSACTIONS = {'enabled':'true'}");
    JsonNode response = ycqlQueryExecutor.executeQuery(universe, queryParams, false);
    assertFalse(response.has("error"));
  }

  public void createIndexTable(Universe universe, IndexTable index) {
    if (index.db.tableType == TableType.YSQL) {
      createYsqlIndexTable(universe, index);
    } else {
      createYcqlIndexTable(universe, index);
    }
  }

  public void deleteYcqlIndexTable(Universe universe, IndexTable index) {
    RunQueryFormData queryParams = new RunQueryFormData();
    queryParams.setTableType(CommonTypes.TableType.YQL_TABLE_TYPE);
    queryParams.setQuery("DROP INDEX " + index.db.name + "." + index.name);
    JsonNode response = ycqlQueryExecutor.executeQuery(universe, queryParams, false);
    assertFalse(response.has("error"));
  }

  public void createYsqlIndexTable(Universe universe, IndexTable index) {
    NodeDetails node = getLiveNode(universe);
    String query =
        String.format("create index %s on %s (%s)", index.name, index.table.name, index.columnName);

    log.debug("Universe: {}, database: {}, Query: {}", universe.getName(), index.db.name, query);
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(node, universe, index.db.name, query, 10);
    assertTrue(ysqlResponse.isSuccess());
  }

  public void createYcqlIndexTable(Universe universe, IndexTable index) {
    RunQueryFormData queryParams = new RunQueryFormData();
    queryParams.setTableType(CommonTypes.TableType.YQL_TABLE_TYPE);
    queryParams.setQuery(
        "CREATE INDEX IF NOT EXISTS "
            + index.name
            + " ON "
            + index.db.name
            + "."
            + index.table.name
            + " ("
            + index.columnName
            + ")");
    JsonNode response = ycqlQueryExecutor.executeQuery(universe, queryParams, false);
    assertFalse(response.has("error"));
  }

  public void dropDatabase(Universe universe, Db db) {
    NodeDetails node = getLiveNode(universe);
    String query = String.format("drop database %s", db.name);
    log.debug("Universe: {}, Query: {}", universe.getName(), query);
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(node, universe, YUGABYTE_DB, query, 20);
    assertTrue(ysqlResponse.isSuccess());
  }

  public void deleteTable(Universe universe, Table table) {
    NodeDetails node = getLiveNode(universe);
    String query = String.format("drop table %s", table.name);
    log.debug("Universe: {}, database: {}, Query: {}", universe.getName(), table.db.name, query);
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(node, universe, table.db.name, query, 10);
    assertTrue(ysqlResponse.isSuccess());
  }

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair<>(120, 180);
  }

  public NodeDetails getLiveNode(Universe universe) {
    return universe.getUniverseDetails().nodeDetailsSet.stream()
        .filter(n -> n.state.equals(NodeDetails.NodeState.Live))
        .findFirst()
        .orElse(null);
  }

  public Result createXClusterConfig(XClusterConfigCreateFormData formData) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "POST",
        "/api/customers/" + customer.getUuid() + "/xcluster_configs",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  public Result getXClusterConfig(UUID xClusterUuid) {
    return FakeApiHelper.doRequestWithAuthToken(
        app,
        "GET",
        "/api/customers/" + customer.getUuid() + "/xcluster_configs/" + xClusterUuid,
        user.createAuthToken());
  }

  public Result editXClusterConfig(XClusterConfigEditFormData formData, UUID xClusterUuid) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "PUT",
        "/api/customers/" + customer.getUuid() + "/xcluster_configs/" + xClusterUuid,
        user.createAuthToken(),
        Json.toJson(formData));
  }

  protected void assertYsqlOutputEqualsWithRetry(
      Universe universe, String ysqlCommand, String expectedValue) {
    NodeDetails targetNodeDetails = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    doWithRetry(
        Duration.ofSeconds(1),
        Duration.ofMinutes(1),
        () -> {
          ShellResponse targetYsqlResponse =
              localNodeUniverseManager.runYsqlCommand(
                  targetNodeDetails, universe, YUGABYTE_DB, ysqlCommand, 10);
          if (!targetYsqlResponse.isSuccess()) {
            throw new RuntimeException("Failed to run ysql command");
          }
          String response = CommonUtils.extractJsonisedSqlResponse(targetYsqlResponse).trim();
          if (!response.equals(expectedValue)) {
            throw new RuntimeException("Expected " + expectedValue + ", got " + response);
          }
        });
  }

  protected Set<XClusterTableConfig> getTableDetailsWithStatus(
      Set<XClusterTableConfig> tableDetails, XClusterTableConfig.Status status) {
    return tableDetails.stream()
        .filter(table -> table.getStatus() == status)
        .collect(Collectors.toSet());
  }
}
