package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PauseOrResumeXClusterReplication extends XClusterReplicationTaskBase {

  @Inject
  protected PauseOrResumeXClusterReplication(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    try {
      checkUniverseVersion();

      // Update the target universe DB with the update to be performed and set the
      // 'updateInProgress' flag to prevent other updates from happening.
      lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      List<AsyncReplicationRelationship> relationships =
          AsyncReplicationRelationship.getBetweenUniverses(
              taskParams().sourceUniverseUUID, taskParams().targetUniverseUUID);

      // Check if xCluster replication exists between universes
      if (relationships.isEmpty()) {
        throw new IllegalArgumentException("No xCluster replication exists between universes.");
      }

      // Check if xCluster replication is already paused/active
      if (relationships.get(0).active != taskParams().active) {
        // Create the task list sequence.
        subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

        if (taskParams().active) {
          createResumeXClusterReplicationTask()
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        } else {
          createPauseXClusterReplicationTask()
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }

        // Sync DB with platform xCluster replication state
        createAsyncReplicationPlatformSyncTask()
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        // Reset universe version to trigger auto sync
        createResetUniverseVersionTask()
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        subTaskGroupQueue.run();
      } else {
        log.info("xCluster replication is already " + (taskParams().active ? "active" : "paused"));
      }
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
