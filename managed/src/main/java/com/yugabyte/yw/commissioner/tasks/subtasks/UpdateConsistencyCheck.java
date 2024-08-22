/*
 * Copyright 2024 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.Util.CONSISTENCY_CHECK;
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
import com.yugabyte.yw.common.YsqlQueryExecutor.ConsistencyInfoResp;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.PendingConsistencyCheck;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.ColumnDetails;
import com.yugabyte.yw.models.helpers.ColumnDetails.YQLDataType;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TableDetails;
import java.time.Duration;
import java.util.ArrayList;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.ColumnSchema.SortOrder;
import org.yb.CommonTypes.TableType;

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

  public void createConsistencyCheckTable() {

    CreateTable task = createTask(CreateTable.class);
    ColumnDetails seqNumColumn = new ColumnDetails();
    seqNumColumn.isClusteringKey = true;
    seqNumColumn.name = "seq_num";
    seqNumColumn.type = YQLDataType.INT;
    seqNumColumn.sortOrder = SortOrder.ASC;

    ColumnDetails opUUIDColumn = new ColumnDetails();
    opUUIDColumn.name = "task_uuid";
    opUUIDColumn.type = YQLDataType.UUID;

    TableDetails details = new TableDetails();
    details.tableName = CONSISTENCY_CHECK;
    details.keyspace = SYSTEM_PLATFORM_DB;
    details.columns = new ArrayList<>();
    details.columns.add(seqNumColumn);
    details.columns.add(opUUIDColumn);

    CreateTable.Params params = new CreateTable.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.tableType = TableType.PGSQL_TABLE_TYPE;
    params.tableName = details.tableName;
    params.tableDetails = details;
    params.ifNotExist = true;

    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    task.run();
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node;
    try {
      node = CommonUtils.getServerToRunYsqlQuery(universe, true);
    } catch (IllegalStateException e) {
      log.warn("Could not find valid tserver, skipping consistency check.");
      return;
    }
    ConsistencyInfoResp response;
    try {
      response = ysqlQueryExecutor.getConsistencyInfo(universe);
    } catch (RecoverableException e) {
      log.info("Creating consistency check table for the first time.");
      createConsistencyCheckTable();
      RunQueryFormData runQueryFormData = new RunQueryFormData();
      runQueryFormData.setQuery(
          String.format(
              "INSERT INTO %s (seq_num, task_uuid) VALUES (0, '%s')",
              CONSISTENCY_CHECK, getTaskUUID().toString()));
      runQueryFormData.setDbName(SYSTEM_PLATFORM_DB);
      JsonNode ysqlResponse =
          ysqlQueryExecutor.executeQueryInNodeShell(universe, runQueryFormData, node);
      if (ysqlResponse != null && ysqlResponse.has("error")) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, ysqlResponse.get("error").asText());
      }
      // Update local YBA sequence number with initial value
      updateUniverseSeqNum(0);
      return;
    }

    if (response != null) {
      int ybaSeqNum = universe.getUniverseDetails().sequenceNumber;
      int dbSeqNum = response.getSeqNum();
      // Accept whatever is in the DB
      if (ybaSeqNum == -1) {
        log.info("Accepting whatever sequence number found in YBDB.");
        updateUniverseSeqNum(dbSeqNum);
        return;
        // Stale
      } else if (dbSeqNum > ybaSeqNum) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Can not operate on universe with stale metadata.");
      } else {
        log.info("Validated YBA sequence number");
      }
    } else {
      log.warn(
          "Could not read consistency info, skipping comparison validation but proceeding with"
              + " update.");
    }

    // Update values
    int ybaSeqNumUpdate = universe.getUniverseDetails().sequenceNumber + 1;
    PendingConsistencyCheck pend = PendingConsistencyCheck.create(getTaskUUID(), universe);
    String taskUUIDString = getTaskUUID().toString();
    RunQueryFormData runQueryFormData = new RunQueryFormData();
    String updateQuery =
        String.format(
            "UPDATE %s SET seq_num = %d, task_uuid = '%s' WHERE seq_num < %d OR"
                + " (seq_num = %d AND task_uuid = '%s') RETURNING seq_num, task_uuid",
            CONSISTENCY_CHECK,
            ybaSeqNumUpdate,
            taskUUIDString,
            ybaSeqNumUpdate,
            ybaSeqNumUpdate,
            taskUUIDString);
    runQueryFormData.setQuery(
        String.format(
            "WITH updated_rows AS (%s) SELECT jsonb_agg(updated_rows) AS result FROM"
                + " updated_rows;",
            updateQuery));
    runQueryFormData.setDbName(SYSTEM_PLATFORM_DB);
    try {
      JsonNode ysqlResponse =
          ysqlQueryExecutor.executeQueryInNodeShell(universe, runQueryFormData, node);
      int retries = 0;
      while (ysqlResponse != null && ysqlResponse.has("error") && retries < 5) {
        waitFor(Duration.ofMillis(2500));
        retries += 1;
        ysqlResponse = ysqlQueryExecutor.executeQueryInNodeShell(universe, runQueryFormData, node);
      }
      if (ysqlResponse != null && ysqlResponse.has("result")) {
        ShellResponse shellResponse = ShellResponse.create(0, ysqlResponse.get("result").asText());
        ObjectMapper objectMapper = new ObjectMapper();
        JsonNode jsonNode =
            objectMapper.readTree(CommonUtils.extractJsonisedSqlResponse(shellResponse));
        if (jsonNode != null && jsonNode.get(0) != null && jsonNode.get(0).has("seq_num")) {
          // Update local YBA sequence number with update value
          updateUniverseSeqNum(jsonNode.get(0).get("seq_num").asInt());
        } else {
          // no rows updated, must be stale
          throw new PlatformServiceException(
              BAD_REQUEST,
              "No rows updated performing consistency check, potentially stale universe"
                  + " metadata.");
        }
      } else if (ysqlResponse != null && ysqlResponse.has("error")) {
        log.warn(
            "Consistency check is not active due to error: {}.",
            ysqlResponse.get("error").asText());
      }
    } catch (JsonProcessingException e) {
      log.warn("Error processing JSON response from update query: {}.", e.getMessage());
    } finally {
      pend.delete();
    }
  }

  private void updateUniverseSeqNum(int seqNum) {
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.sequenceNumber = seqNum;
          universe.setUniverseDetails(universeDetails);
        };
    saveUniverseDetails(updater);
  }
}
