// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Backup;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class YBCBackupSucceeded extends AbstractTaskBase {

  @Inject
  public YBCBackupSucceeded(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends AbstractTaskParams {
    public UUID customerUUID;
    public UUID backupUUID;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    try {
      Backup backup = Backup.getOrBadRequest(taskParams().customerUUID, taskParams().backupUUID);
      if (backup.getCategory().equals(Backup.BackupCategory.YB_CONTROLLER)) {
        backup.onCompletion();
        if (!backup.getBaseBackupUUID().equals(backup.getBackupUUID())) {
          Backup baseBackup =
              Backup.getOrBadRequest(taskParams().customerUUID, backup.getBaseBackupUUID());
          // Refresh backup object to get the updated completed time.
          backup.refresh();
          baseBackup.onIncrementCompletion(backup.getCreateTime());
        }
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
    log.info("Completed {}", getName());
  }
}
