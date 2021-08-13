package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterReplicationTaskBase;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import com.yugabyte.yw.models.Universe;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.AlterXClusterReplicationResponse;
import org.yb.client.YBClient;

@Slf4j
public class AlterXClusterReplicationRemoveTables extends XClusterReplicationTaskBase {

  @Inject
  protected AlterXClusterReplicationRemoveTables(BaseTaskDependencies baseTaskDependencies) {
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
      List<AsyncReplicationRelationship> relationships =
          AsyncReplicationRelationship.getBetweenUniverses(
              taskParams().sourceUniverseUUID, targetUniverse.universeUUID);

      // Check if xCluster replication exists between source and target universe
      if (relationships.isEmpty()) {
        throw new IllegalArgumentException("No xCluster replication exists between universes.");
      }

      // Ensure there are tables to remove
      if (taskParams().sourceTableIdsToRemove.isEmpty()) {
        throw new IllegalArgumentException("No tables to remove from replication were specified");
      }

      // Ensure the tables we are removing are actually being replicated
      Set<String> replicatedSourceTableIds =
          relationships
              .stream()
              .map(relationship -> relationship.sourceTableID)
              .collect(Collectors.toSet());

      if (!replicatedSourceTableIds.containsAll(taskParams().sourceTableIdsToRemove)) {
        Set<String> unReplicatedSourceTableIds = new HashSet<>(taskParams().sourceTableIdsToRemove);
        unReplicatedSourceTableIds.removeAll(replicatedSourceTableIds);
        throw new IllegalArgumentException(
            "Source universe tables "
                + unReplicatedSourceTableIds
                + " that were specified to be removed from replication are not being replicated");
      }

      // Alter the xCluster replication (client is created with target universe as context)
      client = ybService.getClient(masterHostPorts, certificate);

      AlterXClusterReplicationResponse resp =
          client.alterXClusterReplicationRemoveTables(
              taskParams().sourceUniverseUUID, taskParams().sourceTableIdsToRemove);

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
