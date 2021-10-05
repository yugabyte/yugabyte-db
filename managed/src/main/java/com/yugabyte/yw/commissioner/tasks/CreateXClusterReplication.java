package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CreateXClusterReplication extends XClusterReplicationTaskBase {

  @Inject
  protected CreateXClusterReplication(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    try {
      // Update the target universe DB with the update to be performed and set the
      // 'updateInProgress' flag to prevent other updates from happening.
      lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Setup xCluster relationship between databases
      createAsyncReplicationSetupTask()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      // Sync DB with platform xCluster replication state
      createAsyncReplicationPlatformSyncTask()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      // Reset universe version to trigger auto sync
      createResetUniverseVersionTask()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      subTaskGroupQueue.run();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
  }
}
