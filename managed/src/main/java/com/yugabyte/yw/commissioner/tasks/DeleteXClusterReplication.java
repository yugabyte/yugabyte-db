package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.DeleteXClusterReplicationResponse;
import org.yb.client.YBClient;

@Slf4j
public class DeleteXClusterReplication extends XClusterReplicationTaskBase {

  @Inject
  protected DeleteXClusterReplication(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Universe targetUniverse = Universe.getOrBadRequest(taskParams().targetUniverseUUID);

    String masterHostPorts = targetUniverse.getMasterAddresses();
    String certificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = null;

    try {
      // Update the universe DB with the update to be performed and set the
      // 'updateInProgress' flag to prevent other updates from happening.
      lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Check if xCluster replication exists between source and target universe
      if (AsyncReplicationRelationship.getBetweenUniverses(
              taskParams().sourceUniverseUUID, targetUniverse.universeUUID)
          .isEmpty()) {
        throw new IllegalArgumentException("No xCluster replication exists between universes.");
      }

      // Delete the xCluster replication (client is created with target universe as context)
      client = ybService.getClient(masterHostPorts, certificate);

      DeleteXClusterReplicationResponse resp =
          client.deleteXClusterReplication(taskParams().sourceUniverseUUID);

      if (resp.hasError()) {
        throw new RuntimeException(resp.errorMessage());
      }

      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Sync DB with platform xCluster replication state
      createAsyncReplicationPlatformSyncTask()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      // Reset universe version to trigger auto sync
      createResetUniverseVersionTask()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      subTaskGroupQueue.run();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, masterHostPorts);

      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
  }
}
