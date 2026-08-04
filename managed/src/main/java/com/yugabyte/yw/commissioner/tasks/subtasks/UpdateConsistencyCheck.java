/*
 * Copyright 2024 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.Util.CONSISTENCY_CHECK_TABLE_NAME;
import static com.yugabyte.yw.common.Util.SYSTEM_PLATFORM_DB;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RecoverableException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YsqlQueryExecutor.ConsistencyInfoResp;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.PendingConsistencyCheck;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateConsistencyCheck extends UniverseTaskBase {

  @Inject
  protected UpdateConsistencyCheck(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for create table task.
  public static class Params extends UniverseTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public void createConsistencyCheckTable(Universe universe, NodeDetails node) {
    try {
      String createQuery =
          String.format(
              "CREATE TABLE IF NOT EXISTS %s (seq_num INT, task_uuid UUID, yw_uuid UUID, yw_host"
                  + " VARCHAR, PRIMARY KEY (task_uuid HASH, seq_num DESC)) SPLIT INTO 1 TABLETS;",
              CONSISTENCY_CHECK_TABLE_NAME);
      RunQueryFormData runQueryFormData = new RunQueryFormData();
      runQueryFormData.setDbName(SYSTEM_PLATFORM_DB);
      runQueryFormData.setQuery(createQuery);
      JsonNode ysqlResponse =
          ysqlQueryExecutor.executeQueryInNodeShell(
              universe,
              runQueryFormData,
              node,
              confGetter.getConfForScope(universe, UniverseConfKeys.ysqlConsistencyTimeoutSecs));
      int retries = 0;
      // Retry loop
      while (ysqlResponse != null && ysqlResponse.has("error") && retries < 5) {
        retries += 1;
        ysqlResponse =
            ysqlQueryExecutor.executeQueryInNodeShell(
                universe,
                runQueryFormData,
                CommonUtils.getARandomLiveOrToBeRemovedTServer(universe),
                confGetter.getConfForScope(universe, UniverseConfKeys.ysqlConsistencyTimeoutSecs));
      }
      if (ysqlResponse != null && ysqlResponse.has("error")) {
        TaskType taskType = getTaskExecutor().getTaskType(getClass());
        if (taskType == TaskType.CreateUniverse || taskType == TaskType.CreateKubernetesUniverse) {
          log.error(
              "Could not create initial consistency check table for new universe {}.",
              universe.getName());
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, ysqlResponse.get("error").asText());
        } else {
          log.warn(
              "Could not create consistency check table for existing universe {}, skipping. Is the"
                  + " universe healthy?",
              universe.getName());
          return;
        }
      }
      runQueryFormData.setQuery(
          String.format(
              "INSERT INTO %s (seq_num, task_uuid, yw_uuid, yw_host) VALUES (0, '%s', '%s',"
                  + " '%s')",
              CONSISTENCY_CHECK_TABLE_NAME,
              getTaskUUID(),
              configHelper.getYugawareUUID(),
              Util.getYwHostnameOrIP()));
      ysqlResponse =
          ysqlQueryExecutor.executeQueryInNodeShell(
              universe,
              runQueryFormData,
              node,
              confGetter.getConfForScope(universe, UniverseConfKeys.ysqlConsistencyTimeoutSecs));
      retries = 0;
      // retry loop
      while (ysqlResponse != null && ysqlResponse.has("error") && retries < 5) {
        retries += 1;
        node = CommonUtils.getARandomLiveOrToBeRemovedTServer(universe);
        ysqlResponse =
            ysqlQueryExecutor.executeQueryInNodeShell(
                universe,
                runQueryFormData,
                node,
                confGetter.getConfForScope(universe, UniverseConfKeys.ysqlConsistencyTimeoutSecs));
      }
      if (ysqlResponse != null && ysqlResponse.has("error")) {
        log.warn("Could not perform inital insert into consistency check table.");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, ysqlResponse.get("error").asText());
      }
      // Update local YBA sequence number with initial value
      updateUniverseSeqNum(universe, 0);
    } catch (IllegalStateException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not find valid tserver to create consistency check table.");
    }
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String universeName = universe.getName();
    UUID universeUUID = universe.getUniverseUUID();
    int ybaSeqNum = universe.getUniverseDetails().sequenceNumber;
    NodeDetails node;
    try {
      node = CommonUtils.getServerToRunYsqlQuery(universe, true);
    } catch (IllegalStateException e) {
      log.warn(
          "Could not find valid tserver, skipping consistency check for universe {} ({}).",
          universeName,
          universeUUID);
      return;
    }
    // Special case for -1
    if (ybaSeqNum == -1) {
      log.info("YBA sequence number is -1 for universe {} ({})", universeName, universeUUID);
      ConsistencyInfoResp response = null;
      try {
        response = ysqlQueryExecutor.getConsistencyInfo(universe);
      } catch (RecoverableException e) {
        // Table does not exist
        log.info(
            "Creating consistency check table for the first time for universe {} ({}).",
            universeName,
            universeUUID);
        createConsistencyCheckTable(universe, node);
        return;
      }
      if (response != null) {
        log.info(
            "Accepting db sequence number {} for universe {} ({})",
            response.getSeqNum(),
            universeName,
            universeUUID);
        updateUniverseSeqNum(universe, response.getSeqNum());
      } else {
        log.warn(
            "Could not read consistency info for universe {} ({}) and local is -1, skipping"
                + " consistency check",
            universeName,
            universeUUID);
      }
      return;
    }

    // Normal update
    int ybaSeqNumUpdate = universe.getUniverseDetails().sequenceNumber + 1;
    PendingConsistencyCheck pend = PendingConsistencyCheck.create(getTaskUUID(), universe);
    RunQueryFormData runQueryFormData = new RunQueryFormData();
    String updateQuery =
        String.format(
            "WITH updated_rows AS (UPDATE %s SET seq_num = %d, task_uuid = '%s', yw_uuid = '%s',"
                + " yw_host = '%s' WHERE seq_num < %d OR (seq_num = %d AND task_uuid = '%s')"
                + " RETURNING seq_num, task_uuid) SELECT jsonb_agg(updated_rows) AS result FROM"
                + " updated_rows;",
            CONSISTENCY_CHECK_TABLE_NAME,
            ybaSeqNumUpdate,
            getTaskUUID(),
            configHelper.getYugawareUUID(),
            Util.getYwHostnameOrIP(),
            ybaSeqNumUpdate,
            ybaSeqNumUpdate,
            getTaskUUID());
    // Testing string that includes pg_sleep
    if (confGetter.getConfForScope(universe, UniverseConfKeys.consistencyUpdateDelay) > 0) {
      updateQuery =
          String.format(
              "BEGIN; UPDATE %s SET seq_num = %d, task_uuid = '%s', yw_uuid = '%s', yw_host = '%s'"
                  + " WHERE seq_num < %d OR (seq_num = %d AND task_uuid = '%s'); COMMIT; SELECT"
                  + " pg_sleep(%d); SELECT jsonb_agg(x) FROM (SELECT seq_num, task_uuid FROM %s"
                  + " ORDER BY seq_num DESC LIMIT 1) as x;",
              CONSISTENCY_CHECK_TABLE_NAME,
              ybaSeqNumUpdate,
              getTaskUUID(),
              configHelper.getYugawareUUID(),
              Util.getYwHostnameOrIP(),
              ybaSeqNumUpdate,
              ybaSeqNumUpdate,
              getTaskUUID(),
              confGetter.getConfForScope(universe, UniverseConfKeys.consistencyUpdateDelay),
              CONSISTENCY_CHECK_TABLE_NAME);
    }
    runQueryFormData.setQuery(updateQuery);
    runQueryFormData.setDbName(SYSTEM_PLATFORM_DB);
    log.info(
        "Attempting to update DB to sequence number {} for universe {} ({})",
        ybaSeqNumUpdate,
        universeName,
        universeUUID);
    try {
      // Attempt to update DB
      JsonNode ysqlResponse =
          ysqlQueryExecutor.executeQueryInNodeShell(
              universe,
              runQueryFormData,
              node,
              confGetter.getConfForScope(universe, UniverseConfKeys.ysqlConsistencyTimeoutSecs));
      int retries = 0;
      // Retry loop
      while (ysqlResponse != null && ysqlResponse.has("error") && retries < 5) {
        try {
          node = CommonUtils.getARandomLiveOrToBeRemovedTServer(universe);
          retries += 1;
          ysqlResponse =
              ysqlQueryExecutor.executeQueryInNodeShell(
                  universe,
                  runQueryFormData,
                  node,
                  confGetter.getConfForScope(
                      universe, UniverseConfKeys.ysqlConsistencyTimeoutSecs));

        } catch (IllegalStateException e) {
          log.warn(
              "Could not find valid tserver, skipping consistency check for universe {} ({}).",
              universeName,
              universeUUID);
          return;
        }
      }
      // Testing hook for CustomerTaskManager.handlePendingConsistencyTasks
      if (confGetter.getConfForScope(universe, UniverseConfKeys.consistencyCheckPendingTest)) {
        Util.shutdownYbaProcess(0);
        waitFor(Duration.ofMillis(10000));
      }
      // Error case with unexpected failure running remote query, unable to validate so reset to
      // be safe
      if (ysqlResponse != null && ysqlResponse.has("error")) {
        log.warn(
            "Consistency check is not active for universe {} due to error: {}. Resetting local"
                + " sequence number.",
            universeName,
            ysqlResponse.get("error").asText());
        updateUniverseSeqNum(universe, -1);
        return;
      }
      if (ysqlResponse != null && ysqlResponse.has("result")) {
        ShellResponse shellResponse = ShellResponse.create(0, ysqlResponse.get("result").asText());
        ObjectMapper objectMapper = new ObjectMapper();
        JsonNode jsonNode =
            objectMapper.readTree(CommonUtils.extractJsonisedSqlResponse(shellResponse));
        // No rows updated, stale metadata
        if (jsonNode == null
            || jsonNode.get(0) == null
            || !jsonNode.get(0).has("seq_num")
            || !jsonNode.get(0).has("task_uuid")) {
          // Best effort try to read what's in the DB for better error message.
          ConsistencyInfoResp response = ysqlQueryExecutor.getConsistencyInfo(universe);
          if (response != null) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "No rows updated performing consistency check, stale universe metadata. DB at"
                        + " version %d set by YBA %s (%s) during task %s. Task should be run from"
                        + " up to date YBA or contact Yugabyte Support to resolve.",
                    response.getSeqNum(),
                    response.getYwHost(),
                    response.getYwUUID(),
                    response.getTaskUUID()));
          }
          throw new PlatformServiceException(
              BAD_REQUEST,
              "No rows updated performing consistency check,  stale universe metadata. Task should"
                  + " be run from up to date YBA or contact Yugabyte Support to resolve.");
        }
        // Valid result, perform update
        int dbSeqNum = jsonNode.get(0).get("seq_num").asInt();
        UUID dbTaskUuid = UUID.fromString(jsonNode.get(0).get("task_uuid").asText());

        // Doubtful this should ever execute, but in case, treat as stale.
        if (dbSeqNum != ybaSeqNumUpdate || !dbTaskUuid.equals(getTaskUUID())) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              String.format(
                  "Found different values (seq_num: %d, task_uuid: %s) in DB after intended update."
                      + " Expected seq_num: %d and task_uuid: %s.",
                  dbSeqNum, dbTaskUuid, ybaSeqNumUpdate, getTaskUUID()));
        }
        // Update local YBA sequence number with update value
        log.info(
            "Updated DB to seq_num: {} and task_uuid: {}, setting local sequence number for"
                + " universe {}.",
            dbSeqNum,
            dbTaskUuid,
            universeName);
        updateUniverseSeqNum(universe, dbSeqNum);
      }
    } catch (JsonProcessingException e) {
      log.warn(
          "Error processing JSON response from update query: {}. Consistency check may not be"
              + " active.",
          e.getMessage());
      updateUniverseSeqNum(universe, -1);
    } finally {
      pend.delete();
    }
  }

  private void updateUniverseSeqNum(Universe universe, int seqNum) {
    Universe.UniverseUpdater updater =
        u -> {
          UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
          universeDetails.sequenceNumber = seqNum;
          u.setUniverseDetails(universeDetails);
        };
    saveUniverseDetails(updater);
    log.info("Updated {} universe details sequence number to {}.", universe.getName(), seqNum);
  }
}
