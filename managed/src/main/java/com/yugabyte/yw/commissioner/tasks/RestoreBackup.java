package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.ActionType;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RestoreBackup extends UniverseTaskBase {

  @Inject
  protected RestoreBackup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected RestoreBackupParams taskParams() {
    return (RestoreBackupParams) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    try {
      checkUniverseVersion();
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      lockUniverse(-1 /* expectedUniverseVersion */);

      if (universe.getUniverseDetails().backupInProgress) {
        throw new RuntimeException("A backup for this universe is already in progress.");
      }

      createAllRestoreSubtasks(taskParams(), UserTaskDetails.SubTaskGroupType.RestoringBackup);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {

      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      if (taskParams().alterLoadBalancer) {
        // Clear previous tasks if any.
        getRunnableTask().reset();
        // If the task failed, we don't want the loadbalancer to be
        // disabled, so we enable it again in case of errors.
        createLoadBalancerStateChangeTask(true)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        getRunnableTask().runSubTasks();
      }
      throw t;
    } finally {
      // Run an unlock in case the task failed before getting to the unlock. It is okay if it
      // errors out.
      unlockUniverseForUpdate();
    }

    log.info("Finished {} task.", getName());
  }

  private RestoreBackupParams createParamsBody(
      RestoreBackupParams params, BackupStorageInfo backupStorageInfo, ActionType actionType) {
    RestoreBackupParams restoreParams = new RestoreBackupParams();
    restoreParams.customerUUID = params.customerUUID;
    restoreParams.universeUUID = params.universeUUID;
    restoreParams.storageConfigUUID = params.storageConfigUUID;
    restoreParams.restoreTimeStamp = params.restoreTimeStamp;
    restoreParams.kmsConfigUUID = params.kmsConfigUUID;
    restoreParams.backupStorageInfoList = new ArrayList<>();
    restoreParams.actionType = actionType;
    restoreParams.backupStorageInfoList.add(backupStorageInfo);
    restoreParams.disableChecksum = params.disableChecksum;
    restoreParams.useTablespaces = params.useTablespaces;

    return restoreParams;
  }
}
