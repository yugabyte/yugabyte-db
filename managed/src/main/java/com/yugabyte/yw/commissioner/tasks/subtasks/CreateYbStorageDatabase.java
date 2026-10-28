// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/** Creates the yb_storage database the amp controller keeps its state in, if it is missing. */
@Slf4j
public class CreateYbStorageDatabase extends UniverseTaskBase {

  public static final String YB_STORAGE_DB = "yb_storage";
  private static final String DEFAULT_DB = "yugabyte";
  private static final int MAX_ATTEMPTS = 5;

  @Inject
  protected CreateYbStorageDatabase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = getUniverse();
    NodeDetails node = CommonUtils.getServerToRunYsqlQuery(universe, true);
    String lastError = null;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
      if (attempt > 1) {
        node = CommonUtils.getARandomLiveOrToBeRemovedTServer(universe);
      }
      // PG has no CREATE DATABASE IF NOT EXISTS; "already exists" is the idempotent outcome.
      JsonNode created = runQuery(universe, node, "CREATE DATABASE " + YB_STORAGE_DB);
      if (!created.has("error")) {
        log.info("Created database {} in universe {}", YB_STORAGE_DB, universe.getName());
        return;
      }
      lastError = created.get("error").asText();
      if (lastError.contains("already exists")) {
        log.info("Database {} already exists in universe {}", YB_STORAGE_DB, universe.getName());
        return;
      }
      log.warn("Attempt {} to create database {} failed: {}", attempt, YB_STORAGE_DB, lastError);
    }
    throw new PlatformServiceException(
        INTERNAL_SERVER_ERROR,
        String.format("Could not create database %s: %s", YB_STORAGE_DB, lastError));
  }

  private JsonNode runQuery(Universe universe, NodeDetails node, String query) {
    RunQueryFormData queryParams = new RunQueryFormData();
    queryParams.setDbName(DEFAULT_DB);
    queryParams.setQuery(query);
    return ysqlQueryExecutor.executeQueryInNodeShell(universe, queryParams, node);
  }
}
