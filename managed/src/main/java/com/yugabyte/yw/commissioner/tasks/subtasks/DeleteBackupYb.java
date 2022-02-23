package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteBackupYb extends AbstractTaskBase {
  @Inject
  public DeleteBackupYb(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Inject private CustomerConfigService customerConfigService;

  public static class Params extends AbstractTaskParams {
    public UUID customerUUID;
    public UUID backupUUID;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Backup backup = Backup.getOrBadRequest(params().customerUUID, params().backupUUID);
    if (Backup.IN_PROGRESS_STATES.contains(backup.state)) {
      log.error("Cannot delete backup that are in {} state", backup.state);
      return;
    }
    try {
      // TODO: Validate whether storage config has the permission to delete the backup
      // Currently, if storage config does not have right permisssion then during backupGC
      // backup will be moved to "FailedToDelete" state.
      customerConfigService.getOrBadRequest(
          params().customerUUID, backup.getBackupInfo().storageConfigUUID);
      backup.transitionState(BackupState.QueuedForDeletion);
    } catch (Exception e) {
      log.error("Errored out with: " + e);
      backup.transitionState(BackupState.FailedToDelete);
      throw new RuntimeException(e.getMessage());
    }
  }
}
