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
import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Universe;
import java.util.List;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import play.libs.Json;

@Slf4j
public class BackupTableYb extends AbstractTaskBase {

  @Inject
  public BackupTableYb(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected BackupTableParams taskParams() {
    return (BackupTableParams) taskParams;
  }

  @Override
  public void run() {
    Backup backup;
    backup = Backup.get(taskParams().customerUuid, taskParams().backupUuid);

    try {
      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      Map<String, String> config = universe.getConfig();
      if (config.isEmpty() || config.getOrDefault(Universe.TAKE_BACKUPS, "true").equals("true")) {
        long totalBackupSize = 0L;
        int backupIdx = 0;
        for (BackupTableParams backupParams : taskParams().backupList) {
          backupParams.backupUuid = taskParams().backupUuid;
          ShellResponse response = tableManagerYb.createBackup(backupParams).processErrors();
          JsonNode jsonNode = null;
          try {
            jsonNode = Json.parse(response.message);
          } catch (Exception e) {
            log.error("Response code={}, output={}.", response.code, response.message);
            throw e;
          }
          if (response.code != 0 || jsonNode.has("error")) {
            log.error("Response code={}, hasError={}.", response.code, jsonNode.has("error"));
            throw new RuntimeException(response.message);
          }

          log.info("[" + getName() + "] STDOUT: " + response.message);
          long backupSize = BackupUtil.extractBackupSize(jsonNode);
          List<BackupUtil.RegionLocations> locations =
              BackupUtil.extractPerRegionLocationsFromBackupScriptResponse(jsonNode);
          if (CollectionUtils.isNotEmpty(locations)) {
            backup.setPerRegionLocations(backupIdx, locations);
          }
          backup.setBackupSizeInBackupList(backupIdx, backupSize);
          totalBackupSize += backupSize;
          backupIdx++;
        }
        backup.setCompletionTime(backup.getUpdateTime());
        backup.setTotalBackupSize(totalBackupSize);
        backup.transitionState(Backup.BackupState.Completed);
      } else {
        backup.transitionState(Backup.BackupState.Skipped);
      }
    } catch (Exception e) {
      log.error("Errored out with: " + e);
      backup.transitionState(Backup.BackupState.Failed);
      // Do not lose the actual exception.
      Throwables.propagate(e);
    }
  }
}
