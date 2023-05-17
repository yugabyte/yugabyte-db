// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.AlterUniverseReplicationResponse;
import org.yb.client.YBClient;

@Slf4j
public class XClusterConfigRename extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigRename(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // The new name to change the xCluster config name to.
    public String newName;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s,newName=%s)",
        super.getName(), taskParams().getXClusterConfig(), taskParams().newName);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    // Each xCluster config rename task must belong to a parent xCluster config.
    XClusterConfig xClusterConfig = taskParams().getXClusterConfig();
    if (xClusterConfig == null) {
      throw new RuntimeException(
          "taskParams().xClusterConfig is null. Each xCluster config rename subtask must belong "
              + "to an xCluster config");
    }

    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {
      log.info(
          "Renaming XClusterConfig({}): `{}` -> `{}`",
          xClusterConfig.getUuid(),
          xClusterConfig.getName(),
          taskParams().newName);

      AlterUniverseReplicationResponse resp =
          client.alterUniverseReplicationName(
              xClusterConfig.getReplicationGroupName(),
              xClusterConfig.getNewReplicationGroupName(
                  xClusterConfig.getSourceUniverseUUID(), taskParams().newName));
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "Failed to rename XClusterConfig(%s): %s",
                xClusterConfig.getUuid(), resp.errorMessage()));
      }

      // Set the new name of the xCluster config in the DB.
      xClusterConfig.setName(taskParams().newName);
      xClusterConfig.setReplicationGroupName(taskParams().newName);
      xClusterConfig.update();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
