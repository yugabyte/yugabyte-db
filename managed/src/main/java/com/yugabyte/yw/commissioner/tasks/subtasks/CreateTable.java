// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import java.util.ArrayList;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.ColumnSchema;
import org.yb.Schema;
import org.yb.Type;
import org.yb.Common.TableType;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.client.CreateTableOptions;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.Universe;

import play.api.Play;

public class CreateTable extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateTable.class);

  // The YB client to use.
  public YBClientService ybService;

  // Parameters for create table task.
  public static class Params extends NodeTaskParams {
    // The name of the table to be created.
    public String tableName;
    // Number of tablets to be created for the table.
    public int numTablets;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().tableName + " with numTablets = " +
           taskParams().numTablets + ")";
  }

  @Override
  public String toString() {
    return getName();
  }

  @Override
  public void run() {
    // Get the master addresses.
    Universe universe = Universe.get(taskParams().universeUUID);
    String masterAddresses = universe.getMasterAddresses();
    LOG.info("Running {}: universe = {}, masterAddress = {}", getName(),
             taskParams().universeUUID, masterAddresses);
    if (masterAddresses == null || masterAddresses.isEmpty()) {
      throw new IllegalStateException("No master host/ports for a table creation op in " +
                                      taskParams().universeUUID);
    }

    YBClient client = ybService.getClient(masterAddresses);

    if (taskParams().tableName == null || taskParams().tableName.isEmpty()) {
      taskParams().tableName = YBClient.REDIS_DEFAULT_TABLE_NAME;
    }

    try {
      YBTable table = client.createRedisTable(taskParams().tableName, taskParams().numTablets);
      LOG.info("Created table '{}' of type {}.", taskParams().tableName, table.getTableType());
    } catch (Exception e) {
      String msg = "Error " + e.getMessage() + " while creating table " + taskParams().tableName;
      LOG.error(msg, e);
      throw new RuntimeException(msg);
    }
  }
}
