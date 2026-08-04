// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckShellConnectivity extends UniverseTaskBase {

  private final YsqlQueryExecutor ysqlQueryExecutor;
  private final YcqlQueryExecutor ycqlQueryExecutor;
  private final String ERROR_TO_CATCH = "certificate_unknown";

  @Inject
  protected CheckShellConnectivity(
      BaseTaskDependencies baseTaskDependencies,
      YsqlQueryExecutor ysqlQueryExecutor,
      YcqlQueryExecutor ycqlQueryExecutor) {
    super(baseTaskDependencies);
    this.ysqlQueryExecutor = ysqlQueryExecutor;
    this.ycqlQueryExecutor = ycqlQueryExecutor;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    if (primaryCluster.userIntent.enableYSQL) {
      checkYsqlConnectivity(universe);
    }
    if (primaryCluster.userIntent.enableYCQL) {
      checkYcqlConnectivity(universe);
    }
  }

  private void checkYsqlConnectivity(Universe universe) {
    log.info("Checking YSQL connectivity for universe {}", universe.getUniverseUUID());
    RunQueryFormData runQueryFormData = new RunQueryFormData();
    runQueryFormData.setQuery("SELECT 1");
    runQueryFormData.setDbName("postgres");

    String password = Util.DEFAULT_YSQL_PASSWORD;

    JsonNode response =
        ysqlQueryExecutor.executeQuery(
            universe, runQueryFormData, Util.DEFAULT_YSQL_USERNAME, password);

    validateResponse(universe, response, "YSQL");
  }

  private void checkYcqlConnectivity(Universe universe) {
    log.info("Checking YCQL connectivity for universe {}", universe.getUniverseUUID());
    RunQueryFormData runQueryFormData = new RunQueryFormData();
    runQueryFormData.setQuery("SELECT release_version FROM system.local");

    String password = Util.DEFAULT_YCQL_PASSWORD;

    boolean authEnabled =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYCQLAuth;

    JsonNode response =
        ycqlQueryExecutor.executeQuery(
            universe, runQueryFormData, authEnabled, Util.DEFAULT_YCQL_USERNAME, password);

    validateResponse(universe, response, "YCQL");
  }

  private void validateResponse(Universe universe, JsonNode response, String type) {
    if (response.has("error")) {
      String error = response.get("error").asText();
      if (error != null && error.contains(ERROR_TO_CATCH)) {
        log.error(
            "Error checking {} connectivity for universe {}: {}",
            type,
            universe.getUniverseUUID(),
            error);
        if (universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(CloudType.kubernetes)) {
          log.error(
              "If the universe is using cert manager, please ensure the rootCA provided to YBA is"
                  + " valid");
        }
        throw new RuntimeException(type + " connectivity check failed: " + error);
      }
      log.info("{} connectivity check failed with expected error (ignored): {}", type, error);
    } else {
      log.info("{} connectivity check passed.", type);
    }
  }
}
