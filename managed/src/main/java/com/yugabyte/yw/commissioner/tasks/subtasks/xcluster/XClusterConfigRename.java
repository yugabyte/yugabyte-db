// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
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
  protected XClusterConfigRename(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
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

    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {
      log.info(
          "Renaming XClusterConfig({}): `{}` -> `{}`",
          xClusterConfig.uuid,
          xClusterConfig.name,
          taskParams().newName);

      AlterUniverseReplicationResponse resp =
          client.alterUniverseReplicationName(
              xClusterConfig.getReplicationGroupName(),
              XClusterConfig.getReplicationGroupName(
                  xClusterConfig.sourceUniverseUUID, taskParams().newName));
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "Failed to rename XClusterConfig(%s): %s",
                xClusterConfig.uuid, resp.errorMessage()));
      }

      // Set the new name of the xCluster config in the DB.
      xClusterConfig.name = taskParams().newName;
      xClusterConfig.setReplicationGroupName();
      xClusterConfig.update();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
