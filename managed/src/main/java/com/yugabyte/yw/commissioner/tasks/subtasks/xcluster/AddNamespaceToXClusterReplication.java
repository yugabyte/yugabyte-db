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
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.CommonNet;
import org.yb.client.AddNamespaceToXClusterReplicationResponse;
import org.yb.client.IsAlterXClusterReplicationDoneResponse;
import org.yb.client.YBClient;
import org.yb.util.NetUtil;

@Slf4j
public class AddNamespaceToXClusterReplication extends XClusterConfigTaskBase {
  private static long DELAY_BETWEEN_RETRIES_MS = TimeUnit.SECONDS.toMillis(10);

  @Inject
  protected AddNamespaceToXClusterReplication(
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
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());

    if (CollectionUtils.isEmpty(taskParams().getDbs())) {
      throw new RuntimeException(
          String.format(
              "`dbs` in the task parameters must not be null or empty: it was %s",
              taskParams().getDbs()));
    }

    try (YBClient client =
        ybService.getClient(
            sourceUniverse.getMasterAddresses(), sourceUniverse.getCertificateNodetoNode())) {
      Set<CommonNet.HostPortPB> targetMasterAddresses =
          new HashSet<>(
              NetUtil.parseStringsAsPB(
                  targetUniverse.getMasterAddresses(
                      false /* mastersQueryable */, true /* getSecondary */)));
      log.info(
          "Adding databases to replication for XClusterConfig({}): source db ids: {}",
          xClusterConfig.getUuid(),
          taskParams().getDbs());

      for (String dbId : taskParams().getDbs()) {
        AddNamespaceToXClusterReplicationResponse createResponse =
            client.addNamespaceToXClusterReplication(
                xClusterConfig.getReplicationGroupName(), targetMasterAddresses, dbId);

        if (createResponse.hasError()) {
          throw new RuntimeException(
              String.format(
                  "AddNamespaceToXClusterReplication rpc failed with error: %s",
                  createResponse.errorMessage()));
        }
      }

      validateAlterReplicationCompleted(
          client, sourceUniverse, targetMasterAddresses, xClusterConfig);

      log.debug(
          "Alter replication for xClusterConfig {} completed for source db ids:",
          xClusterConfig.getUuid(),
          taskParams().getDbs());

    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
  }

  protected void validateAlterReplicationCompleted(
      YBClient client,
      Universe sourceUniverse,
      Set<CommonNet.HostPortPB> targetMasterAddresses,
      XClusterConfig xClusterConfig)
      throws Exception {
    Duration xclusterWaitTimeout =
        this.confGetter.getConfForScope(sourceUniverse, UniverseConfKeys.xclusterSetupAlterTimeout);
    for (String dbId : taskParams().getDbs()) {
      log.info(
          "Validating database {} for XClusterConfig({}) is done altering replication",
          dbId,
          xClusterConfig.getUuid());
      boolean alterReplicationCompleted =
          doWithConstTimeout(
              DELAY_BETWEEN_RETRIES_MS,
              xclusterWaitTimeout.toMillis(),
              () -> {
                IsAlterXClusterReplicationDoneResponse completionResponse;

                try {
                  completionResponse =
                      client.isAlterXClusterReplicationDone(
                          xClusterConfig.getReplicationGroupName(), targetMasterAddresses);
                } catch (Exception e) {
                  log.error(
                      "IsAlterXClusterReplicationDone rpc for xClusterConfig: {}, db: {}, hit"
                          + " error: {}",
                      xClusterConfig.getName(),
                      dbId,
                      e.getMessage());
                  return false;
                }

                if (completionResponse.hasError()) {
                  log.error(
                      "IsAlterXClusterReplication rpc for xClusterConfig: {}, db: {}, hit error:"
                          + " {}",
                      xClusterConfig.getName(),
                      dbId,
                      completionResponse.errorMessage());
                  return false;
                }
                log.debug(
                    "Altering replication status is complete: {}, for universe: {}, xClusterConfig:"
                        + " {}, dbId: {}",
                    completionResponse.isDone(),
                    sourceUniverse.getUniverseUUID(),
                    xClusterConfig.getName(),
                    dbId);
                return completionResponse.isDone();
              });
      if (!alterReplicationCompleted) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Altering replication for database %s for xClusterConfig %s with source universe %s"
                    + " timed out",
                dbId, xClusterConfig.getName(), sourceUniverse.getUniverseUUID()));
      }
    }
  }
}
