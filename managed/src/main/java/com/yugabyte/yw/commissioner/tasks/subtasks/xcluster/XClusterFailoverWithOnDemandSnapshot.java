// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.common.base.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.UnrecoverableException;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.IsXClusterFailoverDoneResponse;
import org.yb.client.XClusterFailoverResponse;
import org.yb.client.YBClient;

/**
 * Subtask that initiates the DB-side XClusterFailover RPC and polls IsXClusterFailoverDone until
 * the failover completes or fails.
 *
 * <p>The DB master handles the full failover lifecycle: pausing replication, creating on-demand
 * snapshots, restoring to safe time, deleting the replication group, and cleaning up snapshots.
 */
@Slf4j
public class XClusterFailoverWithOnDemandSnapshot extends XClusterConfigTaskBase {

  private final YbClientConfigFactory ybClientConfigFactory;

  @Inject
  protected XClusterFailoverWithOnDemandSnapshot(
      BaseTaskDependencies baseTaskDependencies,
      XClusterUniverseService xClusterUniverseService,
      YbClientConfigFactory ybClientConfigFactory) {
    super(baseTaskDependencies, xClusterUniverseService);
    this.ybClientConfigFactory = ybClientConfigFactory;
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    public long pollDelayMs = 2000;
    public long pollTimeoutMs = 900_000; // 15 minutes
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s)", super.getName(), taskParams().getXClusterConfig());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    String replicationGroupName = xClusterConfig.getReplicationGroupName();

    boolean skipPolling = false;
    try {
      xClusterFailover(targetUniverse, replicationGroupName);
      log.info(
          "XClusterFailover RPC initiated for replication group {}, polling for completion",
          replicationGroupName);
    } catch (Exception initiateError) {
      // Initiation failed, check if a previous attempt already completed or is still in
      // progress. This handles the case where YBA restarted after the DB-side failover was
      // already triggered.
      log.warn(
          "XClusterFailover initiation failed for replication group {}: {}",
          replicationGroupName,
          initiateError.getMessage());
      skipPolling =
          handleFailoverInitiationError(replicationGroupName, targetUniverse, initiateError);
    }

    if (!skipPolling) {
      pollForCompletion(replicationGroupName, targetUniverse);
    }

    log.info("Completed {}", getName());
  }

  /**
   * When the XClusterFailover RPC fails, checks the DB-side failover status to decide whether to
   * poll, skip, or fail. This makes the subtask idempotent across YBA restarts.
   *
   * @return true if polling should be skipped (failover already completed), false otherwise
   */
  private boolean handleFailoverInitiationError(
      String replicationGroupName, Universe targetUniverse, Exception initiateError) {
    try {
      IsXClusterFailoverDoneResponse resp =
          isXClusterFailoverDone(targetUniverse, replicationGroupName);
      if (resp.isDone() && !resp.hasFailoverError()) {
        log.info(
            "xCluster failover already completed for replication group {}, skipping polling",
            replicationGroupName);
        return true;
      }
      if (!resp.isDone()) {
        log.info(
            "xCluster failover already in progress for replication group {}, resuming polling",
            replicationGroupName);
        return false;
      }
      // Previous attempt failed on the DB side and re-initiation also failed.
      throw new UnrecoverableException(
          String.format(
              "Previous xCluster failover for replication group %s failed (%s) "
                  + "and re-initiation also failed (%s)",
              replicationGroupName, resp.failoverErrorMessage(), initiateError.getMessage()));
    } catch (UnrecoverableException ue) {
      throw ue;
    } catch (Exception statusError) {
      log.error(
          "IsXClusterFailoverDone also failed for replication group {}: {}",
          replicationGroupName,
          statusError.getMessage());
      RuntimeException re =
          new RuntimeException(
              String.format(
                  "Failed to initiate xCluster failover for replication group %s and failed to"
                      + " query failover status",
                  replicationGroupName),
              initiateError);
      re.addSuppressed(statusError);
      throw re;
    }
  }

  private void pollForCompletion(String replicationGroupName, Universe targetUniverse) {
    boolean done =
        doWithConstTimeout(
            taskParams().pollDelayMs,
            taskParams().pollTimeoutMs,
            () -> {
              IsXClusterFailoverDoneResponse resp;
              try {
                resp = isXClusterFailoverDone(targetUniverse, replicationGroupName);
              } catch (Exception e) {
                // Transient errors (master leader change, network blip) are treated as
                // "not done" so that polling continues instead of failing the task.
                log.warn(
                    "Transient error polling xCluster failover status for {}: {}",
                    replicationGroupName,
                    e.getMessage());
                return false;
              }
              if (!resp.isDone()) {
                return false;
              }
              if (resp.hasFailoverError()) {
                throw new UnrecoverableException(
                    String.format(
                        "xCluster failover for replication group %s failed: %s",
                        replicationGroupName, resp.failoverErrorMessage()));
              }
              return true;
            });
    if (!done) {
      throw new RuntimeException(
          String.format(
              "Timed out waiting for xCluster failover to complete for replication group %s",
              replicationGroupName));
    }
    log.info(
        "xCluster failover completed successfully for replication group {}", replicationGroupName);
  }

  private void xClusterFailover(Universe targetUniverse, String replicationGroupName) {
    log.info(
        "Initiating xCluster failover for replication group {} on universe {}",
        replicationGroupName,
        targetUniverse.getUniverseUUID());
    YbClientConfig clientConfig =
        ybClientConfigFactory.create(
            targetUniverse.getMasterAddresses(), targetUniverse.getCertificateNodetoNode());
    try (YBClient client = ybService.getClientWithConfig(clientConfig)) {
      XClusterFailoverResponse resp = client.xClusterFailover(replicationGroupName);
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "XClusterFailover RPC call failed for replication group %s: %s",
                replicationGroupName, resp.errorMessage()));
      }
    } catch (Exception e) {
      log.error(
          "xClusterFailover hit error for replication group {}: {}",
          replicationGroupName,
          e.getMessage());
      Throwables.throwIfUnchecked(e);
      throw new RuntimeException(e);
    }
  }

  private IsXClusterFailoverDoneResponse isXClusterFailoverDone(
      Universe targetUniverse, String replicationGroupName) {
    log.debug(
        "Polling xCluster failover status for replication group {} on universe {}",
        replicationGroupName,
        targetUniverse.getUniverseUUID());
    YbClientConfig clientConfig =
        ybClientConfigFactory.create(
            targetUniverse.getMasterAddresses(), targetUniverse.getCertificateNodetoNode());
    try (YBClient client = ybService.getClientWithConfig(clientConfig)) {
      IsXClusterFailoverDoneResponse resp = client.isXClusterFailoverDone(replicationGroupName);
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "IsXClusterFailoverDone RPC call failed for replication group %s: %s",
                replicationGroupName, resp.errorMessage()));
      }
      return resp;
    } catch (Exception e) {
      log.error(
          "isXClusterFailoverDone hit error for replication group {}: {}",
          replicationGroupName,
          e.getMessage());
      Throwables.throwIfUnchecked(e);
      throw new RuntimeException(e);
    }
  }
}
