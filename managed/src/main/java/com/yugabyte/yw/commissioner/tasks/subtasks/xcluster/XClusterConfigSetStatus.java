// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.SetUniverseReplicationEnabledResponse;
import org.yb.client.YBClient;

@Slf4j
public class XClusterConfigSetStatus extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigSetStatus(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = refreshXClusterConfig();
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);

    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate);

    try {
      XClusterConfigStatusType desiredStatus =
          XClusterConfigStatusType.valueOf(taskParams().editFormData.status);

      if (desiredStatus == XClusterConfigStatusType.Running) {
        log.info("Resuming XClusterConfig({})", xClusterConfig.uuid);
      } else {
        log.info("Pausing XClusterConfig({})", xClusterConfig.uuid);
      }

      SetUniverseReplicationEnabledResponse resp =
          client.setUniverseReplicationEnabled(
              xClusterConfig.getReplicationGroupName(),
              desiredStatus == XClusterConfigStatusType.Running);
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "Failed to set XClusterConfig(%s) status: %s",
                xClusterConfig.uuid, resp.errorMessage()));
      }

      xClusterConfig.status = desiredStatus;
      xClusterConfig.update();

      if (HighAvailabilityConfig.get().isPresent()) {
        getUniverse().incrementVersion();
      }

    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, targetUniverseMasterAddresses);
    }

    log.info("Completed {}", getName());
  }
}
