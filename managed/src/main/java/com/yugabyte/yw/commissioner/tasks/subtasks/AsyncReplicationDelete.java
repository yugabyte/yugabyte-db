// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterReplicationTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.DeleteXClusterReplicationResponse;
import org.yb.client.YBClient;
import play.api.Play;

@Slf4j
public class AsyncReplicationDelete extends XClusterReplicationTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AsyncReplicationDelete.class);

  @Inject
  protected AsyncReplicationDelete(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
  }

  @Override
  public void run() {
    LOG.info("Running {}", getName());

    Universe targetUniverse = Universe.getOrBadRequest(taskParams().targetUniverseUUID);

    // Create the xCluster replication client (with target universe as context)
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate);

    try {
      // Ensure universe version matches expected version (for HA platform setup)
      checkUniverseVersion();

      // Check if xCluster replication exists between source and target universe
      if (AsyncReplicationRelationship.getBetweenUniverses(
              taskParams().sourceUniverseUUID, targetUniverse.universeUUID)
          .isEmpty()) {
        throw new IllegalArgumentException("No xCluster replication exists between universes.");
      }

      // Delete the xCluster replication (client is created with target universe as context)
      DeleteXClusterReplicationResponse resp =
          client.deleteXClusterReplication(taskParams().sourceUniverseUUID);
      if (resp.hasError()) {
        throw new RuntimeException(
            "Failed to delete xCluster replication configuration: " + resp.errorMessage());
      }

    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, targetUniverse.getMasterAddresses());
    }
  }
}
