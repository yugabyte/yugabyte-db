// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
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
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonNet;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.client.AddNamespaceToXClusterReplicationResponse;
import org.yb.client.IsAlterXClusterReplicationDoneResponse;
import org.yb.client.YBClient;
import org.yb.util.NetUtil;

@Slf4j
public class AddNamespaceToXClusterReplication extends XClusterConfigTaskBase {

  private static final long DELAY_BETWEEN_RETRIES_MS = TimeUnit.SECONDS.toMillis(10);

  @Inject
  protected AddNamespaceToXClusterReplication(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Getter
  public static class Params extends XClusterConfigTaskParams {

    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // The db to be added to the xcluster replication must be stored in the dbToAdd field.
    public String dbToAdd;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());

    if (StringUtils.isBlank(taskParams().getDbToAdd())) {
      throw new RuntimeException(
          String.format(
              "`db` in the task parameters must not be null or empty: it was %s",
              taskParams().getDbToAdd()));
    }
    String dbId = taskParams().getDbToAdd();

    try (YBClient client =
        ybService.getClient(
            sourceUniverse.getMasterAddresses(), sourceUniverse.getCertificateNodetoNode())) {
      Set<CommonNet.HostPortPB> targetMasterAddresses =
          new HashSet<>(
              NetUtil.parseStringsAsPB(
                  targetUniverse.getMasterAddresses(
                      false /* mastersQueryable */, true /* getSecondary */)));
      log.info(
          "Adding database to replication for XClusterConfig({}): source db id: {}",
          xClusterConfig.getUuid(),
          taskParams().getDbToAdd());

      try {
        AddNamespaceToXClusterReplicationResponse createResponse =
            client.addNamespaceToXClusterReplication(
                xClusterConfig.getReplicationGroupName(), targetMasterAddresses, dbId);
        if (createResponse.hasError()) {
          throw new RuntimeException(
              String.format(
                  "AddNamespaceToXClusterReplication rpc failed with error: %s",
                  createResponse.errorMessage()));
        }
      } catch (Exception e) {
        if (!e.getMessage().contains("already contains")) {
          log.error(
              "AddNamespaceToXClusterReplication rpc for xClusterConfig: {} hit error: {}",
              xClusterConfig.getName(),
              e.getMessage());
          throw e;
        }
        // Skip if the replication group already contains the dbId.
      }

      Duration xClusterWaitTimeout =
          this.confGetter.getConfForScope(
              sourceUniverse, UniverseConfKeys.xclusterSetupAlterTimeout);
      waitForAlterReplicationDone(
          client,
          xClusterConfig.getReplicationGroupName(),
          targetMasterAddresses,
          xClusterWaitTimeout.toMillis());

      log.debug(
          "Alter replication for xClusterConfig {} completed for source db id: {}",
          xClusterConfig.getUuid(),
          taskParams().getDbToAdd());
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
  }

  protected void waitForAlterReplicationDone(
      YBClient client,
      String replicationGroupName,
      Set<CommonNet.HostPortPB> targetMasterAddresses,
      long xClusterWaitTimeoutMs) {
    doWithConstTimeout(
        DELAY_BETWEEN_RETRIES_MS,
        xClusterWaitTimeoutMs,
        () -> {
          try {
            IsAlterXClusterReplicationDoneResponse completionResponse =
                client.isAlterXClusterReplicationDone(replicationGroupName, targetMasterAddresses);
            if (completionResponse.hasError()) {
              throw new RuntimeException(
                  String.format(
                      "IsAlterXClusterReplication rpc for replication group name: %s, hit error:"
                          + " %s",
                      replicationGroupName, completionResponse.errorMessage()));
            }
            if (completionResponse.hasReplicationError()
                && !completionResponse.getReplicationError().getCode().equals(ErrorCode.OK)) {
              throw new RuntimeException(
                  String.format(
                      "IsAlterXClusterReplication rpc for replication group name: %s, hit"
                          + " replication error: %s with error code: %s",
                      replicationGroupName,
                      completionResponse.getReplicationError().getMessage(),
                      completionResponse.getReplicationError().getCode()));
            }
            if (!completionResponse.isDone()) {
              throw new RuntimeException(
                  String.format(
                      "CreateXClusterReplication is not done for replication group name: %s",
                      replicationGroupName));
            }
            // Replication alter is complete, return.
          } catch (Exception e) {
            log.error(
                "IsAlterXClusterReplicationDone rpc for replication group name: {}, hit error: {}",
                replicationGroupName,
                e);
            throw new RuntimeException(e);
          }
        });
  }
}
