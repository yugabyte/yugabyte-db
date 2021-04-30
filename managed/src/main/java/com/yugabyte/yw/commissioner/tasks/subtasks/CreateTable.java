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

import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.helpers.TableDetails;
import com.yugabyte.yw.common.Util;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common.TableType;
import org.yb.client.YBClient;
import org.yb.client.YBTable;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.Session;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.Universe;

import play.api.Play;

import java.net.InetSocketAddress;
import java.util.List;

public class CreateTable extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateTable.class);

  // The YB client to use.
  public YBClientService ybService;

  // To use for the Cassandra client
  private Cluster cassandraCluster;
  private Session cassandraSession;

  // Parameters for create table task.
  public static class Params extends UniverseTaskParams {
    // The name of the table to be created.
    public String tableName;
    // The type of the table to be created (Redis, YSQL)
    public TableType tableType;
    // The schema of the table to be created (required for YSQL)
    public TableDetails tableDetails;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  private Session getCassandraSession() {
    if (cassandraCluster == null) {
      List<InetSocketAddress> addresses = Util.getNodesAsInet(taskParams().universeUUID);
      cassandraCluster = Cluster.builder()
                                .addContactPointsWithPorts(addresses)
                                .build();
      LOG.info("Connected to cluster: " + cassandraCluster.getClusterName());
    }
    if (cassandraSession == null) {
      LOG.info("Creating a session...");
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
    LOG.info("Created table '{}.{}' of type {}.", tableDetails.keyspace, taskParams().tableName,
        taskParams().tableType);
  }

  private void createRedisTable() throws Exception {
    // Get the master addresses.
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String masterAddresses = universe.getMasterAddresses();
    LOG.info("Running {}: universe = {}, masterAddress = {}", getName(),
        taskParams().universeUUID, masterAddresses);
    if (masterAddresses == null || masterAddresses.isEmpty()) {
      throw new IllegalStateException("No master host/ports for a table creation op in " +
          taskParams().universeUUID);
    }
    String certificate = universe.getCertificateNodeToNode();
    String[] rpcClientCertFiles = universe.getFilesForMutualTLS();

    YBClient client = null;
    try {
      client = ybService.getClient(masterAddresses, certificate, rpcClientCertFiles);

      if (StringUtils.isEmpty(taskParams().tableName)) {
        taskParams().tableName = YBClient.REDIS_DEFAULT_TABLE_NAME;
      }
      YBTable table = client.createRedisTable(taskParams().tableName);
      LOG.info("Created table '{}' of type {}.", table.getName(), table.getTableType());
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
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
      if (taskParams().tableType == TableType.YQL_TABLE_TYPE) {
        createCassandraTable();
      } else {
        createRedisTable();
      }
    } catch (Exception e) {
      String msg = "Error " + e.getMessage() + " while creating table " + taskParams().tableName;
      LOG.error(msg, e);
      throw new RuntimeException(msg);
    }
  }
}
