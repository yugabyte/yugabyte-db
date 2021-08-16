package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterReplicationTaskBase;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.AlterXClusterReplicationResponse;
import org.yb.client.YBClient;

@Slf4j
public class AlterXClusterReplicationAddTables extends XClusterReplicationTaskBase {

  @Inject
  protected AlterXClusterReplicationAddTables(BaseTaskDependencies baseTaskDependencies) {
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
      // Check if xCluster replication exists between source and target universe
      if (AsyncReplicationRelationship.getBetweenUniverses(
              taskParams().sourceUniverseUUID, targetUniverse.universeUUID)
          .isEmpty()) {
        throw new IllegalArgumentException("No xCluster replication exists between universes.");
      }

      // Ensure there are tables to add
      if (taskParams().sourceTableIdsToAdd.isEmpty()) {
        throw new IllegalArgumentException("No tables to add were specified");
      }

      // Alter the xCluster replication (client is created with target universe as context)
      client = ybService.getClient(masterHostPorts, certificate);

      AlterXClusterReplicationResponse resp =
          client.alterXClusterReplicationAddTables(
              taskParams().sourceUniverseUUID, taskParams().sourceTableIdsToAdd);

      if (resp.hasError()) {
        throw new RuntimeException(resp.errorMessage());
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, masterHostPorts);
    }
  }
}
