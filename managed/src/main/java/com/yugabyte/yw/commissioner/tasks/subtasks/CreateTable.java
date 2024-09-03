/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.Session;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TableDetails;
import io.swagger.annotations.ApiModelProperty;
import java.net.InetSocketAddress;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.regex.Pattern;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes.TableType;
import org.yb.client.YBClient;
import org.yb.client.YBTable;

@Slf4j
public class CreateTable extends AbstractTaskBase {

  private static final Pattern YSQLSH_CREATE_TABLE_SUCCESS =
      Pattern.compile("Command output:.*CREATE TABLE", Pattern.DOTALL);
  private static final long RETRY_DELAY_SEC = 30;
  private static final long MIN_RETRY_COUNT = 3;
  private static final long TOTAL_ATTEMPTS_DURATION_SEC = TimeUnit.MINUTES.toSeconds(10);

  // To use for the Cassandra client
  private Cluster cassandraCluster;
  private Session cassandraSession;
  private final NodeUniverseManager nodeUniverseManager;

  @Inject
  protected CreateTable(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
  }

  // Parameters for create table task.
  public static class Params extends UniverseTaskParams {
    // The name of the table to be created.
    @ApiModelProperty(value = "Table name")
    public String tableName;

    // The type of the table to be created (Redis, YSQL)
    @ApiModelProperty(value = "Table type")
    public TableType tableType;

    // The schema of the table to be created (required for YSQL)
    @ApiModelProperty(value = "Table details")
    public TableDetails tableDetails;

    // Flag, indicating that we need to create table only if not exists
    @ApiModelProperty(value = "Create only if table does not exist")
    public boolean ifNotExist;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  private void createPgSqlTable() {
    if (StringUtils.isEmpty(taskParams().tableName) || taskParams().tableDetails == null) {
      throw new IllegalArgumentException("No name specified for table.");
    }
    TableDetails tableDetails = taskParams().tableDetails;
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());

    boolean enableConnectionPooling =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableConnectionPooling;

    String createTableStatement = tableDetails.getPgSqlCreateTableString(taskParams().ifNotExist);

    boolean tableCreated = false;
    int attempt = 0;
    Instant timeout = Instant.now().plusSeconds(TOTAL_ATTEMPTS_DURATION_SEC);
    while (Instant.now().isBefore(timeout) || attempt < MIN_RETRY_COUNT) {
      NodeDetails randomTServer = CommonUtils.getARandomLiveTServer(universe);
      int internalYsqlServerRpcPort = randomTServer.internalYsqlServerRpcPort;
      ShellResponse response =
          nodeUniverseManager.runYsqlCommand(
              randomTServer,
              universe,
              tableDetails.keyspace,
              createTableStatement,
              enableConnectionPooling,
              internalYsqlServerRpcPort);
      if (!response.isSuccess()
          || !YSQLSH_CREATE_TABLE_SUCCESS.matcher(response.getMessage()).find()) {
        log.warn(
            "{} attempt to create table via node {} failed, response {}:{}",
            attempt++,
            randomTServer.nodeName,
            response.code,
            response.message);
        waitFor(Duration.ofSeconds(RETRY_DELAY_SEC));
      } else {
        tableCreated = true;
        break;
      }
    }
    if (!tableCreated) {
      throw new RuntimeException(
          "Failed to create table '"
              + tableDetails.keyspace
              + "."
              + CommonUtils.logTableName(tableDetails.tableName)
              + "' of type "
              + taskParams().tableType);
    }
    log.info(
        "Created table '{}.{}' of type {}.",
        tableDetails.keyspace,
        CommonUtils.logTableName(taskParams().tableName),
        taskParams().tableType);
  }

  private Session getCassandraSession() {
    if (cassandraCluster == null) {
      List<InetSocketAddress> addresses = Util.getNodesAsInet(taskParams().getUniverseUUID());
      cassandraCluster = Cluster.builder().addContactPointsWithPorts(addresses).build();
      log.info("Connected to cluster: " + cassandraCluster.getClusterName());
    }
    if (cassandraSession == null) {
      log.info("Creating a session...");
      cassandraSession = cassandraCluster.connect();
    }
    return cassandraSession;
  }

  private void createCassandraTable() {
    if (StringUtils.isEmpty(taskParams().tableName) || taskParams().tableDetails == null) {
      throw new IllegalArgumentException("No name specified for table.");
    }
    TableDetails tableDetails = taskParams().tableDetails;
    Session session = getCassandraSession();
    session.execute(tableDetails.getCQLCreateKeyspaceString());
    session.execute(tableDetails.getCQLUseKeyspaceString());
    session.execute(tableDetails.getCQLCreateTableString());
    log.info(
        "Created table '{}.{}' of type {}.",
        tableDetails.keyspace,
        CommonUtils.logTableName(taskParams().tableName),
        taskParams().tableType);
  }

  private void createRedisTable() throws Exception {
    // Get the master addresses.
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterAddresses = universe.getMasterAddresses();
    log.info(
        "Running {}: universe = {}, masterAddress = {}",
        getName(),
        taskParams().getUniverseUUID(),
        masterAddresses);
    if (masterAddresses == null || masterAddresses.isEmpty()) {
      throw new IllegalStateException(
          "No master host/ports for a table creation op in " + taskParams().getUniverseUUID());
    }
    String certificate = universe.getCertificateNodetoNode();

    YBClient client = null;
    try {
      client = ybService.getClient(masterAddresses, certificate);

      if (StringUtils.isEmpty(taskParams().tableName)) {
        taskParams().tableName = YBClient.REDIS_DEFAULT_TABLE_NAME;
      }
      YBTable table = client.createRedisTable(taskParams().tableName, taskParams().ifNotExist);
      log.info(
          "Created table '{}' of type {}.",
          CommonUtils.logTableName(table.getName()),
          table.getTableType());
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().tableName + ")";
  }

  @Override
  public String toString() {
    return getName();
  }

  @Override
  public void run() {
    try {
      if (taskParams().tableType == TableType.PGSQL_TABLE_TYPE) {
        createPgSqlTable();
      } else if (taskParams().tableType == TableType.YQL_TABLE_TYPE) {
        createCassandraTable();
      } else {
        createRedisTable();
      }
    } catch (Exception e) {
      String msg =
          "Error "
              + e.getMessage()
              + " while creating table "
              + CommonUtils.logTableName(taskParams().tableName);
      log.error(msg, e);
      throw new RuntimeException(msg);
    }
  }
}
