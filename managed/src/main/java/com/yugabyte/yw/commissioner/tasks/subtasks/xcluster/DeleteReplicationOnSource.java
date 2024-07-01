// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.XClusterDeleteOutboundReplicationGroupResponse;
import org.yb.client.YBClient;

@Slf4j
public class DeleteReplicationOnSource extends XClusterConfigTaskBase {
  @Inject
  protected DeleteReplicationOnSource(
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

    try (YBClient client =
        ybService.getClient(
            sourceUniverse.getMasterAddresses(), sourceUniverse.getCertificateNodetoNode())) {

      // TODO: Check whether the replication group exists on the source universe first.

      XClusterDeleteOutboundReplicationGroupResponse response =
          client.xClusterDeleteOutboundReplicationGroup(xClusterConfig.getReplicationGroupName());

      if (response.hasError()) {
        throw new RuntimeException(
            String.format(
                "Failed to delete replication for XClusterConfig(%s) on on source universe %s."
                    + " Error: %s",
                xClusterConfig.getUuid(),
                sourceUniverse.getUniverseUUID(),
                response.errorMessage()));
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
    log.debug(
        "XCluster delete replication on source universe completed for xClusterConfig: {}",
        xClusterConfig.getUuid());
  }
}
