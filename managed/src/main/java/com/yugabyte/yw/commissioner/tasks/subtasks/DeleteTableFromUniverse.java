// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.TableTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.DeleteTableResponse;
import org.yb.client.YBClient;
import play.api.Play;

public class DeleteTableFromUniverse extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(DeleteTableFromUniverse.class);

  // The YB client to use.
  public YBClientService ybService;

  public static class Params extends TableTaskParams {
    // The addresses of the masters of the universe that table to be dropped is in.
    public String masterAddresses;
    // The name of the table to be dropped.
    public String tableName;
    // The keyspace of the table to be dropped.
    public String keyspace;

    public String getFullName() {
      return keyspace + "." + tableName;
    }
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
  public void run() {
    Params params = taskParams();
    YBClient client = null;
    try {
      client = ybService.getClient(params.masterAddresses);
      client.deleteTable(params.tableName, params.keyspace);
      LOG.info("Dropped table {}", params.getFullName());
    } catch (Exception e) {
      String msg = "Error " + e.getMessage() + " while dropping table " + params.getFullName();
      LOG.error(msg, e);
      throw new RuntimeException(msg);
    } finally {
      ybService.closeClient(client, params.masterAddresses);
    }
  }
}
