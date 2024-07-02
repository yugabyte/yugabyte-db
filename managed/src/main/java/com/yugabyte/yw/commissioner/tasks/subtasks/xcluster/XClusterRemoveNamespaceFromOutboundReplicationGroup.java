// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.XClusterRemoveNamespaceFromOutboundReplicationGroupResponse;
import org.yb.client.YBClient;

@Slf4j
public class XClusterRemoveNamespaceFromOutboundReplicationGroup extends XClusterConfigTaskBase {
  private static long DELAY_BETWEEN_RETRIES_MS = TimeUnit.SECONDS.toMillis(10);

  @Inject
  protected XClusterRemoveNamespaceFromOutboundReplicationGroup(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Getter
  public static class Params extends XClusterConfigTaskParams {
    // The parent xCluster config must be stored in xClusterConfig field.
    // The db to be removed from the xcluster replication must be stored in the dbToRemove field.
    public String dbToRemove;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());

    if (StringUtils.isBlank(taskParams().getDbToRemove())) {
      throw new RuntimeException(
          String.format(
              "`dbIdToRemove` in the task parameters must not be null or empty: it was %s",
              taskParams().getDbToRemove()));
    }

    try (YBClient client =
        ybService.getClient(
            sourceUniverse.getMasterAddresses(), sourceUniverse.getCertificateNodetoNode())) {
      log.info(
          "Removing database from XClusterConfig({}): source db id: {}",
          xClusterConfig.getUuid(),
          taskParams().getDbToRemove());

      String dbId = taskParams().getDbToRemove();
      XClusterRemoveNamespaceFromOutboundReplicationGroupResponse createResponse =
          client.xClusterRemoveNamespaceFromOutboundReplicationGroup(
              xClusterConfig.getReplicationGroupName(), dbId);

      if (createResponse.hasError()) {
        throw new RuntimeException(
            String.format(
                "XClusterRemoveNamespaceFromOutboundReplicationGroup rpc failed with error: %s",
                createResponse.errorMessage()));
      }
      xClusterConfig.removeNamespaces(Set.of(dbId));
      log.debug(
          "Removing source db id: {} from xClusterConfig {} completed",
          taskParams().getDbToRemove(),
          xClusterConfig.getUuid());
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
  }
}
