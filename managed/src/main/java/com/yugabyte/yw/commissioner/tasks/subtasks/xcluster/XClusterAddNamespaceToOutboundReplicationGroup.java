// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.time.Duration;
import java.util.Collections;
import javax.inject.Inject;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.GetXClusterOutboundReplicationGroupInfoResponse;
import org.yb.client.XClusterAddNamespaceToOutboundReplicationGroupResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupInfoResponsePB.NamespaceInfoPB;

@Slf4j
public class XClusterAddNamespaceToOutboundReplicationGroup extends CreateOutboundReplicationGroup {

  @Inject
  protected XClusterAddNamespaceToOutboundReplicationGroup(
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
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Duration xclusterWaitTimeout =
        this.confGetter.getConfForScope(sourceUniverse, UniverseConfKeys.xclusterSetupAlterTimeout);

    if (StringUtils.isBlank(taskParams().getDbToAdd())) {
      throw new RuntimeException(
          String.format(
              "`dbIdToAdd` in the task parameters must not be null or empty: it was %s",
              taskParams().getDbToAdd()));
    }
    String dbId = taskParams().getDbToAdd();

    try (YBClient client =
        ybService.getClient(
            sourceUniverse.getMasterAddresses(), sourceUniverse.getCertificateNodetoNode())) {
      log.info(
          "Checkpointing databases for XClusterConfig({}): source db ids: {}",
          xClusterConfig.getUuid(),
          taskParams().getDbToAdd());

      GetXClusterOutboundReplicationGroupInfoResponse rgInfo =
          client.getXClusterOutboundReplicationGroupInfo(xClusterConfig.getReplicationGroupName());
      if (rgInfo.hasError()) {
        throw new RuntimeException(
            String.format(
                "GetXClusterOutboundReplicationGroupInfo failed for universe %s on"
                    + " XClusterConfig(%s): %s",
                sourceUniverse.getUniverseUUID(), xClusterConfig, rgInfo.errorMessage()));
      }
      log.debug("Got namespace infos: {}", rgInfo.getNamespaceInfos());
      if (rgInfo.getNamespaceInfos().stream()
          .map(NamespaceInfoPB::getNamespaceId)
          .anyMatch(id -> id.equals(dbId))) {
        log.debug(
            "Skipping {}: xClusterAddNamespaceToOutboundReplicationGroup {} for source db id: {}"
                + " won't be called because namespace is already part of the outbound replication"
                + " group",
            getName(),
            xClusterConfig.getUuid(),
            taskParams().getDbToAdd());
        waitForCheckpointingToBeReady(
            client,
            xClusterConfig,
            Collections.singleton(taskParams().getDbToAdd()),
            xclusterWaitTimeout.toMillis());
        return;
      }

      XClusterAddNamespaceToOutboundReplicationGroupResponse createResponse =
          client.xClusterAddNamespaceToOutboundReplicationGroup(
              xClusterConfig.getReplicationGroupName(), dbId);

      if (createResponse.hasError()) {
        throw new RuntimeException(
            String.format(
                "XClusterAddNamespaceToOutboundReplicationGroup rpc failed with error: %s",
                createResponse.errorMessage()));
      }
      waitForCheckpointingToBeReady(
          client,
          xClusterConfig,
          Collections.singleton(taskParams().getDbToAdd()),
          xclusterWaitTimeout.toMillis());
      log.debug(
          "Checkpointing for xClusterConfig {} completed for source db id: {}",
          xClusterConfig.getUuid(),
          taskParams().getDbToAdd());
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }

    log.info("Completed {}", getName());
  }
}
