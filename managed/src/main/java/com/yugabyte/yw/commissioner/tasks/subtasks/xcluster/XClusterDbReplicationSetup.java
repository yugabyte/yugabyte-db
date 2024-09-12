// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.UnrecoverableException;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.time.Duration;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonNet;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.client.CreateXClusterReplicationResponse;
import org.yb.client.IsCreateXClusterReplicationDoneResponse;
import org.yb.client.YBClient;
import org.yb.util.NetUtil;

@Slf4j
public class XClusterDbReplicationSetup extends XClusterConfigTaskBase {

  private static final long DELAY_BETWEEN_RETRIES_MS = TimeUnit.SECONDS.toMillis(10);

  @Inject
  protected XClusterDbReplicationSetup(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    Duration xClusterWaitTimeout =
        this.confGetter.getConfForScope(sourceUniverse, UniverseConfKeys.xclusterSetupAlterTimeout);
    try (YBClient client =
        ybService.getClient(
            sourceUniverse.getMasterAddresses(), sourceUniverse.getCertificateNodetoNode())) {
      Set<CommonNet.HostPortPB> targetMasterAddresses =
          new HashSet<>(
              NetUtil.parseStringsAsPB(
                  targetUniverse.getMasterAddresses(
                      false /* mastersQueryable */, true /* getSecondary */)));
      try {
        waitForCreateXClusterReplicationDone(
            client,
            xClusterConfig.getReplicationGroupName(),
            targetMasterAddresses,
            xClusterWaitTimeout.toMillis());
        log.info("Skipping {}: XCluster db replication setup already done", getName());
        return;
      } catch (Exception ignored) {
        // Ignore the exception and continue with the setup because the replication group is not
        // set up yet.
      }

      CreateXClusterReplicationResponse createResponse =
          client.createXClusterReplication(
              xClusterConfig.getReplicationGroupName(), targetMasterAddresses);
      if (createResponse.hasError()) {
        throw new RuntimeException(
            String.format(
                "CreateXClusterReplicationResponse rpc failed for xClusterConfig, %s, with error"
                    + " message: %s",
                xClusterConfig.getUuid(), createResponse.errorMessage()));
      }
      waitForCreateXClusterReplicationDone(
          client,
          xClusterConfig.getReplicationGroupName(),
          targetMasterAddresses,
          xClusterWaitTimeout.toMillis());
      log.debug(
          "XCluster db replication setup complete for xClusterConfig: {}",
          xClusterConfig.getUuid());
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
  }

  protected void waitForCreateXClusterReplicationDone(
      YBClient client,
      String replicationGroupName,
      Set<CommonNet.HostPortPB> targetMasterAddresses,
      long xClusterWaitTimeoutMs) {
    doWithConstTimeout(
        DELAY_BETWEEN_RETRIES_MS,
        xClusterWaitTimeoutMs,
        () -> {
          try {
            IsCreateXClusterReplicationDoneResponse doneResponse =
                client.isCreateXClusterReplicationDone(replicationGroupName, targetMasterAddresses);
            if (doneResponse.hasError()) {
              throw new RuntimeException(
                  String.format(
                      "IsCreateXClusterReplicationDone rpc for replication group name: %s, hit"
                          + " error: %s",
                      replicationGroupName, doneResponse.errorMessage()));
            }
            if (doneResponse.hasReplicationError()
                && !doneResponse.getReplicationError().getCode().equals(ErrorCode.OK)) {
              throw new RuntimeException(
                  String.format(
                      "IsCreateXClusterReplicationDone rpc for replication group name: %s, hit"
                          + " replication error: %s with error code: %s",
                      replicationGroupName,
                      doneResponse.getReplicationError().getMessage(),
                      doneResponse.getReplicationError().getCode()));
            }
            if (!doneResponse.isDone()) {
              throw new RuntimeException(
                  String.format(
                      "CreateXClusterReplication is not done for replication group name: %s",
                      replicationGroupName));
            }
            // Replication setup is complete, return.
          } catch (Exception e) {
            // If the replication group is not found, retrying does not help.
            if (e.getMessage().toLowerCase().contains("not found")) {
              log.error(
                  "Replication group {} not found, aborting wait for replication drain : {}",
                  replicationGroupName,
                  e);
              throw new UnrecoverableException(e.getMessage());
            }
            log.error(
                "IsCreateXClusterReplicationDone rpc for replication group name: {}, hit error: {}",
                replicationGroupName,
                e);
            throw new RuntimeException(e);
          }
        });
  }
}
