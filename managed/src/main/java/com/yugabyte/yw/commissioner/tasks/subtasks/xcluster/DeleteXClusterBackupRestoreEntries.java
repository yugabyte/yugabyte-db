// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Restore;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteXClusterBackupRestoreEntries extends XClusterConfigTaskBase {

  @Inject
  protected DeleteXClusterBackupRestoreEntries(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    public UUID backupUUID;
    public UUID restoreUUID;
    public UUID customerUUID;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(backupUUID=%s,restoreUUID=%s,customerUUID=%s)",
        super.getName(),
        taskParams().backupUUID,
        taskParams().restoreUUID,
        taskParams().customerUUID);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Optional<Backup> optionalBackup =
        Backup.maybeGet(taskParams().customerUUID, taskParams().backupUUID);
    Optional<Restore> optionalRestore = Restore.fetchRestore(taskParams().restoreUUID);

    if (optionalBackup.isPresent()
        && optionalBackup.get().getState() == Backup.BackupState.InProgress) {
      log.debug("Deleting backup: {}", taskParams().backupUUID);
      optionalBackup.get().delete();
    }

    if (optionalRestore.isPresent()
        && optionalRestore.get().getState() == Restore.State.InProgress) {
      log.debug("Deleting restore: {}", taskParams().restoreUUID);
      optionalRestore.get().delete();
    }

    log.info("Completed {}", getName());
  }
}
