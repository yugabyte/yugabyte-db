// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.time.Duration;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.client.IsXClusterBootstrapRequiredResponse;
import org.yb.client.XClusterAddNamespaceToOutboundReplicationGroupResponse;
import org.yb.client.YBClient;

@Slf4j
public class XClusterAddNamespaceToOutboundReplicationGroup extends XClusterConfigTaskBase {
  private static long DELAY_BETWEEN_RETRIES_MS = TimeUnit.SECONDS.toMillis(10);

  @Inject
  protected XClusterAddNamespaceToOutboundReplicationGroup(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  protected XClusterConfigTaskParams taskParams() {
    return (XClusterConfigTaskParams) taskParams;
  }

  @Override
  public void run() {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());

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

      for (String dbId : taskParams().getDbs()) {
        XClusterAddNamespaceToOutboundReplicationGroupResponse createResponse =
            client.xClusterAddNamespaceToOutboundReplicationGroup(
                xClusterConfig.getReplicationGroupName(), dbId);

        if (createResponse.hasError()) {
          throw new RuntimeException(
              String.format(
                  "AddNamespaceToOutboundReplicationGroup rpc failed with error: %s",
                  createResponse.errorMessage()));
        }
      }

      validateCheckpointingCompleted(client, sourceUniverse, xClusterConfig);

      log.debug(
          "Checkpointing for xClusterConfig {} completed for source db ids:",
          xClusterConfig.getUuid(),
          taskParams().getDbs());
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
  }

  protected void validateCheckpointingCompleted(
      YBClient client, Universe sourceUniverse, XClusterConfig xClusterConfig) throws Exception {
    Duration xclusterWaitTimeout =
        this.confGetter.getConfForScope(sourceUniverse, UniverseConfKeys.xclusterSetupAlterTimeout);
    for (String dbId : taskParams().getDbs()) {
      log.info(
          "Validating database {} for XClusterConfig({}) is done checkpointing",
          dbId,
          xClusterConfig.getUuid());
      boolean checkpointingCompleted =
          doWithConstTimeout(
              DELAY_BETWEEN_RETRIES_MS,
              xclusterWaitTimeout.toMillis(),
              () -> {
                IsXClusterBootstrapRequiredResponse completionResponse;

                try {
                  completionResponse =
                      client.isXClusterBootstrapRequired(
                          xClusterConfig.getReplicationGroupName(), dbId);
                } catch (Exception e) {
                  log.error(
                      "IsXClusterBootstrapRequired rpc for xClusterConfig: {}, db: {}, hit error:"
                          + " {}",
                      xClusterConfig.getName(),
                      dbId,
                      e.getMessage());
                  return false;
                }

                if (completionResponse.hasError()) {
                  log.error(
                      "IsXClusterBootstrapRequired rpc for xClusterConfig: {}, db: {}, hit error:"
                          + " {}",
                      xClusterConfig.getName(),
                      dbId,
                      completionResponse.errorMessage());
                  return false;
                }
                log.debug(
                    "Checkpointing status is complete: {}, for universe: {}, xClusterConfig: {},"
                        + " dbId: {}",
                    !completionResponse.isNotReady(),
                    sourceUniverse.getUniverseUUID(),
                    xClusterConfig.getName(),
                    dbId);
                return !completionResponse.isNotReady();
              });
      if (!checkpointingCompleted) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Checkpointing database %s for xClusterConfig %s with source universe %s timed out",
                dbId, xClusterConfig.getName(), sourceUniverse.getUniverseUUID()));
      }
    }
  }
}
