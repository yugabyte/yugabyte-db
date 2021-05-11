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

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.TableTaskParams;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
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
    Universe universe = Universe.getOrBadRequest(params.universeUUID);
    String certificate = universe.getCertificate();
    YBClient client = null;
    try {
      client = ybService.getClient(params.masterAddresses, certificate);
      client.deleteTable(params.keyspace, params.tableName);
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
