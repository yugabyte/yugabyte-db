// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.time.Duration;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.client.IsXClusterBootstrapRequiredResponse;
import org.yb.client.XClusterCreateOutboundReplicationGroupResponse;
import org.yb.client.YBClient;

@Slf4j
public class CreateOutboundReplicationGroup extends XClusterConfigTaskBase {
  private static final long DELAY_BETWEEN_RETRIES_MS = TimeUnit.SECONDS.toMillis(10);

  @Inject
  protected CreateOutboundReplicationGroup(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Duration xclusterWaitTimeout =
        this.confGetter.getConfForScope(sourceUniverse, UniverseConfKeys.xclusterSetupAlterTimeout);

    if (CollectionUtils.isEmpty(taskParams().getDbs())) {
      throw new RuntimeException(
          String.format(
              "`dbs` in the task parameters must not be null or empty: it was %s",
              taskParams().getDbs()));
    }

    try (YBClient client =
        ybService.getClient(
            sourceUniverse.getMasterAddresses(), sourceUniverse.getCertificateNodetoNode())) {
      log.info(
          "Checkpointing databases for XClusterConfig({}): source db ids: {}",
          xClusterConfig.getUuid(),
          taskParams().getDbs());

      try {
        client.getXClusterOutboundReplicationGroupInfo(xClusterConfig.getReplicationGroupName());
        log.info(
            "Skipping {}: OutboundReplicationGroup for xCluster {} already exists; skip creating"
                + " it, and only wait for checkpointing to be ready",
            getName(),
            xClusterConfig);
        waitForCheckpointingToBeReady(
            client, xClusterConfig, taskParams().dbs, xclusterWaitTimeout.toMillis());
        return;
      } catch (Exception ignored) {
        // The outbound replication group does not exist, proceed to create it.
      }

      XClusterCreateOutboundReplicationGroupResponse createResponse =
          client.xClusterCreateOutboundReplicationGroup(
              xClusterConfig.getReplicationGroupName(), taskParams().getDbs());
      if (createResponse.hasError()) {
        throw new RuntimeException(
            String.format(
                "CreateOutboundReplicationGroup rpc failed with error: %s",
                createResponse.errorMessage()));
      }
      waitForCheckpointingToBeReady(
          client, xClusterConfig, taskParams().dbs, xclusterWaitTimeout.toMillis());
      log.debug(
          "Checkpointing for xClusterConfig {} completed for source db ids: {}",
          xClusterConfig.getUuid(),
          taskParams().getDbs());
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }

    log.info("Completed {}", getName());
  }

  protected void waitForCheckpointingToBeReady(
      YBClient client,
      XClusterConfig xClusterConfig,
      Set<String> dbIds,
      long xClusterWaitTimeoutMs) {

    for (String dbId : dbIds) {
      log.info(
          "Validating database {} for XClusterConfig({}) is done checkpointing",
          dbId,
          xClusterConfig.getUuid());
      doWithConstTimeout(
          DELAY_BETWEEN_RETRIES_MS,
          xClusterWaitTimeoutMs,
          () -> {
            try {
              IsXClusterBootstrapRequiredResponse completionResponse =
                  client.isXClusterBootstrapRequired(
                      xClusterConfig.getReplicationGroupName(), dbId);
              if (completionResponse.hasError()) {
                throw new RuntimeException(
                    "IsXClusterBootstrapRequired rpc for xClusterConfig: "
                        + xClusterConfig.getName()
                        + ", db: "
                        + dbId
                        + ", hit error: "
                        + completionResponse.errorMessage());
              }
              if (completionResponse.isNotReady()) {
                throw new RuntimeException(
                    String.format(
                        "Checkpointing database %s for xClusterConfig %s is not ready",
                        dbId, xClusterConfig.getName()));
              }
              log.info(
                  "Checkpointing for xClusterConfig {} is ready for source db id: {}",
                  xClusterConfig.getUuid(),
                  dbId);
              // Checkpointing is complete, return.
            } catch (Exception e) {
              log.error(
                  "IsXClusterBootstrapRequired failed for xClusterConfig: {}, db: {}:" + " {}",
                  xClusterConfig.getName(),
                  dbId,
                  e.getMessage());
              throw new RuntimeException(e);
            }
          });
    }
  }
}
