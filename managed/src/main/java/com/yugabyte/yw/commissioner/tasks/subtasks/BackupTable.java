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

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

import javax.inject.Inject;
import java.util.Map;

@Slf4j
public class BackupTable extends AbstractTaskBase {

  @Inject
  public BackupTable(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected BackupTableParams taskParams() {
    return (BackupTableParams) taskParams;
  }

  @Override
  public void run() {
    Backup backup = taskParams().backup;
    if (backup == null) {
      backup = Backup.fetchByTaskUUID(userTaskUUID);
    }

    try {
      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      Map<String, String> config = universe.getConfig();
      if (config.isEmpty() || config.getOrDefault(Universe.TAKE_BACKUPS, "true").equals("true")) {
        if (taskParams().backupList != null) {
          for (BackupTableParams backupParams : taskParams().backupList) {
            ShellResponse response = tableManager.createBackup(backupParams);
            JsonNode jsonNode = Json.parse(response.message);
            if (response.code != 0 || jsonNode.has("error")) {
              log.error("Response code={}, hasError={}.", response.code, jsonNode.has("error"));

              throw new RuntimeException(response.message);
            } else {
              log.info("[" + getName() + "] STDOUT: " + response.message);
            }
          }

          backup.transitionState(Backup.BackupState.Completed);
        } else {
          ShellResponse response = tableManager.createBackup(taskParams());
          JsonNode jsonNode = Json.parse(response.message);
          if (response.code != 0 || jsonNode.has("error")) {
            log.error("Response code={}, hasError={}.", response.code, jsonNode.has("error"));
            backup.transitionState(Backup.BackupState.Failed);
            throw new RuntimeException(response.message);
          } else {
            log.info("[" + getName() + "] STDOUT: " + response.message);
            backup.transitionState(Backup.BackupState.Completed);
          }
        }
      } else {
        log.info("Skipping table {}:{}", taskParams().getKeyspace(), taskParams().getTableName());
        backup.transitionState(Backup.BackupState.Skipped);
      }
    } catch (Exception e) {
      log.error("Errored out with: " + e);
      backup.transitionState(Backup.BackupState.Failed);
      throw new RuntimeException(e);
    }
  }
}
