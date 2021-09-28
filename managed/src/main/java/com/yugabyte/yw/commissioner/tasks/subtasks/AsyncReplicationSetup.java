// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterReplicationTaskBase;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import com.yugabyte.yw.models.Universe;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.client.CreateXClusterReplicationResponse;
import org.yb.client.IsCreateXClusterReplicationDoneResponse;
import org.yb.client.YBClient;
import org.yb.util.NetUtil;

@Slf4j
public class AsyncReplicationSetup extends XClusterReplicationTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AsyncReplicationSetup.class);

  private static final int POLL_TIMEOUT_SECONDS = 30;

  @Inject
  protected AsyncReplicationSetup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
  }

  @Override
  public void run() {
    LOG.info("Running {}", getName());

    Universe sourceUniverse = Universe.getOrBadRequest(taskParams().sourceUniverseUUID);
    Universe targetUniverse = Universe.getOrBadRequest(taskParams().targetUniverseUUID);

    // Create the xCluster replication client (with target universe as context)
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate);

    ScheduledExecutorService scheduledExecutor = Executors.newSingleThreadScheduledExecutor();

    try {
      // Ensure universe version matches expected version (for HA platform setup)
      checkUniverseVersion();

      // Check if xCluster replication is already enabled between these source and target universes
      if (!AsyncReplicationRelationship.getBetweenUniverses(
              sourceUniverse.universeUUID, targetUniverse.universeUUID)
          .isEmpty()) {
        throw new IllegalArgumentException(
            "xCluster replication already exists between universes."
                + " Alter the replication to add new tables instead");
      }

      // Submit request to create xCluster configuration
      CreateXClusterReplicationResponse resp =
          client.createXClusterReplication(
              taskParams().sourceUniverseUUID,
              taskParams().sourceTableIDs,
              NetUtil.parseStringsAsPB(sourceUniverse.getMasterAddresses()),
              taskParams().bootstrapIDs);
      if (resp.hasError()) {
        throw new RuntimeException(
            "Failed to create xCluster replication configuration: " + resp.errorMessage());
      }

      // Wait for setup xCluster replication to complete
      IsCreateXClusterReplicationDoneResponse doneResponse = null;
      long startTime = System.currentTimeMillis();
      long currentTime = System.currentTimeMillis();
      int numAttempts = 1;
      while (((currentTime - startTime) / 1000) < POLL_TIMEOUT_SECONDS) {
        log.info("Wait for xCluster replication setup to complete (attempt " + numAttempts + ")");

        doneResponse = client.isCreateXClusterReplicationDone(taskParams().sourceUniverseUUID);
        if (doneResponse.isDone()) {
          break;
        }
        if (doneResponse.hasError()) {
          log.warn(
              "Failed to make RPC call (IsSetupUniverseReplicationDoneRequest): "
                  + doneResponse.getError().toString());
        }

        Thread.sleep(getSleepMultiplier() * 1000);
        currentTime = System.currentTimeMillis();
        numAttempts++;
      }

      if (doneResponse == null) {
        // This shouldn't happen
        throw new RuntimeException(
            "Never got a response waiting for xCluster replication setup to complete");
      }
      if (!doneResponse.isDone()) {
        throw new RuntimeException("Timed out waiting for xCluster replication setup to complete");
      }
      if (doneResponse.hasReplicationError()
          && doneResponse.getReplicationError().getCode() != ErrorCode.OK) {
        throw new RuntimeException(
            "Failed to create xCluster replication configuration: "
                + doneResponse.getReplicationError().toString());
      }

    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, targetUniverse.getMasterAddresses());
      scheduledExecutor.shutdown();
    }
  }
}
