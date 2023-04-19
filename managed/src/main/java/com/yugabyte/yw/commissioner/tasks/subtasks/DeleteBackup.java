/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class DeleteBackup extends AbstractTaskBase {

  @Inject
  public DeleteBackup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends AbstractTaskParams {
    public UUID customerUUID;
    public UUID backupUUID;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Backup backup = Backup.get(params().customerUUID, params().backupUUID);
    if ((backup.getState() != Backup.BackupState.Completed)
        && (backup.getState() != Backup.BackupState.Failed)) {
      // TODO: Allow deletion of InProgress backups. But not sure if backend supports it
      //  and may not be worth the effort.
      log.error("Cannot delete backup in any other state other than completed or failed");
      return;
    }
    try {
      BackupTableParams backupParams = backup.getBackupInfo();
      List<BackupTableParams> backupList =
          backupParams.backupList == null
              ? ImmutableList.of(backupParams)
              : backupParams.backupList;
      if (deleteAllBackups(backupList)) {
        backup.delete();
        return;
      }
    } catch (Exception ex) {
      log.error(
          "Unexpected error in DeleteBackup {}. We will ignore the error and Mark the "
              + "backup as failed to be deleted and remove it from scheduled cleanup.",
          params().backupUUID,
          ex);
    }
    transitionState(backup, Backup.BackupState.FailedToDelete);
  }

  private static void transitionState(Backup backup, Backup.BackupState newState) {
    if (backup != null) {
      backup.transitionState(newState);
    }
  }

  private boolean deleteAllBackups(List<BackupTableParams> backupList) {
    boolean success = true;
    for (BackupTableParams childBackupParams : backupList) {
      if (!deleteBackup(childBackupParams)) {
        success = false;
      }
    }
    return success;
  }

  private boolean deleteBackup(BackupTableParams backupTableParams) {
    backupTableParams.actionType = BackupTableParams.ActionType.DELETE;
    ShellResponse response = tableManager.deleteBackup(backupTableParams).processErrors();
    JsonNode jsonNode = null;
    try {
      jsonNode = Json.parse(response.message);
    } catch (Exception e) {
      log.error(
          "Delete Backup failed for {}. Response code={}, Output={}.",
          backupTableParams.storageLocation,
          response.code,
          response.message);
      return false;
    }
    if (response.code != 0 || jsonNode.has("error")) {
      log.error(
          "Delete Backup failed for {}. Response code={}, hasError={}.",
          backupTableParams.storageLocation,
          response.code,
          jsonNode.has("error"));
      return false;
    } else {
      log.info("[" + getName() + "] STDOUT: " + response.message);
      return true;
    }
  }
}
