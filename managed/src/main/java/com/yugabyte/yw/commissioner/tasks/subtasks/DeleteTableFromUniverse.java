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
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.TableTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class DeleteTableFromUniverse extends AbstractTaskBase {

  @Inject
  protected DeleteTableFromUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

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
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Params params = taskParams();
    Universe universe = Universe.getOrBadRequest(params.getUniverseUUID());
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    try {
      client = ybService.getClient(params.masterAddresses, certificate);
      client.deleteTable(params.keyspace, params.tableName);
      log.info("Dropped table {}", CommonUtils.logTableName(params.getFullName()));
    } catch (Exception e) {
      String msg =
          "Error "
              + e.getMessage()
              + " while dropping table "
              + CommonUtils.logTableName(params.getFullName());
      log.error(msg, e);
      throw new RuntimeException(msg);
    } finally {
      ybService.closeClient(client, params.masterAddresses);
    }
  }
}
